/**
 * @file PicoPps_uTest.cpp
 * @brief Mock-mode smoke tests for PicoPps.
 */

#define APEX_HAL_PICO_MOCK 1

#include "src/system/core/hal/pico/inc/PicoPps.hpp"

#include <gtest/gtest.h>

using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;
using apex::hal::pico::PicoPps;

/** @test Default construction leaves the instance uninitialized. */
TEST(PicoPps, DefaultUninitialized) {
  PicoPps pps(0);
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
}

/** @test init succeeds in mock mode. */
TEST(PicoPps, InitSucceeds) {
  PicoPps pps(15);
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
}

/** @test readCapture before init returns ERROR_NOT_INIT. */
TEST(PicoPps, ReadCaptureBeforeInit) {
  PicoPps pps(15);
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
}

/** @test mockEdge converts microseconds to nanoseconds. */
TEST(PicoPps, MockEdgeConvertsUsToNs) {
  PicoPps pps(15);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(12345); // 12345 us = 12_345_000 ns

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 12'345'000);
  EXPECT_EQ(pps.pulseCount(), 1U);
}

/** @test Consuming an edge clears the new-edge flag. */
TEST(PicoPps, ConsumeClearsFlag) {
  PicoPps pps(15);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(1000);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test FALLING edge config doesn't crash mock init. */
TEST(PicoPps, FallingEdgeConfigStored) {
  PicoPps pps(15);
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  EXPECT_EQ(pps.init(cfg), PpsStatus::OK);
}

/** @test deinit clears the initialized flag. */
TEST(PicoPps, DeinitClearsInitialized) {
  PicoPps pps(15);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}
