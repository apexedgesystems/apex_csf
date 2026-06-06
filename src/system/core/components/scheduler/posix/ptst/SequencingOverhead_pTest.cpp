/**
 * @file SequencingOverhead_pTest.cpp
 * @brief Performance tests for intra-frame sequencing overhead.
 *
 * Measures:
 *  - waitForPhase fast-path latency (counter already matches)
 *  - advancePhase + notifyAll cost
 *  - waitForPhase spin/park path with signaler thread
 *  - End-to-end 4-task sequenced chain overhead (minimal and ModelTest2 timing)
 *  - Sequenced vs non-sequenced dispatch comparison
 *  - ThreadPool enqueue-to-execute dispatch latency
 *
 * Usage:
 *   ./SequencingOverhead_PTEST --csv results.csv
 *   ./SequencingOverhead_PTEST --quick
 *   ./SequencingOverhead_PTEST --profile perf --gtest_filter="*WaitForPhase*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ub = vernier::bench;
using apex::concurrency::DelegateU8;
using system_core::schedulable::advancePhase;
using system_core::schedulable::SequenceGroup;
using system_core::schedulable::waitForPhase;
using system_core::scheduler::SchedulableTask;
using system_core::scheduler::SchedulerMultiThread;
using system_core::scheduler::Status;
using system_core::scheduler::TaskConfig;

/* ----------------------------- Helpers ----------------------------- */

namespace {

struct TaskCtx {
  std::atomic<std::uint64_t>* startNs{nullptr};
  std::atomic<std::uint64_t>* endNs{nullptr};
  std::uint32_t workUs{0};
};

static std::uint8_t timedTaskThunk(void* ctx) noexcept {
  auto* c = static_cast<TaskCtx*>(ctx);
  if (c->startNs) {
    c->startNs->store(apex::helpers::cpu::getMonotonicNs(), std::memory_order_release);
  }
  if (c->workUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(c->workUs));
  }
  if (c->endNs) {
    c->endNs->store(apex::helpers::cpu::getMonotonicNs(), std::memory_order_release);
  }
  return 0;
}

inline std::unique_ptr<SchedulerMultiThread> makeScheduler(std::uint16_t ffreq = 100) {
  const auto LOG_DIR = std::filesystem::temp_directory_path() / "seq_overhead_ptest";
  std::filesystem::create_directories(LOG_DIR);
  auto sched = std::make_unique<SchedulerMultiThread>(ffreq, LOG_DIR);
  (void)sched->init();
  return sched;
}

inline double nsToUs(std::uint64_t ns) { return static_cast<double>(ns) / 1000.0; }

} // namespace

/* ----------------------------- waitForPhase Microbenchmarks ----------------------------- */

/**
 * @brief Measure waitForPhase when counter already matches (fast path).
 */
PERF_TEST(SeqOverhead, WaitForPhase_FastPath) {
  UB_PERF_GUARD(perf);

  std::atomic<int> counter{5};
  const int PHASE = 3; // Counter (5) >= phase (3), so fast path

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      waitForPhase(counter, PHASE);
    }
  });

  auto result = perf.throughputLoop([&] { waitForPhase(counter, PHASE); }, "waitForPhase-fastpath");

  std::printf("\n[waitForPhase] Fast path (counter >= phase):\n");
  std::printf("  Median: %.3f ns\n", result.stats.median * 1000.0);
  std::printf("  p90: %.3f ns\n", result.stats.p90 * 1000.0);
  std::printf("  Expected: <100 ns (single atomic load)\n");
}

/**
 * @brief Measure advancePhase cost (CAS + notifyAll).
 */
PERF_TEST(SeqOverhead, AdvancePhase_Cost) {
  UB_PERF_GUARD(perf);

  std::atomic<int> counter{1};
  const int MAX_PHASE = 4;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      advancePhase(counter, MAX_PHASE);
    }
  });

  // Reset counter
  counter.store(1, std::memory_order_release);

  auto result = perf.throughputLoop([&] { advancePhase(counter, MAX_PHASE); }, "advancePhase");

  std::printf("\n[advancePhase] CAS + notifyAll cost:\n");
  std::printf("  Median: %.3f us\n", result.stats.median);
  std::printf("  p90: %.3f us\n", result.stats.p90);
  std::printf("  Expected: 1-5 us (CAS + futex_wake)\n");
  std::printf("  Note: notifyAll wakes ALL waiters (thundering herd potential)\n");
}

