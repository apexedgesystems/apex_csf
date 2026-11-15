/**
 * @file SPSCQueue_uTest.cpp
 * @brief Unit tests for concurrency::SPSCQueue.
 *
 * Notes:
 *  - Tests verify SPSC correctness, capacity limits, and edge cases.
 *  - Single producer/consumer constraint is verified via dedicated threads.
 */

#include "src/utilities/concurrency/inc/SPSCQueue.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"
#include "src/utilities/concurrency/utst/support/Waiting.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using apex::concurrency::SPSCQueue;
using apex::concurrency::test::settles;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class SPSCQueueTest : public ::testing::Test {
protected:
  static constexpr std::size_t DEFAULT_CAPACITY = 16;
};

/** @test Queue capacity matches constructor argument */
TEST_F(SPSCQueueTest, CapacityMatchesArgument) {
  SPSCQueue<int> queue(DEFAULT_CAPACITY);
  EXPECT_EQ(queue.capacity(), DEFAULT_CAPACITY);
}

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

/** @test tryPush with rvalue moves the value */
TEST_F(SPSCQueueTest, TryPushMovesRvalue) {
  SPSCQueue<std::string> queue(4);

  std::string original = "test string";
  EXPECT_TRUE(queue.tryPush(std::move(original)));

  std::string popped;
  EXPECT_TRUE(queue.tryPop(popped));
  EXPECT_EQ(popped, "test string");
}

/** @test tryPop moves value out of queue */
TEST_F(SPSCQueueTest, TryPopMovesValue) {
  SPSCQueue<std::vector<int>> queue(4);

  std::vector<int> original = {1, 2, 3, 4, 5};
  EXPECT_TRUE(queue.tryPush(original));

  std::vector<int> popped;
  EXPECT_TRUE(queue.tryPop(popped));
  EXPECT_EQ(popped.size(), 5U);
  EXPECT_EQ(popped[0], 1);
}

/* ----------------------------- Concurrent Tests ----------------------------- */

