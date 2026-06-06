/**
 * @file SchedulableTask_pTest.cpp
 * @brief Performance tests for SchedulableTask hot paths.
 *
 * Measures:
 *  - Task construction overhead (cold path)
 *  - execute() hot path (direct delegate call)
 *  - Virtual call dispatch cost through base pointer
 *  - Cache hierarchy effects (L1 vs larger working sets)
 *  - Task count scaling
 *
 * Usage:
 *   ./SchedulableTask_PTEST --csv results.csv
 *   ./SchedulableTask_PTEST --quick
 *   ./SchedulableTask_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ub = vernier::bench;

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SchedulableTaskBase;

/* ----------------------------- Test Fixtures ----------------------------- */

namespace {

struct TaskContext {
  std::uint32_t counter{0};
  std::uint8_t rc{0};
};

std::uint8_t simpleTask(void* ctx) noexcept {
  auto* tc = static_cast<TaskContext*>(ctx);
  tc->counter++;
  return tc->rc;
}

std::uint8_t noopTask(void*) noexcept { return 0; }

struct TaskObject {
  std::uint32_t counter{0};
  std::uint8_t increment() noexcept {
    counter++;
    return 0;
  }
};

template <class T, std::uint8_t (T::*MemFn)() noexcept>
inline std::uint8_t memberTrampoline(void* ctx) noexcept {
  return (static_cast<T*>(ctx)->*MemFn)();
}

} // namespace

/* ----------------------------- Core Hot Path ----------------------------- */

/**
 * @brief Task construction overhead (cold path).
 */
PERF_TEST(SchedulableTaskPerf, ConstructionOverhead) {
  UB_PERF_GUARD(perf);

  TaskContext ctx{};
  DelegateU8 del{&simpleTask, &ctx};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      SchedulableTask task(del, "warmup");
      (void)task;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        SchedulableTask task(del, "test");
        (void)task;
      },
      "construction");

  std::printf("\n[ConstructionOverhead] Cold path\n");
  std::printf("  Latency: %.3f us (median), %.3f us (p90)\n", result.stats.median,
              result.stats.p90);
  std::printf("  Throughput: %.0f constructions/sec\n", result.callsPerSecond);
}

/**
 * @brief execute() baseline hot path (direct delegate call).
 */
PERF_TEST(SchedulableTaskPerf, ExecuteBaseline) {
  UB_PERF_GUARD(perf);

  TaskContext ctx{};
  DelegateU8 del{&simpleTask, &ctx};
  SchedulableTask task(del, "test");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)task.execute();
    }
  });

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop([&] { sink = task.execute(); }, "execute-baseline");

  (void)sink;
  EXPECT_GT(ctx.counter, 0);
  std::printf("\n[ExecuteBaseline] Hot path\n");
  std::printf("  Latency: %.3f us (median), %.3f us (p90)\n", result.stats.median,
              result.stats.p90);
  std::printf("  Throughput: %.0f M calls/sec\n", result.callsPerSecond / 1e6);
  std::printf("  Jitter (CV%%): %.1f%%\n", result.stats.cv * 100);
}

/**
 * @brief Noop execute (minimum dispatch overhead, empty function body).
 */
PERF_TEST(SchedulableTaskPerf, ExecuteNoop) {
  UB_PERF_GUARD(perf);

  DelegateU8 del{&noopTask, nullptr};
  SchedulableTask task(del, "noop");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)task.execute();
    }
  });

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop([&] { sink = task.execute(); }, "execute-noop");

  (void)sink;
  std::printf("\n[ExecuteNoop] Absolute minimum overhead\n");
  std::printf("  Latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f M calls/sec\n", result.callsPerSecond / 1e6);
}

/**
 * @brief Virtual call overhead through base pointer (vtable dispatch).
 */