/**
 * @brief Measure waitForPhase spin path cost.
 *
 * Simulates a waiting task that spins until signaled by another thread.
 * This measures the realistic overhead of inter-phase synchronization.
 */
PERF_TEST(SeqOverhead, WaitForPhase_SpinPath) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 1000); // Limited iterations due to thread spawning
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SeqOverhead.WaitForPhase_SpinPath", cfg);

  std::atomic<int> counter{1};
  const int WAIT_PHASE = 2;
  const int MAX_PHASE = 4;

  // Measure waiter latency from signal to wake
  std::vector<double> latenciesUs;
  latenciesUs.reserve(static_cast<std::size_t>(cfg.cycles));

  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) {
      counter.store(1, std::memory_order_release);

      std::thread signaler([&] {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        advancePhase(counter, MAX_PHASE);
      });

      const auto T0 = std::chrono::steady_clock::now();
      waitForPhase(counter, WAIT_PHASE);
      const auto T1 = std::chrono::steady_clock::now();
      (void)(T1 - T0);

      signaler.join();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        counter.store(1, std::memory_order_release);

        std::thread signaler([&] {
          std::this_thread::sleep_for(std::chrono::microseconds(5));
          advancePhase(counter, MAX_PHASE);
        });

        waitForPhase(counter, WAIT_PHASE);
        signaler.join();
      },
      "waitForPhase-spin");

  std::printf("\n[waitForPhase] Spin/park path (waiter + signaler):\n");
  std::printf("  Median: %.3f us\n", result.stats.median);
  std::printf("  p90: %.3f us\n", result.stats.p90);
  std::printf("  Note: Includes ~5us signal delay + thread overhead\n");
}

/* ----------------------------- End-to-End Sequencing ----------------------------- */

/**
 * @brief Measure full 4-task sequenced chain overhead.
 *
 * Chain: pre1|pre2 (parallel) -> step -> post
 * Each task does minimal work (1us spin).
 * Measures pure scheduling + sequencing overhead.
 */
PERF_TEST(SeqOverhead, Chain4_MinimalWork) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SeqOverhead.Chain4_MinimalWork", cfg);

  auto sched = makeScheduler(100);
  SequenceGroup seq(4);

  // Timing capture
  std::atomic<std::uint64_t> pre1Start{0}, pre1End{0};
  std::atomic<std::uint64_t> pre2Start{0}, pre2End{0};
  std::atomic<std::uint64_t> stepStart{0}, stepEnd{0};
  std::atomic<std::uint64_t> postStart{0}, postEnd{0};

  TaskCtx ctxPre1{&pre1Start, &pre1End, 1};
  TaskCtx ctxPre2{&pre2Start, &pre2End, 1};
  TaskCtx ctxStep{&stepStart, &stepEnd, 1};
  TaskCtx ctxPost{&postStart, &postEnd, 1};

  DelegateU8 delPre1{&timedTaskThunk, &ctxPre1};
  DelegateU8 delPre2{&timedTaskThunk, &ctxPre2};
  DelegateU8 delStep{&timedTaskThunk, &ctxStep};
  DelegateU8 delPost{&timedTaskThunk, &ctxPost};

  SchedulableTask taskPre1(delPre1, "pre1");
  SchedulableTask taskPre2(delPre2, "pre2");
  SchedulableTask taskStep(delStep, "step");
  SchedulableTask taskPost(delPost, "post");

  seq.addTask(taskPre1, 1);
  seq.addTask(taskPre2, 1);
  seq.addTask(taskStep, 3);
  seq.addTask(taskPost, 4);

  TaskConfig cfgHigh(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_HIGH);
  TaskConfig cfgNorm(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_NORMAL);
  TaskConfig cfgLow(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_LOW);

  ASSERT_EQ(sched->addTask(taskPre1, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskPre2, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskStep, cfgNorm, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskPost, cfgLow, &seq), Status::SUCCESS);

  const std::uint16_t TICK = 0;

  auto waitForCompletion = [&] {
    for (int wait = 0; wait < 100000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) == 1 && postEnd.load() > 0) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  };

  perf.warmup([&] {
    for (int i = 0; i < 10; ++i) {
      pre1Start = pre1End = pre2Start = pre2End = 0;
      stepStart = stepEnd = postStart = postEnd = 0;
      seq.reset();

      (void)sched->executeTasksOnTickMulti(TICK);
      waitForCompletion();
    }
  });

  std::vector<double> overheadUs;
  overheadUs.reserve(static_cast<std::size_t>(cfg.cycles * cfg.repeats));

  auto result = perf.throughputLoop(
      [&] {
        pre1Start = pre1End = pre2Start = pre2End = 0;
        stepStart = stepEnd = postStart = postEnd = 0;
        seq.reset();

        const auto DISPATCH_START = apex::helpers::cpu::getMonotonicNs();
        (void)sched->executeTasksOnTickMulti(TICK);
        waitForCompletion();
        const auto CHAIN_END = apex::helpers::cpu::getMonotonicNs();

        // Calculate overhead: total time - sequential work time
        const double TOTAL_US = nsToUs(CHAIN_END - DISPATCH_START);
        const double WORK_US = 4.0; // 4 tasks x 1us each (best case)
        overheadUs.push_back(TOTAL_US - WORK_US);
      },
      "chain4-minimal");

  // Calculate overhead stats
  std::sort(overheadUs.begin(), overheadUs.end());
  const std::size_t N = overheadUs.size();
  const double MEDIAN_OVERHEAD = (N > 0) ? overheadUs[N / 2] : 0.0;
  const double P99_OVERHEAD = (N > 0) ? overheadUs[static_cast<std::size_t>(N * 0.99)] : 0.0;

  std::printf("\n[Chain4] Minimal work (1us/task):\n");
  std::printf("  Total median: %.3f us\n", result.stats.median);
  std::printf("  Overhead median: %.3f us (total - 4us work)\n", MEDIAN_OVERHEAD);
  std::printf("  Overhead p99: %.3f us\n", P99_OVERHEAD);
  std::printf("  Breakdown:\n");
  std::printf("    - 4 task enqueues + worker wakes\n");
  std::printf("    - 2 waitForPhase calls (step, post)\n");
  std::printf("    - 4 advancePhase + notifyAll calls\n");
  std::printf("  Target: <100 us overhead for <1ms on 10ms frame\n");
}

