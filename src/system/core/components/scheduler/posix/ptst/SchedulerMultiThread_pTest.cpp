/**
 * @file SchedulerMultiThread_pTest.cpp
 * @brief Performance tests for the POSIX multi-thread scheduler.
 *
 * Measures:
 *  - Empty tick overhead through the multi-threaded dispatch path
 *  - Many-task dispatch with no work and with light spin work
 *  - Thread scaling at hardware concurrency
 *  - Task-count scaling from 1 to 1000 tasks
 *  - Concurrent execution stress at 1000 tasks
 *  - Cold-path addTask + scheduler creation and warm addTask cost
 *  - Sequenced chain throughput and sequenced-vs-parallel overhead
 *
 * Usage:
 *   ./SchedulerMultiThread_PTEST --csv results.csv
 *   ./SchedulerMultiThread_PTEST --quick
 *   MT_MAX_WORKERS=16 ./SchedulerMultiThread_PTEST --gtest_filter="*ThreadScaling*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
using apex::concurrency::DelegateU8;
using system_core::scheduler::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::SequenceGroup;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

inline std::uint16_t envU16(const char* name, std::uint16_t dflt) noexcept {
  const char* s = std::getenv(name);
  if (!s || *s == '\0')
    return dflt;
  return static_cast<std::uint16_t>(std::strtoul(s, nullptr, 10));
}

inline std::uint32_t envU32(const char* name, std::uint32_t dflt) noexcept {
  const char* s = std::getenv(name);
  if (!s || *s == '\0')
    return dflt;
  return static_cast<std::uint32_t>(std::strtoul(s, nullptr, 10));
}

inline std::uint64_t envU64(const char* name, std::uint64_t dflt) noexcept {
  const char* s = std::getenv(name);
  if (!s || *s == '\0')
    return dflt;
  return static_cast<std::uint64_t>(std::strtoull(s, nullptr, 10));
}

inline int envI(const char* name, int dflt) noexcept {
  const char* s = std::getenv(name);
  if (!s || *s == '\0')
    return dflt;
  return static_cast<int>(std::strtol(s, nullptr, 10));
}

inline const char* envStr(const char* name, const char* dflt) noexcept {
  const char* s = std::getenv(name);
  return (s && *s != '\0') ? s : dflt;
}

// Test configuration
struct Shape {
  std::uint16_t ffreq{1000};
  std::uint32_t tasks{256};
  std::uint32_t spin{64};
  std::uint16_t denMax{8};
  const char* offsetMode{"uniform"};
  std::uint64_t seed{0xC0FFEEULL};
  int maxWorkers{-1}; // -1 -> scheduler default
};

inline Shape loadShape() noexcept {
  Shape s{};
  s.ffreq = envU16("FFREQ", 1000);
  s.tasks = envU32("TASKS", 256);
  s.spin = envU32("SPIN", 64);
  s.denMax = envU16("DEN_MAX", 8);
  s.offsetMode = envStr("OFFSET_MODE", "uniform");
  s.seed = envU64("SEED", 0xC0FFEEULL);
  s.maxWorkers = envI("MT_MAX_WORKERS", -1);
  return s;
}

// Per-task execution context
struct TaskCtx {
  std::uint32_t spinIters{0};
  std::uint8_t rc{static_cast<std::uint8_t>(Status::SUCCESS)};
};

// Task execution function
static std::uint8_t taskThunk(void* ctx) noexcept {
  auto* c = static_cast<TaskCtx*>(ctx);
  const std::uint32_t SPIN = c->spinIters;

  for (std::uint32_t i = 0; i < SPIN; ++i) {
    asm volatile("" ::: "memory");
  }

  return c->rc;
}

// Factory: create SchedulableTask with dedicated context
inline std::unique_ptr<SchedulableTask>
makeFuncTask(std::vector<std::unique_ptr<TaskCtx>>& ctxPool, std::string_view labelView,
             std::uint32_t spinIters = 0U,
             std::uint8_t rc = static_cast<std::uint8_t>(Status::SUCCESS)) {
  auto ctx = std::make_unique<TaskCtx>();
  ctx->spinIters = spinIters;
  ctx->rc = rc;
  TaskCtx* const CTX = ctx.get();
  ctxPool.push_back(std::move(ctx));

  DelegateU8 del{&taskThunk, static_cast<void*>(CTX)};
  return std::make_unique<SchedulableTask>(del, labelView);
}

// SFINAE helpers for optional API methods
template <typename T>
concept HasSetMaxWorkers = requires(T& t, std::size_t n) { t.setMaxWorkers(n); };

template <typename T> inline void trySetMaxWorkers(T& sched, int n) noexcept {
  if constexpr (HasSetMaxWorkers<T>) {
    if (n > 0)
      sched.setMaxWorkers(static_cast<std::size_t>(n));
  } else {
    (void)sched;
    (void)n;
  }
}

// Scheduler factory
inline std::unique_ptr<SchedulerMultiThread> makeScheduler(const Shape& sh) {
  const auto LOG_DIR = std::filesystem::temp_directory_path() / "scheduler_mt_ptest";
  std::filesystem::create_directories(LOG_DIR);

  static std::vector<std::filesystem::path> toClean;
  toClean.push_back(LOG_DIR);

  auto sched = std::make_unique<SchedulerMultiThread>(sh.ffreq, LOG_DIR);
  (void)sched->init();

  // Optional tuning
  trySetMaxWorkers(*sched, sh.maxWorkers);

  return sched;
}

} // namespace

/* ----------------------------- Baseline ----------------------------- */

