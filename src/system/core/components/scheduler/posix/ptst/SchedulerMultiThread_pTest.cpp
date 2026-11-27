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

#include <atomic>
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
  int workers{0};     // Workers of the one pool; 0 -> one per hardware thread
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
  s.workers = envI("MT_WORKERS", 0);
  return s;
}

// Per-task execution context
struct TaskCtx {
  std::uint32_t spinIters{0};
  std::uint8_t rc{static_cast<std::uint8_t>(Status::SUCCESS)};
};

// Task bodies run since the process started. A case compares the runs it
// caused with the dispatches it made.
static std::atomic<std::uint64_t> g_bodyRuns{0};

// Task execution function
static std::uint8_t taskThunk(void* ctx) noexcept {
  auto* c = static_cast<TaskCtx*>(ctx);
  const std::uint32_t SPIN = c->spinIters;

  for (std::uint32_t i = 0; i < SPIN; ++i) {
    asm volatile("" ::: "memory");
  }

  g_bodyRuns.fetch_add(1, std::memory_order_relaxed);
  return c->rc;
}

// A task body the case can keep inside itself.
static std::atomic<bool> g_hold{false};
static std::atomic<bool> g_heldInside{false};

static std::uint8_t holdThunk(void*) noexcept {
  if (g_hold.load(std::memory_order_acquire)) {
    g_heldInside.store(true, std::memory_order_release);
    const auto END = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (g_hold.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < END) {
      std::this_thread::yield();
    }
    g_heldInside.store(false, std::memory_order_release);
  }
  g_bodyRuns.fetch_add(1, std::memory_order_relaxed);
  return 0;
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

// Scheduler factory. The scheduler is not started: a case registers its
// tasks and then calls startScheduler(), because init() sizes the dispatch
// contexts for the table it finds.
inline std::unique_ptr<SchedulerMultiThread> makeScheduler(const Shape& sh) {
  const auto LOG_DIR = std::filesystem::temp_directory_path() / "scheduler_mt_ptest";
  std::filesystem::create_directories(LOG_DIR);

  static std::vector<std::filesystem::path> toClean;
  toClean.push_back(LOG_DIR);

  if (sh.workers > 0) {
    system_core::scheduler::PoolSpec spec;
    spec.numThreads = static_cast<std::size_t>(sh.workers);
    return std::make_unique<SchedulerMultiThread>(
        sh.ffreq, LOG_DIR, std::vector<system_core::scheduler::PoolSpec>{spec});
  }
  return std::make_unique<SchedulerMultiThread>(sh.ffreq, LOG_DIR);
}

// Start a scheduler whose tasks are registered.
inline void startScheduler(SchedulerMultiThread& sched, const Shape& sh) {
  ASSERT_EQ(sched.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  trySetMaxWorkers(sched, sh.maxWorkers);
}

// What a case dispatched and what came of it. Every dispatch is followed
// by a wait for the work it queued, so the next one starts from rest and a
// measured interval is a whole tick's work; the instructions of the
// dispatch call alone are read from a profile's call records.
class Delivery {
public:
  static constexpr std::uint64_t SETTLE_NS = 5'000'000'000ULL;

  // TASKS_PER_DISPATCH: the tasks one tick() selects.
  Delivery(SchedulerMultiThread& sched, std::uint64_t tasksPerDispatch) noexcept
      : sched_(sched), perDispatch_(tasksPerDispatch),
        runsAtStart_(g_bodyRuns.load(std::memory_order_acquire)),
        dropsAtStart_(sched.totalDispatchDrops()), skipsAtStart_(sched.totalSkipCount()) {}

  // Dispatch TICK, then wait for everything it queued.
  void tick(std::uint16_t tick) noexcept {
    volatile auto status = sched_.executeTasksOnTickMulti(tick);
    (void)status;
    ++dispatches_;
    visited_ += perDispatch_;
    settle();
  }

  // Dispatch CYCLE, on which VISITED tasks sit on the slot, without waiting.
  void dispatchCycle(std::uint64_t cycle, std::uint64_t visited) noexcept {
    volatile auto status = sched_.executeTasksOnCycleMulti(cycle);
    (void)status;
    ++dispatches_;
    visited_ += visited;
  }

  // Wait for everything queued so far.
  void settle() noexcept {
    if (!sched_.waitForInFlightTasks(SETTLE_NS)) {
      ++unsettled_;
    }
  }

  // Print the counts and require that every selected task was admitted,
  // ran and finished. Selected: visited less those skipped as still busy.
  void expectWhole() const {
    const std::uint64_t SKIPPED = sched_.totalSkipCount() - skipsAtStart_;
    const std::uint64_t DROPPED = sched_.totalDispatchDrops() - dropsAtStart_;
    const std::uint64_t SELECTED = visited_ - SKIPPED;
    const std::uint64_t RUN = g_bodyRuns.load(std::memory_order_acquire) - runsAtStart_;
    std::printf("  Dispatches: %llu  visited: %llu  selected: %llu  admitted: %llu  "
                "completed: %llu  unsettled waits: %llu\n",
                static_cast<unsigned long long>(dispatches_),
                static_cast<unsigned long long>(visited_),
                static_cast<unsigned long long>(SELECTED),
                static_cast<unsigned long long>(SELECTED - DROPPED),
                static_cast<unsigned long long>(RUN), static_cast<unsigned long long>(unsettled_));
    EXPECT_EQ(unsettled_, 0U) << "a wait for queued work ran out";
    EXPECT_EQ(DROPPED, 0U) << "a dispatch found no context or was refused by its pool";
    EXPECT_EQ(RUN, SELECTED) << "not every selected task ran";
  }

private:
  SchedulerMultiThread& sched_;
  std::uint64_t perDispatch_;
  std::uint64_t runsAtStart_;
  std::uint64_t dropsAtStart_;
  std::uint64_t skipsAtStart_;
  std::uint64_t dispatches_{0};
  std::uint64_t visited_{0};
  std::uint64_t unsettled_{0};
};

} // namespace

/* ----------------------------- Baseline ----------------------------- */

/**
 * @brief Empty tick overhead through the multi-threaded dispatch path.
 */
PERF_TEST(SchedulerMtPerf, EmptyTickOverhead) {
  UB_PERF_GUARD(perf);

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  startScheduler(*sched, SH);
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.ManyNoWork", cfg);

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

  startScheduler(*sched, SH);
  Delivery delivery(*sched, N);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      delivery.tick(TICK);
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

  auto result = perf.throughputLoop([&] { delivery.tick(TICK); }, "many-nowork", memProfile);

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

  delivery.expectWhole();

  sched.reset(); // Join workers before caller-owned tasks are destroyed.
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.ManyLightWork", cfg);

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

  startScheduler(*sched, SH);
  Delivery delivery(*sched, N);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      delivery.tick(TICK);
    }
  });

  // Memory profile with work included
  const size_t TASK_META = sizeof(SchedulableTask*) + 64;
  const size_t WORKER_STACK = static_cast<const size_t>(std::thread::hardware_concurrency() * 8192);
  const size_t COHERENCY = N * 64;

  ub::MemoryProfile memProfile{.bytesRead = N * TASK_META + WORKER_STACK + COHERENCY,
                               .bytesWritten = N * sizeof(std::uint8_t),
                               .bytesAllocated = 0};

  auto result = perf.throughputLoop([&] { delivery.tick(TICK); }, "many-light", memProfile);

  // With real work (>1us), expect stable results. The CV gate needs a
  // meaningful sample: reduced Tier-A runs (few repeats) estimate CV
  // with no statistical power and flake at the threshold, so the gate
  // arms only at recorded-battery depth.
  if (result.stats.median > 1.0 && perf.config().repeats >= 5) {
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

  delivery.expectWhole();

  sched.reset(); // Join workers before caller-owned tasks are destroyed.
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.ThreadScaling", cfg);

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

  startScheduler(*sched, SH);
  Delivery delivery(*sched, N);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      delivery.tick(0);
    }
  });

  auto result = perf.throughputLoop([&] { delivery.tick(0); }, "thread-scaling");

  const double tasksPerSec = (result.callsPerSecond * N) / 1e3;

  std::printf("%-10d %-15.3f %-15.0f %-15s %-15s\n", workerCount, result.stats.median, tasksPerSec,
              "N/A", "N/A");

  std::printf("\nInterpretation:\n");
  std::printf("  >80%% efficiency: Excellent scaling\n");
  std::printf("  50-80%% efficiency: Good, some contention\n");
  std::printf("  <50%% efficiency: Poor scaling, investigate locks/cache\n");

  delivery.expectWhole();

  sched.reset(); // Join workers before caller-owned tasks are destroyed.
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.TaskCountScaling", cfg);

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

    startScheduler(*sched, SH);
    Delivery delivery(*sched, taskCount);

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        delivery.tick(0);
      }
    });

    auto result = perf.throughputLoop([&] { delivery.tick(0); }, "task-count");

    const double perTaskNs = (result.stats.median * 1000.0) / taskCount;
    const double cvPercent = result.stats.cv * 100.0;

    std::printf("%-10zu %-15.3f %-15.1f %-15.1f\n", taskCount, result.stats.median, perTaskNs,
                cvPercent);

    delivery.expectWhole();

    sched.reset(); // Join workers before this iteration's tasks are destroyed.
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.ConcurrentExecutionStress", cfg);

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

  startScheduler(*sched, SH);
  Delivery delivery(*sched, N);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      delivery.tick(0);
    }
  });

  // Measure repeated execution to stress test consistency
  auto result = perf.throughputLoop([&] { delivery.tick(0); }, "concurrent-stress");

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

  delivery.expectWhole();

  sched.reset(); // Join workers before caller-owned tasks are destroyed.
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.AddTaskSetupCost", cfg);

  const Shape SH = loadShape();
  std::mt19937_64 rng(SH.seed);
  std::uniform_int_distribution<std::uint16_t> denDist(1, SH.denMax);

  perf.warmup([&] {
    auto warm = makeScheduler(SH);
    startScheduler(*warm, SH);
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
        startScheduler(*sched, SH);
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
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.InitOnlyCost", cfg);

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

  const Shape SH = loadShape();
  auto sched = makeScheduler(SH);
  startScheduler(*sched, SH);
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
  cfg.cycles = std::min(cfg.cycles, 200); // Each dispatch waits for its chain
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.SequencedChain4", cfg);

  const Shape SH = loadShape();

  // 4-task sequence: pre1/pre2 (parallel) -> step -> post
  // After all 4 complete, counter wraps to 1. Declared before the
  // scheduler: it must outlive the shutdown sentinel sweep.
  SequenceGroup seq(4);
  auto sched = makeScheduler(SH);

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

  startScheduler(*sched, SH);
  Delivery delivery(*sched, 4U);

  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) { // Reduced warmup cycles
      delivery.tick(TICK);
    }
  });

  auto result = perf.throughputLoop([&] { delivery.tick(TICK); }, "seq-chain4");

  std::printf("\n[Sequencing-MT] 4-task chain (pre1|pre2 -> step -> post):\n");
  std::printf("  Total: %.3f us\n", result.stats.median);
  std::printf("  Per-task: %.3f us\n", result.stats.median / 4.0);
  std::printf("  Workers: %u\n", std::thread::hardware_concurrency());
  std::printf("  Spin: %u iters\n", SH.spin);

  delivery.expectWhole();

  sched.reset(); // Join workers before caller-owned tasks are destroyed.
}

