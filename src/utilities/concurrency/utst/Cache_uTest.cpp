/**
 * @file Cache_uTest.cpp
 * @brief Unit tests for concurrency::cache::AlignCl.
 *
 * Notes:
 *  - Tests verify cacheline alignment and value storage.
 *  - Alignment is critical for preventing false sharing.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

using apex::concurrency::cache::AlignCl;
using apex::concurrency::cache::CACHE_LINE_SIZE;

/* ----------------------------- Constants Tests ----------------------------- */

/** @test CACHE_LINE_SIZE is 64 bytes */
TEST(CacheConstantsTest, CacheLineSizeIs64) { EXPECT_EQ(CACHE_LINE_SIZE, 64U); }

/* ----------------------------- Alignment Tests ----------------------------- */

/** @test AlignCl<int> is aligned to cache line */
TEST(AlignClTest, IntAlignedToCacheLine) { EXPECT_EQ(alignof(AlignCl<int>), CACHE_LINE_SIZE); }

/** @test AlignCl<atomic<bool>> is aligned to cache line */
TEST(AlignClTest, AtomicBoolAlignedToCacheLine) {
  EXPECT_EQ(alignof(AlignCl<std::atomic<bool>>), CACHE_LINE_SIZE);
}

/** @test AlignCl<atomic<size_t>> is aligned to cache line */
TEST(AlignClTest, AtomicSizeTAlignedToCacheLine) {
  EXPECT_EQ(alignof(AlignCl<std::atomic<std::size_t>>), CACHE_LINE_SIZE);
}

/** @test AlignCl size is at least cache line size */
TEST(AlignClTest, SizeAtLeastCacheLine) { EXPECT_GE(sizeof(AlignCl<int>), CACHE_LINE_SIZE); }

/** @test Multiple AlignCl instances are on separate cache lines */
TEST(AlignClTest, InstancesOnSeparateCacheLines) {
  AlignCl<int> a{};
  AlignCl<int> b{};

  const auto ADDR_A = reinterpret_cast<std::uintptr_t>(&a);
  const auto ADDR_B = reinterpret_cast<std::uintptr_t>(&b);

  // Addresses should differ by at least cache line size
  const auto DIFF = (ADDR_A > ADDR_B) ? (ADDR_A - ADDR_B) : (ADDR_B - ADDR_A);
  EXPECT_GE(DIFF, CACHE_LINE_SIZE);
}

/* ----------------------------- Value Storage Tests ----------------------------- */

/** @test AlignCl stores and retrieves int value */
TEST(AlignClValueTest, StoresIntValue) {
  AlignCl<int> aligned{42};
  EXPECT_EQ(aligned.value, 42);
}

/** @test AlignCl stores and retrieves atomic value */
TEST(AlignClValueTest, StoresAtomicValue) {
  AlignCl<std::atomic<int>> aligned{100};
  EXPECT_EQ(aligned.value.load(), 100);
}

/** @test AlignCl value can be modified */
TEST(AlignClValueTest, ValueCanBeModified) {
  AlignCl<int> aligned{0};
  aligned.value = 42;
  EXPECT_EQ(aligned.value, 42);
}

/** @test AlignCl atomic value can be modified atomically */
TEST(AlignClValueTest, AtomicValueCanBeModified) {
  AlignCl<std::atomic<int>> aligned{0};
  aligned.value.store(42, std::memory_order_relaxed);
  EXPECT_EQ(aligned.value.load(std::memory_order_relaxed), 42);
}

/** @test AlignCl with bool atomic works correctly */
TEST(AlignClValueTest, BoolAtomicWorks) {
  AlignCl<std::atomic<bool>> aligned{false};
  EXPECT_FALSE(aligned.value.load());

  aligned.value.store(true, std::memory_order_relaxed);
  EXPECT_TRUE(aligned.value.load());
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test AlignCl<int> default constructs to zero */
TEST(AlignClDefaultTest, IntDefaultsToZero) {
  AlignCl<int> aligned{};
  EXPECT_EQ(aligned.value, 0);
}

/** @test AlignCl<atomic<bool>> default constructs correctly */
TEST(AlignClDefaultTest, AtomicBoolDefaultConstructs) {
  AlignCl<std::atomic<bool>> aligned{false};
  EXPECT_FALSE(aligned.value.load());
}

/* ----------------------------- Array Tests ----------------------------- */

/** @test Array of AlignCl has proper spacing */
TEST(AlignClArrayTest, ArrayHasProperSpacing) {
  AlignCl<int> arr[4] = {{1}, {2}, {3}, {4}};

  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(arr[i].value, static_cast<int>(i + 1));
  }

  // Each element should be cache-line aligned
  for (std::size_t i = 0; i < 4; ++i) {
    const auto ADDR = reinterpret_cast<std::uintptr_t>(&arr[i]);
    EXPECT_EQ(ADDR % CACHE_LINE_SIZE, 0U);
  }
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test AlignCl alignment is consistent */
TEST(AlignClDeterminismTest, AlignmentConsistent) {
  const std::size_t FIRST = alignof(AlignCl<int>);
  const std::size_t SECOND = alignof(AlignCl<int>);
  EXPECT_EQ(FIRST, SECOND);
}