PERF_TEST(SchedulableTaskPerf, VirtualCallOverhead) {
  UB_PERF_GUARD(perf);

  TaskContext ctx{};
  DelegateU8 del{&simpleTask, &ctx};
  SchedulableTask task(del, "test");

  SchedulableTaskBase* basePtr = &task;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles() / 2; ++i) {
      (void)task.execute();
      (void)basePtr->execute();
    }
  });

  volatile std::uint8_t sink = 0;
  auto r1 = perf.throughputLoop([&] { sink = task.execute(); }, "direct-call");
  auto r2 = perf.throughputLoop([&] { sink = basePtr->execute(); }, "virtual-call");

  (void)sink;
  const double OVERHEAD_NS = (r2.stats.median - r1.stats.median) * 1000;
  std::printf("\n[VirtualCallOverhead] Vtable dispatch cost\n");
  std::printf("  Direct: %.3f us, Virtual: %.3f us\n", r1.stats.median, r2.stats.median);
  std::printf("  Overhead: %.1f ns\n", OVERHEAD_NS);
}

/**
 * @brief Member function binding overhead via trampoline.
 */
PERF_TEST(SchedulableTaskPerf, MemberBindingOverhead) {
  UB_PERF_GUARD(perf);

  TaskObject obj;
  DelegateU8 memberDel{&memberTrampoline<TaskObject, &TaskObject::increment>, &obj};
  SchedulableTask task(memberDel, "member");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)task.execute();
    }
  });

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop([&] { sink = task.execute(); }, "member-binding");

  (void)sink;
  std::printf("\n[MemberBindingOverhead] Member function via trampoline\n");
  std::printf("  Latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f M calls/sec\n", result.callsPerSecond / 1e6);
  std::printf("  Counter: %u\n", obj.counter);
}

/* ----------------------------- Callable Accessor ----------------------------- */

/**
 * @brief Direct callable() access vs virtual execute().
 */
PERF_TEST(SchedulableTaskPerf, CallableAccessor) {
  UB_PERF_GUARD(perf);

  TaskContext ctx{};
  DelegateU8 del{&simpleTask, &ctx};
  SchedulableTask task(del, "accessor");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      (void)task.callable()();
    }
  });

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop([&] { sink = task.callable()(); }, "callable-direct");

  (void)sink;
  std::printf("\n[CallableAccessor] Direct delegate access\n");
  std::printf("  Latency: %.3f us (median)\n", result.stats.median);
  std::printf("  Throughput: %.0f M calls/sec\n", result.callsPerSecond / 1e6);

  EXPECT_STABLE_CV_CPU(result, perf.config());
}

/* ----------------------------- Cache Hierarchy ----------------------------- */

/**
 * @brief Dispatch performance for L1-fit task arrays (~half of 32KB L1).
 */
PERF_TEST(SchedulableTaskCache, L1FitDispatch) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t L1_SIZE = static_cast<const std::size_t>(32 * 1024);
  constexpr std::size_t TASK_SIZE = sizeof(SchedulableTask);
  constexpr std::size_t NUM_TASKS = (L1_SIZE / TASK_SIZE) / 2;

  std::vector<TaskContext> contexts(NUM_TASKS);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(NUM_TASKS);

  for (std::size_t i = 0; i < NUM_TASKS; ++i) {
    DelegateU8 del{&simpleTask, &contexts[i]};
    tasks.push_back(std::make_unique<SchedulableTask>(del, "task"));
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (auto& t : tasks) {
        (void)t->execute();
      }
    }
  });

  volatile std::uint8_t sink = 0;
  auto result = perf.throughputLoop(
      [&] {
        for (auto& t : tasks) {
          sink = t->execute();
        }
      },
      "l1-dispatch");

  (void)sink;
  const double TASKS_PER_SEC = result.callsPerSecond * NUM_TASKS;
  std::printf("\n[L1FitDispatch] Cache-friendly dispatch\n");
  std::printf("  Tasks: %zu (%.1f KB)\n", NUM_TASKS, (NUM_TASKS * TASK_SIZE) / 1024.0);
  std::printf("  Latency per batch: %.3f us\n", result.stats.median);
  std::printf("  Tasks dispatched: %.0f M/sec\n", TASKS_PER_SEC / 1e6);
}

/**
 * @brief Dispatch performance for a working set larger than L1.
 */
