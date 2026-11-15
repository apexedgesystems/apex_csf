/**
 * @file RingStorage_uTest.cpp
 * @brief Unit tests for concurrency::RingStorage, the slot array of the ring queues.
 */

#include "src/utilities/concurrency/inc/RingStorage.hpp"
#include "src/utilities/concurrency/utst/support/AlignProbe.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <vector>

using apex::concurrency::RingStorage;
using apex::concurrency::test::AlignProbe;

namespace {

/// A slot that records the order it and its siblings were built and destroyed in.
struct Recorded {
  static std::vector<int>& built() {
    static std::vector<int> order;
    return order;
  }
  static std::vector<int>& destroyed() {
    static std::vector<int> order;
    return order;
  }
  static void reset() {
    built().clear();
    destroyed().clear();
  }

  explicit Recorded(int id) : id_(id) { built().push_back(id_); }
  ~Recorded() { destroyed().push_back(id_); }

  int id_;
};

} // namespace

/* ----------------------------- Construction Tests ----------------------------- */

/** @test The sized constructor value-constructs every slot: each int reads zero and can be
 * written through operator[]. */
TEST(RingStorageTest, ValueConstructsEverySlot) {
  RingStorage<int> storage(8);
  ASSERT_EQ(storage.size(), 8U);
  for (std::size_t i = 0; i < storage.size(); ++i) {
    EXPECT_EQ(storage[i], 0);
    storage[i] = static_cast<int>(i) + 1;
  }
  const RingStorage<int>& constView = storage;
  EXPECT_EQ(constView[7], 8);
}

/** @test The constructing form builds slot i from its index, once each and in order, and the
 * destructor destroys every slot in reverse order. */
TEST(RingStorageTest, ConstructsInOrderAndDestroysInReverse) {
  Recorded::reset();
  {
    RingStorage<Recorded> storage(
        4, [](void* where, std::size_t i) { ::new (where) Recorded(static_cast<int>(i) * 10); });
    EXPECT_EQ(Recorded::built(), (std::vector<int>{0, 10, 20, 30}));
    EXPECT_EQ(storage[2].id_, 20);
    EXPECT_TRUE(Recorded::destroyed().empty());
  }
  EXPECT_EQ(Recorded::destroyed(), (std::vector<int>{30, 20, 10, 0}));
}

/** @test A constructor that throws part-way leaves nothing behind: the slots already built are
 * destroyed in reverse order, the storage is released, and the exception reaches the caller.
 * AddressSanitizer reports the release if it is missing. */
TEST(RingStorageTest, ThrowingSlotConstructorUnwindsWhatWasBuilt) {
  Recorded::reset();
  const auto BUILD = []() {
    RingStorage<Recorded> storage(5, [](void* where, std::size_t i) {
      if (i == 3) {
        throw std::runtime_error("slot 3");
      }
      ::new (where) Recorded(static_cast<int>(i));
    });
  };
  EXPECT_THROW(BUILD(), std::runtime_error);
  EXPECT_EQ(Recorded::built(), (std::vector<int>{0, 1, 2}));
  EXPECT_EQ(Recorded::destroyed(), (std::vector<int>{2, 1, 0}));
}

/* ----------------------------- Alignment Tests ----------------------------- */

/** @test Slots of a 64-byte-aligned type are constructed on 64-byte boundaries, at small counts
 * and at one large enough to come from a page-granular allocation. */
TEST(RingStorageTest, HonoursSlotAlignment) {
  AlignProbe::misaligned().store(0, std::memory_order_relaxed);
  for (const std::size_t COUNT : {std::size_t{2}, std::size_t{16}, std::size_t{1024}}) {
    for (int repeat = 0; repeat < 8; ++repeat) {
      const RingStorage<AlignProbe> storage(COUNT);
    }
  }
  EXPECT_EQ(AlignProbe::misaligned().load(std::memory_order_relaxed), 0);
}