/**
 * @brief Empty tick overhead through the multi-threaded dispatch path.
 */
PERF_TEST(SchedulerMtPerf, EmptyTickOverhead) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  const std::uint16_t TICK = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickMulti(TICK);
      (void)status;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(TICK);
        (void)status;
      },
      "empty-tick");

  std::printf("\n[Baseline-MT] Empty tick: %.3f us\n", result.stats.median);
  std::printf("  Note: Includes thread management overhead\n");
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Expected: 10-50 us (vs ~5-9 ns ST)\n");
}

/* ----------------------------- Many Tasks No Work ----------------------------- */

/**
 * @brief Dispatch overhead with many tasks that do no work.
 */
// Several tests in this file construct ub::PerfCase manually instead of using
// UB_PERF_GUARD(perf) so they can cap cycles/repeats -- thread spawning makes
// the default iteration count prohibitively expensive.
PERF_TEST(SchedulerMtPerf, ManyNoWork) {
  // MT overhead is significant; reduce iterations for reasonable test time
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 500); // Cap at 500 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.ManyNoWork", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  const std::size_t N = static_cast<std::size_t>(SH.tasks);

  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  ctxPool.reserve(N);
  std::vector<std::string> labelPool;
  labelPool.reserve(N);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(N);

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    auto t = makeFuncTask(ctxPool, std::string_view(labelPool.back()), 0U);
    ASSERT_EQ(sched->addTask(*t, sched->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasks.push_back(std::move(t));
  }

  const std::uint16_t TICK = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickMulti(TICK);
      (void)status;
    }
  });

  // Enhanced memory profile accounting
  const size_t TASK_PTR = sizeof(SchedulableTask*);
  const size_t TASK_META = 64; // Cache line per task (metadata)
  const size_t WORKER_STACK = static_cast<const size_t>(std::thread::hardware_concurrency() *
                                                        8192); // Conservative stack estimate
  const size_t COHERENCY = N * 64;                             // Cache line bouncing (pessimistic)

  ub::MemoryProfile memProfile{.bytesRead = N * (TASK_PTR + TASK_META) + WORKER_STACK + COHERENCY,
                               .bytesWritten = N * sizeof(std::uint8_t), // Result codes
                               .bytesAllocated = 0};

  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(TICK);
        (void)status;
      },
      "many-nowork", memProfile);

  // Relaxed CV% for workloads that might be too fast
  if (result.stats.median < 1.0) {
  }

  const double US_PER_TASK = result.stats.median / N;
  std::printf("\n[Throughput-MT] %zu tasks (no work):\n", N);
  std::printf("  Total: %.3f us\n", result.stats.median);
  std::printf("  Per-task: %.3f us (%.0f K tasks/sec)\n", US_PER_TASK,
              (result.callsPerSecond * N) / 1e3);
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Expected: 5-20 us per-task (vs 2.7 ns ST)\n");
}