/** @test Single producer single consumer correctness */
TEST_F(SPSCQueueTest, SingleProducerSingleConsumer) {
  SPSCQueue<int> queue(1024);
  constexpr int ITEM_COUNT = 100000;
  std::vector<int> received;
  received.reserve(ITEM_COUNT);

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    int count = 0;
    while (count < ITEM_COUNT) {
      if (queue.tryPop(value)) {
        received.push_back(value);
        ++count;
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

  consumer.join();

  // Verify all items received in order
  ASSERT_EQ(received.size(), static_cast<std::size_t>(ITEM_COUNT));
  for (int i = 0; i < ITEM_COUNT; ++i) {
    EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
  }
}

/** @test High-throughput stress test */
TEST_F(SPSCQueueTest, HighThroughputStress) {
  SPSCQueue<std::size_t> queue(256);
  constexpr std::size_t ITEM_COUNT = 1000000;
  std::size_t sum = 0;

  std::thread consumer([&]() {
    std::size_t value;
    std::size_t count = 0;
    while (count < ITEM_COUNT) {
      if (queue.tryPop(value)) {
        sum += value;
        ++count;
      }
    }
  });

  for (std::size_t i = 0; i < ITEM_COUNT; ++i) {
    while (!queue.tryPush(i)) {
      // Spin
    }
  }

  consumer.join();

  // Sum of 0..N-1 = N*(N-1)/2
  const std::size_t EXPECTED = ITEM_COUNT * (ITEM_COUNT - 1) / 2;
  EXPECT_EQ(sum, EXPECTED);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test capacity() returns consistent results */
TEST(SPSCQueueDeterminismTest, CapacityConsistent) {
  SPSCQueue<int> queue(128);
  const std::size_t FIRST = queue.capacity();
  const std::size_t SECOND = queue.capacity();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 128U);
}

/** @test empty() returns consistent results on empty queue */
TEST(SPSCQueueDeterminismTest, EmptyConsistent) {
  SPSCQueue<int> queue(16);
  const bool FIRST = queue.empty();
  const bool SECOND = queue.empty();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_TRUE(FIRST);
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

/// A queue of int whose indices start where the test puts them, with the snapshot arithmetic
/// visible.
class SteppedQueue : public SPSCQueue<int> {
public:
  SteppedQueue(std::size_t capacity, std::size_t firstIndex) noexcept
      : SPSCQueue<int>(capacity, firstIndex) {}
  using SPSCQueue<int>::occupancy;
};

constexpr std::size_t INDEX_MAX = std::numeric_limits<std::size_t>::max();

} // namespace

/** @test A queue whose indices wrap keeps its order, its capacity and its size: started one step
 * below the index width, it fills across the wrap, refuses the next push, and reports each
 * occupancy on the way down, with the head index numerically below the tail throughout. */
TEST_F(SPSCQueueTest, IndicesWrapWithoutLosingOrderCapacityOrSize) {
  SteppedQueue queue(2, INDEX_MAX);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.sizeApprox(), 0U);

  ASSERT_TRUE(queue.tryPush(1)); // The head index wraps to zero; the tail is still at the maximum.
  EXPECT_EQ(queue.sizeApprox(), 1U);
  ASSERT_TRUE(queue.tryPush(2));
  EXPECT_EQ(queue.sizeApprox(), 2U);
  EXPECT_FALSE(queue.tryPush(3)) << "a full queue took a push across the index wrap";

  int value = 0;
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

/** @test The count sizeApprox() derives from one read of each index is within capacity for every
 * pair, on both sides of an index wrap. A reader that is neither producer nor consumer can pair
 * a head with a later tail; the pairs below are that split, written out. */
TEST_F(SPSCQueueTest, OccupancyOfASplitSnapshotStaysWithinCapacity) {
  constexpr std::size_t CAPACITY = 2;

  // One instant of the queue: the distance, across the wrap too.
  EXPECT_EQ(SteppedQueue::occupancy(5, 5, CAPACITY), 0U);
  EXPECT_EQ(SteppedQueue::occupancy(7, 5, CAPACITY), 2U);
  EXPECT_EQ(SteppedQueue::occupancy(0, INDEX_MAX, CAPACITY), 1U);
  EXPECT_EQ(SteppedQueue::occupancy(1, INDEX_MAX, CAPACITY), 2U);

  // The tail was read after the consumer passed the head already read.
  EXPECT_EQ(SteppedQueue::occupancy(4, 5, CAPACITY), 0U);
  EXPECT_EQ(SteppedQueue::occupancy(4, 6, CAPACITY), 0U);

  // The same split across the wrap: the head read at the maximum of an empty queue, then a push,
  // a pop and a push, then the tail read at zero. The numeric difference is the index maximum.
  EXPECT_EQ(SteppedQueue::occupancy(INDEX_MAX, 0, CAPACITY), 0U);
  EXPECT_EQ(SteppedQueue::occupancy(INDEX_MAX, 1, CAPACITY), 0U);
  EXPECT_EQ(SteppedQueue::occupancy(INDEX_MAX - 1, 0, CAPACITY), 0U);

  static_assert(SteppedQueue::occupancy(INDEX_MAX, 0, 2) <= 2);
  static_assert(SteppedQueue::occupancy(1, INDEX_MAX, 2) == 2);
}

/* ----------------------------- sizeApprox Under Concurrency ----------------------------- */

/** @test sizeApprox() read from a third thread never exceeds the capacity while a producer and a
 * consumer run: its two index reads are not atomic together, and a read that catches the consumer
 * ahead of the producer's older index reports zero, not a wrapped difference. The exchange starts
 * only once the reader is sampling. This case meets the split by chance; the arithmetic for it
 * is pinned by OccupancyOfASplitSnapshotStaysWithinCapacity. */
TEST_F(SPSCQueueTest, SizeApproxFromAThirdThreadStaysWithinCapacity) {
  SPSCQueue<int> queue(2);
  constexpr int ITEMS = 200000;
  std::atomic<bool> done{false};
  std::atomic<bool> sampling{false};
  std::atomic<std::size_t> largestSeen{0};

  // The reader samples in bursts and yields between them, so the case stays quick when all three
  // threads share one core.
  std::thread reader([&]() {
    std::size_t largest = 0;
    while (!done.load(std::memory_order_acquire)) {
      for (int sample = 0; sample < 256; ++sample) {
        const std::size_t SIZE = queue.sizeApprox();
        if (SIZE > largest) {
          largest = SIZE;
        }
      }
      sampling.store(true, std::memory_order_release);
      std::this_thread::yield();
    }
    largestSeen.store(largest, std::memory_order_release);
  });
  const bool READER_SAMPLING = settles([&]() { return sampling.load(std::memory_order_acquire); });
  if (READER_SAMPLING) {
    std::thread consumer([&]() {
      int value = 0;
      for (int received = 0; received < ITEMS;) {
        if (queue.tryPop(value)) {
          ++received;
        } else {
          std::this_thread::yield();
        }
      }
    });
    for (int i = 0; i < ITEMS;) {
      if (queue.tryPush(i)) {
        ++i;
      } else {
        std::this_thread::yield();
      }
    }
    consumer.join();
  }
  done.store(true, std::memory_order_release);
  reader.join();

  ASSERT_TRUE(READER_SAMPLING) << "the reader thread never sampled";
  EXPECT_LE(largestSeen.load(), queue.capacity());
}
