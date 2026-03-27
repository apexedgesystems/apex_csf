/**
 * @file FaultInjectionProxy_uTest.cpp
 * @brief Unit tests for static-sized FaultInjectionProxy.
 */

#include "src/system/core/infrastructure/data/inc/FaultInjectionProxy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>

using system_core::data::FAULT_MAX_MASK_LEN;
using system_core::data::FAULT_MAX_MASKS;
using system_core::data::FaultInjectionProxy;
using system_core::data::FaultStatus;
using system_core::data::toString;

/* ----------------------------- Default Construction ----------------------------- */

/** @test FaultInjectionProxy starts empty. */
TEST(FaultInjectionProxy, DefaultConstruction) {
  FaultInjectionProxy proxy;
  EXPECT_TRUE(proxy.empty());
  EXPECT_EQ(proxy.size(), 0u);
  EXPECT_EQ(proxy.capacity(), FAULT_MAX_MASKS);
}

/* ----------------------------- FaultStatus toString ----------------------------- */

/** @test FaultStatus toString returns expected strings. */
TEST(FaultStatus, ToStringReturnsExpected) {
  EXPECT_STREQ(toString(FaultStatus::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(FaultStatus::ERROR_EMPTY), "ERROR_EMPTY");
  EXPECT_STREQ(toString(FaultStatus::ERROR_PARAM), "ERROR_PARAM");
  EXPECT_STREQ(toString(FaultStatus::ERROR_FULL), "ERROR_FULL");
  EXPECT_STREQ(toString(FaultStatus::ERROR_TOO_LONG), "ERROR_TOO_LONG");
  EXPECT_STREQ(toString(FaultStatus::ERROR_BOUNDS), "ERROR_BOUNDS");
}

/* ----------------------------- Push Operations ----------------------------- */

/** @test Push succeeds with valid parameters. */
TEST(FaultInjectionProxy, PushSucceeds) {
  FaultInjectionProxy proxy;
  std::array<std::uint8_t, 4> andMask = {0xFF, 0xFF, 0xFF, 0xFF};
  std::array<std::uint8_t, 4> xorMask = {0x01, 0x02, 0x03, 0x04};

  auto status = proxy.push(0, andMask.data(), xorMask.data(), 4);
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u);
  EXPECT_FALSE(proxy.empty());
}

/** @test Push fails when queue is full. */
TEST(FaultInjectionProxy, PushFailsWhenFull) {
  FaultInjectionProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  // Fill the queue
  for (std::size_t i = 0; i < FAULT_MAX_MASKS; ++i) {
    auto status = proxy.push(0, andMask.data(), xorMask.data(), 2);
    EXPECT_EQ(status, FaultStatus::SUCCESS);
  }

  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS);

  // Next push should fail
  auto status = proxy.push(0, andMask.data(), xorMask.data(), 2);
  EXPECT_EQ(status, FaultStatus::ERROR_FULL);
  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS);
}

/** @test Push fails when mask is too long. */
TEST(FaultInjectionProxy, PushFailsWhenTooLong) {
  FaultInjectionProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  auto status = proxy.push(0, andMask.data(), xorMask.data(), FAULT_MAX_MASK_LEN + 1);
  EXPECT_EQ(status, FaultStatus::ERROR_TOO_LONG);
  EXPECT_TRUE(proxy.empty());
}

/** @test Push fails with null pointers when len > 0. */
TEST(FaultInjectionProxy, PushFailsWithNullPointers) {
  FaultInjectionProxy proxy;

  auto status1 = proxy.push(0, nullptr, nullptr, 4);
  EXPECT_EQ(status1, FaultStatus::ERROR_PARAM);

  std::array<std::uint8_t, 4> mask = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status2 = proxy.push(0, mask.data(), nullptr, 4);
  EXPECT_EQ(status2, FaultStatus::ERROR_PARAM);

  auto status3 = proxy.push(0, nullptr, mask.data(), 4);
  EXPECT_EQ(status3, FaultStatus::ERROR_PARAM);

  EXPECT_TRUE(proxy.empty());
}

/** @test Push with zero length succeeds (null pointers allowed). */
TEST(FaultInjectionProxy, PushZeroLengthSucceeds) {
  FaultInjectionProxy proxy;
  auto status = proxy.push(0, nullptr, nullptr, 0);
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u);
}

/* ----------------------------- Convenience Push Methods ----------------------------- */