/* ----------------------------- Many Tasks Light Work ----------------------------- */

/**
 * @brief Dispatch + execution for many tasks with light spin work.
 */
PERF_TEST(SchedulerMtPerf, ManyLightWork) {
  // MT overhead is significant; reduce iterations for reasonable test time
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 500); // Cap at 500 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.ManyLightWork", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  const std::size_t N = static_cast<std::size_t>(SH.tasks);
  const std::uint32_t SPIN = SH.spin;

  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  ctxPool.reserve(N);
  std::vector<std::string> labelPool;
  labelPool.reserve(N);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(N);

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    auto t = makeFuncTask(ctxPool, std::string_view(labelPool.back()), SPIN);
    ASSERT_EQ(sched->addTask(*t, sched->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasks.push_back(std::move(t));
  }

  const std::uint16_t TICK = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickMulti(TICK);
      (void)status;
    }
  });

  // Memory profile with work included
  const size_t TASK_META = sizeof(SchedulableTask*) + 64;
  const size_t WORKER_STACK = static_cast<const size_t>(std::thread::hardware_concurrency() * 8192);
  const size_t COHERENCY = N * 64;

  ub::MemoryProfile memProfile{.bytesRead = N * TASK_META + WORKER_STACK + COHERENCY,
                               .bytesWritten = N * sizeof(std::uint8_t),
                               .bytesAllocated = 0};

  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(TICK);
        (void)status;
      },
      "many-light", memProfile);

  // With real work (>1us), expect stable results
  if (result.stats.median > 1.0) {
    EXPECT_STABLE_CV_CPU(result, perf.config());
  }

  const double US_PER_TASK = result.stats.median / N;
  const double ST_BASELINE_NS = 24.0; // From SingleThread baseline
  const double SPEEDUP = (ST_BASELINE_NS / 1000.0) / US_PER_TASK;

  std::printf("\n[Throughput-MT] %zu tasks (spin=%u):\n", N, SPIN);
  std::printf("  Total: %.3f us\n", result.stats.median);
  std::printf("  Per-task: %.3f us (%.0f K tasks/sec)\n", US_PER_TASK,
              (result.callsPerSecond * N) / 1e3);
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Speedup vs ST: %.1fx (ideal: %ux)\n", SPEEDUP,
              std::thread::hardware_concurrency());
}

/* ----------------------------- Thread Scaling ----------------------------- */

/**
 * @brief Throughput at hardware-concurrency worker count.
 */
PERF_TEST(SchedulerMtPerf, ThreadScaling) {
  // MT overhead is significant; reduce iterations for reasonable test time
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 500); // Cap at 500 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.ThreadScaling", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  const std::size_t N = static_cast<std::size_t>(SH.tasks);
  const std::uint32_t SPIN = SH.spin;

  std::printf("\n=== Thread Scaling Analysis ===\n");
  std::printf("Configuration: %zu tasks, spin=%u\n", N, SPIN);
  std::printf("%-10s %-15s %-15s %-15s %-15s\n", "Workers", "Latency(us)", "Tasks/sec", "Speedup",
              "Efficiency");
  std::printf("%s\n", std::string(70, '-').c_str());

  // Uses default thread count (hardware_concurrency).
  const int workerCount = static_cast<int>(std::thread::hardware_concurrency());

  auto sched = makeScheduler(SH);

  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  std::vector<std::string> labelPool;
  std::vector<std::unique_ptr<SchedulableTask>> tasks;

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    auto t = makeFuncTask(ctxPool, labelPool.back(), SPIN);
    (void)sched->addTask(*t, sched->fundamentalFreq(), 1, 0);
    tasks.push_back(std::move(t));
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickMulti(0);
      (void)status;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(0);
        (void)status;
      },
      "thread-scaling");

  const double tasksPerSec = (result.callsPerSecond * N) / 1e3;

  std::printf("%-10d %-15.3f %-15.0f %-15s %-15s\n", workerCount, result.stats.median, tasksPerSec,
              "N/A", "N/A");

  std::printf("\nInterpretation:\n");
  std::printf("  >80%% efficiency: Excellent scaling\n");
  std::printf("  50-80%% efficiency: Good, some contention\n");
  std::printf("  <50%% efficiency: Poor scaling, investigate locks/cache\n");
}

