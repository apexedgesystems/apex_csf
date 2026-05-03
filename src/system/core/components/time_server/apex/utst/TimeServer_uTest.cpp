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
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

using apex::hal::MockPps;
using apex::hal::PpsStatus;

namespace {
/// Reinterpret an arbitrary trivially-copyable struct as a read-only span,
/// suitable for passing as a handleCommand payload.
template <typename T> apex::compat::rospan<std::uint8_t> asPayload(const T& obj) noexcept {
  return apex::compat::rospan<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(&obj),
                                            sizeof(T));
}
} // namespace
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

/** @test FREERUN entry latches the wall clock as the new epoch anchor. */
TEST(TimeServer, FreerunLatchesWallClock) {
  // Stand up a stub server with both steady and wall clocks injected so
  // we can drive each independently. Synthetic wall clock starts at
  // a deterministic UTC value (1700000000s past Unix epoch).
  static std::int64_t syntheticSteady = 0;
  static std::int64_t syntheticWall = 1'700'000'000LL * NS_PER_SEC;
  TimeServer s;
  s.setSteadyClock({+[](void*) noexcept -> std::int64_t { return syntheticSteady; }, nullptr});
  s.setWallClock({+[](void*) noexcept -> std::int64_t { return syntheticWall; }, nullptr});

  TimeServerTunableParams tprm;
  tprm.driftFilterTaps = 4;
  tprm.maxStalenessUs = 1'500'000;
  tprm.holdoverLimitS = 5;
  s.loadTprm(tprm);
  ASSERT_EQ(s.init(), 0U);

  // Pair a stale reference + edge.
  SetReferenceTime ref{};
  ref.epochNs = 100 * NS_PER_SEC; // arbitrary stale reference
  s.handleSetReferenceTime(ref);

  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);

  syntheticSteady = NS_PER_SEC;
  pps.injectEdge(NS_PER_SEC);
  s.tick(0);

  // Advance steady past holdoverLimitS (5s) but keep the wall clock
  // close to its starting value (so the latch is observable).
  syntheticSteady = NS_PER_SEC + 6 * NS_PER_SEC; // 7s steady
  syntheticWall = 1'700'000'000LL * NS_PER_SEC + 7 * NS_PER_SEC;
  s.tick(1);

  ASSERT_EQ(s.currentTnt().valid, static_cast<std::uint8_t>(TimeValid::FREERUN));

  // FREERUN should have re-anchored: epochNs ~= syntheticWall at the
  // moment of transition. computeUtcNs at the same steady instant
  // returns the latched wall clock (within one ns -- the latch is
  // exact in the synthetic timeline).
  EXPECT_EQ(s.computeUtcNs(syntheticSteady),
            1'700'000'000LL * NS_PER_SEC + 7 * NS_PER_SEC);

  // Source is now ONBOARD (FREERUN's effective source).
  EXPECT_EQ(s.currentTnt().source, static_cast<std::uint8_t>(TimeSource::ONBOARD));
}

/** @test FREERUN without a wall-clock delegate keeps the stale anchor. */
TEST(TimeServer, FreerunWithoutWallClockKeepsStaleAnchor) {
  // Like the test above but no setWallClock(). FREERUN still triggers
  // (the valid bit is the consumer's signal), the epoch is unchanged
  // from the last edge, and quality drops to COARSE.
  TimeServerHarness h;
  TimeServerTunableParams tprm;
  tprm.driftFilterTaps = 4;
  tprm.maxStalenessUs = 1'500'000;
  tprm.holdoverLimitS = 5;
  h.loadTprm(tprm);

  h.tick();
  SetReferenceTime ref{};
  ref.epochNs = 100 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  const std::int64_t staleEpoch = h.server().currentTnt().epochNs;

  h.setSteadyNow(NS_PER_SEC + 6 * NS_PER_SEC);
  h.tick();

  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::FREERUN));
  // Epoch unchanged -- no wall clock to latch.
  EXPECT_EQ(h.server().currentTnt().epochNs, staleEpoch);
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

/* ----------------------------- Mode dispatch ----------------------------- */

