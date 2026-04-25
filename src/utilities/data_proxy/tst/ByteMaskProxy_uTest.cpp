/**
 * @file ByteMaskProxy_uTest.cpp
 * @brief Unit tests for ByteMaskProxy.
 */

#include "src/utilities/data_proxy/inc/ByteMaskProxy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

using system_core::data_proxy::ByteMaskProxy;
using system_core::data_proxy::ByteMaskStatus;
using system_core::data_proxy::MASK_MAX_ENTRIES;
using system_core::data_proxy::MASK_MAX_LEN;
using system_core::data_proxy::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test ByteMaskProxy starts empty. */
TEST(ByteMaskProxy, DefaultConstruction) {
  ByteMaskProxy proxy;
  EXPECT_TRUE(proxy.empty());
  EXPECT_EQ(proxy.size(), 0u);
  EXPECT_EQ(proxy.capacity(), MASK_MAX_ENTRIES);
}

/* ----------------------------- ByteMaskStatus toString ----------------------------- */

/** @test ByteMaskStatus toString returns expected strings. */
TEST(ByteMaskStatus, ToStringReturnsExpected) {
  EXPECT_STREQ(toString(ByteMaskStatus::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(ByteMaskStatus::ERROR_EMPTY), "ERROR_EMPTY");
  EXPECT_STREQ(toString(ByteMaskStatus::ERROR_PARAM), "ERROR_PARAM");
  EXPECT_STREQ(toString(ByteMaskStatus::ERROR_FULL), "ERROR_FULL");
  EXPECT_STREQ(toString(ByteMaskStatus::ERROR_TOO_LONG), "ERROR_TOO_LONG");
  EXPECT_STREQ(toString(ByteMaskStatus::ERROR_BOUNDS), "ERROR_BOUNDS");
}

/* ----------------------------- Push Operations ----------------------------- */

/** @test Push succeeds with valid parameters. */
TEST(ByteMaskProxy, PushSucceeds) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 4> andMask = {0xFF, 0xFF, 0xFF, 0xFF};
  std::array<std::uint8_t, 4> xorMask = {0x01, 0x02, 0x03, 0x04};

  auto status = proxy.push(0, andMask.data(), xorMask.data(), 4);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u);
  EXPECT_FALSE(proxy.empty());
}

/** @test Push fails when queue is full. */
TEST(ByteMaskProxy, PushFailsWhenFull) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  // Fill the queue
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES; ++i) {
    auto status = proxy.push(0, andMask.data(), xorMask.data(), 2);
    EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  }

  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES);

  // Next push should fail
  auto status = proxy.push(0, andMask.data(), xorMask.data(), 2);
  EXPECT_EQ(status, ByteMaskStatus::ERROR_FULL);
  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES);
}

/** @test Push fails when mask is too long. */
TEST(ByteMaskProxy, PushFailsWhenTooLong) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  auto status = proxy.push(0, andMask.data(), xorMask.data(), MASK_MAX_LEN + 1);
  EXPECT_EQ(status, ByteMaskStatus::ERROR_TOO_LONG);
  EXPECT_TRUE(proxy.empty());
}

/** @test Push fails with null pointers when len > 0. */
TEST(ByteMaskProxy, PushFailsWithNullPointers) {
  ByteMaskProxy proxy;

  auto status1 = proxy.push(0, nullptr, nullptr, 4);
  EXPECT_EQ(status1, ByteMaskStatus::ERROR_PARAM);

  std::array<std::uint8_t, 4> mask = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status2 = proxy.push(0, mask.data(), nullptr, 4);
  EXPECT_EQ(status2, ByteMaskStatus::ERROR_PARAM);

  auto status3 = proxy.push(0, nullptr, mask.data(), 4);
  EXPECT_EQ(status3, ByteMaskStatus::ERROR_PARAM);

  EXPECT_TRUE(proxy.empty());
}

/** @test Push with zero length succeeds (null pointers allowed). */
TEST(ByteMaskProxy, PushZeroLengthSucceeds) {
  ByteMaskProxy proxy;
  auto status = proxy.push(0, nullptr, nullptr, 0);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u);
}

/* ----------------------------- Convenience Push Methods ----------------------------- */

