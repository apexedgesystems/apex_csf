/**
 * @file TimeServer.cpp
 * @brief Implementation of TimeServer's correlation, drift, and state machine.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"

#include <algorithm>
#include <ctime>

namespace system_core {
namespace time_server {

namespace {

/// Cycles->ns conversion isn't needed here; this constant is for readability.
constexpr std::int64_t NS_PER_SECOND = 1'000'000'000LL;

} // namespace

/* ----------------------------- Construction ----------------------------- */

TimeServer::TimeServer() noexcept {
  setConfigured(true);
  // Initialize tnt_ / output_ to a NONE/UNKNOWN baseline so consumers that
  // read before the first tick see explicitly-invalid time rather than
  // zero-valued garbage.
  tnt_.valid = static_cast<std::uint8_t>(TimeValid::NONE);
  tnt_.quality = static_cast<std::uint8_t>(TimeQuality::UNKNOWN);
  tnt_.source = static_cast<std::uint8_t>(TimeSource::ONBOARD);
  output_.correlationValid = tnt_.valid;
  output_.timeQuality = tnt_.quality;
  output_.timeSource = tnt_.source;
}

/* ----------------------------- Configuration ----------------------------- */

void TimeServer::loadTprm(const TimeServerTunableParams& params) noexcept {
  tprm_ = params;
  if (tprm_.driftFilterTaps == 0) {
    tprm_.driftFilterTaps = 1;
  }
  if (tprm_.driftFilterTaps > MAX_DRIFT_TAPS) {
    tprm_.driftFilterTaps = MAX_DRIFT_TAPS;
  }
  source_ = static_cast<TimeSource>(tprm_.primaryRefSource);
}

/* ----------------------------- tick ----------------------------- */

void TimeServer::tick(std::uint32_t currentCycle) noexcept {
  metCycles_ = currentCycle;

  // Boot broadcast: emit one TNT at the very first tick so consumers know
  // TimeServer is alive even before the first PPS edge.
  if (firstTick_) {
    firstTick_ = false;
    publish();
  }

  // Poll PPS for a new edge.
  if (pps_ != nullptr) {
    std::int64_t edgeNs = 0;
    const apex::hal::PpsStatus st = pps_->readCapture(edgeNs);
    if (st == apex::hal::PpsStatus::OK) {
      processEdge(edgeNs, pps_->pulseCount());
    }
  }

  // Even with no edge this frame, staleness can advance. Use the steady
  // clock to detect long silences.
  if (steadyClock_) {
    checkStaleness(now());
  }
}

/* ----------------------------- Edge handling ----------------------------- */

void TimeServer::processEdge(std::int64_t edgeLocalNs, std::uint32_t edgePulseCount) noexcept {
  if (haveLastEdge_) {
    const std::int64_t intervalNs = edgeLocalNs - lastEdgeLocalNs_;
    if (!isIntervalValid(intervalNs)) {
      ++glitchCount_;
      return;
    }
    driftPpb_ = pushDriftSample(intervalNs);
  }

  // Update edge bookkeeping.
  lastEdgeLocalNs_ = edgeLocalNs;
  lastEdgePulseCount_ = edgePulseCount;
  haveLastEdge_ = true;
  ++totalPpsCount_;

  // Pair with any pending reference time.
  if (refPending_) {
    lastEdgeEpochNs_ = pendingRefEpochNs_;
    haveReference_ = true;
    source_ = static_cast<TimeSource>(pendingRefSource_);
    refPending_ = false;
    flags_ |= TNT_FLAG_REF_SWITCHOVER;
  } else if (haveReference_) {
    // No fresh reference this edge; advance the previous epoch by 1 second
    // (drift-corrected via the next-tone prediction below).
    lastEdgeEpochNs_ += NS_PER_SECOND;
  }

  // Promote quality based on edge count and reference availability.
  if (haveReference_) {
    valid_ = TimeValid::VALID;
    if (quality_ == TimeQuality::UNKNOWN || quality_ == TimeQuality::COARSE) {
      quality_ = TimeQuality::FINE;
    }
    promoteQualityIfStable();
  } else {
    // PPS but no ref -> HOLDOVER. valid stays VALID (we have a steady local
    // clock); quality is COARSE.
    valid_ = TimeValid::VALID;
    quality_ = TimeQuality::COARSE;
  }

  updateNextTonePrediction();
  publish();
  flags_ &= static_cast<std::uint8_t>(~TNT_FLAG_REF_SWITCHOVER);
}

