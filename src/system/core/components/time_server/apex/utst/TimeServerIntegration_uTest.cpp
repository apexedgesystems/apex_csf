/**
 * @file TimeServerIntegration_uTest.cpp
 * @brief Integration tests for TimeServer + ATS time-provider wiring.
 *
 * These tests verify the contract between TimeServer and ActionComponent
 * that ApexExecutive establishes in its registerComponents() phase --
 * specifically that ActionComponent.iface().timeProvider, when wired to
 * TimeServer.utcTimeProvider(), returns 0 microseconds before correlation
 * and live UTC microseconds after a PPS edge pairs with a reference time.
 *
 * No real ApexExecutive instance is constructed; these tests stitch the
 * components together manually so the boot sequence, edge pairing, and
 * time-provider readback are all under deterministic control via the
 * SteadyClockDelegate.
 */

#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"
#include "src/system/core/components/action/apex/inc/DataAction.hpp"
#include "src/system/core/components/action/apex/inc/DataSequence.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using apex::hal::MockPps;
using system_core::action::ActionComponent;
using system_core::time_server::SetReferenceTime;
using system_core::time_server::TimeAtNextTone;
using system_core::time_server::TimeQuality;
using system_core::time_server::TimeServer;
using system_core::time_server::TimeServerTunableParams;
using system_core::time_server::TimeSource;
using system_core::time_server::TimeValid;

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Mirrors the ApexExecutive registerComponents() wiring without booting a
/// real executive. Owns the synthetic steady clock and MockPps.
struct ExecutiveStub {
  ExecutiveStub() {
    (void)pps.init({});
    timeServer.setPpsSource(&pps);
    timeServer.setSteadyClock({&ExecutiveStub::steadyTrampoline, this});
    timeServer.setBroadcastDelegate({&ExecutiveStub::broadcastTrampoline, this});

    TimeServerTunableParams tprm;
    tprm.driftFilterTaps = 4;
    tprm.maxStalenessUs = 1'500'000;
    tprm.holdoverLimitS = 60;
    timeServer.loadTprm(tprm);

    // Wire the time provider exactly as the executive does.
    actionComp.iface().timeProvider = timeServer.utcTimeProvider();
  }

  void setSteadyNow(std::int64_t ns) noexcept { steadyNs = ns; }
  std::int64_t steadyNow() const noexcept { return steadyNs; }

  /// Simulate one frame: tick TimeServer first, then ActionComponent.
  void frame(std::uint32_t cycle) noexcept {
    timeServer.tick(cycle);
    actionComp.tick(cycle);
  }

  /// Inject a PPS edge at the current steady time and run one frame.
  void edgeAt(std::int64_t edgeNs, std::uint32_t cycle = 0) {
    setSteadyNow(edgeNs);
    pps.injectEdge(edgeNs);
    frame(cycle);
  }

  TimeServer timeServer;
  ActionComponent actionComp;
  MockPps pps;
  std::int64_t steadyNs = 0;
  std::vector<TimeAtNextTone> tnts;

private:
  static std::int64_t steadyTrampoline(void* ctx) noexcept {
    return static_cast<ExecutiveStub*>(ctx)->steadyNs;
  }
  static void broadcastTrampoline(void* ctx, const TimeAtNextTone& tnt) noexcept {
    static_cast<ExecutiveStub*>(ctx)->tnts.push_back(tnt);
  }
};

} // namespace

/* ----------------------------- Time provider behaviour ----------------------------- */

/** @test Time provider returns 0 before any correlation is established. */
TEST(TimeServerIntegration, TimeProviderZeroBeforeCorrelation) {
  ExecutiveStub stub;
  stub.frame(0); // boot tick, no edge yet
  EXPECT_EQ(stub.actionComp.iface().timeProvider(), 0U);
}

