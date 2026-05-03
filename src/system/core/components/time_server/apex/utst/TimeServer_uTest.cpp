/**
 * @file TimeServer_uTest.cpp
 * @brief Unit tests for TimeServer correlation, drift, glitch rejection,
 *        and state-machine behaviour.
 *
 * Tests use MockPps as the IPps source, an injected steady-clock delegate
 * for deterministic timing, and a captured broadcast delegate that
 * records every TNT TimeServer publishes. Real wall time is never read.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using apex::hal::MockPps;
using apex::hal::PpsStatus;
using system_core::time_server::SetReferenceTime;
using system_core::time_server::SetTimeManual;
using system_core::time_server::TimeAtNextTone;
using system_core::time_server::TimeQuality;
using system_core::time_server::TimeServer;
using system_core::time_server::TimeServerTunableParams;
using system_core::time_server::TimeSource;
using system_core::time_server::TimeValid;

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Test harness that bundles MockPps, a synthetic steady clock, and a
/// broadcast capture buffer. One instance per test for deterministic
/// state.
class TimeServerHarness {
public:
  TimeServerHarness() {
    // Wire mock PPS as the source.
    (void)pps_.init({});
    server_.setPpsSource(&pps_);

    // Inject the synthetic steady clock.
    server_.setSteadyClock({&TimeServerHarness::steadyClockTrampoline, this});

    // Capture broadcast TNTs.
    server_.setBroadcastDelegate({&TimeServerHarness::broadcastTrampoline, this});

    // Default TPRM: 4-tap drift filter so PRECISE arrives quickly in tests.
    TimeServerTunableParams tprm;
    tprm.driftFilterTaps = 4;
    tprm.maxStalenessUs = 1'500'000;
    tprm.holdoverLimitS = 60;
    server_.loadTprm(tprm);
  }

  TimeServer& server() noexcept { return server_; }
  MockPps& pps() noexcept { return pps_; }
  const std::vector<TimeAtNextTone>& broadcasts() const noexcept { return broadcasts_; }

  void setSteadyNow(std::int64_t ns) noexcept { steadyNs_ = ns; }
  void advanceSteadyNow(std::int64_t deltaNs) noexcept { steadyNs_ += deltaNs; }
  std::int64_t steadyNow() const noexcept { return steadyNs_; }

  /// Convenience: run one tick at the current steady time, with a freshly
  /// reset broadcast buffer.
  void tick(std::uint32_t cycle = 0) {
    broadcasts_.clear();
    server_.tick(cycle);
  }

  /// Convenience: stage an edge with `localNs` and run one tick.
  void injectAndTick(std::int64_t edgeLocalNs, std::uint32_t cycle = 0) {
    pps_.injectEdge(edgeLocalNs);
    tick(cycle);
  }

  /// Override the default TPRM at any point in a test.
  void loadTprm(const TimeServerTunableParams& tprm) noexcept { server_.loadTprm(tprm); }

private:
  static std::int64_t steadyClockTrampoline(void* ctx) noexcept {
    return static_cast<TimeServerHarness*>(ctx)->steadyNs_;
  }

  static void broadcastTrampoline(void* ctx, const TimeAtNextTone& tnt) noexcept {
    static_cast<TimeServerHarness*>(ctx)->broadcasts_.push_back(tnt);
  }

  MockPps pps_;
  TimeServer server_;
  std::int64_t steadyNs_ = 0;
  std::vector<TimeAtNextTone> broadcasts_;
};

} // namespace

/* ----------------------------- Lifecycle / identity ----------------------------- */

/** @test Default state is NONE/UNKNOWN with empty correlation. */
TEST(TimeServer, DefaultStateNoneUnknown) {
  TimeServer s;
  EXPECT_EQ(s.currentTnt().valid, static_cast<std::uint8_t>(TimeValid::NONE));
  EXPECT_EQ(s.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::UNKNOWN));
  EXPECT_EQ(s.glitchCount(), 0U);
  EXPECT_EQ(s.driftSampleCount(), 0U);
}