/* ----------------------------- Sequencing Overhead ----------------------------- */

/**
 * @brief Compare sequenced (serial) vs non-sequenced (parallel) dispatch overhead.
 */
PERF_TEST(SchedulerMtPerf, SequencingOverhead) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200); // Each dispatch waits for its tasks
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
  startScheduler(*schedNonSeq, SH);
  Delivery deliveryNonSeq(*schedNonSeq, 4U);
  double nonSeqMedian = 0.0;
  {
    auto resultNonSeq = perf.throughputLoop([&] { deliveryNonSeq.tick(TICK); }, "nonseq-4task");
    nonSeqMedian = resultNonSeq.stats.median;
  }
  deliveryNonSeq.expectWhole(); // Before the second scheduler's tasks add to the run count.

  // Test 2: Sequenced (4 tasks in chain - strictly sequential)
  SequenceGroup seq(4); // Outlives the scheduler (shutdown sentinel sweep).
  auto schedSeq = makeScheduler(SH);
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

  startScheduler(*schedSeq, SH);
  Delivery deliverySeq(*schedSeq, 4U);

  double seqMedian = 0.0;
  {
    auto resultSeq = perf.throughputLoop([&] { deliverySeq.tick(TICK); }, "seq-4task");
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

  deliverySeq.expectWhole();

  schedSeq.reset(); // Join workers before caller-owned tasks are destroyed.
  schedNonSeq.reset();
}

