/**
 * @file TimeServer_pTest.cpp
 * @brief Performance tests for TimeServer hot paths.
 *
 * Measures per-frame and per-edge overhead of the public API surface that
 * runs on the executive's frame loop or on every ATS evaluation:
 *
 *  - tick() bare overhead (no PPS source wired)
 *  - tick() with PPS source but no edge available (NO_NEW_EDGE path)
 *  - tick() consuming one edge per iteration (full correlation + drift + publish)
 *  - tick() with glitched edges (interval out of [500ms, 1500ms]; rejected)
 *  - utcTimeProvider() trampoline (called once per ATS frame from ActionInterface)
 *  - computeUtcNs() interpolation math
 *  - handleSetReferenceTime() command path
 *  - handleAcceptRemoteTnt() RELAY mode anchor
 *
 * Usage:
 *   # Run all tests, export CSV
 *   ./TimeServer_PTEST --csv results.csv
 *
 *   # Quick iteration during development
 *   ./TimeServer_PTEST --quick
 *
 *   # CPU sampling profile (gperftools)
 *   CPUPROFILE_FREQUENCY=1000 ./TimeServer_PTEST --profile gperf --cycles 100000
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace ub = vernier::bench;
using apex::hal::MockPps;
using system_core::time_server::SetReferenceTime;
using system_core::time_server::TimeAtNextTone;
using system_core::time_server::TimeServer;
using system_core::time_server::TimeServerMode;
using system_core::time_server::TimeServerTunableParams;
using system_core::time_server::TimeSource;

/* ----------------------------- Constants ----------------------------- */

namespace {

constexpr std::int64_t NS_PER_SEC = 1'000'000'000LL;

/// Synthetic steady clock state shared across tests via the trampolines.
/// Each test resets it in setup so cross-test ordering does not leak.
std::int64_t g_steadyNs = 0;

std::int64_t steadyClockTrampoline(void* /*ctx*/) noexcept { return g_steadyNs; }

void noopBroadcast(void* /*ctx*/, const TimeAtNextTone& /*tnt*/) noexcept {
  // intentionally empty -- bench measures publish() cost up to the delegate
}

/* ----------------------------- Test Helpers ----------------------------- */

/// Wire a TimeServer in place with synthetic clocks, no-op broadcast, and
/// PRIMARY-mode TPRM. TimeServer is non-copyable / non-movable, so the bench
/// constructs each instance in place and configures via reference.
void wireServer(TimeServer& s) noexcept {
  s.setSteadyClock({steadyClockTrampoline, nullptr});
  s.setBroadcastDelegate({noopBroadcast, nullptr});
  TimeServerTunableParams tprm;
  tprm.driftFilterTaps = 16;
  tprm.maxStalenessUs = 1'500'000;
  tprm.holdoverLimitS = 60;
  s.loadTprm(tprm);
}

/// Pair a reference + initial edge so the server is in VALID/FINE before
/// measurement starts. Subsequent edges drive drift estimation.
void primeCorrelation(TimeServer& s, MockPps& pps) noexcept {
  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);
  s.handleSetReferenceTime(ref);
  g_steadyNs = NS_PER_SEC;
  (void)pps.injectEdge(g_steadyNs);
  s.tick(0);
}

} // namespace

/* ----------------------------- tick() overhead ----------------------------- */

/** @brief Baseline: tick() with no PPS source wired; just frame bookkeeping. */
PERF_TEST(TimeServerPerf, TickNoSource) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  ASSERT_EQ(s.init(), 0U);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      s.tick(static_cast<std::uint32_t>(i));
    }
  });

  std::uint32_t cycle = 0;
  auto result = perf.throughputLoop([&] { s.tick(cycle++); }, "tick_no_source");
}

/** @brief tick() with PPS source set but no edge available (NO_NEW_EDGE poll). */
PERF_TEST(TimeServerPerf, TickNoEdge) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);
  ASSERT_EQ(s.init(), 0U);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      s.tick(static_cast<std::uint32_t>(i));
    }
  });

  std::uint32_t cycle = 0;
  auto result = perf.throughputLoop([&] { s.tick(cycle++); }, "tick_no_edge");
}