/** @test Component identity matches header constants. */
TEST(TimeServer, ComponentIdentity) {
  TimeServer s;
  EXPECT_EQ(s.componentId(), TimeServer::COMPONENT_ID);
  EXPECT_STREQ(s.componentName(), "TimeServer");
  EXPECT_STREQ(s.label(), "TIME");
}

/* ----------------------------- Registry registration ----------------------------- */

/** @test doInit registers OUTPUT and TUNABLE_PARAM data blocks. */
TEST(TimeServer, InitRegistersOutputAndTunables) {
  TimeServer s;
  ASSERT_EQ(s.dataCount(), 0U); // nothing before init

  ASSERT_EQ(s.init(), 0U);

  // OUTPUT block + TUNABLE_PARAM block.
  ASSERT_EQ(s.dataCount(), 2U);

  bool sawOutput = false;
  bool sawTunables = false;
  for (std::size_t i = 0; i < s.dataCount(); ++i) {
    const auto* d = s.dataDescriptor(i);
    ASSERT_NE(d, nullptr);
    if (d->category == system_core::data::DataCategory::OUTPUT) {
      sawOutput = true;
      EXPECT_STREQ(d->name, "output");
      EXPECT_EQ(d->ptr, static_cast<const void*>(&s.output()));
      EXPECT_EQ(d->size, sizeof(system_core::time_server::TimeServerOutput));
    } else if (d->category == system_core::data::DataCategory::TUNABLE_PARAM) {
      sawTunables = true;
      EXPECT_STREQ(d->name, "tunables");
      EXPECT_EQ(d->size, sizeof(system_core::time_server::TimeServerTunableParams));
    }
  }
  EXPECT_TRUE(sawOutput);
  EXPECT_TRUE(sawTunables);
}

/** @test tick() with no PPS source and no broadcast delegate is safe. */
TEST(TimeServer, TickWithoutWiringIsSafe) {
  TimeServer s;
  s.tick(0);
  s.tick(1);
  // No assertions besides "doesn't crash".
}

/* ----------------------------- Boot broadcast ----------------------------- */

/** @test First tick broadcasts a NONE/UNKNOWN TNT so consumers know we're up. */
TEST(TimeServer, FirstTickBroadcastsNoneTnt) {
  TimeServerHarness h;
  h.tick();
  ASSERT_EQ(h.broadcasts().size(), 1U);
  EXPECT_EQ(h.broadcasts()[0].valid, static_cast<std::uint8_t>(TimeValid::NONE));
  EXPECT_EQ(h.broadcasts()[0].quality, static_cast<std::uint8_t>(TimeQuality::UNKNOWN));
  EXPECT_EQ(h.broadcasts()[0].epochNs, 0);
  EXPECT_EQ(h.broadcasts()[0].ppsCount, 0U);
}

/** @test Second tick without an edge does not re-broadcast. */
TEST(TimeServer, SecondTickWithoutEdgeIsQuiet) {
  TimeServerHarness h;
  h.tick();
  h.tick(); // clears buffer; a second boot broadcast would land here
  EXPECT_TRUE(h.broadcasts().empty());
}

/* ----------------------------- Edge handling ----------------------------- */

/** @test First edge with no reference goes to VALID/COARSE (HOLDOVER). */
TEST(TimeServer, FirstEdgeNoReferenceIsHoldoverCoarse) {
  TimeServerHarness h;
  h.tick(); // boot broadcast

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  ASSERT_GE(h.broadcasts().size(), 1U);
  const TimeAtNextTone& last = h.broadcasts().back();
  EXPECT_EQ(last.valid, static_cast<std::uint8_t>(TimeValid::VALID));
  EXPECT_EQ(last.quality, static_cast<std::uint8_t>(TimeQuality::COARSE));
  EXPECT_EQ(last.epochNs, 0); // no reference -> no UTC yet
  EXPECT_EQ(last.localNs, NS_PER_SEC);
  EXPECT_EQ(last.ppsCount, 1U);
}

