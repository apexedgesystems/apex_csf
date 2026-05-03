/**
 * @file C2000Pps_uTest.cpp
 * @brief Mock-mode smoke tests for C2000Pps.
 */

#define APEX_HAL_C2000_MOCK 1

#include "src/system/core/hal/c2000/inc/C2000Pps.hpp"

#include <gtest/gtest.h>

using apex::hal::c2000::C2000_PPS_ERROR_NOT_INIT;
using apex::hal::c2000::C2000_PPS_NO_NEW_EDGE;
using apex::hal::c2000::C2000_PPS_OK;
using apex::hal::c2000::C2000Pps;

TEST(C2000Pps, DefaultUninitialized) {
  C2000Pps pps;
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
}

TEST(C2000Pps, InitSucceeds) {
  C2000Pps pps;
  EXPECT_EQ(pps.init(1), C2000_PPS_OK);
  EXPECT_TRUE(pps.isInitialized());
}

TEST(C2000Pps, ReadCaptureBeforeInit) {
  C2000Pps pps;
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), C2000_PPS_ERROR_NOT_INIT);
}

/** @test At 100 MHz CPU, 100 ticks = 1000 ns (10 ns per tick). */
TEST(C2000Pps, ConvertsTicksToNsAt100MHz) {
  C2000Pps pps(0, 100'000'000U);
  ASSERT_EQ(pps.init(1), C2000_PPS_OK);
  pps.mockEdge(100);
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), C2000_PPS_OK);
  EXPECT_EQ(ts, 1000);
  EXPECT_EQ(pps.pulseCount(), 1U);
}

/** @test At 200 MHz CPU, 1000 ticks = 5000 ns (5 ns per tick). */
TEST(C2000Pps, ConvertsTicksToNsAt200MHz) {
  C2000Pps pps(0, 200'000'000U);
  ASSERT_EQ(pps.init(1), C2000_PPS_OK);
  pps.mockEdge(1000);
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), C2000_PPS_OK);
  EXPECT_EQ(ts, 5000);
}

TEST(C2000Pps, ConsumeClearsFlag) {
  C2000Pps pps;
  ASSERT_EQ(pps.init(1), C2000_PPS_OK);
  pps.mockEdge(1234);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), C2000_PPS_OK);
  EXPECT_EQ(pps.readCapture(ts), C2000_PPS_NO_NEW_EDGE);
}

TEST(C2000Pps, DeinitClearsInitialized) {
  C2000Pps pps;
  ASSERT_EQ(pps.init(1), C2000_PPS_OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}