bool TimeServer::isIntervalValid(std::int64_t intervalNs) const noexcept {
  return intervalNs >= MIN_VALID_INTERVAL_NS && intervalNs <= MAX_VALID_INTERVAL_NS;
}

/* ----------------------------- Drift estimation ----------------------------- */

std::int32_t TimeServer::pushDriftSample(std::int64_t intervalNs) noexcept {
  // 1 ns of interval deviation per second == 1 ppb.
  const std::int32_t sample = static_cast<std::int32_t>(intervalNs - NS_PER_SECOND);

  if (driftSamplesCount_ < tprm_.driftFilterTaps) {
    driftSamples_[driftSamplesHead_] = sample;
    driftSamplesHead_ = (driftSamplesHead_ + 1) % tprm_.driftFilterTaps;
    driftSampleSum_ += sample;
    ++driftSamplesCount_;
  } else {
    const std::int32_t evicted = driftSamples_[driftSamplesHead_];
    driftSamples_[driftSamplesHead_] = sample;
    driftSamplesHead_ = (driftSamplesHead_ + 1) % tprm_.driftFilterTaps;
    driftSampleSum_ += sample;
    driftSampleSum_ -= evicted;
  }

  if (driftSamplesCount_ == 0) {
    return 0;
  }
  return static_cast<std::int32_t>(driftSampleSum_ / static_cast<std::int64_t>(driftSamplesCount_));
}

void TimeServer::promoteQualityIfStable() noexcept {
  if (driftSamplesCount_ >= tprm_.driftFilterTaps && quality_ == TimeQuality::FINE) {
    quality_ = TimeQuality::PRECISE;
  }
}

/* ----------------------------- Predictions ----------------------------- */

void TimeServer::updateNextTonePrediction() noexcept {
  if (!haveReference_) {
    return;
  }
  // Predict next edge as last + 1 s, drift-corrected.
  // drift_ppb = ns/s, so adjustment = drift_ppb (in ns).
  const std::int64_t predictedAdvance = NS_PER_SECOND + driftPpb_;
  // Keep the published next-tone in sync with the most-recent confirmed edge.
  // We store the predicted UTC (epoch domain) for consumer use.
  output_.nextToneEpochNs = lastEdgeEpochNs_ + predictedAdvance;
}

/* ----------------------------- Staleness / FREERUN ----------------------------- */

void TimeServer::checkStaleness(std::int64_t nowNs) noexcept {
  if (!haveLastEdge_) {
    return;
  }
  const std::int64_t sinceLastEdgeNs = nowNs - lastEdgeLocalNs_;
  const std::int64_t maxStalenessNs =
      static_cast<std::int64_t>(tprm_.maxStalenessUs) * 1000LL;
  const std::int64_t holdoverLimitNs =
      static_cast<std::int64_t>(tprm_.holdoverLimitS) * NS_PER_SECOND;

  if (sinceLastEdgeNs > holdoverLimitNs) {
    if (valid_ != TimeValid::FREERUN) {
      valid_ = TimeValid::FREERUN;
      // Quality drops; correlation is now monotonic-only with no PPS to
      // re-anchor against. epochNs continues to advance from the last
      // known value but loses ground steadily to drift.
      if (quality_ != TimeQuality::UNKNOWN) {
        quality_ = TimeQuality::COARSE;
      }
      publish();
    }
  } else if (sinceLastEdgeNs > maxStalenessNs) {
    if (valid_ == TimeValid::VALID) {
      valid_ = TimeValid::STALE;
      publish();
    }
  }
}

/* ----------------------------- Publish ----------------------------- */

void TimeServer::publish() noexcept {
  output_.utcEpochNs = haveReference_ ? lastEdgeEpochNs_ : 0;
  output_.metCycles = metCycles_;
  output_.lastPpsLocalNs = haveLastEdge_ ? lastEdgeLocalNs_ : 0;
  output_.correlationOffsetNs = haveReference_ ? (lastEdgeEpochNs_ - lastEdgeLocalNs_) : 0;
  // nextToneEpochNs already maintained by updateNextTonePrediction()
  output_.driftEstimatePpb = driftPpb_;
  output_.ppsCount = totalPpsCount_;
  output_.correlationValid = static_cast<std::uint8_t>(valid_);
  output_.timeSource = static_cast<std::uint8_t>(source_);
  output_.timeQuality = static_cast<std::uint8_t>(quality_);
  output_.flags = flags_;

  tnt_.epochNs = output_.utcEpochNs;
  tnt_.localNs = output_.lastPpsLocalNs;
  tnt_.nextToneEpochNs = output_.nextToneEpochNs;
  tnt_.driftPpb = driftPpb_;
  tnt_.ppsCount = totalPpsCount_;
  tnt_.source = output_.timeSource;
  tnt_.quality = output_.timeQuality;
  tnt_.valid = output_.correlationValid;
  tnt_.flags = flags_;

  if (broadcastTnt_) {
    broadcastTnt_(tnt_);
  }
}

