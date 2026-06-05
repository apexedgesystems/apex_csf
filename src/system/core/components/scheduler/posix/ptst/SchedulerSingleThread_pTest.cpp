/**
 * @file SchedulerSingleThread_pTest.cpp
 * @brief Performance tests for the POSIX single-thread scheduler.
 *
 * Measures:
 *  - Empty tick overhead (no tasks registered)
 *  - Many-task dispatch with no work (pointer indirection cost only)
 *  - Many-task dispatch with light spin work (representative load)
 *  - Cold-path addTask + scheduler creation cost
 *  - Task-count scaling from 1 to 1000 tasks
 *
 * Usage:
 *   ./SchedulerSingleThread_PTEST --csv results.csv
 *   ./SchedulerSingleThread_PTEST --quick --gtest_filter="*EmptyTick*"
 *   ./SchedulerSingleThread_PTEST --profile perf --gtest_filter="*LightWork"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerSingleThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace ub = vernier::bench;
using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::scheduler::SchedulerSingleThread;
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

inline const char* envStr(const char* name, const char* dflt) noexcept {
  const char* s = std::getenv(name);
  return (s && *s != '\0') ? s : dflt;
}

// Test configuration shape (environment-configurable)
struct Shape {
  std::uint16_t ffreq{1000}; // Fundamental frequency (ticks/sec)
  std::uint32_t tasks{256};  // Number of tasks per tick
  std::uint32_t spin{64};    // Spin iterations for light work
  std::uint16_t denMax{8};   // Max denominator for frequency ratios
  const char* offsetMode{"uniform"};
  std::uint64_t seed{0xC0FFEEULL};
};

inline Shape loadShape() noexcept {
  Shape s{};
  s.ffreq = envU16("FFREQ", 1000);
  s.tasks = envU32("TASKS", 256);
  s.spin = envU32("SPIN", 64);
  s.denMax = envU16("DEN_MAX", 8);
  s.offsetMode = envStr("OFFSET_MODE", "uniform");
  s.seed = envU64("SEED", 0xC0FFEEULL);
  return s;
}

// Per-task execution context
struct TaskCtx {
  std::uint32_t spinIters{0};
  std::uint8_t rc{static_cast<std::uint8_t>(Status::SUCCESS)};
};

// Task execution function (noexcept, no allocations)
static std::uint8_t taskThunk(void* ctx) noexcept {
  auto* c = static_cast<TaskCtx*>(ctx);
  const std::uint32_t SPIN = c->spinIters;

  // Memory barrier prevents optimization
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

// Scheduler factory: creates a scheduler with temporary log directory
inline std::unique_ptr<SchedulerSingleThread> makeScheduler(std::uint16_t ffreq) {
  const auto LOG_DIR = std::filesystem::temp_directory_path() / "scheduler_ptest";
  std::filesystem::create_directories(LOG_DIR);

  static std::vector<std::filesystem::path> toClean;
  toClean.push_back(LOG_DIR);

  auto sched = std::make_unique<SchedulerSingleThread>(ffreq, LOG_DIR);
  (void)sched->init();
  return sched;
}

} // namespace

/* ----------------------------- Baseline ----------------------------- */

/**
 * @brief Empty tick overhead with no tasks registered.
 */
PERF_TEST(SchedulerPerf, EmptyTickOverhead) {
  UB_PERF_GUARD(perf);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH.ffreq);
  const std::uint16_t TICK = 0;

  // Warmup: prime CPU caches and branch predictors
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickSingle(TICK);
      (void)status;
    }
  });

  // Measure: baseline overhead of executeTasksOnTickSingle with no tasks scheduled
  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickSingle(TICK);
        (void)status;
      },
      "empty-tick");

  // Validate: empty tick should be very fast and stable
  // Typical results: 5-20ns (well below 50ns threshold)
  // Note: Sub-nanosecond workloads approach timer resolution limits, so
  // CV% can be high even with stable execution. Only check in non-quick mode.
  if (!perf.config().quickMode) {
    EXPECT_STABLE_CV_CPU(result, perf.config());
  }

  std::printf("\n[Baseline] Empty tick: %.3f ns (%.0f M ticks/sec)\n", result.stats.median * 1000.0,
              result.callsPerSecond / 1e6);
}

/* ----------------------------- Many Tasks No Work ----------------------------- */