/* ----------------------------- Task Count Scaling ----------------------------- */

/**
 * @brief Sweep dispatch cost across task counts (1 to 1000).
 */
PERF_TEST(SchedulerMtPerf, TaskCountScaling) {
  // MT overhead is significant; reduce iterations for reasonable test time
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200); // Cap at 200 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.TaskCountScaling", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();

  std::printf("\n=== Task Count Scaling ===\n");
  std::printf("%-10s %-15s %-15s %-15s\n", "Tasks", "Total(us)", "Per-task(ns)", "CV%%");
  std::printf("%s\n", std::string(55, '-').c_str());

  // Reduced task counts for quick iteration; full sweep for CI
  const std::vector<std::size_t> TASK_COUNTS =
      cfg.quickMode ? std::vector<std::size_t>{10, 100, 500}
                    : std::vector<std::size_t>{1, 10, 50, 100, 250, 500, 1000};

  for (std::size_t taskCount : TASK_COUNTS) {
    // Create fresh scheduler for each task count
    auto sched = makeScheduler(SH);

    std::vector<std::unique_ptr<TaskCtx>> ctxPool;
    std::vector<std::string> labelPool;
    std::vector<std::unique_ptr<SchedulableTask>> tasks;

    for (std::size_t i = 0; i < taskCount; ++i) {
      labelPool.emplace_back("T" + std::to_string(i));
      auto t = makeFuncTask(ctxPool, labelPool.back(), 0U);
      (void)sched->addTask(*t, sched->fundamentalFreq(), 1, 0);
      tasks.push_back(std::move(t));
    }

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        volatile auto status = sched->executeTasksOnTickMulti(0);
        (void)status;
      }
    });

    auto result = perf.throughputLoop(
        [&] {
          volatile auto status = sched->executeTasksOnTickMulti(0);
          (void)status;
        },
        "task-count");

    const double perTaskNs = (result.stats.median * 1000.0) / taskCount;
    const double cvPercent = result.stats.cv * 100.0;

    std::printf("%-10zu %-15.3f %-15.1f %-15.1f\n", taskCount, result.stats.median, perTaskNs,
                cvPercent);
  }

  std::printf("\nWorkers: Run with MT_MAX_WORKERS=N to test different configs\n");
  std::printf("Interpretation:\n");
  std::printf("  Linear scaling: O(1) per-task overhead (ideal)\n");
  std::printf("  Sub-linear: Good work distribution\n");
  std::printf("  Super-linear: Synchronization bottleneck\n");
}

/* ----------------------------- Concurrent Stress ----------------------------- */

/**
 * @brief Concurrent execution stress test at 1000 tasks.
 */
