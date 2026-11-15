/**
 * @file LockFreeQueue_uTest.cpp
 * @brief Unit tests for concurrency::LockFreeQueue.
 *
 * Notes:
 *  - Tests verify MPMC correctness, capacity limits, and edge cases.
 *  - Concurrent tests use multiple threads to stress the lock-free algorithm.
 */

#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using apex::concurrency::LockFreeQueue;
using apex::concurrency::test::settles;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class LockFreeQueueTest : public ::testing::Test {
protected:
  static constexpr std::size_t DEFAULT_CAPACITY = 16;
};

/** @test Queue capacity matches constructor argument */
TEST_F(LockFreeQueueTest, CapacityMatchesArgument) {
  LockFreeQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_EQ(queue.capacity(), DEFAULT_CAPACITY);
}

/** @test Queue with zero capacity rounds up to 2 (minimum power-of-two) */
TEST_F(LockFreeQueueTest, ZeroCapacityRoundsUpToTwo) {
  // Minimum capacity is 2 (power-of-two requirement for fast modulo).
  LockFreeQueue<int> queue(0);
  EXPECT_EQ(queue.capacity(), 2U);
}

/** @test Non-power-of-two capacity rounds up */
TEST_F(LockFreeQueueTest, CapacityRoundsUpToPowerOfTwo) {
  // Capacity 5 rounds up to 8
  LockFreeQueue<int> q5(5);
  EXPECT_EQ(q5.capacity(), 8U);

  // Capacity 17 rounds up to 32
  LockFreeQueue<int> q17(17);
  EXPECT_EQ(q17.capacity(), 32U);

  // Power-of-two stays the same
  LockFreeQueue<int> q16(16);
  EXPECT_EQ(q16.capacity(), 16U);
}

/** @test tryPush succeeds on empty queue */
TEST_F(LockFreeQueueTest, TryPushSucceedsOnEmpty) {
  LockFreeQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_TRUE(queue.tryPush(42));
}

/** @test tryPop succeeds after push */
TEST_F(LockFreeQueueTest, TryPopSucceedsAfterPush) {
  LockFreeQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_TRUE(queue.tryPush(42));

  int value = 0;
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 42);
}

/** @test tryPop fails on empty queue */
TEST_F(LockFreeQueueTest, TryPopFailsOnEmpty) {
  LockFreeQueue<int> queue(DEFAULT_CAPACITY);
  int value = 0;
  EXPECT_FALSE(queue.tryPop(value));
}

/** @test tryPush fails when queue is full */
TEST_F(LockFreeQueueTest, TryPushFailsWhenFull) {
  LockFreeQueue<int> queue(4);

  // Fill the queue
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(queue.tryPush(i));
  }

  // Next push should fail
  EXPECT_FALSE(queue.tryPush(99));
}

/** @test Queue maintains FIFO order */
TEST_F(LockFreeQueueTest, MaintainsFifoOrder) {
  LockFreeQueue<int> queue(DEFAULT_CAPACITY);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.tryPush(i));
  }

  for (int i = 0; i < 10; ++i) {
    int value = -1;
    EXPECT_TRUE(queue.tryPop(value));
    EXPECT_EQ(value, i);
  }
}

/* ----------------------------- Move Semantics Tests ----------------------------- */

/** @test tryPush with rvalue moves the value */
TEST_F(LockFreeQueueTest, TryPushMovesRvalue) {
  LockFreeQueue<std::string> queue(4);

  std::string original = "test string";
  EXPECT_TRUE(queue.tryPush(std::move(original)));

  std::string popped;
  EXPECT_TRUE(queue.tryPop(popped));
  EXPECT_EQ(popped, "test string");
}

