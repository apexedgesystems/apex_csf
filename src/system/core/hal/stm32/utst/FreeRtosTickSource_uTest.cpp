/**
 * @file FreeRtosTickSource_uTest.cpp
 * @brief Unit tests for FreeRtosTickSource (mock mode).
 *
 * Tests run on host with APEX_HAL_STM32_MOCK defined.
 * FreeRTOS calls are stubbed; tick counting and state transitions are verified.
 */

#include "src/system/core/hal/stm32/inc/FreeRtosTickSource.hpp"

#include <gtest/gtest.h>

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz tick source. */
TEST(FreeRtosTickSource, DefaultConstruction) {
  apex::hal::stm32::FreeRtosTickSource ts;
  EXPECT_EQ(ts.tickFrequency(), 100U);
  EXPECT_EQ(ts.currentTick(), 0U);
  EXPECT_FALSE(ts.isRunning());
}

/** @test Custom frequency is stored. */
TEST(FreeRtosTickSource, CustomFrequency) {
  apex::hal::stm32::FreeRtosTickSource ts(200);
  EXPECT_EQ(ts.tickFrequency(), 200U);
}

/** @test Zero frequency is clamped to 1 Hz. */
TEST(FreeRtosTickSource, ZeroFrequencyClamped) {
  apex::hal::stm32::FreeRtosTickSource ts(0);
  EXPECT_EQ(ts.tickFrequency(), 1U);
}

/** @test 1 Hz frequency produces period = 1000 ticks (mock assumes 1 kHz). */
TEST(FreeRtosTickSource, OneHzPeriod) {
  apex::hal::stm32::FreeRtosTickSource ts(1);
  EXPECT_EQ(ts.periodTicks(), 1000U);
}

/** @test 100 Hz frequency produces period = 10 ticks. */
TEST(FreeRtosTickSource, HundredHzPeriod) {
  apex::hal::stm32::FreeRtosTickSource ts(100);
  EXPECT_EQ(ts.periodTicks(), 10U);
}

/** @test 1000 Hz frequency produces period = 1 tick. */
TEST(FreeRtosTickSource, OneKhzPeriod) {
  apex::hal::stm32::FreeRtosTickSource ts(1000);
  EXPECT_EQ(ts.periodTicks(), 1U);
}

/** @test Frequency above 1 kHz clamps period to 1 tick. */
TEST(FreeRtosTickSource, AboveMaxFrequencyClampsPeriod) {
  apex::hal::stm32::FreeRtosTickSource ts(5000);
  EXPECT_EQ(ts.periodTicks(), 1U);
}

/* ----------------------------- Start / Stop ----------------------------- */

/** @test Start sets running state. */
TEST(FreeRtosTickSource, StartSetsRunning) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();
  EXPECT_TRUE(ts.isRunning());
}

/** @test Stop clears running state. */
TEST(FreeRtosTickSource, StopClearsRunning) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();
  ts.stop();
  EXPECT_FALSE(ts.isRunning());
}

/** @test Start resets tick count to zero. */
TEST(FreeRtosTickSource, StartResetsTicks) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();
  ts.waitForNextTick();
  ts.waitForNextTick();
  EXPECT_EQ(ts.currentTick(), 2U);

  ts.start();
  EXPECT_EQ(ts.currentTick(), 0U);
}

/** @test Multiple start/stop cycles work correctly. */
TEST(FreeRtosTickSource, MultipleStartStopCycles) {
  apex::hal::stm32::FreeRtosTickSource ts;

  for (int i = 0; i < 5; ++i) {
    ts.start();
    EXPECT_TRUE(ts.isRunning());
    EXPECT_EQ(ts.currentTick(), 0U);
    ts.waitForNextTick();
    EXPECT_EQ(ts.currentTick(), 1U);
    ts.stop();
    EXPECT_FALSE(ts.isRunning());
  }
}

/* ----------------------------- Tick Counting ----------------------------- */

/** @test waitForNextTick increments tick count when running. */
TEST(FreeRtosTickSource, WaitIncrementsTickCount) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();

  for (uint32_t i = 1; i <= 10; ++i) {
    ts.waitForNextTick();
    EXPECT_EQ(ts.currentTick(), i);
  }
}

/** @test waitForNextTick does nothing when stopped. */
TEST(FreeRtosTickSource, WaitDoesNothingWhenStopped) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.waitForNextTick();
  EXPECT_EQ(ts.currentTick(), 0U);
}

/** @test waitForNextTick does nothing after stop. */
TEST(FreeRtosTickSource, WaitDoesNothingAfterStop) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();
  ts.waitForNextTick();
  ts.waitForNextTick();
  EXPECT_EQ(ts.currentTick(), 2U);

  ts.stop();
  ts.waitForNextTick();
  ts.waitForNextTick();
  EXPECT_EQ(ts.currentTick(), 2U);
}

/** @test Tick count is monotonic. */
TEST(FreeRtosTickSource, TickCountMonotonic) {
  apex::hal::stm32::FreeRtosTickSource ts;
  ts.start();

  uint32_t prev = ts.currentTick();
  for (int i = 0; i < 100; ++i) {
    ts.waitForNextTick();
    uint32_t curr = ts.currentTick();
    EXPECT_GT(curr, prev);
    prev = curr;
  }
}

/* ----------------------------- ITickSource Interface ----------------------------- */

/** @test Verify ITickSource pointer works (polymorphism). */
TEST(FreeRtosTickSource, PolymorphicAccess) {
  apex::hal::stm32::FreeRtosTickSource ts(50);
  executive::lite::ITickSource* iface = &ts;

  EXPECT_EQ(iface->tickFrequency(), 50U);
  EXPECT_FALSE(iface->isRunning());

  iface->start();
  EXPECT_TRUE(iface->isRunning());
  EXPECT_EQ(iface->currentTick(), 0U);

  iface->waitForNextTick();
  EXPECT_EQ(iface->currentTick(), 1U);

  iface->stop();
  EXPECT_FALSE(iface->isRunning());
}
