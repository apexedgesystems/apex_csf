/**
 * @file RingBuffer_uTest.cpp
 * @brief Unit tests for concurrency::RingBuffer.
 *
 * Notes:
 *  - Tests verify single-threaded correctness, capacity limits, and edge cases.
 *  - RingBuffer is NOT thread-safe. Use SPSCQueue or LockFreeQueue for MT access.
 */

#include "src/utilities/concurrency/inc/RingBuffer.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using apex::concurrency::RingBuffer;

/* ----------------------------- Basic Operations Tests ----------------------------- */

class RingBufferTest : public ::testing::Test {
protected:
  static constexpr std::size_t DEFAULT_CAPACITY = 16;
};

/** @test Buffer capacity is at least the requested amount */
TEST_F(RingBufferTest, CapacityAtLeastRequested) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  EXPECT_GE(buffer.capacity(), DEFAULT_CAPACITY);
}

/** @test Buffer with zero capacity rounds up to minimum */
TEST_F(RingBufferTest, ZeroCapacityRoundsUp) {
  RingBuffer<int> buffer(0);
  EXPECT_GE(buffer.capacity(), 2U);
}

/** @test Non-power-of-two capacity rounds up */
TEST_F(RingBufferTest, CapacityRoundsUpToPowerOfTwo) {
  RingBuffer<int> b5(5);
  EXPECT_GE(b5.capacity(), 5U);

  RingBuffer<int> b17(17);
  EXPECT_GE(b17.capacity(), 17U);
}

/** @test tryPush succeeds on empty buffer */
TEST_F(RingBufferTest, TryPushSucceedsOnEmpty) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  EXPECT_TRUE(buffer.tryPush(42));
}

/** @test tryPop succeeds after push */
TEST_F(RingBufferTest, TryPopSucceedsAfterPush) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  EXPECT_TRUE(buffer.tryPush(42));

  int value = 0;
  EXPECT_TRUE(buffer.tryPop(value));
  EXPECT_EQ(value, 42);
}

/** @test tryPop fails on empty buffer */
TEST_F(RingBufferTest, TryPopFailsOnEmpty) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  int value = 0;
  EXPECT_FALSE(buffer.tryPop(value));
}

/** @test empty() returns true for empty buffer */
TEST_F(RingBufferTest, EmptyReturnsTrueWhenEmpty) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  EXPECT_TRUE(buffer.empty());
}

/** @test empty() returns false after push */
TEST_F(RingBufferTest, EmptyReturnsFalseAfterPush) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  ASSERT_TRUE(buffer.tryPush(42));
  EXPECT_FALSE(buffer.empty());
}

/** @test full() returns true when buffer is full */
TEST_F(RingBufferTest, FullReturnsTrueWhenFull) {
  RingBuffer<int> buffer(4);
  const std::size_t CAP = buffer.capacity();

  for (std::size_t i = 0; i < CAP; ++i) {
    EXPECT_FALSE(buffer.full());
    ASSERT_TRUE(buffer.tryPush(static_cast<int>(i)));
  }
  EXPECT_TRUE(buffer.full());
}

/** @test size() returns correct count */
TEST_F(RingBufferTest, SizeReturnsCount) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  EXPECT_EQ(buffer.size(), 0U);

  ASSERT_TRUE(buffer.tryPush(1));
  EXPECT_EQ(buffer.size(), 1U);

  ASSERT_TRUE(buffer.tryPush(2));
  ASSERT_TRUE(buffer.tryPush(3));
  EXPECT_EQ(buffer.size(), 3U);

  int value = 0;
  ASSERT_TRUE(buffer.tryPop(value));
  EXPECT_EQ(buffer.size(), 2U);
}

/** @test Buffer maintains FIFO order */
TEST_F(RingBufferTest, MaintainsFifoOrder) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(buffer.tryPush(i));
  }

  for (int i = 0; i < 10; ++i) {
    int value = -1;
    EXPECT_TRUE(buffer.tryPop(value));
    EXPECT_EQ(value, i);
  }
}

/* ----------------------------- Capacity Tests ----------------------------- */

/** @test tryPush fails when buffer is full */
TEST_F(RingBufferTest, TryPushFailsWhenFull) {
  RingBuffer<int> buffer(4);
  const std::size_t CAP = buffer.capacity();

  // Fill the buffer
  for (std::size_t i = 0; i < CAP; ++i) {
    EXPECT_TRUE(buffer.tryPush(static_cast<int>(i)));
  }

  // Next push should fail
  EXPECT_FALSE(buffer.tryPush(99));
}

/** @test Buffer with small capacity works correctly */
TEST_F(RingBufferTest, SmallCapacityWorks) {
  RingBuffer<int> buffer(2);

  EXPECT_TRUE(buffer.tryPush(42));

  int value = 0;
  EXPECT_TRUE(buffer.tryPop(value));
  EXPECT_EQ(value, 42);

  EXPECT_TRUE(buffer.tryPush(43));
  EXPECT_TRUE(buffer.tryPop(value));
  EXPECT_EQ(value, 43);
}

/** @test Buffer can be refilled after draining */
TEST_F(RingBufferTest, CanRefillAfterDrain) {
  RingBuffer<int> buffer(8);
  const std::size_t CAP = buffer.capacity();

  for (int round = 0; round < 3; ++round) {
    // Fill
    for (std::size_t i = 0; i < CAP; ++i) {
      EXPECT_TRUE(buffer.tryPush(round * 10 + static_cast<int>(i)));
    }
    // Drain
    for (std::size_t i = 0; i < CAP; ++i) {
      int value = -1;
      EXPECT_TRUE(buffer.tryPop(value));
      EXPECT_EQ(value, round * 10 + static_cast<int>(i));
    }
    EXPECT_TRUE(buffer.empty());
  }
}