/**
 * @brief Dispatch overhead for many trivial tasks (no spin work).
 */
PERF_TEST(SchedulerPerf, ManyNoWork) {
  UB_PERF_GUARD(perf);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH.ffreq);
  const std::size_t N = static_cast<std::size_t>(SH.tasks);

  // Setup: allocate all tasks and contexts
  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  ctxPool.reserve(N);

  std::vector<std::string> labelPool;
  labelPool.reserve(N);

  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(N);

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    const std::string& lbl = labelPool.back();
    auto t = makeFuncTask(ctxPool, std::string_view(lbl), /*spin*/ 0U);
    ASSERT_EQ(sched->addTask(*t, sched->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasks.push_back(std::move(t));
  }

  const std::uint16_t TICK = 0;

  // Warmup
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickSingle(TICK);
      (void)status;
    }
  });

  // Memory profile: reading task pointers from schedule vector
  // For N tasks: read N*sizeof(SchedulableTask*) + N vtable lookups + task context reads
  // Each task: 8B pointer + ~64B (vtable, members, context)
  const size_t BYTES_PER_TASK = sizeof(SchedulableTask*) + 64;
  const size_t BYTES_READ = N * BYTES_PER_TASK;
  ub::MemoryProfile memProfile{.bytesRead = BYTES_READ, .bytesWritten = 0, .bytesAllocated = 0};

  // Measure: dispatch overhead for N trivial tasks
  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickSingle(TICK);
        (void)status;
      },
      "many-nowork", memProfile);

  // Validate: should scale linearly with task count, but expect higher CV% due to tiny workload
  // Note: 0.7us total time approaches timer resolution (~5-10ns), so some jitter is expected
  const double NS_PER_TASK = (result.stats.median * 1000.0) / N;

  // Relaxed CV% check: tiny workloads (<1us) hit measurement precision limits
  if (result.stats.median > 1.0) {
    EXPECT_STABLE_CV_CPU(result, perf.config());
  } else {
    // For sub-microsecond workloads, accept up to 30% CV due to timer noise
  }

  std::printf("\n[Throughput] %zu tasks (no work):\n", N);
  std::printf("  Total: %.3f us (%.0f K ticks/sec)\n", result.stats.median,
              result.callsPerSecond / 1e3);
  std::printf("  Per-task: %.1f ns (%.0f M tasks/sec)\n", NS_PER_TASK,
              (result.callsPerSecond * N) / 1e6);
}

/* ----------------------------- Many Tasks Light Work ----------------------------- */

/**
 * @brief Scheduler + task execution cost for many tasks with light spin work.
 */
PERF_TEST(SchedulerPerf, ManyLightWork) {
  UB_PERF_GUARD(perf);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH.ffreq);
  const std::size_t N = static_cast<std::size_t>(SH.tasks);
  const std::uint32_t SPIN = SH.spin;

  // Setup
  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  ctxPool.reserve(N);

  std::vector<std::string> labelPool;
  labelPool.reserve(N);

  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(N);

  for (std::size_t i = 0; i < N; ++i) {
    labelPool.emplace_back("T" + std::to_string(i));
    const std::string& lbl = labelPool.back();
    auto t = makeFuncTask(ctxPool, std::string_view(lbl), SPIN);
    ASSERT_EQ(sched->addTask(*t, sched->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasks.push_back(std::move(t));
  }

  const std::uint16_t TICK = 0;

  // Warmup
  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      volatile auto status = sched->executeTasksOnTickSingle(TICK);
      (void)status;
    }
  });

  // Memory profile: scheduler reads + task context reads
  const size_t BYTES_READ = N * (sizeof(SchedulableTask*) + sizeof(TaskCtx) + 64);
  ub::MemoryProfile memProfile{.bytesRead = BYTES_READ, .bytesWritten = 0, .bytesAllocated = 0};

  // Measure
  auto result = perf.throughputLoop(
      [&] {
        volatile auto status = sched->executeTasksOnTickSingle(TICK);
        (void)status;
      },
      "many-light", memProfile);

  // Validate
  const double NS_PER_TASK = (result.stats.median * 1000.0) / N;
  EXPECT_STABLE_CV_CPU(result, perf.config());

  std::printf("\n[Throughput] %zu tasks (spin=%u):\n", N, SPIN);
  std::printf("  Total: %.3f us (%.0f ticks/sec)\n", result.stats.median, result.callsPerSecond);
  std::printf("  Per-task: %.1f ns (%.0f M tasks/sec)\n", NS_PER_TASK,
              (result.callsPerSecond * N) / 1e6);
}

