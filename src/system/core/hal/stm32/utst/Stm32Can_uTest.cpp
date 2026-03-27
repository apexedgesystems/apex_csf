/**
 * @file Stm32Can_uTest.cpp
 * @brief Unit tests for Stm32Can implementation (using mock mode).
 *
 * These tests run on the host by defining APEX_HAL_STM32_MOCK,
 * which removes STM32 HAL dependencies and allows testing the
 * buffer logic and interface compliance.
 */

#define APEX_HAL_STM32_MOCK 1

#include "src/system/core/hal/stm32/inc/Stm32Can.hpp"

#include <gtest/gtest.h>

using apex::hal::CanConfig;
using apex::hal::CanFilter;
using apex::hal::CanFrame;
using apex::hal::CanMode;
using apex::hal::CanStats;
using apex::hal::CanStatus;
using apex::hal::stm32::Stm32Can;
using apex::hal::stm32::Stm32CanOptions;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify Stm32Can can be default constructed in mock mode. */
TEST(Stm32Can, DefaultConstruction) {
  Stm32Can<16> can;

  EXPECT_FALSE(can.isInitialized());
  EXPECT_EQ(can.rxAvailable(), 0U);
  EXPECT_FALSE(can.isBusOff());
}

/** @test Verify different buffer sizes compile. */
TEST(Stm32Can, DifferentBufferSizes) {
  Stm32Can<4> small;
  Stm32Can<32> medium;
  Stm32Can<64> large;

  EXPECT_EQ(small.rxCapacity(), 3U);
  EXPECT_EQ(medium.rxCapacity(), 31U);
  EXPECT_EQ(large.rxCapacity(), 63U);
}

/* ----------------------------- Init/Deinit Tests ----------------------------- */

/** @test Verify init succeeds in mock mode. */
TEST(Stm32Can, InitSucceeds) {
  Stm32Can<16> can;
  CanConfig config;
  config.bitrate = 500000;

  const CanStatus STATUS = can.init(config);

  EXPECT_EQ(STATUS, CanStatus::OK);
  EXPECT_TRUE(can.isInitialized());
}

/** @test Verify init with loopback mode succeeds. */
TEST(Stm32Can, InitLoopbackMode) {
  Stm32Can<16> can;
  CanConfig config;
  config.bitrate = 1000000;
  config.mode = CanMode::LOOPBACK;

  const CanStatus STATUS = can.init(config);

  EXPECT_EQ(STATUS, CanStatus::OK);
  EXPECT_TRUE(can.isInitialized());
}

/** @test Verify deinit resets state. */
TEST(Stm32Can, DeinitResetsState) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  can.deinit();

  EXPECT_FALSE(can.isInitialized());
  EXPECT_EQ(can.rxAvailable(), 0U);
}

/** @test Verify multiple init/deinit cycles work. */
TEST(Stm32Can, MultipleInitDeinitCycles) {
  Stm32Can<16> can;
  CanConfig config;

  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(can.init(config), CanStatus::OK);
    EXPECT_TRUE(can.isInitialized());
    can.deinit();
    EXPECT_FALSE(can.isInitialized());
  }
}

/** @test Verify double init reinitializes cleanly (double-init guard). */
TEST(Stm32Can, DoubleInitReinitializes) {
  Stm32Can<16> can;
  CanConfig config;
  config.bitrate = 500000;

  EXPECT_EQ(can.init(config), CanStatus::OK);
  EXPECT_TRUE(can.isInitialized());

  // Init again without explicit deinit -- should succeed
  config.bitrate = 250000;
  EXPECT_EQ(can.init(config), CanStatus::OK);
  EXPECT_TRUE(can.isInitialized());
}

/* ----------------------------- Send Tests ----------------------------- */

/** @test Verify send returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Can, SendNotInitialized) {
  Stm32Can<16> can;
  CanFrame frame;
  frame.canId.id = 0x123;
  frame.dlc = 1;
  frame.data[0] = 0xAB;

  const CanStatus STATUS = can.send(frame);

  EXPECT_EQ(STATUS, CanStatus::ERROR_NOT_INIT);
}

/** @test Verify send succeeds in mock mode. */
TEST(Stm32Can, SendSucceeds) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x321;
  frame.dlc = 2;
  frame.data[0] = 0xAB;
  frame.data[1] = 0xCD;

  const CanStatus STATUS = can.send(frame);

  EXPECT_EQ(STATUS, CanStatus::OK);
  EXPECT_EQ(can.stats().framesTx, 1U);
}

/** @test Verify send rejects DLC > 8. */
TEST(Stm32Can, SendInvalidDlc) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x123;
  frame.dlc = 9;

  const CanStatus STATUS = can.send(frame);

  EXPECT_EQ(STATUS, CanStatus::ERROR_INVALID_ARG);
}

/** @test Verify send with DLC=0 succeeds. */
TEST(Stm32Can, SendZeroDlc) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x100;
  frame.dlc = 0;

  const CanStatus STATUS = can.send(frame);

  EXPECT_EQ(STATUS, CanStatus::OK);
  EXPECT_EQ(can.stats().framesTx, 1U);
}

/** @test Verify send with extended ID succeeds. */
TEST(Stm32Can, SendExtendedId) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x1FFFFFFF;
  frame.canId.extended = true;
  frame.dlc = 4;

  const CanStatus STATUS = can.send(frame);

  EXPECT_EQ(STATUS, CanStatus::OK);
}