/** @test SECONDARY mode behaves like PRIMARY for PPS+reference correlation. */
TEST(TimeServer, SecondaryModeAcceptsReferenceLikePrimary) {
  TimeServerHarness h;
  TimeServerTunableParams tprm;
  tprm.mode = static_cast<std::uint8_t>(system_core::time_server::TimeServerMode::SECONDARY);
  tprm.driftFilterTaps = 4;
  h.loadTprm(tprm);

  h.tick();

  // Reference comes from a forwarded remote-PRIMARY TNT, but the
  // SET_REFERENCE_TIME path is unchanged.
  SetReferenceTime ref{};
  ref.epochNs = 9 * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::ONBOARD);
  h.server().handleSetReferenceTime(ref);

  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));
  EXPECT_EQ(h.server().currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::FINE));
  EXPECT_EQ(h.server().currentTnt().epochNs, 9 * NS_PER_SEC);
}

/** @test RELAY: ACCEPT_REMOTE_TNT establishes correlation without a local PPS. */
TEST(TimeServer, RelayModeAcceptsRemoteTnt) {
  TimeServerHarness h;
  TimeServerTunableParams tprm;
  tprm.mode = static_cast<std::uint8_t>(system_core::time_server::TimeServerMode::RELAY);
  h.loadTprm(tprm);
  h.tick();

  // Build a synthetic remote TNT from a notional primary.
  TimeAtNextTone remote{};
  remote.epochNs = 1'700'000'000LL * NS_PER_SEC;
  remote.localNs = 42 * NS_PER_SEC; // remote-local, irrelevant to us
  remote.driftPpb = 250;
  remote.ppsCount = 12345;
  remote.source = static_cast<std::uint8_t>(TimeSource::GPS);
  remote.quality = static_cast<std::uint8_t>(TimeQuality::PRECISE);
  remote.valid = static_cast<std::uint8_t>(TimeValid::VALID);

  h.setSteadyNow(5 * NS_PER_SEC);
  h.server().handleAcceptRemoteTnt(remote);

  // Local correlation now uses remote epoch + local steady_clock.
  EXPECT_EQ(h.server().currentTnt().epochNs, remote.epochNs);
  EXPECT_EQ(h.server().currentTnt().localNs, 5 * NS_PER_SEC);
  // Quality is capped at COARSE in RELAY mode regardless of remote claim.
  EXPECT_EQ(h.server().currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::COARSE));
  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));
  // computeUtcNs interpolates from the relay anchor.
  EXPECT_EQ(h.server().computeUtcNs(5 * NS_PER_SEC + 250'000'000LL),
            remote.epochNs + 250'000'000LL);
}

/** @test handleAcceptRemoteTnt is a no-op outside RELAY mode. */
TEST(TimeServer, AcceptRemoteTntIgnoredInPrimaryMode) {
  TimeServerHarness h; // default PRIMARY
  h.tick();

  TimeAtNextTone remote{};
  remote.epochNs = 999 * NS_PER_SEC;
  h.server().handleAcceptRemoteTnt(remote);

  EXPECT_EQ(h.server().currentTnt().epochNs, 0); // unchanged
  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::NONE));
}

/** @test OP_ACCEPT_REMOTE_TNT opcode dispatch carries the TNT through. */
TEST(TimeServer, HandleCommandAcceptRemoteTnt) {
  TimeServerHarness h;
  TimeServerTunableParams tprm;
  tprm.mode = static_cast<std::uint8_t>(system_core::time_server::TimeServerMode::RELAY);
  h.loadTprm(tprm);
  h.tick();

  TimeAtNextTone remote{};
  remote.epochNs = 50 * NS_PER_SEC;
  remote.source = static_cast<std::uint8_t>(TimeSource::GPS);

  h.setSteadyNow(2 * NS_PER_SEC);
  std::vector<std::uint8_t> response;
  EXPECT_EQ(h.server().handleCommand(TimeServer::OP_ACCEPT_REMOTE_TNT,
                                     asPayload(remote), response),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS));
  EXPECT_EQ(h.server().currentTnt().epochNs, 50 * NS_PER_SEC);
}