/**
 * @brief Measure realistic 4-task chain with ModelTest2-like timings.
 *
 * Chain: pre1|pre2 (4ms each, parallel) -> step (3ms) -> post (2ms)
 * Critical path: 4ms + 3ms + 2ms = 9ms
 * Budget for 10ms frame: 1ms overhead
 */
PERF_TEST(SeqOverhead, Chain4_ModelTest2Timing) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 50); // Slow due to 9ms+ per iteration
  cfg.repeats = std::min(cfg.repeats, 3);
  auto perf = ub::makePerfCaseWithProfiler("SeqOverhead.Chain4_ModelTest2Timing", cfg);

  auto sched = makeScheduler(100);
  SequenceGroup seq(4);

  // Timing capture
  std::atomic<std::uint64_t> pre1Start{0}, pre1End{0};
  std::atomic<std::uint64_t> pre2Start{0}, pre2End{0};
  std::atomic<std::uint64_t> stepStart{0}, stepEnd{0};
  std::atomic<std::uint64_t> postStart{0}, postEnd{0};

  // ModelTest2 timings: pre1=4ms, pre2=4ms, step=3ms, post=2ms
  TaskCtx ctxPre1{&pre1Start, &pre1End, 4000};
  TaskCtx ctxPre2{&pre2Start, &pre2End, 4000};
  TaskCtx ctxStep{&stepStart, &stepEnd, 3000};
  TaskCtx ctxPost{&postStart, &postEnd, 2000};

  DelegateU8 delPre1{&timedTaskThunk, &ctxPre1};
  DelegateU8 delPre2{&timedTaskThunk, &ctxPre2};
  DelegateU8 delStep{&timedTaskThunk, &ctxStep};
  DelegateU8 delPost{&timedTaskThunk, &ctxPost};

  SchedulableTask taskPre1(delPre1, "pre1");
  SchedulableTask taskPre2(delPre2, "pre2");
  SchedulableTask taskStep(delStep, "step");
  SchedulableTask taskPost(delPost, "post");

  seq.addTask(taskPre1, 1);
  seq.addTask(taskPre2, 1);
  seq.addTask(taskStep, 3);
  seq.addTask(taskPost, 4);

  TaskConfig cfgHigh(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_HIGH);
  TaskConfig cfgNorm(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_NORMAL);
  TaskConfig cfgLow(sched->fundamentalFreq(), 1, 0, system_core::scheduler::PRIORITY_LOW);

  ASSERT_EQ(sched->addTask(taskPre1, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskPre2, cfgHigh, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskStep, cfgNorm, &seq), Status::SUCCESS);
  ASSERT_EQ(sched->addTask(taskPost, cfgLow, &seq), Status::SUCCESS);

  const std::uint16_t TICK = 0;

  auto waitForCompletion = [&] {
    for (int wait = 0; wait < 200000; ++wait) {
      if (seq.counter()->load(std::memory_order_acquire) == 1 && postEnd.load() > 0) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  };

  perf.warmup([&] {
    for (int i = 0; i < 3; ++i) {
      pre1Start = pre1End = pre2Start = pre2End = 0;
      stepStart = stepEnd = postStart = postEnd = 0;
      seq.reset();

      (void)sched->executeTasksOnTickMulti(TICK);
      waitForCompletion();
    }
  });

  std::vector<double> totalMs;
  std::vector<double> overheadMs;
  totalMs.reserve(static_cast<std::size_t>(cfg.cycles * cfg.repeats));
  overheadMs.reserve(static_cast<std::size_t>(cfg.cycles * cfg.repeats));

  auto result = perf.throughputLoop(
      [&] {
        pre1Start = pre1End = pre2Start = pre2End = 0;
        stepStart = stepEnd = postStart = postEnd = 0;
        seq.reset();

        const auto DISPATCH_START = apex::helpers::cpu::getMonotonicNs();
        (void)sched->executeTasksOnTickMulti(TICK);
        waitForCompletion();
        const auto CHAIN_END = apex::helpers::cpu::getMonotonicNs();

        const double TOTAL = static_cast<double>(CHAIN_END - DISPATCH_START) / 1e6;
        totalMs.push_back(TOTAL);

        // Critical path work: max(pre1,pre2)=4ms + step=3ms + post=2ms = 9ms
        const double WORK_MS = 9.0;
        overheadMs.push_back(TOTAL - WORK_MS);
      },
      "chain4-modeltest2");

  // Calculate stats
  std::sort(totalMs.begin(), totalMs.end());
  std::sort(overheadMs.begin(), overheadMs.end());
  const std::size_t N = totalMs.size();

  const double TOTAL_MEDIAN = (N > 0) ? totalMs[N / 2] : 0.0;
  const double TOTAL_P99 = (N > 0) ? totalMs[static_cast<std::size_t>(N * 0.99)] : 0.0;
  const double OVERHEAD_MEDIAN = (N > 0) ? overheadMs[N / 2] : 0.0;
  const double OVERHEAD_P99 = (N > 0) ? overheadMs[static_cast<std::size_t>(N * 0.99)] : 0.0;

  std::printf("\n[Chain4] ModelTest2 timing (pre=4ms, step=3ms, post=2ms):\n");
  std::printf("  Total median: %.3f ms (target: <10 ms)\n", TOTAL_MEDIAN);
  std::printf("  Total p99: %.3f ms\n", TOTAL_P99);
  std::printf("  Work time: 9.0 ms (critical path)\n");
  std::printf("  Overhead median: %.3f ms (target: <1 ms)\n", OVERHEAD_MEDIAN);
  std::printf("  Overhead p99: %.3f ms\n", OVERHEAD_P99);

  if (OVERHEAD_MEDIAN < 1.0) {
    std::printf("  Status: PASS - Overhead within 1ms budget\n");
  } else {
    std::printf("  Status: FAIL - Overhead exceeds 1ms budget\n");
    std::printf("  Breakdown needed:\n");
    std::printf("    - Check dispatch latency (enqueue + wake)\n");
    std::printf("    - Check waitForPhase spin duration\n");
    std::printf("    - Check advancePhase/notifyAll cost\n");
  }

  // Soft assertion - document but don't fail CI
  if (OVERHEAD_MEDIAN >= 1.0) {
    std::printf("  WARNING: Overhead %.3f ms exceeds 1ms target\n", OVERHEAD_MEDIAN);
  }
}