/* ----------------------------- Recv Tests ----------------------------- */

/** @test Verify recv returns ERROR_NOT_INIT when not initialized. */
TEST(Stm32Can, RecvNotInitialized) {
  Stm32Can<16> can;
  CanFrame frame;

  const CanStatus STATUS = can.recv(frame);

  EXPECT_EQ(STATUS, CanStatus::ERROR_NOT_INIT);
}

/** @test Verify recv returns WOULD_BLOCK when buffer empty. */
TEST(Stm32Can, RecvEmptyBuffer) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  const CanStatus STATUS = can.recv(frame);

  EXPECT_EQ(STATUS, CanStatus::WOULD_BLOCK);
  EXPECT_EQ(can.rxAvailable(), 0U);
}

/* ----------------------------- Filter Tests ----------------------------- */

/** @test Verify addFilter succeeds. */
TEST(Stm32Can, AddFilter) {
  Stm32Can<16> can;

  CanFilter filter = {0x123, 0x7FF, false};
  const CanStatus STATUS = can.addFilter(filter);

  EXPECT_EQ(STATUS, CanStatus::OK);
}

/** @test Verify addFilter rejects when full. */
TEST(Stm32Can, AddFilterFull) {
  Stm32Can<16> can;
  CanFilter filter = {0x100, 0x7FF, false};

  for (size_t i = 0; i < Stm32Can<16>::MAX_FILTERS; ++i) {
    EXPECT_EQ(can.addFilter(filter), CanStatus::OK);
  }

  const CanStatus STATUS = can.addFilter(filter);
  EXPECT_EQ(STATUS, CanStatus::ERROR_INVALID_ARG);
}

/** @test Verify clearFilters resets filter count. */
TEST(Stm32Can, ClearFilters) {
  Stm32Can<16> can;
  CanFilter filter = {0x100, 0x7FF, false};

  (void)can.addFilter(filter);
  (void)can.addFilter(filter);
  can.clearFilters();

  // Should be able to add MAX_FILTERS again
  for (size_t i = 0; i < Stm32Can<16>::MAX_FILTERS; ++i) {
    EXPECT_EQ(can.addFilter(filter), CanStatus::OK);
  }
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test Verify initial stats are zero. */
TEST(Stm32Can, InitialStatsZero) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  const auto& STATS = can.stats();

  EXPECT_EQ(STATS.framesTx, 0U);
  EXPECT_EQ(STATS.framesRx, 0U);
  EXPECT_EQ(STATS.totalErrors(), 0U);
}

/** @test Verify send increments framesTx. */
TEST(Stm32Can, SendIncrementsStats) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x100;
  frame.dlc = 1;
  frame.data[0] = 0xFF;

  (void)can.send(frame);
  (void)can.send(frame);
  (void)can.send(frame);

  EXPECT_EQ(can.stats().framesTx, 3U);
}

/** @test Verify resetStats clears counters. */
TEST(Stm32Can, ResetStats) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  CanFrame frame;
  frame.canId.id = 0x100;
  frame.dlc = 1;
  (void)can.send(frame);

  EXPECT_EQ(can.stats().framesTx, 1U);

  can.resetStats();

  EXPECT_EQ(can.stats().framesTx, 0U);
  EXPECT_EQ(can.stats().framesRx, 0U);
}

/* ----------------------------- Capacity Tests ----------------------------- */

/** @test Verify rxCapacity returns buffer size minus 1. */
TEST(Stm32Can, RxCapacity) {
  EXPECT_EQ((Stm32Can<16>::rxCapacity()), 15U);
  EXPECT_EQ((Stm32Can<8>::rxCapacity()), 7U);
  EXPECT_EQ((Stm32Can<32>::rxCapacity()), 31U);
}

/* ----------------------------- Stm32CanOptions Tests ----------------------------- */

/** @test Verify Stm32CanOptions defaults to priority 1. */
TEST(Stm32CanOptions, DefaultValues) {
  Stm32CanOptions opts;

  EXPECT_EQ(opts.nvicPreemptPriority, 1U);
  EXPECT_EQ(opts.nvicSubPriority, 0U);
}

/** @test Verify Stm32CanOptions can be aggregate-initialized. */
TEST(Stm32CanOptions, AggregateInit) {
  Stm32CanOptions opts = {3, 1};

  EXPECT_EQ(opts.nvicPreemptPriority, 3U);
  EXPECT_EQ(opts.nvicSubPriority, 1U);
}

/** @test Verify init accepts Stm32CanOptions in mock mode. */
TEST(Stm32Can, InitWithOptions) {
  Stm32Can<16> can;
  CanConfig config;
  config.bitrate = 500000;
  Stm32CanOptions opts = {2, 0};

  const CanStatus STATUS = can.init(config, opts);

  EXPECT_EQ(STATUS, CanStatus::OK);
  EXPECT_TRUE(can.isInitialized());
}

/* ----------------------------- txReady Tests ----------------------------- */

/** @test Verify txReady returns false when not initialized. */
TEST(Stm32Can, TxReadyNotInit) {
  Stm32Can<16> can;
  EXPECT_FALSE(can.txReady());
}

/** @test Verify txReady returns true when initialized in mock mode. */
TEST(Stm32Can, TxReadyAfterInit) {
  Stm32Can<16> can;
  CanConfig config;
  static_cast<void>(can.init(config));

  EXPECT_TRUE(can.txReady());
}