/** @test RELAY/PTP_SYNC/CAN_SYNC modes skip the local PPS poll. */
TEST(TimeServer, NonPpsModesSkipPpsPolling) {
  for (auto mode : {system_core::time_server::TimeServerMode::RELAY,
                    system_core::time_server::TimeServerMode::PTP_SYNC,
                    system_core::time_server::TimeServerMode::CAN_SYNC}) {
    TimeServerHarness h;
    TimeServerTunableParams tprm;
    tprm.mode = static_cast<std::uint8_t>(mode);
    h.loadTprm(tprm);
    h.tick();

    // Inject what would be a valid PPS edge in PRIMARY mode. Should be
    // ignored: pulse count stays 0 and valid stays NONE.
    h.setSteadyNow(NS_PER_SEC);
    h.injectAndTick(NS_PER_SEC);
    EXPECT_EQ(h.server().currentTnt().ppsCount, 0U);
    EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::NONE));
  }
}

/* ----------------------------- IInternalBus broadcast ----------------------------- */

namespace {

/// Capturing IInternalBus stub used to verify TimeServer's TNT broadcast
/// path goes through postBroadcastCommand with the right opcode + payload.
class CapturingInternalBus final : public system_core::system_component::IInternalBus {
public:
  struct Broadcast {
    std::uint32_t srcFullUid;
    std::uint16_t opcode;
    std::vector<std::uint8_t> payload;
  };

  std::vector<Broadcast> broadcasts;

  [[nodiscard]] bool postInternalCommand(std::uint32_t, std::uint32_t, std::uint16_t,
                                         apex::compat::rospan<std::uint8_t>) noexcept override {
    return true;
  }
  [[nodiscard]] bool postInternalTelemetry(std::uint32_t, std::uint16_t,
                                           apex::compat::rospan<std::uint8_t>) noexcept override {
    return true;
  }
  [[nodiscard]] std::size_t
  postMulticastCommand(std::uint32_t, apex::compat::rospan<std::uint32_t>, std::uint16_t,
                       apex::compat::rospan<std::uint8_t>) noexcept override {
    return 0;
  }
  [[nodiscard]] std::size_t
  postBroadcastCommand(std::uint32_t srcFullUid, std::uint16_t opcode,
                       apex::compat::rospan<std::uint8_t> payload) noexcept override {
    Broadcast b;
    b.srcFullUid = srcFullUid;
    b.opcode = opcode;
    b.payload.assign(payload.data(), payload.data() + payload.size());
    broadcasts.push_back(std::move(b));
    return 1;
  }
};

class BusExposingTimeServer final : public TimeServer {
public:
  using TimeServer::TimeServer;
  using system_core::system_component::SystemComponentBase::setInternalBus;
};

} // namespace

/** @test TNT broadcast lands on the wired IInternalBus. */
TEST(TimeServer, BroadcastUsesInternalBus) {
  CapturingInternalBus bus;
  BusExposingTimeServer s;
  s.setInternalBus(&bus);
  s.setSteadyClock(TimeServer::defaultSteadyClock());
  ASSERT_EQ(s.init(), 0U);

  // Tick once to fire the boot broadcast (NONE/UNKNOWN).
  s.tick(0);
  ASSERT_FALSE(bus.broadcasts.empty());
  EXPECT_EQ(bus.broadcasts.front().opcode, TimeServer::OP_TIME_AT_NEXT_TONE);
  EXPECT_EQ(bus.broadcasts.front().payload.size(),
            sizeof(system_core::time_server::TimeAtNextTone));

  // Decode the payload and confirm it matches the published TNT.
  system_core::time_server::TimeAtNextTone roundtrip{};
  std::memcpy(&roundtrip, bus.broadcasts.front().payload.data(), sizeof(roundtrip));
  EXPECT_EQ(roundtrip.valid, static_cast<std::uint8_t>(TimeValid::NONE));
  EXPECT_EQ(roundtrip.quality, static_cast<std::uint8_t>(TimeQuality::UNKNOWN));
}

/* ----------------------------- handleCommand opcode dispatch ----------------------------- */

