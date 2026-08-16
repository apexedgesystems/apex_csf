/**
 * @file SequenceGroup_uTest.cpp
 * @brief Unit tests for SequenceGroup phase sequencing.
 *
 * Coverage:
 *  - Registry lookup (addTask/getSeqInfo)
 *  - Counter lifecycle (initial value, reset, wrap at maxPhase)
 *  - Cross-thread phase ordering (waitForPhase/advancePhase)
 *  - The frame contract at its boundary: a tail-phase waiter that loses
 *    the counter to a wrap completes in the following cycle -- delayed,
 *    never corrupt
 *
 * The cross-thread tests are the TSan witnesses for the phase hand-off;
 * run them under ThreadSanitizer pinned to one CPU with --gtest_repeat
 * to force thin interleavings.
 */

#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using apex::concurrency::DelegateU8;
using system_core::schedulable::advancePhase;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;
using system_core::schedulable::waitForPhase;

namespace {

std::uint8_t noopTask(void*) noexcept { return 0; }

SchedulableTask makeTask(std::string_view label) {
  return SchedulableTask(DelegateU8{&noopTask, nullptr}, label);
}

} // namespace

/* ----------------------------- Registry Tests ----------------------------- */

/** @test Registered tasks resolve to their phase and maxPhase. */
TEST(SequenceGroupTest, RegistryLookupReturnsSeqInfo) {
  SequenceGroup seq(3);
  SchedulableTask a = makeTask("a");
  SchedulableTask b = makeTask("b");

  seq.addTask(a, 1);
  seq.addTask(b, 3);

  const auto* infoA = seq.getSeqInfo(&a);
  ASSERT_NE(infoA, nullptr);
  EXPECT_EQ(infoA->phase, 1);
  EXPECT_EQ(infoA->maxPhase, 3);

  const auto* infoB = seq.getSeqInfo(&b);
  ASSERT_NE(infoB, nullptr);
  EXPECT_EQ(infoB->phase, 3);
}

/** @test Unregistered tasks resolve to nullptr. */
TEST(SequenceGroupTest, UnregisteredTaskHasNoSeqInfo) {
  SequenceGroup seq(2);
  SchedulableTask stranger = makeTask("stranger");

  EXPECT_EQ(seq.getSeqInfo(&stranger), nullptr);
}

/* ----------------------------- Counter Tests ----------------------------- */

/** @test Counter starts at phase 1 and reset restores it. */
TEST(SequenceGroupTest, CounterStartsAtOneAndResets) {
  SequenceGroup seq(3);

  EXPECT_EQ(seq.counter()->load(), 1);

  advancePhase(*seq.counter(), seq.maxPhase());
  EXPECT_EQ(seq.counter()->load(), 2);

  seq.reset();
  EXPECT_EQ(seq.counter()->load(), 1);
}

/** @test The final advance of a cycle wraps the counter to phase 1. */
TEST(SequenceGroupTest, AdvanceWrapsAtMaxPhase) {
  SequenceGroup seq(3);

  advancePhase(*seq.counter(), seq.maxPhase()); // 1 -> 2
  advancePhase(*seq.counter(), seq.maxPhase()); // 2 -> 3
  EXPECT_EQ(seq.counter()->load(), 3);

  advancePhase(*seq.counter(), seq.maxPhase()); // 3 -> wrap
  EXPECT_EQ(seq.counter()->load(), 1);
}

/* ----------------------------- Cross-Thread Ordering ----------------------------- */

/**
 * @test Three phases on three threads execute in phase order.
 *
 * Each thread waits for its phase, stamps its slot in the execution
 * log, and advances. The log must come out in phase order regardless
 * of thread start order.
 */
