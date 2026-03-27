/**
 * @file LockFreeQueue_uTest.cpp
 * @brief Unit tests for concurrency::LockFreeQueue.
 *
 * Notes:
 *  - Tests verify MPMC correctness, capacity limits, and edge cases.
 *  - Concurrent tests use multiple threads to stress the lock-free algorithm.
 */

#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

using apex::concurrency::LockFreeQueue;

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

/** @test Multiple producers single consumer */
TEST_F(LockFreeQueueTest, MultipleProducersSingleConsumer) {
  LockFreeQueue<int> queue(1024);
  constexpr int PRODUCER_COUNT = 4;
  constexpr int ITEMS_PER_PRODUCER = 1000;
  std::atomic<int> producersDone{0};
  std::set<int> received;
  std::mutex receivedMutex;

  // Consumer thread
  std::thread consumer([&]() {
    int value;
    while (true) {
      if (queue.tryPop(value)) {
        std::lock_guard<std::mutex> lk(receivedMutex);
        received.insert(value);
      } else if (producersDone.load(std::memory_order_acquire) >= PRODUCER_COUNT) {
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // Producer threads
  std::vector<std::thread> producers;
  for (int p = 0; p < PRODUCER_COUNT; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        const int VALUE = p * ITEMS_PER_PRODUCER + i;
        while (!queue.tryPush(VALUE)) {
          std::this_thread::yield();
        }
      }
      producersDone.fetch_add(1, std::memory_order_release);
    });
  }

  for (auto& t : producers) {
    t.join();
  }
  consumer.join();

  // Drain remaining
  int value;
  while (queue.tryPop(value)) {
    std::lock_guard<std::mutex> lk(receivedMutex);
    received.insert(value);
  }

  // Verify all unique items received
  EXPECT_EQ(received.size(), static_cast<std::size_t>(PRODUCER_COUNT * ITEMS_PER_PRODUCER));
}

/** @test Multiple producers multiple consumers */
TEST_F(LockFreeQueueTest, MultipleProducersMultipleConsumers) {
  LockFreeQueue<int> queue(1024);
  constexpr int PRODUCER_COUNT = 2;
  constexpr int CONSUMER_COUNT = 2;
  constexpr int ITEMS_PER_PRODUCER = 500;
  std::atomic<int> producersDone{0};
  std::atomic<int> totalReceived{0};

  // Consumer threads
  std::vector<std::thread> consumers;
  for (int c = 0; c < CONSUMER_COUNT; ++c) {
    consumers.emplace_back([&]() {
      int value;
      while (true) {
        if (queue.tryPop(value)) {
          totalReceived.fetch_add(1, std::memory_order_relaxed);
        } else if (producersDone.load(std::memory_order_acquire) >= PRODUCER_COUNT) {
          break;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  // Producer threads
  std::vector<std::thread> producers;
  for (int p = 0; p < PRODUCER_COUNT; ++p) {
    producers.emplace_back([&, p]() {
      for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
        while (!queue.tryPush(p * ITEMS_PER_PRODUCER + i)) {
          std::this_thread::yield();
        }
      }
      producersDone.fetch_add(1, std::memory_order_release);
    });
  }

  for (auto& t : producers) {
    t.join();
  }
  for (auto& t : consumers) {
    t.join();
  }

  // Drain remaining
  int value;
  while (queue.tryPop(value)) {
    totalReceived.fetch_add(1, std::memory_order_relaxed);
  }

  EXPECT_EQ(totalReceived.load(), PRODUCER_COUNT * ITEMS_PER_PRODUCER);
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
