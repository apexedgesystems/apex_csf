/**
 * @file McuScheduler_pTest.cpp
 * @brief Performance tests for the zero-allocation MCU scheduler.
 *
 * Measures:
 *  - tick() overhead with zero registered tasks (baseline)
 *  - Noop task scaling at 8 and 32 tasks
 *  - Tick cost with light spin work
 *  - Rate-group decimation overhead (mixed 100 / 50 / 10 / 1 Hz tasks)
 *  - addTask() and init() priority-sort costs
 *
 * Usage:
 *   ./McuScheduler_PTEST --csv results.csv
 *   ./McuScheduler_PTEST --quick --gtest_filter="*EmptyTick*"
 *   ./McuScheduler_PTEST --profile gperf --cycles 100000
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/scheduler/mcu/inc/McuScheduler.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ub = vernier::bench;
using system_core::scheduler::mcu::McuTaskEntry;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

static void mcuTaskThunk(void* ctx) noexcept {
  auto* c = static_cast<std::uint32_t*>(ctx);
  const std::uint32_t SPIN = *c;
  for (std::uint32_t i = 0; i < SPIN; ++i) {
    asm volatile("" ::: "memory");
  }
}

static void liteNoopTask(void* /*ctx*/) noexcept {}

inline std::uint32_t envU32(const char* name, std::uint32_t dflt) noexcept {
  const char* s = std::getenv(name);
  if (!s || *s == '\0')
    return dflt;
  return static_cast<std::uint32_t>(std::strtoul(s, nullptr, 10));
}

} // namespace

/* ----------------------------- Baseline ----------------------------- */

/** @brief Tick overhead with zero registered tasks. */
PERF_TEST(McuSchedulerPerf, EmptyTickOverhead) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  system_core::scheduler::mcu::McuScheduler<> sched(100);
  (void)sched.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sched.tick();
    }
  });

  auto result = perf.throughputLoop([&] { sched.tick(); }, "empty-tick");

  std::printf("\n[Baseline] Empty tick: %.3f ns (%.0f M ticks/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/* ----------------------------- Noop Task Scaling ----------------------------- */

/** @brief Tick cost with 8 no-op tasks at full rate. */
PERF_TEST(McuSchedulerPerf, NoopTasks8) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  system_core::scheduler::mcu::McuScheduler<8> sched(100);
  for (std::uint8_t i = 0; i < 8; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 1, 0, 0, i});
  }
  (void)sched.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sched.tick();
    }
  });

  auto result = perf.throughputLoop([&] { sched.tick(); }, "noop-8");

  std::printf("\n[Noop 8] tick: %.3f ns (%.0f M ticks/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/** @brief Tick cost with 32 no-op tasks at full rate. */
PERF_TEST(McuSchedulerPerf, NoopTasks32) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  system_core::scheduler::mcu::McuScheduler<32> sched(100);
  for (std::uint8_t i = 0; i < 32; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 1, 0, 0, i});
  }
  (void)sched.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sched.tick();
    }
  });

  auto result = perf.throughputLoop([&] { sched.tick(); }, "noop-32");

  std::printf("\n[Noop 32] tick: %.3f ns (%.0f M ticks/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/* ----------------------------- Light Work ----------------------------- */

/** @brief Tick cost with 8 tasks doing light spin work. */
PERF_TEST(McuSchedulerPerf, LightWork8) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  const std::uint32_t SPIN = envU32("SPIN", 64);
  std::uint32_t spinCtx[8];
  for (auto& s : spinCtx) {
    s = SPIN;
  }

  system_core::scheduler::mcu::McuScheduler<8> sched(100);
  for (std::uint8_t i = 0; i < 8; ++i) {
    sched.addTask({mcuTaskThunk, &spinCtx[i], 1, 1, 0, 0, i});
  }
  (void)sched.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sched.tick();
    }
  });

  auto result = perf.throughputLoop([&] { sched.tick(); }, "light-8");

  std::printf("\n[Light 8] tick: %.3f ns (%.0f M ticks/sec), spin=%u\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6, SPIN);
}

/* ----------------------------- Rate Decimation ----------------------------- */

/** @brief Tick cost with mixed rate groups (100/50/10/1 Hz). */
PERF_TEST(McuSchedulerPerf, RateDecimation) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  system_core::scheduler::mcu::McuScheduler<16> sched(100);

  // Mix of rate groups: 4 at 100Hz, 4 at 50Hz, 4 at 10Hz, 4 at 1Hz
  for (std::uint8_t i = 0; i < 4; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 1, 0, 0, static_cast<std::uint8_t>(i)});
  }
  for (std::uint8_t i = 0; i < 4; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 2, 0, 0, static_cast<std::uint8_t>(4 + i)});
  }
  for (std::uint8_t i = 0; i < 4; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 10, 0, 0, static_cast<std::uint8_t>(8 + i)});
  }
  for (std::uint8_t i = 0; i < 4; ++i) {
    sched.addTask({liteNoopTask, nullptr, 1, 100, 0, 0, static_cast<std::uint8_t>(12 + i)});
  }
  (void)sched.init();

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sched.tick();
    }
  });

  auto result = perf.throughputLoop([&] { sched.tick(); }, "rate-decimate-16");

  std::printf("\n[RateDecimation 16] tick: %.3f ns (%.0f M ticks/sec)\n",
              result.stats.median * 1000.0, result.callsPerSecond / 1e6);
}

/* ----------------------------- AddTask Cost ----------------------------- */

/** @brief Cost of populating 32 tasks via addTask(). */
PERF_TEST(McuSchedulerPerf, AddTaskCost) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      system_core::scheduler::mcu::McuScheduler<32> sched(100);
      for (std::uint8_t j = 0; j < 32; ++j) {
        sched.addTask({liteNoopTask, nullptr, 1, 1, 0, 0, j});
      }
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        system_core::scheduler::mcu::McuScheduler<32> sched(100);
        for (std::uint8_t j = 0; j < 32; ++j) {
          sched.addTask({liteNoopTask, nullptr, 1, 1, 0, 0, j});
        }
      },
      "add-task-32");

  std::printf("\n[AddTask] 32 tasks: %.3f ns (%.0f K ops/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e3);
}

/* ----------------------------- Init (Sort) Cost ----------------------------- */

/** @brief Cost of init() priority sort with 32 reverse-ordered tasks. */
PERF_TEST(McuSchedulerPerf, InitSortCost) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      system_core::scheduler::mcu::McuScheduler<32> sched(100);
      for (std::uint8_t j = 0; j < 32; ++j) {
        sched.addTask({liteNoopTask, nullptr, 1, 1, 0, static_cast<int8_t>(31 - j), j});
      }
      (void)sched.init();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        system_core::scheduler::mcu::McuScheduler<32> sched(100);
        for (std::uint8_t j = 0; j < 32; ++j) {
          sched.addTask({liteNoopTask, nullptr, 1, 1, 0, static_cast<int8_t>(31 - j), j});
        }
        (void)sched.init();
      },
      "init-sort-32");

  std::printf("\n[InitSort] 32 tasks: %.3f ns (%.0f K ops/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e3);
}

PERF_MAIN()