/**
 * @brief Compare sequenced vs non-sequenced dispatch overhead.
 *
 * Same 4 tasks, measures raw dispatch cost difference.
 * Uses atomic completion counter for reliable synchronization.
 */
PERF_TEST(SeqOverhead, SequencedVsNonSequenced) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 200);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SeqOverhead.SequencedVsNonSequenced", cfg);

  // Shared completion counter for non-sequenced tasks
  std::atomic<int> completionCount{0};

  // Task thunk that increments completion counter after work
  struct CompletionCtx {
    std::uint32_t workUs{0};
    std::atomic<int>* done{nullptr};
  };

  auto completionThunk = [](void* raw) noexcept -> std::uint8_t {
    auto* c = static_cast<CompletionCtx*>(raw);
    if (c->workUs > 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(c->workUs));
    }
    c->done->fetch_add(1, std::memory_order_release);
    return 0;
  };

  // Test 1: Non-sequenced (4 tasks in parallel)
  auto schedNonSeq = makeScheduler(100);
  schedNonSeq->setSkipOnBusy(true); // Prevent double-dispatch race
  std::vector<CompletionCtx> ctxNonSeq(4);
  std::vector<std::string> labelsNonSeq; // Keep labels alive
  std::vector<std::unique_ptr<SchedulableTask>> tasksNonSeq;
  labelsNonSeq.reserve(4);

  for (int i = 0; i < 4; ++i) {
    ctxNonSeq[static_cast<std::size_t>(i)].workUs = 100; // 100us work each
    ctxNonSeq[static_cast<std::size_t>(i)].done = &completionCount;
    DelegateU8 del{completionThunk, &ctxNonSeq[static_cast<std::size_t>(i)]};
    labelsNonSeq.push_back("T" + std::to_string(i));
    auto task = std::make_unique<SchedulableTask>(del, std::string_view(labelsNonSeq.back()));
    ASSERT_EQ(schedNonSeq->addTask(*task, schedNonSeq->fundamentalFreq(), 1, 0), Status::SUCCESS);
    tasksNonSeq.push_back(std::move(task));
  }

  auto waitForNonSeqCompletion = [&](int expected) {
    for (int wait = 0; wait < 100000; ++wait) {
      if (completionCount.load(std::memory_order_acquire) >= expected) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  };

  double nonSeqMedian = 0.0;
  int nonSeqExpected = 0;
  {
    // Warmup
    completionCount.store(0, std::memory_order_release);
    (void)schedNonSeq->executeTasksOnTickMulti(0);
    waitForNonSeqCompletion(4);
    nonSeqExpected = 4;

    auto resultNonSeq = perf.throughputLoop(
        [&] {
          nonSeqExpected += 4;
          (void)schedNonSeq->executeTasksOnTickMulti(0);
          waitForNonSeqCompletion(nonSeqExpected);
        },
        "nonseq-4task");
    nonSeqMedian = resultNonSeq.stats.median;
  }

  // Test 2: Sequenced (4 tasks in strict chain)
  auto schedSeq = makeScheduler(100);
  schedSeq->setSkipOnBusy(true);
  SequenceGroup seq(4);
  std::vector<CompletionCtx> ctxSeq(4);
  std::vector<std::string> labelsSeq; // Keep labels alive
  std::vector<std::unique_ptr<SchedulableTask>> tasksSeq;
  labelsSeq.reserve(4);

  std::atomic<int> seqCompletionCount{0};
  for (int i = 0; i < 4; ++i) {
    ctxSeq[static_cast<std::size_t>(i)].workUs = 100;
    ctxSeq[static_cast<std::size_t>(i)].done = &seqCompletionCount;
    DelegateU8 del{completionThunk, &ctxSeq[static_cast<std::size_t>(i)]};
    labelsSeq.push_back("S" + std::to_string(i));
    auto task = std::make_unique<SchedulableTask>(del, std::string_view(labelsSeq.back()));
    seq.addTask(*task, i + 1); // Sequential phases: 1, 2, 3, 4
    TaskConfig taskCfg(schedSeq->fundamentalFreq(), 1, 0);
    ASSERT_EQ(schedSeq->addTask(*task, taskCfg, &seq), Status::SUCCESS);
    tasksSeq.push_back(std::move(task));
  }

  auto waitForSeqCompletion = [&](int expected) {
    for (int wait = 0; wait < 100000; ++wait) {
      if (seqCompletionCount.load(std::memory_order_acquire) >= expected) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  };

  double seqMedian = 0.0;
  int seqExpected = 0;
  {
    // Warmup
    seqCompletionCount.store(0, std::memory_order_release);
    seq.reset();
    (void)schedSeq->executeTasksOnTickMulti(0);
    waitForSeqCompletion(4);
    seqExpected = 4;

    auto resultSeq = perf.throughputLoop(
        [&] {
          seqExpected += 4;
          seq.reset();
          (void)schedSeq->executeTasksOnTickMulti(0);
          waitForSeqCompletion(seqExpected);
        },
        "seq-4task");
    seqMedian = resultSeq.stats.median;
  }

  const double OVERHEAD_DIFF = seqMedian - nonSeqMedian;
  const double PER_TASK_OVERHEAD = OVERHEAD_DIFF / 4.0;

  std::printf("\n[Comparison] Sequenced vs Non-sequenced (4 tasks x 100us work):\n");
  std::printf("  Non-sequenced: %.3f us (parallel execution)\n", nonSeqMedian);
  std::printf("  Sequenced: %.3f us (serial execution)\n", seqMedian);
  std::printf("  Sequencing overhead: %.3f us total\n", OVERHEAD_DIFF);
  std::printf("  Per-task overhead: %.3f us\n", PER_TASK_OVERHEAD);
  std::printf("  Expected serial time: ~400 us (4 x 100us)\n");
  std::printf("  Note: Includes 3 phase transitions (waitForPhase + advancePhase)\n");
}

/**
 * @brief Measure ThreadPool enqueue + worker wake latency.
 *
 * Isolates dispatch overhead from sequencing overhead.
 */
PERF_TEST(SeqOverhead, ThreadPoolDispatchLatency) {
  ub::PerfConfig cfg = ub::detail::getPerfConfig();
  cfg.cycles = std::min(cfg.cycles, 500);
  cfg.repeats = std::min(cfg.repeats, 5);
  auto perf = ub::makePerfCaseWithProfiler("SeqOverhead.ThreadPoolDispatchLatency", cfg);

  auto sched = makeScheduler(100);

  // Single no-op task
  std::atomic<std::uint64_t> taskStart{0};
  TaskCtx ctx{&taskStart, nullptr, 0};
  DelegateU8 del{&timedTaskThunk, &ctx};
  SchedulableTask task(del, "noop");
  ASSERT_EQ(sched->addTask(task, sched->fundamentalFreq(), 1, 0), Status::SUCCESS);

  const std::uint16_t TICK = 0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      taskStart.store(0, std::memory_order_release);
      (void)sched->executeTasksOnTickMulti(TICK);
      // Wait for task to start
      for (int w = 0; w < 10000 && taskStart.load() == 0; ++w) {
        std::this_thread::yield();
      }
    }
  });

  std::vector<double> dispatchLatencyUs;
  dispatchLatencyUs.reserve(static_cast<std::size_t>(cfg.cycles * cfg.repeats));

  auto result = perf.throughputLoop(
      [&] {
        taskStart.store(0, std::memory_order_release);
        const auto ENQUEUE_TIME = apex::helpers::cpu::getMonotonicNs();
        (void)sched->executeTasksOnTickMulti(TICK);
        // Wait for task to start executing
        for (int w = 0; w < 100000 && taskStart.load() == 0; ++w) {
          std::this_thread::yield();
        }
        const auto START_TIME = taskStart.load();
        if (START_TIME > ENQUEUE_TIME) {
          dispatchLatencyUs.push_back(nsToUs(START_TIME - ENQUEUE_TIME));
        }
      },
      "dispatch-latency");

  std::sort(dispatchLatencyUs.begin(), dispatchLatencyUs.end());
  const std::size_t N = dispatchLatencyUs.size();
  const double MEDIAN = (N > 0) ? dispatchLatencyUs[N / 2] : 0.0;
  const double P99 = (N > 0) ? dispatchLatencyUs[static_cast<std::size_t>(N * 0.99)] : 0.0;

  std::printf("\n[Dispatch] Enqueue-to-execution latency:\n");
  std::printf("  Median: %.3f us\n", MEDIAN);
  std::printf("  p99: %.3f us\n", P99);
  std::printf("  Includes: mutex acquire, queue push, CV notify, worker wake\n");
  std::printf("  Target: <50 us for low-latency RT systems\n");
}

PERF_MAIN()
