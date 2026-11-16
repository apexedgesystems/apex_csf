/**
 * @file SPSCQueue_uTest.cpp
 * @brief Unit tests for concurrency::SPSCQueue.
 *
 * Notes:
 *  - Tests verify SPSC correctness, capacity limits, and edge cases.
 *  - Single producer/consumer constraint is verified via dedicated threads.
 */

#include "src/utilities/concurrency/inc/SPSCQueue.hpp"
#include "src/utilities/concurrency/utst/support/MoveTracking.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"
#include "src/utilities/concurrency/utst/support/Crew.hpp"
#include "src/utilities/concurrency/utst/support/SizeReader.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <limits>
#include <thread>
#include <vector>

using apex::concurrency::SPSCQueue;
using apex::concurrency::test::Crew;
using apex::concurrency::test::CrewState;
using apex::concurrency::test::readSizeFromAThirdThread;
using apex::concurrency::test::SizeReadings;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class SPSCQueueTest : public ::testing::Test {
protected:
  static constexpr std::size_t DEFAULT_CAPACITY = 16;
};

/** @test Queue with zero capacity rounds up to 2 (minimum power-of-two) */
TEST_F(SPSCQueueTest, ZeroCapacityRoundsUpToTwo) {
  SPSCQueue<int> queue(0);
  EXPECT_EQ(queue.capacity(), 2U);
}

/** @test Non-power-of-two capacity rounds up */
TEST_F(SPSCQueueTest, CapacityRoundsUpToPowerOfTwo) {
  SPSCQueue<int> q5(5);
  EXPECT_EQ(q5.capacity(), 8U);

  SPSCQueue<int> q17(17);
  EXPECT_EQ(q17.capacity(), 32U);

  SPSCQueue<int> q16(16);
  EXPECT_EQ(q16.capacity(), 16U);
}

/** @test tryPush succeeds on empty queue */
TEST_F(SPSCQueueTest, TryPushSucceedsOnEmpty) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_TRUE(queue.tryPush(42));
}

/** @test tryPop succeeds after push */
TEST_F(SPSCQueueTest, TryPopSucceedsAfterPush) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_TRUE(queue.tryPush(42));

  int value = 0;
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 42);
}

/** @test tryPop fails on empty queue */
TEST_F(SPSCQueueTest, TryPopFailsOnEmpty) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  int value = 0;
  EXPECT_FALSE(queue.tryPop(value));
}

/** @test empty() returns true for empty queue */
TEST_F(SPSCQueueTest, EmptyReturnsTrueWhenEmpty) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_TRUE(queue.empty());
}

/** @test empty() returns false after push */
TEST_F(SPSCQueueTest, EmptyReturnsFalseAfterPush) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  ASSERT_TRUE(queue.tryPush(42));
  EXPECT_FALSE(queue.empty());
}

/** @test sizeApprox returns correct count */
TEST_F(SPSCQueueTest, SizeApproxReturnsCount) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_EQ(queue.sizeApprox(), 0U);

  ASSERT_TRUE(queue.tryPush(1));
  EXPECT_EQ(queue.sizeApprox(), 1U);

  ASSERT_TRUE(queue.tryPush(2));
  ASSERT_TRUE(queue.tryPush(3));
  EXPECT_EQ(queue.sizeApprox(), 3U);

  int value = 0;
  ASSERT_TRUE(queue.tryPop(value));
  EXPECT_EQ(queue.sizeApprox(), 2U);
}

/** @test Queue maintains FIFO order */
TEST_F(SPSCQueueTest, MaintainsFifoOrder) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(queue.tryPush(i));
  }

  for (int i = 0; i < 10; ++i) {
    int value = -1;
    EXPECT_TRUE(queue.tryPop(value));
    EXPECT_EQ(value, i);
  }
}

/* ----------------------------- Capacity Tests ----------------------------- */

/** @test A queue takes exactly capacity() elements and refuses the next, for both tryPush
 * overloads, at the smallest capacity and a larger one; what it took comes back in order. */