/** @test pushZeroMask creates correct mask. */
TEST(FaultInjectionProxy, PushZeroMaskCreatesCorrectMask) {
  FaultInjectionProxy proxy;
  auto status = proxy.pushZeroMask(2, 4);
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  // Apply to data and verify bytes become zero
  std::array<std::uint8_t, 8> data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);

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
TEST(FaultInjectionProxy, PushHighMaskCreatesCorrectMask) {
  FaultInjectionProxy proxy;
  auto status = proxy.pushHighMask(1, 2);
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0x00, 0x00, 0x00};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  EXPECT_EQ(data[0], 0x00); // Unchanged
  EXPECT_EQ(data[1], 0xFF); // Set high
  EXPECT_EQ(data[2], 0xFF); // Set high
  EXPECT_EQ(data[3], 0x00); // Unchanged
}

/** @test pushFlipMask creates correct mask. */
TEST(FaultInjectionProxy, PushFlipMaskCreatesCorrectMask) {
  FaultInjectionProxy proxy;
  auto status = proxy.pushFlipMask(0, 3);
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0xFF, 0xAA, 0x55};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  EXPECT_EQ(data[0], 0xFF); // Flipped from 0x00
  EXPECT_EQ(data[1], 0x00); // Flipped from 0xFF
  EXPECT_EQ(data[2], 0x55); // Flipped from 0xAA
  EXPECT_EQ(data[3], 0x55); // Unchanged
}

/* ----------------------------- Convenience Push Error Paths ----------------------------- */

/** @test pushZeroMask returns ERROR_FULL when queue is at capacity. */
TEST(FaultInjectionProxy, PushZeroMaskFullQueue) {
  FaultInjectionProxy proxy;
  for (std::size_t i = 0; i < FAULT_MAX_MASKS; ++i) {
    EXPECT_EQ(proxy.pushZeroMask(i, 1), FaultStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.pushZeroMask(0, 1), FaultStatus::ERROR_FULL);
}

/** @test pushHighMask returns ERROR_TOO_LONG when mask exceeds limit. */
TEST(FaultInjectionProxy, PushHighMaskTooLong) {
  FaultInjectionProxy proxy;
  EXPECT_EQ(proxy.pushHighMask(0, FAULT_MAX_MASK_LEN + 1), FaultStatus::ERROR_TOO_LONG);
}

/** @test pushFlipMask returns ERROR_FULL when queue is at capacity. */
TEST(FaultInjectionProxy, PushFlipMaskFullQueue) {
  FaultInjectionProxy proxy;
  for (std::size_t i = 0; i < FAULT_MAX_MASKS; ++i) {
    EXPECT_EQ(proxy.pushFlipMask(i, 1), FaultStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.pushFlipMask(0, 1), FaultStatus::ERROR_FULL);
}

/* ----------------------------- Pop Operations ----------------------------- */

/** @test Pop removes front mask. */
TEST(FaultInjectionProxy, PopRemovesFrontMask) {
  FaultInjectionProxy proxy;
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
TEST(FaultInjectionProxy, PopOnEmptyIsSafe) {
  FaultInjectionProxy proxy;
  EXPECT_TRUE(proxy.empty());
  proxy.pop(); // Should not crash
  EXPECT_TRUE(proxy.empty());
}

/* ----------------------------- Clear Operations ----------------------------- */

/** @test Clear removes all masks. */
TEST(FaultInjectionProxy, ClearRemovesAllMasks) {
  FaultInjectionProxy proxy;
  std::array<std::uint8_t, 2> andMask = {0xFF, 0xFF};
  std::array<std::uint8_t, 2> xorMask = {0x00, 0x00};

  for (std::size_t i = 0; i < FAULT_MAX_MASKS; ++i) {
    (void)proxy.push(0, andMask.data(), xorMask.data(), 2);
  }
  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS);

  proxy.clear();
  EXPECT_TRUE(proxy.empty());
  EXPECT_EQ(proxy.size(), 0u);
}

/* ----------------------------- Apply Operations ----------------------------- */

/** @test Apply fails on empty queue. */
TEST(FaultInjectionProxy, ApplyFailsOnEmptyQueue) {
  FaultInjectionProxy proxy;
  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::ERROR_EMPTY);
}

/** @test Apply fails with null data. */
TEST(FaultInjectionProxy, ApplyFailsWithNullData) {
  FaultInjectionProxy proxy;
  (void)proxy.pushZeroMask(0, 4);

  auto status = proxy.apply(nullptr, 8);
  EXPECT_EQ(status, FaultStatus::ERROR_PARAM);
}

/** @test Apply fails with zero size. */
TEST(FaultInjectionProxy, ApplyFailsWithZeroSize) {
  FaultInjectionProxy proxy;
  (void)proxy.pushZeroMask(0, 4);

  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), 0);
  EXPECT_EQ(status, FaultStatus::ERROR_PARAM);
}