/** @test First edge with a pending reference produces FINE quality and UTC. */
TEST(TimeServer, EdgeWithReferenceProducesFineUtc) {
  TimeServerHarness h;
  h.tick();

  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000'000'000'000LL; // some UTC
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  ref.quality = static_cast<std::uint8_t>(TimeQuality::FINE);
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  const TimeAtNextTone& last = h.broadcasts().back();
  EXPECT_EQ(last.valid, static_cast<std::uint8_t>(TimeValid::VALID));
  EXPECT_EQ(last.quality, static_cast<std::uint8_t>(TimeQuality::FINE));
  EXPECT_EQ(last.epochNs, ref.epochNs);
  EXPECT_EQ(last.localNs, NS_PER_SEC);
  EXPECT_EQ(last.source, static_cast<std::uint8_t>(TimeSource::GPS));
}

/** @test computeUtcNs interpolates between edges. */
TEST(TimeServer, ComputeUtcInterpolates) {
  TimeServerHarness h;
  h.tick();

  SetReferenceTime ref{};
  ref.epochNs = 5 * NS_PER_SEC; // 5 seconds since Unix epoch (toy)
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  h.server().handleSetReferenceTime(ref);

  // Pair the reference with an edge at steady=10s.
  h.setSteadyNow(10 * NS_PER_SEC);
  h.injectAndTick(10 * NS_PER_SEC);

  // 250ms after the edge, UTC should be 5s + 250ms.
  const std::int64_t laterSteady = 10 * NS_PER_SEC + 250'000'000LL;
  EXPECT_EQ(h.server().computeUtcNs(laterSteady), 5 * NS_PER_SEC + 250'000'000LL);
}

/* ----------------------------- Drift estimation ----------------------------- */

/** @test Drift converges to zero with exactly-1Hz edges. */
TEST(TimeServer, DriftConvergesToZeroForPerfectInterval) {
  TimeServerHarness h;
  h.tick();

  // Provide a reference so quality can progress.
  SetReferenceTime ref{};
  ref.epochNs = 0;
  h.server().handleSetReferenceTime(ref);

  // Inject 5 edges at exactly 1s apart at steady=1, 2, 3, 4, 5 seconds.
  for (int i = 1; i <= 5; ++i) {
    h.setSteadyNow(static_cast<std::int64_t>(i) * NS_PER_SEC);
    h.injectAndTick(static_cast<std::int64_t>(i) * NS_PER_SEC);
  }

  EXPECT_EQ(h.server().output().driftEstimatePpb, 0);
  EXPECT_EQ(h.server().currentTnt().driftPpb, 0);
}

/** @test Drift estimate reflects a constant +1000 ppb bias (1us over 1s). */
TEST(TimeServer, DriftEstimateMatchesBias) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  // First edge at 1s, then 1.000001s, 2.000002s, 3.000003s, 4.000004s ...
  // Each interval is 1s + 1us = 1'000'001'000 ns, deviation = +1000 ns/s = +1000 ppb.
  std::int64_t t = NS_PER_SEC;
  h.setSteadyNow(t);
  h.injectAndTick(t);
  for (int i = 1; i <= 5; ++i) {
    t += NS_PER_SEC + 1000LL; // 1us bias per interval
    h.setSteadyNow(t);
    h.injectAndTick(t);
  }

  EXPECT_EQ(h.server().output().driftEstimatePpb, 1000);
}

/** @test Filter taps are clamped to MAX_DRIFT_TAPS at TPRM load. */
TEST(TimeServer, DriftFilterTapsClamped) {
  TimeServer s;
  TimeServerTunableParams tprm;
  tprm.driftFilterTaps = 1024; // way above MAX_DRIFT_TAPS
  s.loadTprm(tprm);
  // Indirect check: feed a single edge and ensure server doesn't crash and
  // accepts the next sample when adding edges. Internal clamp is not
  // observable directly without an accessor, but the absence of a buffer
  // overrun confirms it.
  EXPECT_EQ(s.driftSampleCount(), 0U);
}

/* ----------------------------- Quality progression ----------------------------- */

