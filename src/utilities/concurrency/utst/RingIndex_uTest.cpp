/**
 * @file RingIndex_uTest.cpp
 * @brief Unit tests for the ring queues' index arithmetic in RingIndex.hpp.
 */

#include "src/utilities/concurrency/inc/RingIndex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

using apex::concurrency::nextPowerOfTwo;
using apex::concurrency::ringOccupancy;
using apex::concurrency::ringSlots;

namespace {

constexpr std::size_t INDEX_MAX = std::numeric_limits<std::size_t>::max();
constexpr std::size_t CAPACITY = 2;

} // namespace

/* ----------------------------- Slot Count Tests ----------------------------- */

// A power of two stays, anything else goes up to the next one, across the range.
static_assert(nextPowerOfTwo(1) == 1);
static_assert(nextPowerOfTwo(2) == 2);
static_assert(nextPowerOfTwo(3) == 4);
static_assert(nextPowerOfTwo(16) == 16);
static_assert(nextPowerOfTwo(17) == 32);
static_assert(nextPowerOfTwo((std::size_t{1} << 40) + 1) == std::size_t{1} << 41);
static_assert(nextPowerOfTwo(std::size_t{1} << 63) == std::size_t{1} << 63);

// A ring never has fewer than two slots.
static_assert(ringSlots(0) == 2);
static_assert(ringSlots(1) == 2);
static_assert(ringSlots(2) == 2);
static_assert(ringSlots(5) == 8);
static_assert(ringSlots(1024) == 1024);

/** @test The slot count for a request is a power of two that holds it, and at least two, for
 * every request up to a few thousand. */
TEST(RingSlotsTest, IsTheSmallestPowerOfTwoHoldingTheRequest) {
  for (std::size_t requested = 0; requested <= 4100; ++requested) {
    const std::size_t SLOTS = ringSlots(requested);
    ASSERT_GE(SLOTS, 2U) << requested;
    ASSERT_GE(SLOTS, requested) << requested;
    ASSERT_EQ(SLOTS & (SLOTS - 1), 0U) << requested << " is not a power of two";
    if (SLOTS > 2) {
      ASSERT_LT(SLOTS / 2, requested) << requested << " fits in half as many slots";
    }
  }
}

/* ----------------------------- ringOccupancy Tests ----------------------------- */

/** @test For a head and a tail read at one instant the result is the element count, with the
 * head numerically above the tail and, after an index wrap, numerically below it. */
TEST(RingOccupancyTest, CountsElementsOnBothSidesOfAnIndexWrap) {
  EXPECT_EQ(ringOccupancy(5, 5, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(6, 5, CAPACITY), 1U);
  EXPECT_EQ(ringOccupancy(7, 5, CAPACITY), 2U);
  EXPECT_EQ(ringOccupancy(INDEX_MAX, INDEX_MAX, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(0, INDEX_MAX, CAPACITY), 1U);
  EXPECT_EQ(ringOccupancy(1, INDEX_MAX, CAPACITY), 2U);

  static_assert(ringOccupancy(1, INDEX_MAX, 2) == 2);
}

/** @test A head paired with a tail read later, after consumers passed that head, gives zero and
 * never a value above capacity. The last three pairs are that split across an index wrap: the
 * head read at the top of the index range, the tail read after it wrapped, where the numeric
 * difference is close to the index maximum. */
TEST(RingOccupancyTest, SplitSnapshotGivesZeroNeverAboveCapacity) {
  EXPECT_EQ(ringOccupancy(4, 5, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(4, 6, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(INDEX_MAX, 0, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(INDEX_MAX, 1, CAPACITY), 0U);
  EXPECT_EQ(ringOccupancy(INDEX_MAX - 1, 0, CAPACITY), 0U);

  static_assert(ringOccupancy(INDEX_MAX, 0, 2) <= 2);
}