/** @test tryPop moves value out of queue */
TEST_F(LockFreeQueueTest, TryPopMovesValue) {
  LockFreeQueue<std::vector<int>> queue(4);

  std::vector<int> original = {1, 2, 3, 4, 5};
  EXPECT_TRUE(queue.tryPush(original));

  std::vector<int> popped;
  EXPECT_TRUE(queue.tryPop(popped));
  EXPECT_EQ(popped.size(), 5U);
  EXPECT_EQ(popped[0], 1);
}

/* ----------------------------- Capacity Edge Cases ----------------------------- */

/** @test Queue with capacity 2 works correctly (minimum recommended) */
TEST_F(LockFreeQueueTest, CapacityTwoWorks) {
  // Note: Vyukov algorithm requires capacity >= 2 for reliable full detection.
  // Capacity 1 is a degenerate case where sequence numbering collides.
  LockFreeQueue<int> queue(2);

  EXPECT_TRUE(queue.tryPush(42));
  EXPECT_TRUE(queue.tryPush(43));
  EXPECT_FALSE(queue.tryPush(44)); // Full

  int value = 0;
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 42);
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 43);
  EXPECT_FALSE(queue.tryPop(value)); // Empty

  EXPECT_TRUE(queue.tryPush(45)); // Can push again
}

