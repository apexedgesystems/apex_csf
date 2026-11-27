/**
 * @file RingBuffer_uTest.cpp
 * @brief Unit tests for concurrency::RingBuffer.
 *
 * Notes:
 *  - Tests verify single-threaded correctness, capacity limits, and edge cases.
 *  - RingBuffer is NOT thread-safe. Use SPSCQueue or LockFreeQueue for MT access.
 */

#include "src/utilities/concurrency/inc/RingBuffer.hpp"
#include "src/utilities/concurrency/utst/support/MoveTracking.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"

#include <gtest/gtest.h>

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

/** @test A buffer takes exactly capacity() elements and refuses the next, through the copy
 * overload and through the move overload. */
TEST_F(RingBufferTest, TakesExactlyCapacityThenRefuses) {
  RingBuffer<int> byCopy(4);
  for (std::size_t i = 0; i < byCopy.capacity(); ++i) {
    const int VALUE = static_cast<int>(i);
    EXPECT_TRUE(byCopy.tryPush(VALUE)) << "copy push " << i;
  }
  const int EXTRA = 99;
  EXPECT_FALSE(byCopy.tryPush(EXTRA)) << "a full buffer took a copy";
  EXPECT_EQ(byCopy.size(), byCopy.capacity());

  RingBuffer<int> byMove(4);
  for (std::size_t i = 0; i < byMove.capacity(); ++i) {
    EXPECT_TRUE(byMove.tryPush(static_cast<int>(i))) << "move push " << i;
  }
  EXPECT_FALSE(byMove.tryPush(99)) << "a full buffer took a move";
  EXPECT_EQ(byMove.size(), byMove.capacity());
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

/** @test An rvalue pushed is moved in without a copy and its source is left moved-from, an
 * lvalue pushed is copied exactly once, a pop moves the element out without a copy, and a
 * move-only element type goes through. */
TEST_F(RingBufferTest, MovesRvaluesAndCopiesLvaluesOnce) {
  apex::concurrency::test::checkQueueValueCategories<RingBuffer>();
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