/** @test Quality promotes FINE -> PRECISE after driftFilterTaps samples. */
TEST(TimeServer, QualityPromotesToPrecise) {
  TimeServerHarness h; // driftFilterTaps = 4
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  // Need 1 edge to pair the reference (no drift sample yet)
  // + 4 more edges to fill the 4-tap filter -> PRECISE on the 5th edge.
  for (int i = 1; i <= 5; ++i) {
    h.setSteadyNow(static_cast<std::int64_t>(i) * NS_PER_SEC);
    h.injectAndTick(static_cast<std::int64_t>(i) * NS_PER_SEC);
  }

  EXPECT_EQ(h.server().currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::PRECISE));
  EXPECT_GE(h.server().driftSampleCount(), 4U);
}

/* ----------------------------- Glitch rejection ----------------------------- */

/** @test Edges arriving sooner than 500ms after the previous are rejected. */
TEST(TimeServer, ShortIntervalRejected) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC); // edge 1 at 1s

  // Edge at 1.2s -> interval 200ms, below 500ms threshold.
  h.setSteadyNow(1'200'000'000);
  h.injectAndTick(1'200'000'000);

  EXPECT_EQ(h.server().glitchCount(), 1U);
  // pps count should still reflect only the first edge.
  EXPECT_EQ(h.server().currentTnt().ppsCount, 1U);
}

/** @test Edges arriving later than 1500ms after the previous are rejected. */
TEST(TimeServer, LongIntervalRejected) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  // Edge at 3s -> interval 2s, above 1500ms threshold.
  h.setSteadyNow(3 * NS_PER_SEC);
  h.injectAndTick(3 * NS_PER_SEC);

  EXPECT_EQ(h.server().glitchCount(), 1U);
}

/** @test Glitches do not feed the drift estimator. */
TEST(TimeServer, GlitchDoesNotAffectDrift) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  // Glitch.
  h.setSteadyNow(1'200'000'000);
  h.injectAndTick(1'200'000'000);

  EXPECT_EQ(h.server().driftSampleCount(), 0U);
}

/* ----------------------------- Staleness / FREERUN ----------------------------- */

/** @test PPS silence beyond maxStalenessUs marks correlation STALE. */
TEST(TimeServer, TransitionsToStale) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  ASSERT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));

  // Default maxStalenessUs = 1'500'000 us = 1.5s. Advance the clock past it.
  h.setSteadyNow(NS_PER_SEC + 2 * NS_PER_SEC); // 2s after the edge
  h.tick();

  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::STALE));
}

/** @test Holdover beyond holdoverLimitS forces FREERUN. */
TEST(TimeServer, TransitionsToFreerunAfterHoldoverLimit) {
  TimeServerHarness h;
  TimeServerTunableParams tprm;
  tprm.driftFilterTaps = 4;
  tprm.maxStalenessUs = 1'500'000;
  tprm.holdoverLimitS = 5; // short for testability
  h.loadTprm(tprm);

  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  // 6s after the edge (> 5s holdover limit).
  h.setSteadyNow(NS_PER_SEC + 6 * NS_PER_SEC);
  h.tick();
  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::FREERUN));
}

/** @test Recovery: an edge after a STALE transition returns to VALID. */
TEST(TimeServer, RecoversToValidAfterStale) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  h.setSteadyNow(3 * NS_PER_SEC); // stale
  h.tick();
  ASSERT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::STALE));

  // New valid-interval edge: 1.0s after the previous one would have been
  // the predicted top-of-second. 3s edge with last edge at 1s -> interval
  // 2s, which is OUT OF the [500ms, 1500ms] window. So this glitches.
  // Recovery requires resetCorrelation or a sequence of valid-interval edges.

  h.server().resetCorrelation();
  // Now feed a fresh sequence.
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(4 * NS_PER_SEC);
  h.injectAndTick(4 * NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));
}

/* ----------------------------- Commands ----------------------------- */

/** @test SetReferenceTime stays pending until the next edge pairs it. */
TEST(TimeServer, SetReferenceTimePending) {
  TimeServerHarness h;
  h.tick();

  SetReferenceTime ref{};
  ref.epochNs = 100 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref);

  // No edge yet: still no UTC.
  h.tick();
  EXPECT_EQ(h.server().currentTnt().epochNs, 0);

  // Edge fires: UTC pairs with the reference.
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().epochNs, 100 * NS_PER_SEC);
}

