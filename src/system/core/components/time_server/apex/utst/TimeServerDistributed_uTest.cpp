/**
 * @file TimeServerDistributed_uTest.cpp
 * @brief Multi-TimeServer distributed-timing tests.
 *
 * Wires two or more TimeServer instances together to exercise the
 * distributed-timing modes end-to-end:
 *
 *  - PRIMARY produces TNTs from a shared synthetic PPS line; SECONDARY
 *    consumes the PRIMARY's reference time and correlates against the
 *    same edges. Verifies that both nodes converge to the same UTC
 *    interpolation within one PPS period.
 *  - PRIMARY produces TNTs; RELAY accepts them through
 *    handleAcceptRemoteTnt without a local PPS source. Verifies that
 *    the RELAY's published epoch tracks the PRIMARY's and that quality
 *    is degraded to COARSE on the relay path.
 *  - Quality propagation: as the PRIMARY climbs from FINE to PRECISE,
 *    SECONDARY follows (tied to its own driftFilterTaps), while the
 *    RELAY stays capped at COARSE because its uncertainty is dominated
 *    by network latency, not local PPS jitter.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using apex::hal::MockPps;
using system_core::time_server::SetReferenceTime;
using system_core::time_server::TimeAtNextTone;
using system_core::time_server::TimeQuality;
using system_core::time_server::TimeServer;
using system_core::time_server::TimeServerMode;
using system_core::time_server::TimeServerTunableParams;
using system_core::time_server::TimeSource;
using system_core::time_server::TimeValid;

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Common synthetic steady clock shared by two TimeServer instances when
/// they live on the same simulated platform (PRIMARY + SECONDARY case).
struct SharedSteady {
  std::int64_t ns = 0;
  static std::int64_t trampoline(void* ctx) noexcept { return static_cast<SharedSteady*>(ctx)->ns; }
};

/// Per-node steady clock used when nodes run on separate simulated hosts
/// (PRIMARY + RELAY case): the relay's local time is independent of the
/// primary's local time.
struct LocalSteady {
  std::int64_t ns = 0;
  static std::int64_t trampoline(void* ctx) noexcept { return static_cast<LocalSteady*>(ctx)->ns; }
};

void wireServer(TimeServer& s, void* ctx, std::int64_t (*clk)(void*) noexcept) noexcept {
  s.setSteadyClock({clk, ctx});
  // Tests that observe broadcasts override this; default is a no-op.
  s.setBroadcastDelegate({+[](void*, const TimeAtNextTone&) noexcept {}, nullptr});
}

} // namespace

/* ----------------------------- PRIMARY + SECONDARY ----------------------------- */

/** @test PRIMARY and SECONDARY both consume the shared PPS line; SECONDARY's
 *        UTC tracks the PRIMARY's after the SET_REFERENCE_TIME forward. */
TEST(TimeServerDistributed, PrimaryAndSecondaryAgreeOnUtc) {
  SharedSteady steady;

  // PRIMARY: PPS-driven, owns the reference time.
  TimeServer primary;
  MockPps primaryPps;
  (void)primaryPps.init({});
  primary.setPpsSource(&primaryPps);
  wireServer(primary, &steady, &SharedSteady::trampoline);

  // SECONDARY: PPS-driven, accepts reference forwarded from the primary.
  TimeServer secondary;
  MockPps secondaryPps;
  (void)secondaryPps.init({});
  secondary.setPpsSource(&secondaryPps);
  wireServer(secondary, &steady, &SharedSteady::trampoline);

  TimeServerTunableParams secondaryTprm;
  secondaryTprm.mode = static_cast<std::uint8_t>(TimeServerMode::SECONDARY);
  secondaryTprm.driftFilterTaps = 4;
  secondary.loadTprm(secondaryTprm);

  ASSERT_EQ(primary.init(), 0U);
  ASSERT_EQ(secondary.init(), 0U);

  // PRIMARY ingests a GPS reference and pairs it with the first edge.
  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  primary.handleSetReferenceTime(ref);

  steady.ns = NS_PER_SEC;
  primaryPps.injectEdge(steady.ns);
  secondaryPps.injectEdge(steady.ns); // same physical PPS line
  primary.tick(0);

  // Forward the PRIMARY's TNT as the SECONDARY's reference. In production
  // this is the OP_FORWARD_REFERENCE_TIME path; here we just call the
  // command handler directly.
  SetReferenceTime forward{};
  forward.epochNs = primary.currentTnt().epochNs;
  forward.source = static_cast<std::uint8_t>(TimeSource::ONBOARD);
  secondary.handleSetReferenceTime(forward);
  secondary.tick(0);

  // Both nodes interpolate UTC against the same steady clock; they must
  // produce identical UTC at any subsequent instant.
  steady.ns = NS_PER_SEC + 250'000'000LL;
  EXPECT_EQ(primary.computeUtcNs(steady.ns), secondary.computeUtcNs(steady.ns));
}