/* ----------------------------- Commands ----------------------------- */

void TimeServer::handleSetReferenceTime(const SetReferenceTime& cmd) noexcept {
  refPending_ = true;
  pendingRefEpochNs_ = cmd.epochNs;
  pendingRefSource_ = cmd.source;
  pendingRefQuality_ = cmd.quality;
}

void TimeServer::handleSetTimeManual(const SetTimeManual& cmd) noexcept {
  // Anchor the manual UTC at the current steady-clock moment so consumers
  // can interpolate immediately. Without a PPS edge, quality is COARSE and
  // there is no drift estimate.
  const std::int64_t nowNs = steadyClock_ ? now() : 0;
  lastEdgeLocalNs_ = nowNs;
  lastEdgeEpochNs_ = cmd.epochNs;
  lastEdgePulseCount_ = totalPpsCount_;
  haveLastEdge_ = true;
  haveReference_ = true;
  source_ = TimeSource::MANUAL;
  valid_ = TimeValid::VALID;
  quality_ = TimeQuality::COARSE;
  // No drift estimate available; clear the filter to keep stats honest.
  driftSamplesCount_ = 0;
  driftSamplesHead_ = 0;
  driftSampleSum_ = 0;
  driftPpb_ = 0;
  updateNextTonePrediction();
  publish();
}

void TimeServer::resetCorrelation() noexcept {
  refPending_ = false;
  haveReference_ = false;
  haveLastEdge_ = false;
  lastEdgeLocalNs_ = 0;
  lastEdgeEpochNs_ = 0;
  lastEdgePulseCount_ = 0;
  driftSamplesCount_ = 0;
  driftSamplesHead_ = 0;
  driftSampleSum_ = 0;
  driftPpb_ = 0;
  totalPpsCount_ = 0;
  glitchCount_ = 0;
  flags_ = 0;
  valid_ = TimeValid::NONE;
  quality_ = TimeQuality::UNKNOWN;
  source_ = TimeSource::ONBOARD;
  output_ = {};
  tnt_ = {};
  output_.correlationValid = static_cast<std::uint8_t>(valid_);
  output_.timeQuality = static_cast<std::uint8_t>(quality_);
  output_.timeSource = static_cast<std::uint8_t>(source_);
  tnt_.valid = output_.correlationValid;
  tnt_.quality = output_.timeQuality;
  tnt_.source = output_.timeSource;
}

/* ----------------------------- Interpolation ----------------------------- */

std::int64_t TimeServer::computeUtcNs(std::int64_t steadyNowNs) const noexcept {
  if (!haveReference_ || !haveLastEdge_) {
    return 0;
  }
  return lastEdgeEpochNs_ + (steadyNowNs - lastEdgeLocalNs_);
}

std::int64_t TimeServer::currentUtcNs() const noexcept {
  if (!steadyClock_ || !haveReference_ || !haveLastEdge_) {
    return 0;
  }
  return computeUtcNs(steadyClock_());
}

/* ----------------------------- TimeProvider plumbing ----------------------------- */

namespace {

std::uint64_t utcTimeProviderTrampoline(void* ctx) noexcept {
  auto* ts = static_cast<TimeServer*>(ctx);
  if (ts == nullptr) {
    return 0;
  }
  // currentUtcNs() reads through TimeServer's configured steady-clock
  // delegate, so a test driving synthetic time gets deterministic UTC out
  // of the provider. The executive's default delegate is
  // clock_gettime(CLOCK_MONOTONIC) so production sees real wall time.
  const std::int64_t utcNs = ts->currentUtcNs();
  return static_cast<std::uint64_t>(utcNs / 1000); // ns -> us for ATS
}

std::int64_t defaultSteadyClockTrampoline(void* /*ctx*/) noexcept {
  struct timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::int64_t>(now.tv_sec) * NS_PER_SECOND + now.tv_nsec;
}

} // namespace

apex::time::TimeProviderDelegate TimeServer::utcTimeProvider() noexcept {
  return {&utcTimeProviderTrampoline, this};
}

TimeServer::SteadyClockDelegate TimeServer::defaultSteadyClock() noexcept {
  return {&defaultSteadyClockTrampoline, nullptr};
}

} // namespace time_server
} // namespace system_core