TEST(SequenceGroupTest, ChainExecutesInPhaseOrder) {
  SequenceGroup seq(3);

  std::atomic<int> logIdx{0};
  std::array<int, 3> order{-1, -1, -1};

  auto worker = [&seq, &logIdx, &order](int phase) {
    EXPECT_TRUE(waitForPhase(*seq.counter(), phase));
    order[logIdx.fetch_add(1, std::memory_order_acq_rel)] = phase;
    advancePhase(*seq.counter(), seq.maxPhase());
  };

  // Start in reverse phase order to make the sequencing do the work.
  std::thread t3(worker, 3);
  std::thread t2(worker, 2);
  std::thread t1(worker, 1);
  t1.join();
  t2.join();
  t3.join();

  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
  EXPECT_EQ(order[2], 3);
  EXPECT_EQ(seq.counter()->load(), 1); // Full cycle wrapped back.
}

/**
 * @test Frame contract at its boundary: a tail waiter that loses the
 *       counter to a wrap completes in the next cycle.
 *
 * Constructs the overrun deliberately: the full cycle advances (and
 * wraps) before the phase-3 waiter ever samples the counter. The
 * waiter must then complete when the NEXT cycle reaches phase 3 --
 * delayed by one cycle, never lost and never corrupt.
 */
TEST(SequenceGroupTest, TailWaiterDelayedByWrapCompletesNextCycle) {
  SequenceGroup seq(3);

  std::atomic<bool> tailReleased{false};
  std::atomic<bool> tailDone{false};

  std::thread tail([&]() {
    while (!tailReleased.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    EXPECT_TRUE(waitForPhase(*seq.counter(), 3));
    tailDone.store(true, std::memory_order_release);
  });

  // Full cycle completes before the tail waiter looks: 1->2->3->wrap.
  advancePhase(*seq.counter(), seq.maxPhase());
  advancePhase(*seq.counter(), seq.maxPhase());
  advancePhase(*seq.counter(), seq.maxPhase());
  ASSERT_EQ(seq.counter()->load(), 1);

  // Tail samples a wrapped counter: 1 < 3, so it must wait.
  tailReleased.store(true, std::memory_order_release);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(tailDone.load(std::memory_order_acquire));

  // Next cycle reaches phase 3: the delayed waiter completes.
  advancePhase(*seq.counter(), seq.maxPhase()); // 1 -> 2
  advancePhase(*seq.counter(), seq.maxPhase()); // 2 -> 3
  tail.join();
  EXPECT_TRUE(tailDone.load(std::memory_order_acquire));
}

/**
 * @test Sustained multi-cycle hand-off across threads.
 *
 * Three phase workers cycle the chain 500 times. Every worker is gated
 * on frame completion the way the scheduler gates tasks on dispatch --
 * once per frame. Without that gate a worker whose phase check stays
 * satisfied after its own advance (phase 1 always; phase 2 while the
 * counter sits at 3) could lap the chain and strand the tail. This is
 * the standing TSan witness for waitForPhase/advancePhase pairing
 * under repeated wraps; the counter must land exactly back at phase 1.
 */
TEST(SequenceGroupTest, SustainedCyclesHandOffCleanly) {
  SequenceGroup seq(3);
  constexpr int CYCLES = 500;

  std::atomic<int> executions{0};
  std::atomic<int> framesDone{0}; // Stands in for the per-frame dispatch gate.

  auto worker = [&seq, &executions, &framesDone](int phase) {
    for (int c = 0; c < CYCLES; ++c) {
      while (framesDone.load(std::memory_order_acquire) < c) {
        std::this_thread::yield();
      }
      EXPECT_TRUE(waitForPhase(*seq.counter(), phase));
      executions.fetch_add(1, std::memory_order_acq_rel);
      advancePhase(*seq.counter(), seq.maxPhase());
      if (phase == 3) {
        framesDone.fetch_add(1, std::memory_order_acq_rel);
      }
    }
  };

  std::thread t1(worker, 1);
  std::thread t2(worker, 2);
  std::thread t3(worker, 3);
  t1.join();
  t2.join();
  t3.join();

  EXPECT_EQ(executions.load(), 3 * CYCLES);
  EXPECT_EQ(seq.counter()->load(), 1);
}