/** @test SECONDARY tracks PRIMARY through several PPS edges; both reach
 *        PRECISE quality after the drift filter fills. */
TEST(TimeServerDistributed, SecondaryConvergesToPreciseWithPrimary) {
  SharedSteady steady;

  TimeServer primary;
  MockPps primaryPps;
  (void)primaryPps.init({});
  primary.setPpsSource(&primaryPps);
  wireServer(primary, &steady, &SharedSteady::trampoline);

  TimeServer secondary;
  MockPps secondaryPps;
  (void)secondaryPps.init({});
  secondary.setPpsSource(&secondaryPps);
  wireServer(secondary, &steady, &SharedSteady::trampoline);

  TimeServerTunableParams primaryTprm;
  primaryTprm.driftFilterTaps = 4;
  primary.loadTprm(primaryTprm);

  TimeServerTunableParams secondaryTprm;
  secondaryTprm.mode = static_cast<std::uint8_t>(TimeServerMode::SECONDARY);
  secondaryTprm.driftFilterTaps = 4;
  secondary.loadTprm(secondaryTprm);

  ASSERT_EQ(primary.init(), 0U);
  ASSERT_EQ(secondary.init(), 0U);

  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  primary.handleSetReferenceTime(ref);
  // SECONDARY accepts the forwarded reference once.
  SetReferenceTime forward{};
  forward.epochNs = ref.epochNs;
  forward.source = static_cast<std::uint8_t>(TimeSource::ONBOARD);
  secondary.handleSetReferenceTime(forward);

  // Five edges: enough for both nodes to fill driftFilterTaps and reach PRECISE.
  for (int i = 1; i <= 5; ++i) {
    steady.ns = static_cast<std::int64_t>(i) * NS_PER_SEC;
    primaryPps.injectEdge(steady.ns);
    secondaryPps.injectEdge(steady.ns);
    primary.tick(static_cast<std::uint32_t>(i));
    secondary.tick(static_cast<std::uint32_t>(i));
  }

  EXPECT_EQ(primary.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::PRECISE));
  EXPECT_EQ(secondary.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::PRECISE));
  // ppsCount is independent (each MockPps is its own counter) but the
  // computed UTC must match.
  EXPECT_EQ(primary.computeUtcNs(steady.ns), secondary.computeUtcNs(steady.ns));
}

/* ----------------------------- PRIMARY + RELAY ----------------------------- */

/** @test PRIMARY publishes TNT; RELAY (no local PPS) anchors at receipt and
 *        reports the PRIMARY's epoch with COARSE quality. */