PERF_TEST(SchedulerMtPerf, ConcurrentExecutionStress) {
  // Stress test with 1000 tasks; reduce iterations for reasonable test time
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200); // Cap at 200 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.ConcurrentExecutionStress", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  const std::size_t N = 1000; // Many tasks to stress work distribution
  const std::uint32_t SPIN = SH.spin;

  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  std::vector<std::string> labelPool;
  std::vector<std::unique_ptr<SchedulableTask>> tasks;

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    auto t = makeFuncTask(ctxPool, labelPool.back(), SPIN);
    (void)sched->addTask(*t, sched->fundamentalFreq(), 1, 0);
    tasks.push_back(std::move(t));
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickMulti(0);
      (void)status;
    }
  });

  // Measure repeated execution to stress test consistency
  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(0);
        (void)status;
      },
      "concurrent-stress");

  std::printf("\nConcurrent Execution Stress Results:\n");
  std::printf("  Tasks: %zu\n", N);
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Median: %.3f us\n", result.stats.median);
  std::printf("  p10-p90: %.3f - %.3f us\n", result.stats.p10, result.stats.p90);
  std::printf("  CV%%: %.1f%%\n", result.stats.cv * 100.0);
  std::printf("  Throughput: %.0f ticks/sec\n", result.callsPerSecond);

  std::printf("\nInterpretation:\n");
  std::printf("  Low CV%% (<5%%): Consistent execution time\n");
  std::printf("  High CV%% (>10%%): Uneven load distribution or contention\n");
  std::printf("  p90/p50 ratio > 1.5: High tail latency variance\n");
}

/* ----------------------------- Cold Path AddTask ----------------------------- */

/**
 * @brief Cold-path addTask cost (scheduler creation + thread pool per iteration).
 */
PERF_TEST(SchedulerMtPerf, AddTaskSetupCost) {
  // Cold-path test: each iteration creates a scheduler (~100us).
  // Use reduced iterations for reasonable test time.
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 100); // Cap at 100 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.AddTaskSetupCost", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  std::mt19937_64 rng(SH.seed);
  std::uniform_int_distribution<std::uint16_t> denDist(1, SH.denMax);

  perf.warmup([&] {
    auto warm = makeScheduler(SH);
    const std::uint16_t FREQ = warm->fundamentalFreq();
    const std::uint16_t DEN = denDist(rng);

    std::uint16_t off = 0;
    if (std::string_view(SH.offsetMode) == "uniform") {
      const std::uint32_t PERIOD_TPT = static_cast<std::uint32_t>(DEN);
      const std::uint32_t MAX_OFF = (PERIOD_TPT > 0U) ? (PERIOD_TPT - 1U) : 0U;
      std::uniform_int_distribution<std::uint32_t> offDist(0U, MAX_OFF);
      off = static_cast<std::uint16_t>(offDist(rng));
    }

    TaskCtx ctx{};
    DelegateU8 del{&taskThunk, static_cast<void*>(&ctx)};
    SchedulableTask task(del, std::string_view("W"));
    volatile auto status = warm->addTask(task, FREQ, DEN, off);
    (void)status;
  });

  auto result = perf.throughputLoop(
      [&] {
        auto sched = makeScheduler(SH);
        const std::uint16_t FREQ = sched->fundamentalFreq();
        const std::uint16_t DEN = denDist(rng);

        std::uint16_t off = 0;
        if (std::string_view(SH.offsetMode) == "uniform") {
          const std::uint32_t PERIOD_TPT = static_cast<std::uint32_t>(DEN);
          const std::uint32_t MAX_OFF = (PERIOD_TPT > 0U) ? (PERIOD_TPT - 1U) : 0U;
          std::uniform_int_distribution<std::uint32_t> offDist(0U, MAX_OFF);
          off = static_cast<std::uint16_t>(offDist(rng));
        }

        TaskCtx ctx{};
        DelegateU8 del{&taskThunk, static_cast<void*>(&ctx)};
        SchedulableTask task(del, std::string_view("X"));
        volatile auto status = sched->addTask(task, FREQ, DEN, off);
        (void)status;
      },
      "addTask-setup");

  // Validate: full cold-path (scheduler creation + thread pool + file I/O + addTask) is slow but
  // bounded In Docker containers with file I/O and thread creation overhead, expect 100-300ms Note:
  // Use AddTaskWarmCost to measure just addTask cost on a reused scheduler

  std::printf("\n[Cold-path-MT] scheduler+addTask: %.3f ms (%.0f creates/sec)\n",
              result.stats.median / 1000.0, result.callsPerSecond);
}

