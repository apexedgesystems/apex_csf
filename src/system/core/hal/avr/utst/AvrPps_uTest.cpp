/**
 * @file AvrPps_uTest.cpp
 * @brief Mock-mode smoke tests for AvrPps.
 */

#define APEX_HAL_AVR_MOCK 1

#include "src/system/core/hal/avr/inc/AvrPps.hpp"

#include <gtest/gtest.h>

using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;
using apex::hal::avr::AvrPps;
using apex::hal::avr::AvrPpsOptions;

/** @test Default-constructed AvrPps reports uninitialized with zero pulses. */
TEST(AvrPps, DefaultUninitialized) {
  AvrPps pps;
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
}

/** @test init() with default config returns OK and marks the driver initialized. */
TEST(AvrPps, InitSucceeds) {
  AvrPps pps;
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
}

/** @test readCapture() before init() reports ERROR_NOT_INIT. */
TEST(AvrPps, ReadCaptureBeforeInit) {
  AvrPps pps;
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
}

/** @test At 16 MHz prescaler=1, 16 ticks = 1000 ns. */
TEST(AvrPps, ConvertsTicksToNsAt16MHzPrescaler1) {
  AvrPpsOptions opts;
  opts.prescaler = 1;
  AvrPps pps(opts);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(16); // 16 ticks * 62.5 ns = 1000 ns
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 1000);
}

/** @test At 16 MHz prescaler=64, 16 ticks = 64000 ns (16 * 64 / 16 us). */
TEST(AvrPps, ConvertsTicksToNsAt16MHzPrescaler64) {
  AvrPpsOptions opts;
  opts.prescaler = 64;
  AvrPps pps(opts);
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(16);
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 64'000);
}

/** @test Successfully consuming an edge clears the latched-flag for the next read. */
TEST(AvrPps, ConsumeClearsFlag) {
  AvrPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.mockEdge(100);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test FALLING-edge configuration is accepted by init(). */
TEST(AvrPps, FallingEdgeConfigStored) {
  AvrPps pps;
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  EXPECT_EQ(pps.init(cfg), PpsStatus::OK);
}

/** @test deinit() returns the driver to the uninitialized state. */
TEST(AvrPps, DeinitClearsInitialized) {
  AvrPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}