/** @test Time provider returns live UTC microseconds after edge + reference. */
TEST(TimeServerIntegration, TimeProviderReturnsUtcAfterPairing) {
  ExecutiveStub stub;
  stub.frame(0);

  // Reference time = 1700000000s past Unix epoch (in ns).
  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  stub.timeServer.handleSetReferenceTime(ref);

  // Edge at steady = 5s.
  stub.edgeAt(5 * NS_PER_SEC, 1);

  // At the edge moment: UTC = 1700000000 s = 1700000000000000 us.
  EXPECT_EQ(stub.actionComp.iface().timeProvider(),
            1'700'000'000ULL * 1'000'000ULL);

  // 250 ms later: UTC = epoch + 250 ms.
  stub.setSteadyNow(5 * NS_PER_SEC + 250'000'000LL);
  EXPECT_EQ(stub.actionComp.iface().timeProvider(),
            1'700'000'000ULL * 1'000'000ULL + 250'000ULL);
}

/** @test Time provider tracks steady-clock advancement between edges. */
TEST(TimeServerIntegration, TimeProviderInterpolates) {
  ExecutiveStub stub;
  stub.frame(0);
  SetReferenceTime ref{};
  ref.epochNs = 0;
  stub.timeServer.handleSetReferenceTime(ref);

  stub.edgeAt(NS_PER_SEC, 1); // pair at t=1s, UTC=0
  ASSERT_EQ(stub.actionComp.iface().timeProvider(), 0U);

  // Each ms of steady advance == 1000 us of UTC.
  for (int ms = 1; ms <= 10; ++ms) {
    stub.setSteadyNow(NS_PER_SEC + static_cast<std::int64_t>(ms) * 1'000'000LL);
    EXPECT_EQ(stub.actionComp.iface().timeProvider(),
              static_cast<std::uint64_t>(ms) * 1000ULL);
  }
}

/* ----------------------------- Boot convergence ----------------------------- */

/** @test Boot convergence: NONE -> FINE -> PRECISE through the wired stack. */
TEST(TimeServerIntegration, BootConvergence) {
  ExecutiveStub stub;
  stub.frame(0);

  // Boot: NONE.
  EXPECT_EQ(stub.timeServer.currentTnt().valid,
            static_cast<std::uint8_t>(TimeValid::NONE));
  EXPECT_EQ(stub.timeServer.currentTnt().quality,
            static_cast<std::uint8_t>(TimeQuality::UNKNOWN));

  // Reference + 1st edge -> VALID/FINE.
  SetReferenceTime ref{};
  stub.timeServer.handleSetReferenceTime(ref);
  stub.edgeAt(NS_PER_SEC, 1);
  EXPECT_EQ(stub.timeServer.currentTnt().valid,
            static_cast<std::uint8_t>(TimeValid::VALID));
  EXPECT_EQ(stub.timeServer.currentTnt().quality,
            static_cast<std::uint8_t>(TimeQuality::FINE));

  // 4 more edges fill the drift filter (TPRM driftFilterTaps = 4) -> PRECISE.
  for (int i = 2; i <= 5; ++i) {
    stub.edgeAt(static_cast<std::int64_t>(i) * NS_PER_SEC, static_cast<std::uint32_t>(i));
  }
  EXPECT_EQ(stub.timeServer.currentTnt().quality,
            static_cast<std::uint8_t>(TimeQuality::PRECISE));
}

/** @test Time provider drops to 0 when correlation is reset. */
TEST(TimeServerIntegration, TimeProviderZeroAfterReset) {
  ExecutiveStub stub;
  stub.frame(0);
  SetReferenceTime ref{};
  ref.epochNs = 100 * NS_PER_SEC;
  stub.timeServer.handleSetReferenceTime(ref);
  stub.edgeAt(NS_PER_SEC, 1);
  ASSERT_GT(stub.actionComp.iface().timeProvider(), 0U);

  stub.timeServer.resetCorrelation();
  EXPECT_EQ(stub.actionComp.iface().timeProvider(), 0U);
}

/* ----------------------------- TNT broadcast ----------------------------- */

/** @test Boot tick broadcasts a NONE TNT through the wired delegate. */
TEST(TimeServerIntegration, BootTntBroadcastReachesDelegate) {
  ExecutiveStub stub;
  stub.frame(0);
  ASSERT_FALSE(stub.tnts.empty());
  EXPECT_EQ(stub.tnts.front().valid, static_cast<std::uint8_t>(TimeValid::NONE));
}

/** @test Each PPS edge produces one TNT broadcast. */
TEST(TimeServerIntegration, EdgeProducesTntBroadcast) {
  ExecutiveStub stub;
  stub.frame(0);
  const std::size_t baseline = stub.tnts.size();

  SetReferenceTime ref{};
  stub.timeServer.handleSetReferenceTime(ref);
  stub.edgeAt(NS_PER_SEC, 1);

  EXPECT_GT(stub.tnts.size(), baseline);
  EXPECT_EQ(stub.tnts.back().ppsCount, 1U);
}

/* ----------------------------- ATS AT_TIME end-to-end ----------------------------- */

/**
 * @test An ATS sequence with an AT_TIME-style step fires only once UTC
 *       crosses the target.
 *
 * Builds the smallest possible ATS sequence (one step, fixed delay), wires it
 * through ActionInterface.timeProvider (which the executive sets to
 * TimeServer.utcTimeProvider()), then drives synthetic time so UTC crosses
 * the target. Asserts the action is enqueued only after the crossing.
 */
TEST(TimeServerIntegration, AtsAtTimeTriggersFireWhenUtcCrosses) {
  ExecutiveStub stub;
  stub.frame(0);

  // Establish correlation at UTC=0 (epoch base), steady=1s, ppsCount=1.
  SetReferenceTime ref{};
  ref.epochNs = 0;
  stub.timeServer.handleSetReferenceTime(ref);
  stub.edgeAt(NS_PER_SEC, 1);
  ASSERT_EQ(stub.actionComp.iface().timeProvider(), 0U);

  // Build a 1-step ATS sequence in slot 0. Step delay = 5_000_000 us = 5 s
  // UTC after sequence start. With sequence.startTime = 0, the step fires
  // when timeProvider() >= 5_000_000.
  auto& iface = stub.actionComp.iface();
  iface.sequences[0] = system_core::data::DataSequence{};
  auto& seq = iface.sequences[0];
  seq.type = system_core::data::SequenceType::ATS;
  seq.armed = true;
  seq.stepCount = 1;
  seq.sequenceId = 0xABCD;
  seq.steps[0].delayCycles = 5'000'000U;                // 5 s in microseconds
  seq.steps[0].action.trigger = system_core::data::ActionTrigger::AT_TIME;
  seq.steps[0].action.triggerParam = 5'000'000U;
  seq.steps[0].action.actionType = system_core::data::ActionType::COMMAND;
  seq.steps[0].action.commandOpcode = 0x0001;

  system_core::data::startSequence(seq, iface.timeProvider());
  ASSERT_EQ(seq.status, system_core::data::SequenceStatus::WAITING);

  // Cycle 1: UTC = 1s (steady=2s, edge at steady=1s with ref=0). Step still
  // waiting -- 1s << 5s target.
  stub.setSteadyNow(2 * NS_PER_SEC);
  system_core::data::tickSequences(iface, 1);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING);
  EXPECT_EQ(iface.actionCount, 0U);

  // Cycle 4: UTC ~= 4.9s. Just under the threshold.
  stub.setSteadyNow(NS_PER_SEC + 4'900'000'000LL);
  system_core::data::tickSequences(iface, 4);
  EXPECT_EQ(seq.status, system_core::data::SequenceStatus::WAITING);
  EXPECT_EQ(iface.actionCount, 0U);

  // Cycle 5: UTC = 5.1s. Past target -- step fires (action enqueued and
  // sequence moves out of WAITING).
  stub.setSteadyNow(NS_PER_SEC + 5'100'000'000LL);
  system_core::data::tickSequences(iface, 5);
  EXPECT_NE(seq.status, system_core::data::SequenceStatus::WAITING);
  EXPECT_EQ(iface.actionCount, 1U);
  EXPECT_EQ(iface.actions[0].commandOpcode, 0x0001);
}