/** @test Apply fails when mask exceeds buffer bounds. */
TEST(FaultInjectionProxy, ApplyFailsOnBoundsViolation) {
  FaultInjectionProxy proxy;
  (void)proxy.pushZeroMask(6, 4); // Mask at bytes 6-9

  std::array<std::uint8_t, 8> data = {}; // Only 8 bytes
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::ERROR_BOUNDS);
}

/** @test Apply does not pop mask. */
TEST(FaultInjectionProxy, ApplyDoesNotPop) {
  FaultInjectionProxy proxy;
  (void)proxy.pushZeroMask(0, 2);
  EXPECT_EQ(proxy.size(), 1u);

  std::array<std::uint8_t, 4> data = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_EQ(proxy.size(), 1u); // Still there
}

/** @test ApplyAndPop removes mask after application. */
TEST(FaultInjectionProxy, ApplyAndPopRemovesMask) {
  FaultInjectionProxy proxy;
  (void)proxy.pushZeroMask(0, 2);
  EXPECT_EQ(proxy.size(), 1u);

  std::array<std::uint8_t, 4> data = {0xFF, 0xFF, 0xFF, 0xFF};
  auto status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_TRUE(proxy.empty());

  // Verify mask was applied
  EXPECT_EQ(data[0], 0x00);
  EXPECT_EQ(data[1], 0x00);
  EXPECT_EQ(data[2], 0xFF);
  EXPECT_EQ(data[3], 0xFF);
}

/* ----------------------------- Custom Mask Application ----------------------------- */

/** @test Custom AND/XOR mask works correctly. */
TEST(FaultInjectionProxy, CustomMaskApplication) {
  FaultInjectionProxy proxy;

  // Create a mask that sets specific bit patterns
  // byte = (byte & 0xF0) ^ 0x05
  // For 0xAB: (0xAB & 0xF0) ^ 0x05 = 0xA0 ^ 0x05 = 0xA5
  std::array<std::uint8_t, 2> andMask = {0xF0, 0x0F};
  std::array<std::uint8_t, 2> xorMask = {0x05, 0x50};

  auto status = proxy.push(1, andMask.data(), xorMask.data(), 2);
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  std::array<std::uint8_t, 4> data = {0x00, 0xAB, 0xCD, 0xFF};
  status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);

  EXPECT_EQ(data[0], 0x00); // Unchanged
  EXPECT_EQ(data[1], 0xA5); // (0xAB & 0xF0) ^ 0x05 = 0xA5
  EXPECT_EQ(data[2], 0x5D); // (0xCD & 0x0F) ^ 0x50 = 0x5D
  EXPECT_EQ(data[3], 0xFF); // Unchanged
}

/* ----------------------------- Circular Buffer Behavior ----------------------------- */

/** @test Circular buffer wraps correctly after pop and push. */
TEST(FaultInjectionProxy, CircularBufferWrapAround) {
  FaultInjectionProxy proxy;

  // Fill queue
  for (std::size_t i = 0; i < FAULT_MAX_MASKS; ++i) {
    (void)proxy.pushZeroMask(static_cast<std::size_t>(i), 1);
  }
  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS);

  // Pop half
  for (std::size_t i = 0; i < FAULT_MAX_MASKS / 2; ++i) {
    proxy.pop();
  }
  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS / 2);

  // Push more (should wrap around in circular buffer)
  for (std::size_t i = 0; i < FAULT_MAX_MASKS / 2; ++i) {
    auto status = proxy.pushHighMask(static_cast<std::size_t>(i), 1);
    EXPECT_EQ(status, FaultStatus::SUCCESS);
  }
  EXPECT_EQ(proxy.size(), FAULT_MAX_MASKS);

  // Verify we can still apply masks
  std::array<std::uint8_t, 8> data = {};
  auto status = proxy.apply(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);
}

/* ----------------------------- FIFO Order ----------------------------- */

/** @test Masks are applied in FIFO order. */
TEST(FaultInjectionProxy, FifoOrder) {
  FaultInjectionProxy proxy;

  // Push masks for different indices
  (void)proxy.pushHighMask(0, 1); // First: set byte 0 to 0xFF
  (void)proxy.pushZeroMask(1, 1); // Second: set byte 1 to 0x00

  std::array<std::uint8_t, 4> data = {0x00, 0xFF, 0x00, 0xFF};

  // Apply first mask
  auto status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_EQ(data[0], 0xFF); // Changed by first mask
  EXPECT_EQ(data[1], 0xFF); // Not yet changed

  // Apply second mask
  status = proxy.applyAndPop(data.data(), data.size());
  EXPECT_EQ(status, FaultStatus::SUCCESS);
  EXPECT_EQ(data[0], 0xFF); // Unchanged
  EXPECT_EQ(data[1], 0x00); // Changed by second mask
}