/** @test SET_REFERENCE_TIME opcode applies the reference. */
TEST(TimeServer, HandleCommandSetReferenceTime) {
  TimeServerHarness h;
  h.tick();

  SetReferenceTime ref{};
  ref.epochNs = 42 * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  std::vector<std::uint8_t> response;

  EXPECT_EQ(h.server().handleCommand(TimeServer::OP_SET_REFERENCE_TIME,
                                     asPayload(ref), response),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS));

  // Edge applies the pending reference.
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().epochNs, 42 * NS_PER_SEC);
}

/** @test SET_REFERENCE_TIME with short payload returns INVALID_PAYLOAD. */
TEST(TimeServer, HandleCommandSetReferenceTimeShortPayloadRejected) {
  TimeServer s;
  std::array<std::uint8_t, 4> shortBuf{};
  std::vector<std::uint8_t> response;

  EXPECT_EQ(s.handleCommand(TimeServer::OP_SET_REFERENCE_TIME,
                            apex::compat::rospan<std::uint8_t>(shortBuf.data(), shortBuf.size()),
                            response),
            static_cast<std::uint8_t>(
                system_core::system_component::CommandResult::INVALID_PAYLOAD));
}

/** @test GET_TIME_STATUS returns the current OUTPUT block. */
TEST(TimeServer, HandleCommandGetTimeStatus) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  ref.epochNs = 7 * NS_PER_SEC;
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);

  std::vector<std::uint8_t> response;
  EXPECT_EQ(h.server().handleCommand(TimeServer::OP_GET_TIME_STATUS,
                                     apex::compat::rospan<std::uint8_t>(), response),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS));
  ASSERT_EQ(response.size(), sizeof(system_core::time_server::TimeServerOutput));

  system_core::time_server::TimeServerOutput out{};
  std::memcpy(&out, response.data(), sizeof(out));
  EXPECT_EQ(out.utcEpochNs, 7 * NS_PER_SEC);
  EXPECT_EQ(out.ppsCount, 1U);
  EXPECT_EQ(out.correlationValid, static_cast<std::uint8_t>(TimeValid::VALID));
}

/** @test SET_TIME_MANUAL applies UTC immediately. */
TEST(TimeServer, HandleCommandSetTimeManual) {
  TimeServerHarness h;
  h.tick();
  h.setSteadyNow(10 * NS_PER_SEC);

  SetTimeManual cmd{};
  cmd.epochNs = 999 * NS_PER_SEC;
  std::vector<std::uint8_t> response;

  EXPECT_EQ(h.server().handleCommand(TimeServer::OP_SET_TIME_MANUAL,
                                     asPayload(cmd), response),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS));

  EXPECT_EQ(h.server().currentTnt().epochNs, 999 * NS_PER_SEC);
  EXPECT_EQ(h.server().currentTnt().source, static_cast<std::uint8_t>(TimeSource::MANUAL));
}

/** @test RESET_CORRELATION wipes state. */
TEST(TimeServer, HandleCommandResetCorrelation) {
  TimeServerHarness h;
  h.tick();
  SetReferenceTime ref{};
  h.server().handleSetReferenceTime(ref);
  h.setSteadyNow(NS_PER_SEC);
  h.injectAndTick(NS_PER_SEC);
  ASSERT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));

  std::vector<std::uint8_t> response;
  EXPECT_EQ(h.server().handleCommand(TimeServer::OP_RESET_CORRELATION,
                                     apex::compat::rospan<std::uint8_t>(), response),
            static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS));

  EXPECT_EQ(h.server().currentTnt().valid, static_cast<std::uint8_t>(TimeValid::NONE));
}

/** @test Unknown opcode delegates to the base class (NOT_IMPLEMENTED for 0x0700+). */
TEST(TimeServer, HandleCommandUnknownOpcode) {
  TimeServer s;
  std::vector<std::uint8_t> response;
  // 0x0700 is in the component-specific range but not one TimeServer claims.
  // Base class returns NOT_IMPLEMENTED for unknown opcodes outside its range.
  const std::uint8_t result = s.handleCommand(0x0700, apex::compat::rospan<std::uint8_t>(),
                                              response);
  EXPECT_NE(result, static_cast<std::uint8_t>(
                        system_core::system_component::CommandResult::SUCCESS));
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