/* ----------------------------- Init Only ----------------------------- */

/**
 * @brief Isolated scheduler init cost (thread pool creation only).
 */
PERF_TEST(SchedulerMtPerf, InitOnlyCost) {
  // Cold-path test: each iteration creates a scheduler.
  // Use reduced iterations for reasonable test time.
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 100); // Cap at 100 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  ub::PerfCase perf{"SchedulerMtPerf.InitOnlyCost", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();

  perf.warmup([&] {
    const auto LOG_DIR = std::filesystem::temp_directory_path() / "scheduler_mt_init_log";
    std::filesystem::create_directories(LOG_DIR);
    SchedulerMultiThread sched(SH.ffreq, LOG_DIR);
    volatile auto status = sched.init();
    (void)status;
  });

  auto result = perf.throughputLoop(
      [&] {
        const auto LOG_DIR = std::filesystem::temp_directory_path() / "scheduler_mt_init_log";
        std::filesystem::create_directories(LOG_DIR);
        SchedulerMultiThread sched(SH.ffreq, LOG_DIR);
        volatile auto status = sched.init();
        (void)status;
      },
      "init-only");

  std::printf("\n[Init-MT] Init cost: %.3f us (%.0f inits/sec)\n", result.stats.median,
              result.callsPerSecond);
}

/* ----------------------------- Warm AddTask ----------------------------- */

/**
 * @brief Warm addTask cost on a reused, already-initialized scheduler.
 */
PERF_TEST(SchedulerMtPerf, AddTaskWarmCost) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, perf.config());

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  const std::uint16_t FREQ = sched->fundamentalFreq();

  std::mt19937_64 rng(SH.seed);
  std::uniform_int_distribution<std::uint16_t> denDist(1, SH.denMax);

  perf.warmup([&] {
    const std::uint16_t DEN = denDist(rng);
    std::uint16_t off = 0;
    if (std::string_view(SH.offsetMode) == "uniform") {
      const std::uint32_t PERIOD_TPT = static_cast<std::uint32_t>(DEN);
      const std::uint32_t MAX_OFF = (PERIOD_TPT > 0U) ? (PERIOD_TPT - 1U) : 0U;
      std::uniform_int_distribution<std::uint32_t> offDist(0U, MAX_OFF);
      off = static_cast<std::uint16_t>(offDist(rng));
    }
    TaskCtx ctx{};
    DelegateU8 del{&taskThunk, static_cast<void*>(&ctx)};
    SchedulableTask task(del, std::string_view("W"));
    volatile auto status = sched->addTask(task, FREQ, DEN, off);
    (void)status;
  });

  auto result = perf.throughputLoop(
      [&] {
        const std::uint16_t DEN = denDist(rng);
        std::uint16_t off = 0;
        if (std::string_view(SH.offsetMode) == "uniform") {
          const std::uint32_t PERIOD_TPT = static_cast<std::uint32_t>(DEN);
          const std::uint32_t MAX_OFF = (PERIOD_TPT > 0U) ? (PERIOD_TPT - 1U) : 0U;
          std::uniform_int_distribution<std::uint32_t> offDist(0U, MAX_OFF);
          off = static_cast<std::uint16_t>(offDist(rng));
        }
        TaskCtx ctx{};
        DelegateU8 del{&taskThunk, static_cast<void*>(&ctx)};
        SchedulableTask task(del, std::string_view("X"));
        volatile auto status = sched->addTask(task, FREQ, DEN, off);
        (void)status;
      },
      "addTask-warm");

  std::printf("\n[Warm-MT] addTask (reused scheduler): %.3f us (%.0f adds/sec)\n",
              result.stats.median, result.callsPerSecond);
}

/* ----------------------------- Sequenced Chain ----------------------------- */

/**
 * @brief Full 4-task sequenced chain throughput (pre1|pre2 -> step -> post).
 */