/** @test pushZeroMask creates correct mask. */
TEST(ByteMaskProxy, PushZeroMaskCreatesCorrectMask) {
  ByteMaskProxy proxy;
  auto status = proxy.pushZeroMask(2, 4);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  // Apply to data and verify bytes become zero
  std::array<std::uint8_t, 8> data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  EXPECT_EQ(data[0], 0x11); // Unchanged
  EXPECT_EQ(data[1], 0x22); // Unchanged
  EXPECT_EQ(data[2], 0x00); // Zeroed
  EXPECT_EQ(data[3], 0x00); // Zeroed
  EXPECT_EQ(data[4], 0x00); // Zeroed
  EXPECT_EQ(data[5], 0x00); // Zeroed
  EXPECT_EQ(data[6], 0x77); // Unchanged
  EXPECT_EQ(data[7], 0x88); // Unchanged
}

/** @test pushHighMask creates correct mask. */
TEST(ByteMaskProxy, PushHighMaskCreatesCorrectMask) {
  ByteMaskProxy proxy;
  auto status = proxy.pushHighMask(1, 2);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0x00, 0x00, 0x00};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  EXPECT_EQ(data[0], 0x00); // Unchanged
  EXPECT_EQ(data[1], 0xFF); // Set high
  EXPECT_EQ(data[2], 0xFF); // Set high
  EXPECT_EQ(data[3], 0x00); // Unchanged
}

/** @test pushFlipMask creates correct mask. */
TEST(ByteMaskProxy, PushFlipMaskCreatesCorrectMask) {
  ByteMaskProxy proxy;
  auto status = proxy.pushFlipMask(0, 3);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0xFF, 0xAA, 0x55};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  EXPECT_EQ(data[0], 0xFF); // Flipped from 0x00
  EXPECT_EQ(data[1], 0x00); // Flipped from 0xFF
  EXPECT_EQ(data[2], 0x55); // Flipped from 0xAA
  EXPECT_EQ(data[3], 0x55); // Unchanged
}

/* ----------------------------- Convenience Push Error Paths ----------------------------- */

/** @test pushZeroMask returns ERROR_FULL when queue is at capacity. */
TEST(ByteMaskProxy, PushZeroMaskFullQueue) {
  ByteMaskProxy proxy;
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES; ++i) {
    EXPECT_EQ(proxy.pushZeroMask(i, 1), ByteMaskStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.pushZeroMask(0, 1), ByteMaskStatus::ERROR_FULL);
}

/** @test pushHighMask returns ERROR_TOO_LONG when mask exceeds limit. */
TEST(ByteMaskProxy, PushHighMaskTooLong) {
  ByteMaskProxy proxy;
  EXPECT_EQ(proxy.pushHighMask(0, MASK_MAX_LEN + 1), ByteMaskStatus::ERROR_TOO_LONG);
}

/** @test pushFlipMask returns ERROR_FULL when queue is at capacity. */
TEST(ByteMaskProxy, PushFlipMaskFullQueue) {
  ByteMaskProxy proxy;
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES; ++i) {
    EXPECT_EQ(proxy.pushFlipMask(i, 1), ByteMaskStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.pushFlipMask(0, 1), ByteMaskStatus::ERROR_FULL);
}

/* ----------------------------- Pop Operations ----------------------------- */

/** @test Pop removes front mask. */
TEST(ByteMaskProxy, PopRemovesFrontMask) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  (void)proxy.push(0, andMask.data(), xorMask.data(), 2);
  (void)proxy.push(4, andMask.data(), xorMask.data(), 2);
  EXPECT_EQ(proxy.size(), 2u);

  proxy.pop();
  EXPECT_EQ(proxy.size(), 1u);

  proxy.pop();
  EXPECT_TRUE(proxy.empty());
}

/** @test Pop on empty queue is safe no-op. */
TEST(ByteMaskProxy, PopOnEmptyIsSafe) {
  ByteMaskProxy proxy;
  EXPECT_TRUE(proxy.empty());
  proxy.pop(); // Should not crash
  EXPECT_TRUE(proxy.empty());
}

/* ----------------------------- Clear Operations ----------------------------- */

/** @test Clear removes all masks. */
TEST(ByteMaskProxy, ClearRemovesAllMasks) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  for (std::size_t i = 0; i < MASK_MAX_ENTRIES; ++i) {
    (void)proxy.push(0, andMask.data(), xorMask.data(), 2);
  }
  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES);

  proxy.clear();
  EXPECT_TRUE(proxy.empty());
  EXPECT_EQ(proxy.size(), 0u);
}

/* ----------------------------- Apply Operations ----------------------------- */

/** @test Apply fails on empty queue. */
TEST(ByteMaskProxy, ApplyFailsOnEmptyQueue) {
  ByteMaskProxy proxy;
  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::ERROR_EMPTY);
}