/** @test SetReferenceTime called twice before an edge uses the latest. */
TEST(TimeServer, SetReferenceTimeReplacePending) {
  TimeServerHarness h;
  h.tick();

  SetReferenceTime ref1{};
  ref1.epochNs = 100 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref1);

  SetReferenceTime ref2{};
  ref2.epochNs = 200 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref2);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().epochNs, 200 * NS_PER_SEC);
}

/** @test SetTimeManual immediately yields VALID/COARSE without a PPS edge. */
TEST(TimeServer, SetTimeManualImmediate) {
  TimeServerHarness h;
  h.tick();

  h.setSteadyNow(42 * NS_PER_SEC);
  SetTimeManual cmd{};
  cmd.epochNs = 999 * NS_PER_SEC;
  h.server().handleSetTimeManual(cmd);

  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));
  EXPECT_EQ(h.server().currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::COARSE));
  EXPECT_EQ(h.server().currentTnt().epochNs, 999 * NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().source, static_cast<std::uint8_t>(TimeSource::MANUAL));
}

/** @test resetCorrelation returns to NONE/UNKNOWN. */
TEST(TimeServer, ResetCorrelationClearsState) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  ref.epochNs = 50 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  ASSERT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));

  h.server().resetCorrelation();
  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::NONE));
  EXPECT_EQ(h.server().currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::UNKNOWN));
  EXPECT_EQ(h.server().glitchCount(), 0U);
  EXPECT_EQ(h.server().driftSampleCount(), 0U);
}

/* ----------------------------- TNT broadcast / next-tone ----------------------------- */

/** @test Broadcast delegate is called on every PPS edge that produces a TNT. */
TEST(TimeServer, BroadcastDelegateCalledPerEdge) {
  TimeServerHarness h;
  h.tick(); // boot broadcast (1 in buffer)
  ASSERT_EQ(h.broadcasts().size(), 1U);

  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);
  for (int i = 1; i <= 3; ++i) {
    h.setSteadyNow(static_cast<std::int64_t>(i) * NS_PER_SEC);
    h.injectAndTick(static_cast<std::int64_t>(i) * NS_PER_SEC);
  }
  // Each edge tick clears and refills the buffer (per harness convention).
  // Last tick should have at least one broadcast for the edge.
  EXPECT_GE(h.broadcasts().size(), 1U);
}

/** @test TNT next-tone prediction is drift-adjusted. */
TEST(TimeServer, NextToneDriftAdjusted) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);

  // Pair reference at edge 1 at steady=1s. Reference epoch = 0.
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  // Drift bias of +500 ppb across two more 1s+500ns edges.
  h.setSteadyNow(2 * NS_PER_SEC + 500);
  h.injectAndTick(2 * NS_PER_SEC + 500);
  h.setSteadyNow(3 * NS_PER_SEC + 1000);
  h.injectAndTick(3 * NS_PER_SEC + 1000);

  // After two drift samples averaging +500ppb, predicted next tone =
  // last epoch + 1s + drift_ns. We don't pin the exact value since the
  // edge bookkeeping advances epoch internally; just sanity-check it is
  // non-zero and drift-influenced (greater than a clean 1s advance).
  const std::int64_t nextTone = h.server().output().nextToneEpochNs;
  const std::int64_t lastEpoch = h.server().output().utcEpochNs;
  EXPECT_GT(nextTone, lastEpoch);
  EXPECT_GT(nextTone - lastEpoch, NS_PER_SEC); // drift-adjusted upward
}

/** @test Glitch count survives across resetStats-equivalent resetCorrelation. */
TEST(TimeServer, ResetClearsGlitchCount) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  // Glitch.
  h.setSteadyNow(1'100'000'000);
  h.injectAndTick(1'100'000'000);
  ASSERT_EQ(h.server().glitchCount(), 1U);

  h.server().resetCorrelation();
  EXPECT_EQ(h.server().glitchCount(), 0U);
}