PERF_TEST(SchedulableTaskCache, LargeWorkingSet) {
  UB_PERF_GUARD(perf);

  constexpr std::size_t NUM_TASKS = 4096;

  std::vector<TaskContext> contexts(NUM_TASKS);
  std::vector<std::unique_ptr<SchedulableTask>> tasks;
  tasks.reserve(NUM_TASKS);

  for (std::size_t i = 0; i < NUM_TASKS; ++i) {
    DelegateU8 del{&simpleTask, &contexts[i]};
    tasks.push_back(std::make_unique<SchedulableTask>(del, "task"));
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (auto& t : tasks) {
        (void)t->execute();
      }
    }
  });

  volatile std::uint8_t sink = 0;
  const std::size_t TASK_SIZE = sizeof(SchedulableTask);
  ub::MemoryProfile memProfile{};
  memProfile.bytesRead = NUM_TASKS * (sizeof(SchedulableTask*) + TASK_SIZE);
  memProfile.bytesWritten = NUM_TASKS * sizeof(std::uint32_t);
  memProfile.bytesAllocated = 0;

  auto result = perf.throughputLoop(
      [&] {
        for (auto& t : tasks) {
          sink = t->execute();
        }
      },
      "large-dispatch", memProfile);

  (void)sink;
  const double TASKS_PER_SEC = result.callsPerSecond * NUM_TASKS;
  const double BW_MBS = memProfile.bandwidthMBs(result.stats.median);

  std::printf("\n[LargeWorkingSet] Beyond L1 cache\n");
  std::printf("  Tasks: %zu (%.1f KB)\n", NUM_TASKS, (NUM_TASKS * TASK_SIZE) / 1024.0);
  std::printf("  Latency per batch: %.3f us\n", result.stats.median);
  std::printf("  Tasks dispatched: %.0f M/sec\n", TASKS_PER_SEC / 1e6);
  std::printf("  Memory bandwidth: %.0f MB/s\n", BW_MBS);
}

/* ----------------------------- Scaling ----------------------------- */

/**
 * @brief Task count scaling sweep (1 to 1000 tasks per batch).
 */
PERF_TEST(SchedulableTaskScaling, TaskCountSweep) {
  const std::vector<std::size_t> TASK_COUNTS = {1, 10, 50, 100, 250, 500, 1000};

  std::printf("\n[TaskCountSweep] Scaling analysis:\n");
  std::printf("%-10s %-15s %-15s %-15s\n", "Tasks", "Batch(us)", "Tasks/sec", "Per-task(ns)");
  std::printf("%s\n", std::string(60, '-').c_str());

  for (std::size_t taskCount : TASK_COUNTS) {
    ub::PerfConfig cfg = ub::detail::getPerfConfig();
    std::string testName = "SchedulableTaskScaling.TaskCountSweep/" + std::to_string(taskCount);
    ub::PerfCase perf{testName, cfg};
    ub::attachProfilerHooks(perf, cfg);

    std::vector<TaskContext> contexts(taskCount);
    std::vector<std::unique_ptr<SchedulableTask>> tasks;
    tasks.reserve(taskCount);

    for (std::size_t i = 0; i < taskCount; ++i) {
      DelegateU8 del{&simpleTask, &contexts[i]};
      tasks.push_back(std::make_unique<SchedulableTask>(del, "task"));
    }

    perf.warmup([&] {
      for (int i = 0; i < perf.cycles(); ++i) {
        for (auto& t : tasks) {
          (void)t->execute();
        }
      }
    });

    volatile std::uint8_t sink = 0;
    auto result = perf.throughputLoop(
        [&] {
          for (auto& t : tasks) {
            sink = t->execute();
          }
        },
        "scaling");

    (void)sink;
    const double TASKS_PER_SEC = result.callsPerSecond * taskCount;
    const double NS_PER_TASK = (result.stats.median * 1000.0) / taskCount;

    std::printf("%-10zu %-15.3f %-15.0f %-15.1f\n", taskCount, result.stats.median, TASKS_PER_SEC,
                NS_PER_TASK);
  }
}

PERF_MAIN()
