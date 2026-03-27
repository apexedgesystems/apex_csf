/**
 * @file MasterDataProxy_uTest.cpp
 * @brief Unit tests for MasterDataProxy.
 */

#include "src/system/core/infrastructure/data/inc/MasterDataProxy.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using system_core::data::FaultStatus;
using system_core::data::MasterDataProxy;
using system_core::data::MasterStatus;
using system_core::data::ProxySlot;
using system_core::data::toString;

/* ----------------------------- Test Struct ----------------------------- */

namespace {

struct TestPacket {
  std::uint32_t fieldA;
  std::uint16_t fieldB;
  std::uint8_t fieldC;
  std::uint8_t padding;
};

void endianSwap(const TestPacket& in, TestPacket& out) noexcept {
  out.fieldA = system_core::data::swapBytes(in.fieldA);
  out.fieldB = system_core::data::swapBytes(in.fieldB);
  out.fieldC = in.fieldC;
  out.padding = in.padding;
}

} // namespace

/* ----------------------------- MasterStatus toString ----------------------------- */

/** @test MasterStatus toString returns expected strings. */
TEST(MasterStatus, ToStringReturnsExpected) {
  EXPECT_STREQ(toString(MasterStatus::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(MasterStatus::ERROR_PARAM), "ERROR_PARAM");
  EXPECT_STREQ(toString(MasterStatus::ERROR_PROXIES), "ERROR_PROXIES");
}

/* ----------------------------- Passthrough Mode ----------------------------- */

/** @test MasterDataProxy with no transformations returns input pointer. */
TEST(MasterDataProxy, PassthroughReturnsInputPointer) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, false, false> proxy(&input);

  EXPECT_FALSE(proxy.swapRequired());
  EXPECT_FALSE(proxy.faultsSupported());
  EXPECT_FALSE(proxy.needsOverlay());

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  // Output should point to input (zero-copy)
  EXPECT_EQ(proxy.output(), &input);
  EXPECT_EQ(*proxy.output(), 0x12345678);
}

/** @test MasterDataProxy passthrough with struct. */
TEST(MasterDataProxy, PassthroughStruct) {
  TestPacket input{0x12345678, 0xABCD, 0xEF, 0x00};
  MasterDataProxy<TestPacket, false, false> proxy(&input);

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  EXPECT_EQ(proxy.output()->fieldA, 0x12345678);
  EXPECT_EQ(proxy.output()->fieldB, 0xABCD);
  EXPECT_EQ(proxy.output()->fieldC, 0xEF);
}

/* ----------------------------- Endianness Only ----------------------------- */

/** @test MasterDataProxy with swap swaps scalar bytes. */
TEST(MasterDataProxy, SwapOnlyScalar) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, true, false> proxy(&input);

  EXPECT_TRUE(proxy.swapRequired());
  EXPECT_FALSE(proxy.faultsSupported());
  EXPECT_TRUE(proxy.needsOverlay());

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  // Output should be different from input (uses overlay)
  EXPECT_NE(proxy.output(), &input);
  EXPECT_EQ(*proxy.output(), 0x78563412);

  // Input unchanged
  EXPECT_EQ(input, 0x12345678);
}

/** @test MasterDataProxy with swap swaps struct fields. */
TEST(MasterDataProxy, SwapOnlyStruct) {
  TestPacket input{0x12345678, 0xABCD, 0xEF, 0x00};
  MasterDataProxy<TestPacket, true, false> proxy(&input);

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  EXPECT_EQ(proxy.output()->fieldA, 0x78563412);
  EXPECT_EQ(proxy.output()->fieldB, 0xCDAB);
  EXPECT_EQ(proxy.output()->fieldC, 0xEF); // Single byte unchanged

  // Input unchanged
  EXPECT_EQ(input.fieldA, 0x12345678);
}

/* ----------------------------- Faults Only ----------------------------- */

/** @test MasterDataProxy with faults applies zero mask. */
TEST(MasterDataProxy, FaultsOnlyZeroMask) {
  std::uint32_t input = 0xFFFFFFFF;
  MasterDataProxy<std::uint32_t, false, true> proxy(&input);

  EXPECT_FALSE(proxy.swapRequired());
  EXPECT_TRUE(proxy.faultsSupported());
  EXPECT_TRUE(proxy.needsOverlay());

  // Push mask to zero first 2 bytes
  auto pushStatus = proxy.pushZeroMask(0, 2);
  EXPECT_EQ(pushStatus, FaultStatus::SUCCESS);
  EXPECT_EQ(proxy.maskCount(), 1u);

  // Enable faults
  proxy.setFaultsEnabled(true);
  EXPECT_TRUE(proxy.faultsEnabled());

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  // First 2 bytes should be zero
  auto* bytes = proxy.outputBytes();
  EXPECT_EQ(bytes[0], 0x00);
  EXPECT_EQ(bytes[1], 0x00);
  EXPECT_EQ(bytes[2], 0xFF);
  EXPECT_EQ(bytes[3], 0xFF);
}

/** @test MasterDataProxy faults not applied when disabled. */
TEST(MasterDataProxy, FaultsDisabledDoesNotApply) {
  std::uint32_t input = 0xFFFFFFFF;
  MasterDataProxy<std::uint32_t, false, true> proxy(&input);

  (void)proxy.pushZeroMask(0, 4);
  proxy.setFaultsEnabled(false); // Explicitly disabled

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  // No mask applied - all bytes still 0xFF (copied from input)
  EXPECT_EQ(*proxy.output(), 0xFFFFFFFF);
}