/** @test Order and size() hold while the indices wrap: the buffer is filled and drained through
 * many cycles, so the write index is below the read index for part of each. */
TEST_F(RingBufferTest, WraparoundKeepsOrderAndSize) {
  RingBuffer<int> buffer(4);
  const std::size_t CAP = buffer.capacity();

  for (int cycle = 0; cycle < 100; ++cycle) {
    for (std::size_t i = 0; i < CAP; ++i) {
      EXPECT_TRUE(buffer.tryPush(cycle * 100 + static_cast<int>(i)));
      EXPECT_EQ(buffer.size(), i + 1) << "cycle " << cycle;
    }
    for (std::size_t i = 0; i < CAP; ++i) {
      int value = -1;
      EXPECT_TRUE(buffer.tryPop(value));
      EXPECT_EQ(value, cycle * 100 + static_cast<int>(i));
      EXPECT_EQ(buffer.size(), CAP - i - 1) << "cycle " << cycle;
    }
  }
}

/* ----------------------------- Peek Tests ----------------------------- */

/** @test tryPeek returns front without removing */
TEST_F(RingBufferTest, TryPeekDoesNotRemove) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  ASSERT_TRUE(buffer.tryPush(42));
  ASSERT_TRUE(buffer.tryPush(43));

  int value = 0;
  EXPECT_TRUE(buffer.tryPeek(value));
  EXPECT_EQ(value, 42);
  EXPECT_EQ(buffer.size(), 2U);

  // Peek again should return same value
  EXPECT_TRUE(buffer.tryPeek(value));
  EXPECT_EQ(value, 42);
}

/** @test tryPeek fails on empty buffer */
TEST_F(RingBufferTest, TryPeekFailsOnEmpty) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);
  int value = 0;
  EXPECT_FALSE(buffer.tryPeek(value));
}

/* ----------------------------- Clear Tests ----------------------------- */

/** @test clear() empties the buffer */
TEST_F(RingBufferTest, ClearEmptiesBuffer) {
  RingBuffer<int> buffer(DEFAULT_CAPACITY);

  ASSERT_TRUE(buffer.tryPush(1));
  ASSERT_TRUE(buffer.tryPush(2));
  ASSERT_TRUE(buffer.tryPush(3));
  EXPECT_EQ(buffer.size(), 3U);

  buffer.clear();

  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.size(), 0U);
  EXPECT_FALSE(buffer.full());
}

/** @test Buffer works correctly after clear() */
TEST_F(RingBufferTest, WorksAfterClear) {
  RingBuffer<int> buffer(8);

  ASSERT_TRUE(buffer.tryPush(1));
  ASSERT_TRUE(buffer.tryPush(2));
  buffer.clear();

  EXPECT_TRUE(buffer.tryPush(42));
  int value = 0;
  EXPECT_TRUE(buffer.tryPop(value));
  EXPECT_EQ(value, 42);
}

/* ----------------------------- Move Semantics Tests ----------------------------- */

/** @test tryPush with rvalue moves the value */
TEST_F(RingBufferTest, TryPushMovesRvalue) {
  RingBuffer<std::string> buffer(4);

  std::string original = "test string";
  EXPECT_TRUE(buffer.tryPush(std::move(original)));

  std::string popped;
  EXPECT_TRUE(buffer.tryPop(popped));
  EXPECT_EQ(popped, "test string");
}

/** @test tryPop moves value out of buffer */
TEST_F(RingBufferTest, TryPopMovesValue) {
  RingBuffer<std::vector<int>> buffer(4);

  std::vector<int> original = {1, 2, 3, 4, 5};
  EXPECT_TRUE(buffer.tryPush(original));

  std::vector<int> popped;
  EXPECT_TRUE(buffer.tryPop(popped));
  EXPECT_EQ(popped.size(), 5U);
  EXPECT_EQ(popped[0], 1);
}

/* ----------------------------- High Throughput Tests ----------------------------- */

/** @test High-throughput single-threaded stress test */
TEST_F(RingBufferTest, HighThroughputStress) {
  RingBuffer<std::size_t> buffer(256);
  constexpr std::size_t ITEM_COUNT = 1000000;
  std::size_t sum = 0;

  // Alternating push/pop to stress wraparound
  std::size_t pushed = 0;
  std::size_t popped = 0;

  while (popped < ITEM_COUNT) {
    // Push a batch
    while (pushed < ITEM_COUNT && buffer.tryPush(pushed)) {
      ++pushed;
    }

    // Pop a batch
    std::size_t value;
    while (buffer.tryPop(value)) {
      sum += value;
      ++popped;
    }
  }

  // Sum of 0..N-1 = N*(N-1)/2
  const std::size_t EXPECTED = ITEM_COUNT * (ITEM_COUNT - 1) / 2;
  EXPECT_EQ(sum, EXPECTED);
}

/* ----------------------------- Storage Alignment Tests ----------------------------- */

/** @test The storage honours the element's alignment: every element of a 64-byte-aligned type is
 * constructed at a 64-byte boundary, at small capacities and at one large enough to come from a
 * page-granular allocation. */
TEST_F(RingBufferTest, StorageHonoursElementAlignment) {
  using apex::concurrency::test::AlignProbe;
  AlignProbe::misaligned().store(0, std::memory_order_relaxed);
  for (const std::size_t CAPACITY : {std::size_t{2}, std::size_t{16}, std::size_t{1024}}) {
    for (int repeat = 0; repeat < 8; ++repeat) {
      const RingBuffer<AlignProbe> queue(CAPACITY);
    }
  }
  EXPECT_EQ(AlignProbe::misaligned().load(std::memory_order_relaxed), 0)
      << "elements were constructed off their 64-byte alignment";
}
