/**
 * @file SPSCQueue_uTest.cpp
 * @brief Unit tests for concurrency::SPSCQueue.
 *
 * Notes:
 *  - Tests verify SPSC correctness, capacity limits, and edge cases.
 *  - Single producer/consumer constraint is verified via dedicated threads.
 */

#include "src/utilities/concurrency/inc/SPSCQueue.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

using apex::concurrency::SPSCQueue;

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

/** @test tryPush fails when queue is full */
TEST_F(SPSCQueueTest, TryPushFailsWhenFull) {
  SPSCQueue<int> queue(4);

  // Fill the queue (capacity is 4, but usable slots are capacity-1 = 3 in classic ring buffer)
  // Actually with our implementation, we should be able to fill all 4 slots
  int pushed = 0;
  while (queue.tryPush(pushed)) {
    ++pushed;
    if (pushed > 10)
      break; // Safety limit
  }

  // Should have pushed at least capacity-1 items
  EXPECT_GE(pushed, 3);
  EXPECT_LE(pushed, 4);
}

/** @test Queue with capacity 2 works correctly */
TEST_F(SPSCQueueTest, CapacityTwoWorks) {
  SPSCQueue<int> queue(2);

  EXPECT_TRUE(queue.tryPush(42));

  int value = 0;
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 42);

  EXPECT_TRUE(queue.tryPush(43));
  EXPECT_TRUE(queue.tryPop(value));
  EXPECT_EQ(value, 43);
}

/** @test Queue can be refilled after draining */
TEST_F(SPSCQueueTest, CanRefillAfterDrain) {
  SPSCQueue<int> queue(8);

  for (int round = 0; round < 3; ++round) {
    // Fill
    for (int i = 0; i < 7; ++i) {
      EXPECT_TRUE(queue.tryPush(round * 10 + i));
    }
    // Drain
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