/** @test Apply fails with null data. */
TEST(ByteMaskProxy, ApplyFailsWithNullData) {
  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(0, 4);

  auto status = proxy.apply(nullptr, 8);
  EXPECT_EQ(status, ByteMaskStatus::ERROR_PARAM);
}

/** @test Apply fails with zero size. */
TEST(ByteMaskProxy, ApplyFailsWithZeroSize) {
  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(0, 4);

  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), 0);
  EXPECT_EQ(status, ByteMaskStatus::ERROR_PARAM);
}

/** @test Apply fails when mask exceeds buffer bounds. */
TEST(ByteMaskProxy, ApplyFailsOnBoundsViolation) {
  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(6, 4); // Mask at bytes 6-9

  std::array<std::uint8_t, 8> data = {}; // Only 8 bytes
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::ERROR_BOUNDS);
}

/** @test Apply does not pop mask. */
TEST(ByteMaskProxy, ApplyDoesNotPop) {
  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(0, 2);
  EXPECT_EQ(proxy.size(), 1u);

  std::array<std::uint8_t, 4> data = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u); // Still there
}

/** @test ApplyAndPop removes mask after application. */
TEST(ByteMaskProxy, ApplyAndPopRemovesMask) {
  ByteMaskProxy proxy;
  (void)proxy.pushZeroMask(0, 2);
  EXPECT_EQ(proxy.size(), 1u);

  std::array<std::uint8_t, 4> data = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_TRUE(proxy.empty());

  // Verify mask was applied
  EXPECT_EQ(data[0], 0x00);
  EXPECT_EQ(data[1], 0x00);
  EXPECT_EQ(data[2], 0xFF);
  EXPECT_EQ(data[3], 0xFF);
}

/* ----------------------------- Custom Mask Application ----------------------------- */

/** @test Custom AND/XOR mask works correctly. */
TEST(ByteMaskProxy, CustomMaskApplication) {
  ByteMaskProxy proxy;

  // Create a mask that sets specific bit patterns
  // byte = (byte & 0xF0) ^ 0x05
  // For 0xAB: (0xAB & 0xF0) ^ 0x05 = 0xA0 ^ 0x05 = 0xA5
  std::array<std::uint8_t, 2> andMask = {0xF0, 0x0F};
  std::array<std::uint8_t, 2> xorMask = {0x05, 0x50};

  auto status = proxy.push(1, andMask.data(), xorMask.data(), 2);
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0xAB, 0xCD, 0xFF};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);

  EXPECT_EQ(data[0], 0x00); // Unchanged
  EXPECT_EQ(data[1], 0xA5); // (0xAB & 0xF0) ^ 0x05 = 0xA5
  EXPECT_EQ(data[2], 0x5D); // (0xCD & 0x0F) ^ 0x50 = 0x5D
  EXPECT_EQ(data[3], 0xFF); // Unchanged
}

/* ----------------------------- Circular Buffer Behavior ----------------------------- */

/** @test Circular buffer wraps correctly after pop and push. */
TEST(ByteMaskProxy, CircularBufferWrapAround) {
  ByteMaskProxy proxy;

  // Fill queue
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES; ++i) {
    (void)proxy.pushZeroMask(static_cast<std::size_t>(i), 1);
  }
  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES);

  // Pop half
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES / 2; ++i) {
    proxy.pop();
  }
  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES / 2);

  // Push more (should wrap around in circular buffer)
  for (std::size_t i = 0; i < MASK_MAX_ENTRIES / 2; ++i) {
    auto status = proxy.pushHighMask(static_cast<std::size_t>(i), 1);
    EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.size(), MASK_MAX_ENTRIES);

  // Verify we can still apply masks
  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
}

/* ----------------------------- FIFO Order ----------------------------- */

/** @test Masks are applied in FIFO order. */
TEST(ByteMaskProxy, FifoOrder) {
  ByteMaskProxy proxy;

  // Push masks for different indices
  (void)proxy.pushHighMask(0, 1); // First: set byte 0 to 0xFF
  (void)proxy.pushZeroMask(1, 1); // Second: set byte 1 to 0x00

  std::array<std::uint8_t, 4> data = {0x00, 0xFF, 0x00, 0xFF};

  // Apply first mask
  auto status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_EQ(data[0], 0xFF); // Changed by first mask
  EXPECT_EQ(data[1], 0xFF); // Not yet changed

  // Apply second mask
  status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, ByteMaskStatus::SUCCESS);
  EXPECT_EQ(data[0], 0xFF); // Unchanged
  EXPECT_EQ(data[1], 0x00); // Changed by second mask
}