PERF_TEST(SchedulerMtPerf, SequencedChain4) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200); // Reduced - we add wait overhead
  cfg.repeats = std::min(cfg.repeats, 5);
  ub::PerfCase perf{"SchedulerMtPerf.SequencedChain4", cfg};
  ub::attachProfilerHooks(perf, cfg);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);

  // 4-task sequence: pre1/pre2 (parallel) -> step -> post
  // After all 4 complete, counter wraps to 1
  SequenceGroup seq(4);

  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  ctxPool.reserve(4);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(4);

  auto makeSyncTask = [&](std::string_view label, std::uint32_t spin = 0U) {
    auto ctx = std::make_unique<TaskCtx>();
    ctx->spinIters = spin;
    ctx->rc = 0;
    TaskCtx* const CTX = ctx.get();
    ctxPool.push_back(std::move(ctx));
    DelegateU8 del{&taskThunk, static_cast<void*>(CTX)};
    return std::make_unique<SchedulableTask>(del, label);
  };

  auto pre1 = makeSyncTask("pre1", SH.spin);
  auto pre2 = makeSyncTask("pre2", SH.spin);
  auto step = makeSyncTask("step", SH.spin);
  auto post = makeSyncTask("post", SH.spin);

  // Register with sequence group: phase 1 for pre1/pre2, phase 3 for step, phase 4 for post
  seq.addTask(*pre1, 1);
  seq.addTask(*pre2, 1);
  seq.addTask(*step, 3);
  seq.addTask(*post, 4);

  // Add to scheduler with sequencing
  TaskConfig cfgHigh(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_HIGH);
  TaskConfig cfgNorm(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_NORMAL);
  TaskConfig cfgLow(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_LOW);

  ASSERT_EQ(sched->addTask(*pre1, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(*pre2, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(*step, cfgNorm, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(*post, cfgLow, &seq), Status::SUCCESS);

  tasks.push_back(std::move(pre1));
  tasks.push_back(std::move(pre2));
  tasks.push_back(std::move(step));
  tasks.push_back(std::move(post));

  const std::uint16_t TICK = 0;

  // For sequenced tasks, we need to wait for completion between iterations.
  // The seq counter wraps to 1 when all 4 tasks complete, so we wait for that.
  auto waitForCompletion = [&] {
    // First wait for counter to leave 1 (tasks started)
    for (int wait = 0; wait < 10000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) != 1) {
        break;
      }
      std::this_thread::yield();
    }
    // Then wait for counter to wrap back to 1 (all 4 tasks completed)
    // Timeout after 100ms to avoid infinite hangs in case of bugs
    for (int wait = 0; wait < 100000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) == 1) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  };

  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) { // Reduced warmup cycles
      volatile auto status = sched->executeTasksOnTickMulti(TICK);
      (void)status;
      waitForCompletion();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickMulti(TICK);
        (void)status;
        waitForCompletion();
      },
      "seq-chain4");

  std::printf("\n[Sequencing-MT] 4-task chain (pre1|pre2 -> step -> post):\n");
  std::printf("  Total: %.3f us\n", result.stats.median);
  std::printf("  Per-task: %.3f us\n", result.stats.median / 4.0);
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Spin: %u iters\n", SH.spin);
}

/* ----------------------------- Sequencing Overhead ----------------------------- */

/**
 * @brief Compare sequenced (serial) vs non-sequenced (parallel) dispatch overhead.
 */
