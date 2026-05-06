/**
 * @file Stm32Pps_uTest.cpp
 * @brief Mock-mode smoke tests for Stm32Pps.
 *
 * These tests exercise the IPps lifecycle and the mockEdge() seam to
 * confirm the header compiles cleanly on the host (APEX_HAL_STM32_MOCK
 * defined) and that the cycles -> nanoseconds conversion math works.
 *
 * Real EXTI / NVIC / DWT behavior is exercised on hardware, not in
 * these host-side unit tests.
 */

#define APEX_HAL_STM32_MOCK 1

#include "src/system/core/hal/stm32/inc/Stm32Pps.hpp"

#include <gtest/gtest.h>

using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;
using apex::hal::stm32::Stm32Pps;
using apex::hal::stm32::Stm32PpsOptions;

namespace {
Stm32Pps makeMock(uint32_t coreFreqHz = 80'000'000U) {
  Stm32PpsOptions opts;
  opts.coreFreqHz = coreFreqHz;
  return Stm32Pps(nullptr, 0, 0, opts);
}
} // namespace

/** @test Default construction leaves the impl uninitialized. */
TEST(Stm32Pps, DefaultUninitialized) {
  Stm32Pps pps = makeMock();
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
}

/** @test init succeeds and flips the flag in mock mode. */
TEST(Stm32Pps, InitSucceeds) {
  Stm32Pps pps = makeMock();
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
}

/** @test readCapture before init returns ERROR_NOT_INIT. */
TEST(Stm32Pps, ReadCaptureBeforeInit) {
  Stm32Pps pps = makeMock();
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
  EXPECT_EQ(ts, -1);
}

/** @test readCapture without a staged edge returns NO_NEW_EDGE. */
TEST(Stm32Pps, NoEdgeReturnsNoNewEdge) {
  Stm32Pps pps = makeMock();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test mockEdge(cycles) reports cycles converted to nanoseconds. */
TEST(Stm32Pps, MockEdgeConvertsCyclesToNs) {
  // 80 MHz core = 12.5 ns per cycle; 8 cycles = 100 ns.
  Stm32Pps pps = makeMock(80'000'000U);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(8);

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 100);
  EXPECT_EQ(pps.pulseCount(), 1U);
}

/** @test Conversion math at a higher core frequency. */
TEST(Stm32Pps, MockEdgeConvertsCyclesToNs200MHz) {
  // 200 MHz core = 5 ns per cycle; 1000 cycles = 5000 ns.
  Stm32Pps pps = makeMock(200'000'000U);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(1000);

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 5000);
}

/** @test Consuming an edge clears the new-edge flag. */
TEST(Stm32Pps, ConsumeClearsFlag) {
  Stm32Pps pps = makeMock();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(160);

  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test pulseCount counts every staged edge, even unconsumed ones. */
TEST(Stm32Pps, PulseCountTracksEvenUnconsumed) {
  Stm32Pps pps = makeMock();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(10);
  pps.mockEdge(20);
  pps.mockEdge(30); // consumer fell behind; only the latest is latched

  EXPECT_EQ(pps.pulseCount(), 3U);

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test deinit clears the initialized flag. */
TEST(Stm32Pps, DeinitClearsInitialized) {
  Stm32Pps pps = makeMock();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}

/** @test FALLING-edge config is preserved across init. */
TEST(Stm32Pps, FallingConfigStored) {
  Stm32Pps pps = makeMock();
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  EXPECT_EQ(pps.init(cfg), PpsStatus::OK);
}