/** @brief tick() consuming a fresh edge each iteration (full correlation path). */
PERF_TEST(TimeServerPerf, TickWithEdge) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);
  ASSERT_EQ(s.init(), 0U);
  primeCorrelation(s, pps);

  // Each iteration: advance synthetic time by 1 s, inject edge, tick.
  // MockPps queue is bounded (16); injecting one per iteration matches consumption.
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      g_steadyNs += NS_PER_SEC;
      (void)pps.injectEdge(g_steadyNs);
      s.tick(static_cast<std::uint32_t>(i));
    }
  });

  std::uint32_t cycle = 0;
  auto result = perf.throughputLoop(
      [&] {
        g_steadyNs += NS_PER_SEC;
        (void)pps.injectEdge(g_steadyNs);
        s.tick(cycle++);
      },
      "tick_with_edge");
}

/** @brief tick() rejecting glitched intervals (correlation untouched). */
PERF_TEST(TimeServerPerf, TickGlitch) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);
  ASSERT_EQ(s.init(), 0U);
  primeCorrelation(s, pps);

  // Glitched edges arrive 200 ms apart -> below MIN_VALID_INTERVAL_NS.
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      g_steadyNs += 200'000'000LL;
      (void)pps.injectEdge(g_steadyNs);
      s.tick(static_cast<std::uint32_t>(i));
    }
  });

  std::uint32_t cycle = 0;
  auto result = perf.throughputLoop(
      [&] {
        g_steadyNs += 200'000'000LL;
        (void)pps.injectEdge(g_steadyNs);
        s.tick(cycle++);
      },
      "tick_glitch");
}

/* ----------------------------- ATS time-provider hot path ----------------------------- */

/** @brief utcTimeProvider() trampoline cost (called every ATS frame). */
PERF_TEST(TimeServerPerf, UtcTimeProvider) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);
  ASSERT_EQ(s.init(), 0U);
  primeCorrelation(s, pps);

  apex::time::TimeProviderDelegate provider = s.utcTimeProvider();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      g_steadyNs += 1'000;
      (void)provider();
    }
  });

  volatile std::uint64_t sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        g_steadyNs += 1'000;
        sink ^= provider();
      },
      "utc_time_provider");
}

/** @brief computeUtcNs() pure interpolation (per-call math only). */
PERF_TEST(TimeServerPerf, ComputeUtcNs) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  MockPps pps;
  (void)pps.init({});
  s.setPpsSource(&pps);
  ASSERT_EQ(s.init(), 0U);
  primeCorrelation(s, pps);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)s.computeUtcNs(g_steadyNs + i);
    }
  });

  volatile std::int64_t sink = 0;
  std::int64_t now = g_steadyNs;
  auto result = perf.throughputLoop([&] { sink ^= s.computeUtcNs(now++); }, "compute_utc_ns");
}

/* ----------------------------- Command paths ----------------------------- */

/** @brief handleSetReferenceTime() command path (stores pending reference). */
PERF_TEST(TimeServerPerf, HandleSetReferenceTime) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  ASSERT_EQ(s.init(), 0U);

  SetReferenceTime ref{};
  ref.epochNs = 1'700'000'000LL * NS_PER_SEC;
  ref.source = static_cast<std::uint8_t>(TimeSource::GPS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      s.handleSetReferenceTime(ref);
    }
  });

  auto result =
      perf.throughputLoop([&] { s.handleSetReferenceTime(ref); }, "handle_set_reference_time");
}

/** @brief handleAcceptRemoteTnt() RELAY mode anchor (network TNT receipt). */
PERF_TEST(TimeServerPerf, HandleAcceptRemoteTnt) {
  UB_PERF_GUARD(perf);
  g_steadyNs = 0;

  TimeServer s;
  wireServer(s);
  TimeServerTunableParams tprm;
  tprm.mode = static_cast<std::uint8_t>(TimeServerMode::RELAY);
  tprm.driftFilterTaps = 16;
  tprm.maxStalenessUs = 1'500'000;
  tprm.holdoverLimitS = 60;
  s.loadTprm(tprm);
  ASSERT_EQ(s.init(), 0U);

  TimeAtNextTone remote{};
  remote.epochNs = 1'700'000'000LL * NS_PER_SEC;
  remote.driftPpb = 250;
  remote.ppsCount = 1;
  remote.source = static_cast<std::uint8_t>(TimeSource::GPS);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      g_steadyNs += NS_PER_SEC;
      remote.epochNs += NS_PER_SEC;
      s.handleAcceptRemoteTnt(remote);
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        g_steadyNs += NS_PER_SEC;
        remote.epochNs += NS_PER_SEC;
        s.handleAcceptRemoteTnt(remote);
      },
      "handle_accept_remote_tnt");
}

PERF_MAIN()
