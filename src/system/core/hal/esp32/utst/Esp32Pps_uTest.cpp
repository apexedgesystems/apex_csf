/**
 * @file Esp32Pps_uTest.cpp
 * @brief Mock-mode smoke tests for Esp32Pps.
 */

#define APEX_HAL_ESP32_MOCK 1

#include "src/system/core/hal/esp32/inc/Esp32Pps.hpp"

#include <gtest/gtest.h>

using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;
using apex::hal::esp32::Esp32Pps;

TEST(Esp32Pps, DefaultUninitialized) {
  Esp32Pps pps(0);
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
}

TEST(Esp32Pps, InitSucceeds) {
  Esp32Pps pps(4);
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
}

TEST(Esp32Pps, ReadCaptureBeforeInit) {
  Esp32Pps pps(4);
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
}

TEST(Esp32Pps, MockEdgeConvertsUsToNs) {
  Esp32Pps pps(4);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(7777); // 7777 us = 7_777_000 ns
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 7'777'000);
  EXPECT_EQ(pps.pulseCount(), 1U);
}

TEST(Esp32Pps, ConsumeClearsFlag) {
  Esp32Pps pps(4);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(100);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

TEST(Esp32Pps, FallingEdgeConfigStored) {
  Esp32Pps pps(4);
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  EXPECT_EQ(pps.init(cfg), PpsStatus::OK);
}

TEST(Esp32Pps, DeinitClearsInitialized) {
  Esp32Pps pps(4);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}