TEST(TimeServerDistributed, RelayTracksPrimaryEpochWithDegradedQuality) {
  // PRIMARY runs on its own steady clock.
  LocalSteady primarySteady;
  TimeServer primary;
  MockPps primaryPps;
  (void)primaryPps.init({});
  primary.setPpsSource(&primaryPps);
  wireServer(primary, &primarySteady, &LocalSteady::trampoline);

  TimeServerTunableParams primaryTprm;
  primaryTprm.driftFilterTaps = 4;
  primary.loadTprm(primaryTprm);

  // RELAY runs on a separate steady clock, no local PPS.
  LocalSteady relaySteady;
  TimeServer relay;
  wireServer(relay, &relaySteady, &LocalSteady::trampoline);

  TimeServerTunableParams relayTprm;
  relayTprm.mode = static_cast<std::uint8_t>(TimeServerMode::RELAY);
  relay.loadTprm(relayTprm);

  ASSERT_EQ(primary.init(), 0U);
  ASSERT_EQ(relay.init(), 0U);

  // Drive the PRIMARY to a stable VALID/PRECISE state.
  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  primary.handleSetReferenceTime(ref);
  for (int i = 1; i <= 5; ++i) {
    primarySteady.ns = static_cast<std::int64_t>(i) * NS_PER_SEC;
    primaryPps.injectEdge(primarySteady.ns);
    primary.tick(static_cast<std::uint32_t>(i));
  }
  ASSERT_EQ(primary.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::PRECISE));

  // Capture the PRIMARY's TNT and forward it to the RELAY at a chosen
  // local instant. Network latency is implicitly folded into the offset.
  const TimeAtNextTone primaryTnt = primary.currentTnt();
  relaySteady.ns = 100 * NS_PER_SEC;
  relay.handleAcceptRemoteTnt(primaryTnt);

  EXPECT_EQ(relay.currentTnt().epochNs, primaryTnt.epochNs);
  EXPECT_EQ(relay.currentTnt().valid, static_cast<std::uint8_t>(TimeValid::VALID));
  // Even though PRIMARY claimed PRECISE, RELAY caps at COARSE because the
  // network/IPC link delay dominates the uncertainty.
  EXPECT_EQ(relay.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::COARSE));

  // RELAY's UTC must interpolate forward from the same epoch using the
  // RELAY's local steady clock.
  EXPECT_EQ(relay.computeUtcNs(relaySteady.ns + 500'000'000LL), primaryTnt.epochNs + 500'000'000LL);
}

/** @test Quality propagation: PRIMARY climbing FINE -> PRECISE does not lift
 *        the RELAY past COARSE; the relay's quality is link-bound, not
 *        primary-bound. */
TEST(TimeServerDistributed, RelayQualityCapDoesNotFollowPrimary) {
  LocalSteady primarySteady;
  TimeServer primary;
  MockPps primaryPps;
  (void)primaryPps.init({});
  primary.setPpsSource(&primaryPps);
  wireServer(primary, &primarySteady, &LocalSteady::trampoline);

  TimeServerTunableParams primaryTprm;
  primaryTprm.driftFilterTaps = 4;
  primary.loadTprm(primaryTprm);

  LocalSteady relaySteady;
  TimeServer relay;
  wireServer(relay, &relaySteady, &LocalSteady::trampoline);

  TimeServerTunableParams relayTprm;
  relayTprm.mode = static_cast<std::uint8_t>(TimeServerMode::RELAY);
  relay.loadTprm(relayTprm);

  ASSERT_EQ(primary.init(), 0U);
  ASSERT_EQ(relay.init(), 0U);

  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  primary.handleSetReferenceTime(ref);

  // Edge 1: PRIMARY -> FINE; forward TNT to RELAY -> COARSE.
  primarySteady.ns = NS_PER_SEC;
  primaryPps.injectEdge(primarySteady.ns);
  primary.tick(1);
  ASSERT_EQ(primary.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::FINE));

  relaySteady.ns = 10 * NS_PER_SEC;
  relay.handleAcceptRemoteTnt(primary.currentTnt());
  EXPECT_EQ(relay.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::COARSE));

  // Edges 2..5: PRIMARY climbs to PRECISE.
  for (int i = 2; i <= 5; ++i) {
    primarySteady.ns = static_cast<std::int64_t>(i) * NS_PER_SEC;
    primaryPps.injectEdge(primarySteady.ns);
    primary.tick(static_cast<std::uint32_t>(i));
  }
  ASSERT_EQ(primary.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::PRECISE));

  // RELAY accepts the now-PRECISE primary TNT; quality stays COARSE.
  relaySteady.ns = 20 * NS_PER_SEC;
  relay.handleAcceptRemoteTnt(primary.currentTnt());
  EXPECT_EQ(relay.currentTnt().quality, static_cast<std::uint8_t>(TimeQuality::COARSE));
  // But the epoch tracks the primary.
  EXPECT_EQ(relay.currentTnt().epochNs, primary.currentTnt().epochNs);
}