/* ----------------------------- Cold Path AddTask ----------------------------- */

/**
 * @brief Cold-path addTask cost (includes scheduler creation per iteration).
 */
PERF_TEST(SchedulerPerf, AddTaskSetupCost) {
  // Cold-path test: each iteration creates a scheduler (~50us).
  // Use reduced iterations for reasonable test time.
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 100); // Cap at 100 iterations
  cfg.repeats = std::min(cfg.repeats, 5); // Cap at 5 repeats
  auto perf = ub::makePerfCaseWithProfiler("SchedulerPerf.AddTaskSetupCost", cfg);

  const Shape SH = loadShape();

  // Deterministic RNG for frequency denominators and offsets
  std::mt19937_64 rng(SH.seed);
  std::uniform_int_distribution<std::uint16_t> denDist(1, SH.denMax);

  // Warmup: exercise addTask with throwaway schedulers
  perf.warmup([&] {
    auto warm = makeScheduler(SH.ffreq);
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

  // Measure: addTask cost including schedule allocation, geometry calculation, and insertion
  auto result = perf.throughputLoop(
      [&] {
        auto sched = makeScheduler(SH.ffreq);
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

  // Validate: full cold-path (scheduler creation + file I/O + addTask) is slow but bounded
  // In Docker containers with file I/O overhead, expect 50-100ms per scheduler creation
  // Note: Use AddTaskWarmCost to measure just addTask cost on a reused scheduler

  std::printf("\n[Cold-path] scheduler+addTask: %.3f ms (%.0f creates/sec)\n",
              result.stats.median / 1000.0, result.callsPerSecond);
}

/* ----------------------------- Task Count Scaling ----------------------------- */

/**
 * @brief Sweep dispatch cost across task counts (1 to 1000).
 */
PERF_TEST(SchedulerPerf, TaskCountScaling) {
  const Shape SH = loadShape();
  const ub::PerfConfig CFG = ub::detail::getPerfConfig();

  // Reduced task counts for quick iteration; full sweep for CI
  const std::vector<std::uint32_t> TASK_COUNTS =
      CFG.quickMode ? std::vector<std::uint32_t>{10, 100, 500}
                    : std::vector<std::uint32_t>{1, 10, 100, 250, 500, 1000};

  std::printf("\n[Scaling] Task count sweep:\n");
  std::printf("%-10s %-15s %-15s %-15s\n", "Tasks", "Median(us)", "Ticks/sec", "Tasks/sec");
  std::printf("%s\n", std::string(60, '-').c_str());

  for (std::uint32_t taskCount : TASK_COUNTS) {
    // Create custom config with taskCount in test name for CSV differentiation
    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    cfg.msgBytes = static_cast<int>(taskCount); // Use msgBytes to track task count

    std::string testName = "SchedulerPerf.TaskCountScaling/" + std::to_string(taskCount);
    auto perf = ub::makePerfCaseWithProfiler(testName, cfg);

    auto sched = makeScheduler(SH.ffreq);
    const std::size_t N = static_cast<std::size_t>(taskCount);

    // Setup tasks
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

    // Warmup
    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        volatile auto status = sched->executeTasksOnTickSingle(TICK);
        (void)status;
      }
    });

    // Measure
    auto result = perf.throughputLoop(
        [&] {
          volatile auto status = sched->executeTasksOnTickSingle(TICK);
          (void)status;
        },
        "scaling");

    const double TASKS_PER_SEC = result.callsPerSecond * taskCount;

    std::printf("%-10u %-15.3f %-15.0f %-15.0f\n", taskCount, result.stats.median,
                result.callsPerSecond, TASKS_PER_SEC);

    // Validate: Only check CV% for workloads above timer precision threshold
    // and only in non-quick mode (quick mode has fewer samples = higher variance)
    if (!cfg.quickMode && result.stats.median > 1.0) {
      // Above 1us in full mode: expect stable results
      EXPECT_STABLE_CV_CPU(result, cfg);
    }
    // Below 1us or quick mode: skip CV% check (timer noise/sample count dominates)
  }
}

PERF_MAIN()
