/**
 * @file TimeServer.cpp
 * @brief Implementation of TimeServer's correlation, drift, and state machine.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"

#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace system_core {
namespace time_server {

namespace {

/// Cycles->ns conversion isn't needed here; this constant is for readability.
constexpr std::int64_t NS_PER_SECOND = 1'000'000'000LL;

} // namespace

/* ----------------------------- doInit (registry registration) ----------------------------- */

std::uint8_t TimeServer::doInit() noexcept {
  // Register OUTPUT (current correlation state) and TUNABLE_PARAM (TPRM).
  // Consumers reach these through the registry by (componentId, category, name)
  // so they don't need a direct TimeServer reference. registerData() returns
  // false only on capacity overflow (MAX_DATA_PER_COMPONENT), which is a
  // build-time bound for our 2 entries.
  if (!registerData(data::DataCategory::OUTPUT, "output", &output_, sizeof(output_))) {
    return 1;
  }
  if (!registerData(data::DataCategory::TUNABLE_PARAM, "tunables", &tprm_, sizeof(tprm_))) {
    return 1;
  }
  return 0;
}

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

  // Mode-specific frame work. PRIMARY and SECONDARY both consume a local
  // PPS source; the difference is purely operational (where the reference
  // time comes from). RELAY does no per-tick work -- updates arrive via
  // the OP_ACCEPT_REMOTE_TNT command. PTP_SYNC and CAN_SYNC read their
  // sync source each tick when wired.
  const TimeServerMode MODE = static_cast<TimeServerMode>(tprm_.mode);
  const bool IS_PPS_MODE = (MODE == TimeServerMode::PRIMARY || MODE == TimeServerMode::SECONDARY);

  if (IS_PPS_MODE && pps_ != nullptr) {
    std::int64_t edgeNs = 0;
    const apex::hal::PpsStatus ST = pps_->readCapture(edgeNs);
    if (ST == apex::hal::PpsStatus::OK) {
      processEdge(edgeNs, pps_->pulseCount());
    }
  }

  // PTP_SYNC: read the (assumed) PTP-disciplined wall clock each tick.
  // Re-anchor every tick but only publish at ~1 Hz to match the broadcast
  // cadence consumers expect.
  if (MODE == TimeServerMode::PTP_SYNC && wallClock_ && steadyClock_) {
    tickPtpSync();
  }

  // CAN_SYNC: poll the CAN HW-timestamp delegate. No-op if unwired
  // (the underlying ICan extension is a separate work item).
  if (MODE == TimeServerMode::CAN_SYNC && canSync_ && steadyClock_) {
    tickCanSync();
  }

  // Even with no edge this frame, staleness can advance. Use the steady
  // clock to detect long silences. PPS modes care; the others manage
  // their own sync-source health checks.
  if (IS_PPS_MODE && steadyClock_) {
    checkStaleness(now());
  }
}

/* ----------------------------- Edge handling ----------------------------- */