/* ----------------------------- Sequenced Slots ----------------------------- */

namespace {

/// One slot's tasks: GROUPS chains of four (pre1 | pre2 -> step -> post),
/// each its own sequence group, and UNSEQUENCED plain tasks. The groups
/// are declared ahead of the scheduler: they outlive its shutdown sweep.
struct SequencedSlot {
  std::vector<std::unique_ptr<SequenceGroup>> groups;
  std::unique_ptr<SchedulerMultiThread> sched;
  std::vector<std::unique_ptr<TaskCtx>> ctxPool;
  std::vector<std::string> labels;
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
};

/// Register the slot's tasks and start the scheduler. HELD_PRE1 gives the
/// first group's pre1 the body a case can hold; PACED_PRE2 runs the first
/// group's pre2 on every other tick instead of on each.
void buildSequencedSlot(SequencedSlot& slot, const Shape& sh, std::size_t groups,
                        std::size_t unsequenced, bool heldPre1 = false, bool pacedPre2 = false) {
  slot.sched = makeScheduler(sh);
  SchedulerMultiThread& sched = *slot.sched;
  const std::uint16_t HZ = sched.fundamentalFreq();
  slot.labels.reserve(groups * 4U + unsequenced);

  const auto ADD = [&](std::string_view role, std::size_t n, SequenceGroup* seq, int phase,
                       system_core::scheduler::TaskPriority priority, bool held, bool paced) {
    slot.labels.emplace_back(std::string(role) + std::to_string(n));
    std::unique_ptr<SchedulableTask> task;
    if (held) {
      task = std::make_unique<SchedulableTask>(DelegateU8{&holdThunk, nullptr},
                                               std::string_view(slot.labels.back()));
    } else {
      task = makeFuncTask(slot.ctxPool, std::string_view(slot.labels.back()), 0U);
    }
    const TaskConfig CFG(HZ, paced ? 2 : 1, 0, priority);
    if (seq != nullptr) {
      seq->addTask(*task, phase);
      ASSERT_EQ(sched.addTask(*task, CFG, seq), Status::SUCCESS);
    } else {
      ASSERT_EQ(sched.addTask(*task, CFG), Status::SUCCESS);
    }
    slot.tasks.push_back(std::move(task));
  };

  for (std::size_t g = 0; g < groups; ++g) {
    slot.groups.push_back(std::make_unique<SequenceGroup>(4));
    SequenceGroup* const SEQ = slot.groups.back().get();
    const bool FIRST = (g == 0U);
    ADD("pre1_", g, SEQ, 1, system_core::scheduler::PRIORITY_HIGH, FIRST && heldPre1, false);
    ADD("pre2_", g, SEQ, 1, system_core::scheduler::PRIORITY_HIGH, false, FIRST && pacedPre2);
    ADD("step_", g, SEQ, 3, system_core::scheduler::PRIORITY_NORMAL, false, false);
    ADD("post_", g, SEQ, 4, system_core::scheduler::PRIORITY_LOW, false, false);
  }
  for (std::size_t u = 0; u < unsequenced; ++u) {
    ADD("plain_", u, nullptr, 0, system_core::scheduler::PRIORITY_NORMAL, false, false);
  }
  startScheduler(sched, sh);
}

/// Dispatch one tick of GROUPS chains of four beside UNSEQUENCED plain
/// tasks, each dispatch waiting for the tick's work.
void runSequencedSlot(const char* name, const char* label, std::size_t groups,
                      std::size_t unsequenced) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler(name, cfg);

