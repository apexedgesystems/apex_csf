/**
 * @file Cache_uTest.cpp
 * @brief Unit tests for concurrency::cache::AlignCl.
 *
 * Notes:
 *  - AlignCl is an aligned aggregate with no code of its own. What it promises
 *    is fixed when it compiles, so those promises are static assertions here.
 *  - The run-time case checks the one thing the compiler does not decide:
 *    that storage the allocator hands out for it is aligned too.
 */

#include "src/utilities/concurrency/inc/Cache.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

using apex::concurrency::cache::AlignCl;
using apex::concurrency::cache::CACHE_LINE_SIZE;

/* ----------------------------- Compile-Time Contract ----------------------------- */

static_assert(CACHE_LINE_SIZE == 64, "the cache line the library's layouts assume");

// The wrapper takes a whole number of cache lines, whatever it wraps, so two of them never
// share one, on the stack, in an array or as neighbouring members.
static_assert(alignof(AlignCl<int>) == CACHE_LINE_SIZE);
static_assert(alignof(AlignCl<std::atomic<bool>>) == CACHE_LINE_SIZE);
static_assert(alignof(AlignCl<std::atomic<std::size_t>>) == CACHE_LINE_SIZE);
static_assert(sizeof(AlignCl<int>) == CACHE_LINE_SIZE);
static_assert(sizeof(AlignCl<std::atomic<std::size_t>>) == CACHE_LINE_SIZE);
static_assert(sizeof(std::array<AlignCl<int>, 4>) == 4 * CACHE_LINE_SIZE);

// It adds nothing to the wrapped type but the alignment: an aggregate, and as copyable as the
// value it holds.
static_assert(std::is_aggregate_v<AlignCl<int>>);
static_assert(std::is_trivially_copyable_v<AlignCl<int>>);
static_assert(AlignCl<int>{42}.value == 42);
static_assert(AlignCl<int>{}.value == 0);

/* ----------------------------- Heap Storage Tests ----------------------------- */

/** @test Storage the allocator provides for AlignCl is cache-line aligned: single objects and a
 * vector's elements all start on a 64-byte boundary. The pools and queues hold AlignCl members
 * inside heap objects, which depends on the allocator honouring the alignment. No change to
 * AlignCl makes this case fail while the assertions above hold: it fails on a toolchain or
 * allocator that ignores over-alignment for new. */
TEST(AlignClTest, HeapStorageIsCacheLineAligned) {
  const auto ALIGNED = [](const void* address) {
    return reinterpret_cast<std::uintptr_t>(address) % CACHE_LINE_SIZE == 0;
  };

  for (int round = 0; round < 8; ++round) {
    const auto SINGLE = std::make_unique<AlignCl<std::atomic<int>>>();
    EXPECT_TRUE(ALIGNED(SINGLE.get()));
  }

  const std::vector<AlignCl<std::atomic<int>>> ELEMENTS(7);
  for (std::size_t i = 0; i < ELEMENTS.size(); ++i) {
    EXPECT_TRUE(ALIGNED(&ELEMENTS[i])) << "vector element " << i;
  }
}