TEST_F(SPSCQueueTest, TakesExactlyCapacityThenRefuses) {
  for (const std::size_t REQUESTED : {std::size_t{2}, std::size_t{8}}) {
    SCOPED_TRACE("capacity " + std::to_string(REQUESTED));
    SPSCQueue<int> byCopy(REQUESTED);
    SPSCQueue<int> byMove(REQUESTED);
    ASSERT_EQ(byCopy.capacity(), REQUESTED);
    const int CAPACITY = static_cast<int>(REQUESTED);

    for (int i = 0; i < CAPACITY; ++i) {
      const int VALUE = i;
      EXPECT_TRUE(byCopy.tryPush(VALUE)) << "copy push " << i;
      EXPECT_TRUE(byMove.tryPush(int{i})) << "move push " << i;
    }
    const int EXTRA = CAPACITY;
    EXPECT_FALSE(byCopy.tryPush(EXTRA)) << "a full queue took one more by copy";
    EXPECT_FALSE(byMove.tryPush(int{CAPACITY})) << "a full queue took one more by move";
    EXPECT_EQ(byCopy.sizeApprox(), REQUESTED);
    EXPECT_EQ(byMove.sizeApprox(), REQUESTED);

    for (int i = 0; i < CAPACITY; ++i) {
      int fromCopy = -1;
      int fromMove = -1;
      EXPECT_TRUE(byCopy.tryPop(fromCopy));
      EXPECT_TRUE(byMove.tryPop(fromMove));
      EXPECT_EQ(fromCopy, i);
      EXPECT_EQ(fromMove, i);
    }
    EXPECT_TRUE(byCopy.empty());
    EXPECT_TRUE(byMove.empty());
  }
}

/** @test Queue can be refilled after draining */
TEST_F(SPSCQueueTest, CanRefillAfterDrain) {
  SPSCQueue<int> queue(8);

  // Seven of eight slots a round, so the indices wrap at a different slot each time.
  for (int round = 0; round < 5; ++round) {
    for (int i = 0; i < 7; ++i) {
      EXPECT_TRUE(queue.tryPush(round * 10 + i));
    }
    EXPECT_EQ(queue.sizeApprox(), 7U);
    for (int i = 0; i < 7; ++i) {
      int value = -1;
      EXPECT_TRUE(queue.tryPop(value));
      EXPECT_EQ(value, round * 10 + i);
    }
    EXPECT_TRUE(queue.empty());
  }
}

/* ----------------------------- Move Semantics Tests ----------------------------- */

/** @test An rvalue pushed is moved in without a copy and its source is left moved-from, an
 * lvalue pushed is copied exactly once, a pop moves the element out without a copy, and a
 * move-only element type goes through. */