/** @test Queue can be refilled after draining */
TEST_F(LockFreeQueueTest, CanRefillAfterDrain) {
  LockFreeQueue<int> queue(4);

  // Fill and drain multiple times
  for (int round = 0; round < 3; ++round) {
    for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(queue.tryPush(round * 10 + i));
    }
    for (int i = 0; i < 4; ++i) {
      int value = -1;
      EXPECT_TRUE(queue.tryPop(value));
      EXPECT_EQ(value, round * 10 + i);
    }
  }
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Single producer single consumer correctness */
TEST_F(LockFreeQueueTest, SingleProducerSingleConsumer) {
  LockFreeQueue<int> queue(1024);
  constexpr int ITEM_COUNT = 10000;
  std::atomic<bool> done{false};
  std::vector<int> received;
  received.reserve(ITEM_COUNT);

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    while (true) {
      if (queue.tryPop(value)) {
        received.push_back(value);
      } else if (done.load(std::memory_order_acquire)) {
        // Producer done and queue empty - exit
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Producer (main thread)
  for (int i = 0; i < ITEM_COUNT; ++i) {
    while (!queue.tryPush(i)) {
      std::this_thread::yield();
    }
  }

  done.store(true, std::memory_order_release);
  consumer.join();

  // Drain any remaining items
  int value;
  while (queue.tryPop(value)) {
    received.push_back(value);
  }

  // Verify all items received in order
  ASSERT_EQ(received.size(), static_cast<std::size_t>(ITEM_COUNT));
  for (int i = 0; i < ITEM_COUNT; ++i) {
    EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
  }
}

namespace {

/// What a run of producers and consumers through one small ring left behind.
struct Exchange {
  std::vector<std::vector<int>> popped; ///< Per consumer, in pop order.
  std::uint64_t refusedPushes{0};       ///< tryPush() calls that found the ring full.
  bool finished{false};                 ///< Every thread ran to its end within the bound.
};

/// PRODUCERS threads each push ITEMS values, producer p pushing p * ITEMS + i for rising i and
/// retrying while the ring is full, half of them through each tryPush() overload; CONSUMERS
/// threads record what they pop. The ring holds
/// eight, so it fills and laps thousands of times. One start gate releases the producers; the
/// consumers start once a push has been refused, so the full path is always taken.
///
/// Everything the threads touch is shared heap state. A run that does not end within the bound
/// is told to stop; threads that still do not return, because a broken queue has them looping
/// inside a push or a pop, are detached with the state they hold, and the run is reported as
/// unfinished.
Exchange exchangeThroughSmallRing(int producers, int consumers, int items) {
  struct State {
    explicit State(int consumerCount) : popped(static_cast<std::size_t>(consumerCount)) {}
    LockFreeQueue<int> queue{8};
    std::vector<std::vector<int>> popped;
    std::atomic<bool> start{false};
    std::atomic<bool> ringWasFull{false};
    std::atomic<bool> stop{false};
    std::atomic<int> producersDone{0};
    std::atomic<int> threadsDone{0};
    std::atomic<std::uint64_t> refused{0};
  };
  const auto state = std::make_shared<State>(consumers);

  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(producers + consumers));
  for (int p = 0; p < producers; ++p) {
    threads.emplace_back([state, p, items]() {
      while (!state->start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::uint64_t refusedHere = 0;
      // Even producers push by copy and odd ones by move, so both tryPush() overloads meet
      // contention.
      const auto PUSH = [&](int value) {
        return (p % 2 == 0) ? state->queue.tryPush(value) : state->queue.tryPush(int{value});
      };
      for (int i = 0; i < items && !state->stop.load(std::memory_order_relaxed); ++i) {
        while (!PUSH(p * items + i)) {
          ++refusedHere;
          state->ringWasFull.store(true, std::memory_order_release);
          if (state->stop.load(std::memory_order_relaxed)) {
            break;
          }
          std::this_thread::yield();
        }
      }
      state->refused.fetch_add(refusedHere, std::memory_order_relaxed);
      state->producersDone.fetch_add(1, std::memory_order_release);
      state->threadsDone.fetch_add(1, std::memory_order_release);
    });
  }
  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([state, c, producers, items]() {
      auto& mine = state->popped[static_cast<std::size_t>(c)];
      const std::size_t TOTAL =
          static_cast<std::size_t>(producers) * static_cast<std::size_t>(items);
      mine.reserve(TOTAL);
      while (!state->ringWasFull.load(std::memory_order_acquire) &&
             !state->stop.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
      }
      int value = 0;
      // A consumer never needs more than TOTAL pops; past that the queue is handing out
      // values it was never given, and the count is the verdict.
      while (!state->stop.load(std::memory_order_relaxed) && mine.size() <= TOTAL) {
        if (state->queue.tryPop(value)) {
          mine.push_back(value);
        } else if (state->producersDone.load(std::memory_order_acquire) == producers) {
          // Every push is published; one more look settles whether the ring is empty.
          while (mine.size() <= TOTAL && state->queue.tryPop(value)) {
            mine.push_back(value);
          }
          break;
        } else {
          std::this_thread::yield();
        }
      }
      state->threadsDone.fetch_add(1, std::memory_order_release);
    });
  }

  const auto ALL_DONE = [&]() { return state->threadsDone.load() == producers + consumers; };
  state->start.store(true, std::memory_order_release);
  Exchange result;
  result.finished = settles(ALL_DONE, std::chrono::seconds(20));
  state->stop.store(true, std::memory_order_release);
  if (!settles(ALL_DONE, std::chrono::seconds(2))) {
    for (auto& t : threads) {
      t.detach();
    }
    return result;
  }
  for (auto& t : threads) {
    t.join();
  }
  result.popped = std::move(state->popped);
  result.refusedPushes = state->refused.load();
  return result;
}

/// Every value in [0, producers * items) was popped exactly once, and within each consumer's
/// sequence every producer's values rise.
void expectEachValueOnceAndInProducerOrder(const Exchange& exchange, int producers, int items) {
  ASSERT_TRUE(exchange.finished) << "the producers and consumers did not finish";
  EXPECT_GT(exchange.refusedPushes, 0U) << "the ring was never full";
  const std::size_t TOTAL = static_cast<std::size_t>(producers) * static_cast<std::size_t>(items);
  std::vector<int> timesPopped(TOTAL, 0);
  std::size_t popped = 0;
  std::size_t outOfRange = 0;
  std::size_t outOfOrder = 0;
  for (const auto& sequence : exchange.popped) {
    std::vector<int> lastFromProducer(static_cast<std::size_t>(producers), -1);
    for (const int VALUE : sequence) {
      ++popped;
      if (VALUE < 0 || static_cast<std::size_t>(VALUE) >= TOTAL) {
        ++outOfRange;
        continue;
      }
      ++timesPopped[static_cast<std::size_t>(VALUE)];
      auto& last = lastFromProducer[static_cast<std::size_t>(VALUE / items)];
      if (VALUE <= last) {
        ++outOfOrder;
      }
      last = VALUE;
    }
  }
  std::size_t missing = 0;
  std::size_t duplicated = 0;
  for (const int TIMES : timesPopped) {
    missing += TIMES == 0 ? 1U : 0U;
    duplicated += TIMES > 1 ? 1U : 0U;
  }
  EXPECT_EQ(popped, TOTAL);
  EXPECT_EQ(outOfRange, 0U) << "values that no producer pushed";
  EXPECT_EQ(missing, 0U) << "values pushed and never popped";
  EXPECT_EQ(duplicated, 0U) << "values popped more than once";
  EXPECT_EQ(outOfOrder, 0U) << "a consumer saw one producer's values out of order";
}

} // namespace

/** @test Four producers and one consumer through an eight-cell ring: every value arrives exactly
 * once and each producer's values arrive in the order it pushed them. */
TEST_F(LockFreeQueueTest, MultipleProducersSingleConsumerKeepEveryValueInOrder) {
  constexpr int PRODUCERS = 4;
  constexpr int ITEMS = 10000;
  expectEachValueOnceAndInProducerOrder(exchangeThroughSmallRing(PRODUCERS, 1, ITEMS), PRODUCERS,
                                        ITEMS);
}

/** @test Four producers and four consumers through an eight-cell ring: every value is popped by
 * exactly one consumer, and each consumer sees each producer's values in rising order. */
TEST_F(LockFreeQueueTest, MultipleProducersMultipleConsumersPopEachValueOnce) {
  constexpr int PRODUCERS = 4;
  constexpr int ITEMS = 10000;
  expectEachValueOnceAndInProducerOrder(exchangeThroughSmallRing(PRODUCERS, 4, ITEMS), PRODUCERS,
                                        ITEMS);
}

/** @test sizeApprox() follows pushes and pops, through a lap of the ring. */
TEST_F(LockFreeQueueTest, SizeApproxFollowsPushAndPop) {
  LockFreeQueue<int> queue(4);
  EXPECT_EQ(queue.sizeApprox(), 0U);
  int value = 0;
  for (int lap = 0; lap < 3; ++lap) {
    for (std::size_t i = 0; i < queue.capacity(); ++i) {
      ASSERT_TRUE(queue.tryPush(lap));
      EXPECT_EQ(queue.sizeApprox(), i + 1);
    }
    for (std::size_t i = 0; i < queue.capacity(); ++i) {
      ASSERT_TRUE(queue.tryPop(value));
      EXPECT_EQ(queue.sizeApprox(), queue.capacity() - i - 1);
    }
  }
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test capacity() returns consistent results */
TEST(LockFreeQueueDeterminismTest, CapacityConsistent) {
  LockFreeQueue<int> queue(128);
  const std::size_t FIRST = queue.capacity();
  const std::size_t SECOND = queue.capacity();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 128U);
}

/* ----------------------------- Storage Alignment Tests ----------------------------- */

using apex::concurrency::test::AlignProbe;

/** @test The cell storage honours alignas(64): every element is constructed at a 64-byte boundary,
 * at small capacities and at one large enough to come from a page-granular allocation. */
TEST_F(LockFreeQueueTest, CellStorageHonoursAlignment) {
  AlignProbe::misaligned().store(0, std::memory_order_relaxed);
  for (const std::size_t CAPACITY :
       {std::size_t{2}, std::size_t{16}, std::size_t{1024}, std::size_t{32768}}) {
    LockFreeQueue<AlignProbe> queue(CAPACITY);
    EXPECT_EQ(queue.capacity(), CAPACITY);
  }
  EXPECT_EQ(AlignProbe::misaligned().load(std::memory_order_relaxed), 0);
}