/** @test MasterDataProxy clear masks. */
TEST(MasterDataProxy, ClearMasks) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, false, true> proxy(&input);

  (void)proxy.pushZeroMask(0, 2);
  (void)proxy.pushHighMask(2, 2);
  EXPECT_EQ(proxy.maskCount(), 2u);

  proxy.clearMasks();
  EXPECT_EQ(proxy.maskCount(), 0u);
}

/** @test MasterDataProxy pop mask. */
TEST(MasterDataProxy, PopMask) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, false, true> proxy(&input);

  (void)proxy.pushZeroMask(0, 2);
  (void)proxy.pushHighMask(2, 2);
  EXPECT_EQ(proxy.maskCount(), 2u);

  proxy.popMask();
  EXPECT_EQ(proxy.maskCount(), 1u);
}

/* ----------------------------- Combined Swap + Faults ----------------------------- */

/** @test MasterDataProxy applies swap then faults. */
TEST(MasterDataProxy, SwapThenFaults) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, true, true> proxy(&input);

  EXPECT_TRUE(proxy.swapRequired());
  EXPECT_TRUE(proxy.faultsSupported());

  // After swap: 0x78563412
  // Zero first byte: 0x00563412
  (void)proxy.pushZeroMask(0, 1);
  proxy.setFaultsEnabled(true);

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);

  auto* bytes = proxy.outputBytes();
  // After swap, byte[0] was 0x78, now zeroed
  EXPECT_EQ(bytes[0], 0x00);
}

/* ----------------------------- Accessors ----------------------------- */

/** @test MasterDataProxy accessors return correct values. */
TEST(MasterDataProxy, Accessors) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, true, true> proxy(&input);

  EXPECT_EQ(proxy.input(), &input);
  EXPECT_NE(proxy.output(), &input); // Uses overlay
  EXPECT_EQ(proxy.size(), sizeof(std::uint32_t));
}

/** @test MasterDataProxy outputBytes returns byte pointer. */
TEST(MasterDataProxy, OutputBytes) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, false, false> proxy(&input);

  (void)proxy.resolve();

  auto* bytes = proxy.outputBytes();
  EXPECT_NE(bytes, nullptr);

  // Verify it points to the right data
  // (On little-endian: bytes[0]=0x78, bytes[3]=0x12)
  std::uint32_t reconstructed = 0;
  for (int i = 0; i < 4; ++i) {
    reconstructed |= static_cast<std::uint32_t>(bytes[i]) << (i * 8);
  }
  EXPECT_EQ(reconstructed, 0x12345678);
}

/* ----------------------------- Status ----------------------------- */

/** @test MasterDataProxy status table tracks results. */
TEST(MasterDataProxy, StatusTable) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, true, true> proxy(&input);

  auto status = proxy.resolve();
  EXPECT_EQ(status, MasterStatus::SUCCESS);
  EXPECT_EQ(proxy.masterStatus(), MasterStatus::SUCCESS);
  EXPECT_FALSE(proxy.anyProxyFailed());

  auto statusCopy = proxy.statusCopy();
  EXPECT_EQ(statusCopy[static_cast<std::size_t>(ProxySlot::MASTER)], 0);
  EXPECT_EQ(statusCopy[static_cast<std::size_t>(ProxySlot::ENDIAN)], 0);
}

/* ----------------------------- Compile-Time Queries ----------------------------- */

/** @test MasterDataProxy compile-time queries. */
TEST(MasterDataProxy, CompileTimeQueries) {
  // Passthrough
  {
    MasterDataProxy<std::uint32_t, false, false> proxy(nullptr);
    EXPECT_FALSE(proxy.swapRequired());
    EXPECT_FALSE(proxy.faultsSupported());
    EXPECT_FALSE(proxy.needsOverlay());
  }

  // Swap only
  {
    MasterDataProxy<std::uint32_t, true, false> proxy(nullptr);
    EXPECT_TRUE(proxy.swapRequired());
    EXPECT_FALSE(proxy.faultsSupported());
    EXPECT_TRUE(proxy.needsOverlay());
  }

  // Faults only
  {
    MasterDataProxy<std::uint32_t, false, true> proxy(nullptr);
    EXPECT_FALSE(proxy.swapRequired());
    EXPECT_TRUE(proxy.faultsSupported());
    EXPECT_TRUE(proxy.needsOverlay());
  }

  // Both
  {
    MasterDataProxy<std::uint32_t, true, true> proxy(nullptr);
    EXPECT_TRUE(proxy.swapRequired());
    EXPECT_TRUE(proxy.faultsSupported());
    EXPECT_TRUE(proxy.needsOverlay());
  }
}

/* ----------------------------- Multiple Resolves ----------------------------- */

/** @test MasterDataProxy can be resolved multiple times. */
TEST(MasterDataProxy, MultipleResolves) {
  std::uint32_t input = 0x12345678;
  MasterDataProxy<std::uint32_t, true, false> proxy(&input);

  // First resolve
  auto status1 = proxy.resolve();
  EXPECT_EQ(status1, MasterStatus::SUCCESS);
  EXPECT_EQ(*proxy.output(), 0x78563412);

  // Modify input
  input = 0xAABBCCDD;

  // Second resolve reflects new input
  auto status2 = proxy.resolve();
  EXPECT_EQ(status2, MasterStatus::SUCCESS);
  EXPECT_EQ(*proxy.output(), 0xDDCCBBAA);
}