  const Shape SH = loadShape();
  SequencedSlot slot;
  ASSERT_NO_FATAL_FAILURE(buildSequencedSlot(slot, SH, groups, unsequenced));
  const std::size_t ON_SLOT = groups * 4U + unsequenced;
  Delivery delivery(*slot.sched, ON_SLOT);
  const std::uint16_t TICK = 0;

  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) {
      delivery.tick(TICK);
    }
  });
  auto result = perf.throughputLoop([&] { delivery.tick(TICK); }, label);

  std::printf("\n[Sequencing-MT] %zu chain(s) of four and %zu plain task(s) on one tick:\n", groups,
              unsequenced);
  std::printf("  Total: %.3f us  (%.3f us per task)\n", result.stats.median,
              result.stats.median / static_cast<double>(ON_SLOT));
  delivery.expectWhole();

  slot.sched.reset(); // Join workers before the tasks and groups are destroyed.
}

} // namespace

/** @brief One chain of four on the tick: 4 sequenced entries, 1 group. */
PERF_TEST(SchedulerMtPerf, SequencedGroups1) {
  runSequencedSlot("SchedulerMtPerf.SequencedGroups1", "seq-groups-1", 1, 0);
}

/** @brief Four independent chains of four: 16 sequenced entries, 4 groups. */
PERF_TEST(SchedulerMtPerf, SequencedGroups4) {
  runSequencedSlot("SchedulerMtPerf.SequencedGroups4", "seq-groups-4", 4, 0);
}