void TimeServer::processEdge(std::int64_t edgeLocalNs, std::uint32_t edgePulseCount) noexcept {
  if (haveLastEdge_) {
    const std::int64_t INTERVAL_NS = edgeLocalNs - lastEdgeLocalNs_;
    if (!isIntervalValid(INTERVAL_NS)) {
      ++glitchCount_;
      return;
    }
    driftPpb_ = pushDriftSample(INTERVAL_NS);
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
  const std::int32_t SAMPLE = static_cast<std::int32_t>(intervalNs - NS_PER_SECOND);

  if (driftSamplesCount_ < tprm_.driftFilterTaps) {
    driftSamples_[driftSamplesHead_] = SAMPLE;
    driftSamplesHead_ = (driftSamplesHead_ + 1) % tprm_.driftFilterTaps;
    driftSampleSum_ += SAMPLE;
    ++driftSamplesCount_;
  } else {
    const std::int32_t EVICTED = driftSamples_[driftSamplesHead_];
    driftSamples_[driftSamplesHead_] = SAMPLE;
    driftSamplesHead_ = (driftSamplesHead_ + 1) % tprm_.driftFilterTaps;
    driftSampleSum_ += SAMPLE;
    driftSampleSum_ -= EVICTED;
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
  const std::int64_t PREDICTED_ADVANCE = NS_PER_SECOND + driftPpb_;
  // Keep the published next-tone in sync with the most-recent confirmed edge.
  // We store the predicted UTC (epoch domain) for consumer use.
  output_.nextToneEpochNs = lastEdgeEpochNs_ + PREDICTED_ADVANCE;
}

/* ----------------------------- Staleness / FREERUN ----------------------------- */

void TimeServer::checkStaleness(std::int64_t nowNs) noexcept {
  if (!haveLastEdge_) {
    return;
  }
  const std::int64_t SINCE_LAST_EDGE_NS = nowNs - lastEdgeLocalNs_;
  const std::int64_t MAX_STALENESS_NS =
      static_cast<std::int64_t>(tprm_.maxStalenessUs) * 1000LL;
  const std::int64_t HOLDOVER_LIMIT_NS =
      static_cast<std::int64_t>(tprm_.holdoverLimitS) * NS_PER_SECOND;

  if (SINCE_LAST_EDGE_NS > HOLDOVER_LIMIT_NS) {
    if (valid_ != TimeValid::FREERUN) {
      // One-shot anchor: latch the host's wall clock now so subsequent
      // computeUtcNs() interpolations advance from a fresh epoch instead
      // of from a long-stale lastEdgeEpochNs. NTP-disciplined hosts give
      // us ~10ms accuracy here, which is the documented FREERUN goal.
      // If no wall-clock delegate is wired (e.g. bare metal), keep the
      // stale anchor -- the quality bit already reflects the degradation.
      if (wallClock_) {
        const std::int64_t WAL_NS = wallClock_();
        if (WAL_NS > 0) {
          lastEdgeEpochNs_ = WAL_NS;
          lastEdgeLocalNs_ = nowNs;
          haveReference_ = true;
          source_ = TimeSource::ONBOARD;
        }
      }
      valid_ = TimeValid::FREERUN;
      // Quality drops; correlation is now monotonic-only with no PPS to
      // re-anchor against. epochNs continues to advance from the latched
      // anchor but loses ground steadily to local-oscillator drift.
      if (quality_ != TimeQuality::UNKNOWN) {
        quality_ = TimeQuality::COARSE;
      }
      publish();
    }
  } else if (SINCE_LAST_EDGE_NS > MAX_STALENESS_NS) {
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

  // Production path: broadcast through the IInternalBus the executive wired
  // into us during registerComponent(). postBroadcastCommand delivers the
  // TNT to every other registered component as opcode OP_TIME_AT_NEXT_TONE.
  if (auto* bus = internalBus()) {
    apex::compat::rospan<std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(&tnt_),
                                               sizeof(tnt_));
    (void)bus->postBroadcastCommand(fullUid(), OP_TIME_AT_NEXT_TONE, payload);
  }

  // Test seam: the broadcast delegate gives unit tests a way to capture
  // every TNT without standing up an IInternalBus implementation. In
  // production both paths fire; in tests typically only this one does.
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
  const std::int64_t NOW_NS = steadyClock_ ? now() : 0;
  lastEdgeLocalNs_ = NOW_NS;
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

/* ----------------------------- PTP_SYNC mode ----------------------------- */

void TimeServer::tickPtpSync() noexcept {
  // Lightweight PTP_SYNC interpretation: read CLOCK_REALTIME (or whatever
  // wallClock_ provides) and trust ptp4l or a similar daemon is keeping it
  // disciplined. Hardware PHC (/dev/ptp0) reads via PTP_CLOCK_GETTIME would
  // give better accuracy but require a separate dependency on the PTP
  // user-space stack.
  const std::int64_t EPOCH_NS = wallClock_();
  if (EPOCH_NS <= 0) {
    return;
  }
  const std::int64_t LOCAL_NS = now();

  // Re-anchor every tick: the disciplined wall clock IS our reference.
  lastEdgeEpochNs_ = EPOCH_NS;
  lastEdgeLocalNs_ = LOCAL_NS;
  haveLastEdge_ = true;
  haveReference_ = true;
  source_ = TimeSource::ONBOARD;
  valid_ = TimeValid::VALID;
  // Per the table: PTP HW gives 50ns-50us, software stack ~1us. FINE
  // is the right quality cap for the lightweight interpretation.
  if (quality_ != TimeQuality::PRECISE) {
    quality_ = TimeQuality::FINE;
  }

  // Pace TNT broadcasts to ~1 Hz so we don't flood the bus at the
  // scheduler's frame rate. Each "tone" we publish increments
  // totalPpsCount_ to keep gap-detection semantics consistent with the
  // PPS modes.
  if (!havePublishMark_ || (LOCAL_NS - lastPublishLocalNs_) >= NS_PER_SECOND) {
    ++totalPpsCount_;
    lastPublishLocalNs_ = LOCAL_NS;
    havePublishMark_ = true;
    updateNextTonePrediction();
    publish();
  }
}

/* ----------------------------- CAN_SYNC mode ----------------------------- */

void TimeServer::tickCanSync() noexcept {
  // The delegate boundary keeps TimeServer ignorant of the CAN HAL: a
  // CAN_SYNC source extracts hardware-timestamped sync frames and
  // surfaces them to TimeServer through the delegate. If no event is
  // available this frame the delegate returns present=false and we do
  // nothing.
  const CanSyncEvent EV = canSync_();
  if (!EV.present) {
    return;
  }

  // CAN HW timestamping is in the 1-10 us range (ticket table). Quality
  // FINE is the right cap; we don't promote to PRECISE because there's
  // no drift estimator running in this mode (the CAN sync frame IS the
  // anchor; sub-frame interpolation rides the local steady clock).
  lastEdgeEpochNs_ = EV.epochNs;
  lastEdgeLocalNs_ = EV.localNs;
  haveLastEdge_ = true;
  haveReference_ = true;
  source_ = TimeSource::ONBOARD;
  valid_ = TimeValid::VALID;
  if (quality_ != TimeQuality::PRECISE) {
    quality_ = TimeQuality::FINE;
  }

  // Pace broadcasts to ~1 Hz like PTP_SYNC.
  const std::int64_t LOCAL_NS = now();
  if (!havePublishMark_ || (LOCAL_NS - lastPublishLocalNs_) >= NS_PER_SECOND) {
    ++totalPpsCount_;
    lastPublishLocalNs_ = LOCAL_NS;
    havePublishMark_ = true;
    updateNextTonePrediction();
    publish();
  }
}

void TimeServer::handleAcceptRemoteTnt(const TimeAtNextTone& remote) noexcept {
  // RELAY mode only -- ignore in PRIMARY/SECONDARY/PTP/CAN where the
  // local sync source is authoritative.
  if (static_cast<TimeServerMode>(tprm_.mode) != TimeServerMode::RELAY) {
    return;
  }
  if (!steadyClock_) {
    return;
  }

  // Anchor at receipt: remote epoch becomes our published epoch, local
  // steady_clock at receipt is the interpolation origin. Network link
  // latency lives entirely inside this offset; we don't try to estimate
  // and remove it. The published quality bit reflects that uncertainty.
  const std::int64_t NOW_NS = now();
  lastEdgeEpochNs_ = remote.epochNs;
  lastEdgeLocalNs_ = NOW_NS;
  lastEdgePulseCount_ = remote.ppsCount;
  haveLastEdge_ = true;
  haveReference_ = true;
  ++totalPpsCount_;

  // Drift estimate, source, and ppsCount come from the remote TNT.
  driftPpb_ = remote.driftPpb;
  source_ = static_cast<TimeSource>(remote.source);
  valid_ = TimeValid::VALID;
  // Cap quality: even if the remote claims PRECISE, RELAY adds link
  // latency (table: 0.1-5ms). COARSE is the realistic accuracy class.
  quality_ = TimeQuality::COARSE;

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
  lastPublishLocalNs_ = 0;
  havePublishMark_ = false;
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

/* ----------------------------- Bus command dispatch ----------------------------- */

std::uint8_t TimeServer::handleCommand(std::uint16_t opcode,
                                       apex::compat::rospan<std::uint8_t> payload,
                                       std::vector<std::uint8_t>& response) noexcept {
  using system_component::CommandResult;

  switch (opcode) {
  case OP_SET_REFERENCE_TIME: {
    if (payload.size() < sizeof(SetReferenceTime)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    SetReferenceTime cmd{};
    std::memcpy(&cmd, payload.data(), sizeof(cmd));
    handleSetReferenceTime(cmd);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case OP_GET_TIME_STATUS: {
    // Snapshot the current OUTPUT block as the response payload.
    response.resize(sizeof(TimeServerOutput));
    std::memcpy(response.data(), &output_, sizeof(TimeServerOutput));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case OP_SET_TIME_MANUAL: {
    if (payload.size() < sizeof(SetTimeManual)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    SetTimeManual cmd{};
    std::memcpy(&cmd, payload.data(), sizeof(cmd));
    handleSetTimeManual(cmd);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case OP_RESET_CORRELATION: {
    resetCorrelation();
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case OP_ACCEPT_REMOTE_TNT: {
    if (payload.size() < sizeof(TimeAtNextTone)) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    TimeAtNextTone remote{};
    std::memcpy(&remote, payload.data(), sizeof(remote));
    handleAcceptRemoteTnt(remote);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  default:
    // Defer to base class for common 0x0080-0x00FF opcodes.
    return SystemComponentBase::handleCommand(opcode, payload, response);
  }
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
  const std::int64_t UTC_NS = ts->currentUtcNs();
  return static_cast<std::uint64_t>(UTC_NS / 1000); // ns -> us for ATS
}

std::int64_t defaultSteadyClockTrampoline(void* /*ctx*/) noexcept {
  struct timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::int64_t>(now.tv_sec) * NS_PER_SECOND + now.tv_nsec;
}

std::int64_t defaultWallClockTrampoline(void* /*ctx*/) noexcept {
  struct timespec now {};
  clock_gettime(CLOCK_REALTIME, &now);
  return static_cast<std::int64_t>(now.tv_sec) * NS_PER_SECOND + now.tv_nsec;
}

} // namespace

apex::time::TimeProviderDelegate TimeServer::utcTimeProvider() noexcept {
  return {&utcTimeProviderTrampoline, this};
}

TimeServer::SteadyClockDelegate TimeServer::defaultSteadyClock() noexcept {
  return {&defaultSteadyClockTrampoline, nullptr};
}

TimeServer::WallClockDelegate TimeServer::defaultWallClock() noexcept {
  return {&defaultWallClockTrampoline, nullptr};
}

} // namespace time_server
} // namespace system_core
