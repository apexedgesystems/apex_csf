/**
 * @file EndiannessProxy_uTest.cpp
 * @brief Unit tests for EndiannessProxy.
 */

#include "src/system/core/infrastructure/data/inc/EndiannessProxy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using system_core::data::EndiannessProxy;
using system_core::data::EndianStatus;

/* ----------------------------- Test Struct ----------------------------- */

namespace {

/**
 * @brief Test struct with mixed field sizes.
 */
struct TestPacket {
  std::uint32_t fieldA;
  std::uint16_t fieldB;
  std::uint8_t fieldC;
  std::uint8_t padding;
};

/**
 * @brief User-defined endian swap for TestPacket.
 *
 * This is found via ADL when EndiannessProxy resolves.
 */
void endianSwap(const TestPacket& in, TestPacket& out) noexcept {
  out.fieldA = system_core::data::swapBytes(in.fieldA);
  out.fieldB = system_core::data::swapBytes(in.fieldB);
  out.fieldC = in.fieldC; // Single byte - no swap needed
  out.padding = in.padding;
}

} // namespace

/* ----------------------------- Scalar No-Swap ----------------------------- */

/** @test EndiannessProxy with SwapRequired=false copies scalar. */
TEST(EndiannessProxy, ScalarNoSwapCopies) {
  std::uint32_t in = 0x12345678;
  std::uint32_t out = 0;

  EndiannessProxy<std::uint32_t, false> proxy(&in, &out);
  EXPECT_FALSE(proxy.swapRequired());

  auto status = proxy.resolve();
  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, 0x12345678);
}

/** @test EndiannessProxy with SwapRequired=false and same in/out is no-op. */
TEST(EndiannessProxy, ScalarNoSwapSamePointerIsNoOp) {
  std::uint32_t data = 0x12345678;

  EndiannessProxy<std::uint32_t, false> proxy(&data, &data);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(data, 0x12345678); // Unchanged
}

/* ----------------------------- Scalar Swap ----------------------------- */

/** @test EndiannessProxy swaps uint32_t bytes. */
TEST(EndiannessProxy, ScalarSwapUint32) {
  std::uint32_t in = 0x12345678;
  std::uint32_t out = 0;

  EndiannessProxy<std::uint32_t, true> proxy(&in, &out);
  EXPECT_TRUE(proxy.swapRequired());

  auto status = proxy.resolve();
  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, 0x78563412);
}

/** @test EndiannessProxy swaps uint16_t bytes. */
TEST(EndiannessProxy, ScalarSwapUint16) {
  std::uint16_t in = 0x1234;
  std::uint16_t out = 0;

  EndiannessProxy<std::uint16_t, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, 0x3412);
}

/** @test EndiannessProxy swaps uint64_t bytes. */
TEST(EndiannessProxy, ScalarSwapUint64) {
  std::uint64_t in = 0x123456789ABCDEF0ULL;
  std::uint64_t out = 0;

  EndiannessProxy<std::uint64_t, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, 0xF0DEBC9A78563412ULL);
}

/** @test EndiannessProxy with uint8_t is identity (single byte). */
TEST(EndiannessProxy, ScalarSwapUint8IsIdentity) {
  std::uint8_t in = 0xAB;
  std::uint8_t out = 0;

  EndiannessProxy<std::uint8_t, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, 0xAB); // No change for single byte
}

/** @test EndiannessProxy swaps signed integers correctly. */
TEST(EndiannessProxy, ScalarSwapSigned) {
  std::int32_t in = -1; // 0xFFFFFFFF
  std::int32_t out = 0;

  EndiannessProxy<std::int32_t, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out, -1); // 0xFFFFFFFF swapped is still 0xFFFFFFFF
}

/** @test EndiannessProxy swaps float bytes. */
TEST(EndiannessProxy, ScalarSwapFloat) {
  float in = 1.0f;
  float out = 0.0f;

  EndiannessProxy<float, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);

  // Verify bytes were actually swapped by checking they differ
  // (unless we're on a weird platform)
  union {
    float f;
    std::uint32_t u;
  } outU;
  outU.f = out;

  // 1.0f = 0x3F800000, swapped = 0x0000803F
  EXPECT_EQ(outU.u, 0x0000803F);
}

/** @test EndiannessProxy swaps double bytes. */
TEST(EndiannessProxy, ScalarSwapDouble) {
  double in = 1.0;
  double out = 0.0;

  EndiannessProxy<double, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);

  union {
    double d;
    std::uint64_t u;
  } outU;
  outU.d = out;

  // 1.0 = 0x3FF0000000000000, swapped = 0x000000000000F03F
  EXPECT_EQ(outU.u, 0x000000000000F03FULL);
}

/* ----------------------------- Struct No-Swap ----------------------------- */

/** @test EndiannessProxy with SwapRequired=false copies struct. */
TEST(EndiannessProxy, StructNoSwapCopies) {
  TestPacket in{0x12345678, 0xABCD, 0xEF, 0x00};
  TestPacket out{};

  EndiannessProxy<TestPacket, false> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out.fieldA, 0x12345678);
  EXPECT_EQ(out.fieldB, 0xABCD);
  EXPECT_EQ(out.fieldC, 0xEF);
}

/* ----------------------------- Struct Swap ----------------------------- */

/** @test EndiannessProxy swaps struct fields via ADL endianSwap. */
TEST(EndiannessProxy, StructSwapViaAdl) {
  TestPacket in{0x12345678, 0xABCD, 0xEF, 0x00};
  TestPacket out{};

  EndiannessProxy<TestPacket, true> proxy(&in, &out);
  auto status = proxy.resolve();

  EXPECT_EQ(status, EndianStatus::SUCCESS);
  EXPECT_EQ(out.fieldA, 0x78563412); // Swapped
  EXPECT_EQ(out.fieldB, 0xCDAB);     // Swapped
  EXPECT_EQ(out.fieldC, 0xEF);       // Not swapped (single byte)
}

/* ----------------------------- Accessor Methods ----------------------------- */

/** @test EndiannessProxy accessors return correct pointers. */
TEST(EndiannessProxy, Accessors) {
  std::uint32_t in = 0;
  std::uint32_t out = 0;

  EndiannessProxy<std::uint32_t, true> proxy(&in, &out);

  EXPECT_EQ(proxy.input(), &in);
  EXPECT_EQ(proxy.output(), &out);
}

/* ----------------------------- Round-Trip ----------------------------- */

/** @test Double swap returns original value. */
TEST(EndiannessProxy, RoundTrip) {
  std::uint32_t original = 0x12345678;
  std::uint32_t swapped = 0;
  std::uint32_t restored = 0;

  // First swap
  EndiannessProxy<std::uint32_t, true> proxy1(&original, &swapped);
  (void)proxy1.resolve();

  // Second swap (back to original)
  EndiannessProxy<std::uint32_t, true> proxy2(&swapped, &restored);
  (void)proxy2.resolve();

  EXPECT_EQ(restored, original);
}

/** @test Struct round-trip restores original values. */
TEST(EndiannessProxy, StructRoundTrip) {
  TestPacket original{0x12345678, 0xABCD, 0xEF, 0x00};
  TestPacket swapped{};
  TestPacket restored{};

  EndiannessProxy<TestPacket, true> proxy1(&original, &swapped);
  (void)proxy1.resolve();

  EndiannessProxy<TestPacket, true> proxy2(&swapped, &restored);
  (void)proxy2.resolve();

  EXPECT_EQ(restored.fieldA, original.fieldA);
  EXPECT_EQ(restored.fieldB, original.fieldB);
  EXPECT_EQ(restored.fieldC, original.fieldC);
}