/** @brief Sixteen independent chains of four: 64 sequenced entries, 16 groups. */
PERF_TEST(SchedulerMtPerf, SequencedGroups16) {
  runSequencedSlot("SchedulerMtPerf.SequencedGroups16", "seq-groups-16", 16, 0);
}

/** @brief One chain of four beside 252 plain tasks: a slot of 256 that mixes both. */
PERF_TEST(SchedulerMtPerf, SequencedMixedSlot) {
  runSequencedSlot("SchedulerMtPerf.SequencedMixedSlot", "seq-mixed-256", 1, 252);
}

/**
 * @brief A chain whose pre2 runs on every other tick, over consecutive
 *        cycles: the cadence test comes before the chain is ordered, so
 *        the step follows two tasks on one cycle and one on the next.
 */
PERF_TEST(SchedulerMtPerf, SequencedPacedMember) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.SequencedPacedMember", cfg);

  const Shape SH = loadShape();
  SequencedSlot slot;
  ASSERT_NO_FATAL_FAILURE(buildSequencedSlot(slot, SH, 1, 0, false, true));
  Delivery delivery(*slot.sched, 0);
  std::uint64_t cycle = 0;

  // pre2 runs on the even cycles: four tasks there, three on the odd ones.
  const auto PAIR = [&] {
    delivery.dispatchCycle(cycle, (cycle % 2U == 0U) ? 4U : 3U);
    delivery.settle();
    ++cycle;
    delivery.dispatchCycle(cycle, (cycle % 2U == 0U) ? 4U : 3U);
    delivery.settle();
    ++cycle;
  };
  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) {
      PAIR();
    }
  });
  auto result = perf.throughputLoop(PAIR, "seq-paced-pair");

  std::printf("\n[Sequencing-MT] chain of four, pre2 on every other tick (two cycles):\n");
  std::printf("  Total: %.3f us\n", result.stats.median);
  delivery.expectWhole();

  slot.sched.reset();
}

/**
 * @brief A chain whose pre1 is still running when the next cycle comes:
 *        that cycle visits four entries and selects none, the group
 *        being still under way. pre1 is let go afterwards and the pair
 *        of cycles is waited out.
 */
PERF_TEST(SchedulerMtPerf, SequencedBusyMember) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 100);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SchedulerMtPerf.SequencedBusyMember", cfg);

  const Shape SH = loadShape();
  SequencedSlot slot;
  ASSERT_NO_FATAL_FAILURE(buildSequencedSlot(slot, SH, 1, 0, true, false));
  slot.sched->setSkipOnBusy(true);
  Delivery delivery(*slot.sched, 0);
  std::uint64_t cycle = 0;
  std::uint64_t notHeld = 0;

  const auto PAIR = [&] {
    // First cycle: all four go out; pre1 stays in its body, pre2 finishes,
    // step and post wait behind pre1.
    const std::uint64_t RUNS = g_bodyRuns.load(std::memory_order_acquire);
    g_hold.store(true, std::memory_order_release);
    delivery.dispatchCycle(cycle++, 4U);
    const auto END = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!g_heldInside.load(std::memory_order_acquire) ||
            g_bodyRuns.load(std::memory_order_acquire) == RUNS) &&
           std::chrono::steady_clock::now() < END) {
      std::this_thread::yield();
    }
    if (!g_heldInside.load(std::memory_order_acquire)) {
      ++notHeld;
    }
    // Second cycle: pre1, step and post are still busy and pre2's group
    // is still running, so all four are skipped.
    delivery.dispatchCycle(cycle++, 4U);
    g_hold.store(false, std::memory_order_release);
    delivery.settle();
  };
  perf.warmup([&] {
    for (int i = 0; i < 5; ++i) {
      PAIR();
    }
  });
  auto result = perf.throughputLoop(PAIR, "seq-busy-pair");

  std::printf("\n[Sequencing-MT] chain of four, pre1 busy across the next cycle (two cycles):\n");
  std::printf("  Total: %.3f us\n", result.stats.median);
  std::printf("  Skipped as busy: %zu\n", slot.sched->totalSkipCount());
  EXPECT_EQ(notHeld, 0U) << "pre1 was not inside its body when the second cycle came";
  delivery.expectWhole();

  slot.sched.reset();
}

PERF_MAIN()