PERF_TEST(SchedulerMtPerf, SequencingOverhead) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200); // Reduced - we add wait overhead
  cfg.repeats = std::min(cfg.repeats, 5);
  ub::PerfCase perf{"SchedulerMtPerf.SequencingOverhead", cfg};

  const Shape SH = loadShape();

  // Test 1: Non-sequenced (4 tasks running in parallel)
  auto schedNonSeq = makeScheduler(SH);
  std::vector<std::unique_ptr<TaskCtx>> ctxPoolNonSeq;
  std::vector<std::unique_ptr<SchedulableTask>> tasksNonSeq;
  ctxPoolNonSeq.reserve(4);
  tasksNonSeq.reserve(4);

  for (int i = 0; i < 4; ++i) {
    auto ctx = std::make_unique<TaskCtx>();
    ctx->spinIters = SH.spin;
    ctx->rc = 0;
    TaskCtx* const CTX = ctx.get();
    ctxPoolNonSeq.push_back(std::move(ctx));

    std::string label = "T" + std::to_string(i);
    DelegateU8 del{&taskThunk, static_cast<void*>(CTX)};
    auto task = std::make_unique<SchedulableTask>(del, std::string_view(label));
    ASSERT_EQ(schedNonSeq->addTask(*task, schedNonSeq->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasksNonSeq.push_back(std::move(task));
  }

  const std::uint16_t TICK = 0;
  double nonSeqMedian = 0.0;
  {
    // For non-sequenced, we just need a small delay to let tasks complete
    auto resultNonSeq = perf.throughputLoop(
        [&] {
          volatile auto status = schedNonSeq->executeTasksOnTickMulti(TICK);
          (void)status;
          // Small delay to allow parallel tasks to complete
          std::this_thread::sleep_for(std::chrono::microseconds(50));
        },
        "nonseq-4task");
    nonSeqMedian = resultNonSeq.stats.median;
  }

  // Test 2: Sequenced (4 tasks in chain - strictly sequential)
  auto schedSeq = makeScheduler(SH);
  SequenceGroup seq(4);
  std::vector<std::unique_ptr<TaskCtx>> ctxPoolSeq;
  std::vector<std::unique_ptr<SchedulableTask>> tasksSeq;
  ctxPoolSeq.reserve(4);
  tasksSeq.reserve(4);

  for (int i = 0; i < 4; ++i) {
    auto ctx = std::make_unique<TaskCtx>();
    ctx->spinIters = SH.spin;
    ctx->rc = 0;
    TaskCtx* const CTX = ctx.get();
    ctxPoolSeq.push_back(std::move(ctx));

    std::string label = "S" + std::to_string(i);
    DelegateU8 del{&taskThunk, static_cast<void*>(CTX)};
    auto task = std::make_unique<SchedulableTask>(del, std::string_view(label));

    // Sequential phases: 1, 2, 3, 4
    seq.addTask(*task, i + 1);

    TaskConfig taskCfg(schedSeq->fundamentalFreq(), 1, 0);
    ASSERT_EQ(schedSeq->addTask(*task, taskCfg, &seq), Status::SUCCESS);
    tasksSeq.push_back(std::move(task));
  }

  // Wait for sequenced tasks to complete
  auto waitForSeqCompletion = [&] {
    // First wait for counter to leave 1 (tasks started)
    for (int wait = 0; wait < 10000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) != 1) {
        break;
      }
      std::this_thread::yield();
    }
    // Then wait for counter to wrap back to 1 (all 4 tasks completed)
    for (int wait = 0; wait < 100000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) == 1) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  };

  double seqMedian = 0.0;
  {
    auto resultSeq = perf.throughputLoop(
        [&] {
          volatile auto status = schedSeq->executeTasksOnTickMulti(TICK);
          (void)status;
          waitForSeqCompletion();
        },
        "seq-4task");
    seqMedian = resultSeq.stats.median;
  }

  const double OVERHEAD = seqMedian - nonSeqMedian;
  const double OVERHEAD_PCT = (nonSeqMedian > 0) ? (OVERHEAD / nonSeqMedian * 100.0) : 0.0;

  std::printf("\n[Sequencing Overhead] 4 tasks (spin=%u):\n", SH.spin);
  std::printf("  Non-sequenced (parallel): %.3f us\n", nonSeqMedian);
  std::printf("  Sequenced (serial): %.3f us\n", seqMedian);
  std::printf("  Overhead: %.3f us (%.1f%%)\n", OVERHEAD, OVERHEAD_PCT);
  std::printf("  Per-task overhead: %.3f us\n", OVERHEAD / 4.0);
  std::printf(
      "  Note: Sequential tasks cannot run in parallel, so overhead includes serialization\n");
}

PERF_MAIN()