TEST_F(SPSCQueueTest, MovesRvaluesAndCopiesLvaluesOnce) {
  apex::concurrency::test::checkQueueValueCategories<SPSCQueue>();
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test One producer and one consumer pass 200,000 values through a sixteen-slot queue: every
 * value arrives, once, in order. The small ring keeps both threads at the full and empty edges.
 * Both run on threads the case waits for with a bound, and stop when it gives up on them. */
TEST_F(SPSCQueueTest, SingleProducerSingleConsumer) {
  constexpr int ITEM_COUNT = 200000;
  struct Exchange : CrewState {
    SPSCQueue<int> queue{16};
    std::vector<int> received;
  };
  Crew<Exchange> crew;
  crew->received.reserve(ITEM_COUNT);

  crew.start([](Exchange& state) {
    int value = 0;
    while (static_cast<int>(state.received.size()) < ITEM_COUNT &&
           !state.abandoned.load(std::memory_order_acquire)) {
      if (state.queue.tryPop(value)) {
        state.received.push_back(value);
      } else {
        std::this_thread::yield();
      }
    }
  });
  crew.start([](Exchange& state) {
    for (int i = 0; i < ITEM_COUNT && !state.abandoned.load(std::memory_order_acquire);) {
      if (state.queue.tryPush(i)) {
        ++i;
      } else {
        std::this_thread::yield();
      }
    }
  });
  ASSERT_TRUE(crew.finish(std::chrono::seconds(20))) << "the exchange did not finish";

  ASSERT_EQ(crew->received.size(), static_cast<std::size_t>(ITEM_COUNT));
  for (int i = 0; i < ITEM_COUNT; ++i) {
    ASSERT_EQ(crew->received[static_cast<std::size_t>(i)], i);
  }
}

/* ----------------------------- Storage Alignment Tests ----------------------------- */

/** @test The storage honours the element's alignment: every element of a 64-byte-aligned type is
 * constructed at a 64-byte boundary, at small capacities and at one large enough to come from a
 * page-granular allocation. */
TEST_F(SPSCQueueTest, StorageHonoursElementAlignment) {
  using apex::concurrency::test::AlignProbe;
  AlignProbe::misaligned().store(0, std::memory_order_relaxed);
  for (const std::size_t CAPACITY : {std::size_t{2}, std::size_t{16}, std::size_t{1024}}) {
    for (int repeat = 0; repeat < 8; ++repeat) {
      const SPSCQueue<AlignProbe> queue(CAPACITY);
    }
  }
  EXPECT_EQ(AlignProbe::misaligned().load(std::memory_order_relaxed), 0)
      << "elements were constructed off their 64-byte alignment";
}

/* ----------------------------- Index Wrap Tests ----------------------------- */

namespace {

/// A queue of int whose indices start where the test puts them.
class SteppedQueue : public SPSCQueue<int> {
public:
  SteppedQueue(std::size_t capacity, std::size_t firstIndex) noexcept
      : SPSCQueue<int>(capacity, firstIndex) {}
};

constexpr std::size_t INDEX_MAX = std::numeric_limits<std::size_t>::max();

} // namespace

/** @test A queue whose indices wrap keeps its order, its capacity and its size: started one step
 * below the index width, it is empty to a pop before anything is pushed, fills across the wrap,
 * refuses the next push, and reports each occupancy on the way down, with the head index
 * numerically below the tail throughout. */
TEST_F(SPSCQueueTest, IndicesWrapWithoutLosingOrderCapacityOrSize) {
  SteppedQueue queue(2, INDEX_MAX);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.sizeApprox(), 0U);
  int value = 0;
  EXPECT_FALSE(queue.tryPop(value)) << "a queue nothing was pushed to gave an item";

  ASSERT_TRUE(queue.tryPush(1)); // The head index wraps to zero; the tail is still at the maximum.
  EXPECT_EQ(queue.sizeApprox(), 1U);
  ASSERT_TRUE(queue.tryPush(2));
  EXPECT_EQ(queue.sizeApprox(), 2U);
  EXPECT_FALSE(queue.tryPush(3)) << "a full queue took a push across the index wrap";

  ASSERT_TRUE(queue.tryPop(value)); // The tail index wraps to zero.
  EXPECT_EQ(value, 1);
  EXPECT_EQ(queue.sizeApprox(), 1U);
  ASSERT_TRUE(queue.tryPush(3));
  EXPECT_EQ(queue.sizeApprox(), 2U);
  ASSERT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 2);
  ASSERT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 3);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.sizeApprox(), 0U);
  EXPECT_FALSE(queue.tryPop(value));
}

/* ----------------------------- sizeApprox Under Concurrency ----------------------------- */

/** @test sizeApprox() read from a third thread never exceeds the capacity while a producer and a
 * consumer run: its two index reads are not atomic together, and a read that catches the consumer
 * ahead of the producer's older index reports zero, not a wrapped difference. This case meets
 * the split by chance; the arithmetic for it is pinned in RingIndex_uTest.cpp. */
TEST_F(SPSCQueueTest, SizeApproxFromAThirdThreadStaysWithinCapacity) {
  const SizeReadings SEEN = readSizeFromAThirdThread<SPSCQueue>(2, 200000);
  ASSERT_TRUE(SEEN.sampled) << "the reader thread never sampled";
  ASSERT_TRUE(SEEN.finished) << "the exchange did not finish";
  EXPECT_LE(SEEN.largest, SEEN.capacity);
}
