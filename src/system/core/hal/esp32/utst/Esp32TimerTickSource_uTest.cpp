/**
 * @file Esp32TimerTickSource_uTest.cpp
 * @brief Unit tests for Esp32TimerTickSource (mock mode).
 */

#define APEX_HAL_ESP32_MOCK 1

#include "src/system/core/hal/esp32/inc/Esp32TimerTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::esp32::Esp32TimerTickSource;
using executive::lite::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz source. */
TEST(Esp32TimerTickSource, DefaultConstruction) {
  Esp32TimerTickSource source;

  EXPECT_EQ(source.tickFrequency(), 100U);
  EXPECT_EQ(source.currentTick(), 0U);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(Esp32TimerTickSource, CustomFrequency) {
  Esp32TimerTickSource source(200);

  EXPECT_EQ(source.tickFrequency(), 200U);
}

/** @test Zero Hz is clamped to 1 Hz (defensive). */
TEST(Esp32TimerTickSource, ZeroHzClampedTo1Hz) {
  Esp32TimerTickSource source(0);

  EXPECT_EQ(source.tickFrequency(), 1U);
}

/* ----------------------------- Start/Stop Tests ----------------------------- */

/** @test start() enables running. */
TEST(Esp32TimerTickSource, StartEnablesRunning) {
  Esp32TimerTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0U);
}

/** @test stop() disables running. */
TEST(Esp32TimerTickSource, StopDisablesRunning) {
  Esp32TimerTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test start() resets tick count. */
TEST(Esp32TimerTickSource, StartResetsTick) {
  Esp32TimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0U);
}

/** @test Multiple start/stop cycles work. */
TEST(Esp32TimerTickSource, MultipleStartStopCycles) {
  Esp32TimerTickSource source;

  source.start();
  source.waitForNextTick();
  EXPECT_EQ(source.currentTick(), 1U);

  source.stop();
  source.start();
  EXPECT_EQ(source.currentTick(), 0U);
  EXPECT_TRUE(source.isRunning());
}

/** @test Double start resets tick count (stop + restart). */
TEST(Esp32TimerTickSource, DoubleStartResets) {
  Esp32TimerTickSource source;

  source.start();
  source.waitForNextTick();
  EXPECT_EQ(source.currentTick(), 1U);

  // Second start stops and restarts, resetting tick count
  source.start();
  EXPECT_EQ(source.currentTick(), 0U);
  EXPECT_TRUE(source.isRunning());
}

/** @test Double stop is safe. */
TEST(Esp32TimerTickSource, DoubleStopSafe) {
  Esp32TimerTickSource source;
  source.start();

  source.stop();
  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/* ----------------------------- Tick Generation Tests ----------------------------- */

/** @test waitForNextTick() increments tick when running. */
TEST(Esp32TimerTickSource, WaitIncrementsTick) {
  Esp32TimerTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3U);
}

/** @test waitForNextTick() does not increment when stopped. */
TEST(Esp32TimerTickSource, WaitDoesNotIncrementWhenStopped) {
  Esp32TimerTickSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0U);
}

/** @test Tick count preserved after stop. */
TEST(Esp32TimerTickSource, TickCountPreservedAfterStop) {
  Esp32TimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2U);
}

/** @test Consistent increments over 100 iterations. */
TEST(Esp32TimerTickSource, ConsistentIncrements) {
  Esp32TimerTickSource source;
  source.start();

  for (int i = 0; i < 100; ++i) {
    const std::uint32_t BEFORE = source.currentTick();
    source.waitForNextTick();
    const std::uint32_t AFTER = source.currentTick();
    EXPECT_EQ(AFTER, BEFORE + 1) << "Increment failed at iteration " << i;
  }
}

/* ----------------------------- Query Tests ----------------------------- */

/** @test tickFrequency() matches constructor argument. */
TEST(Esp32TimerTickSource, FrequencyMatchesConstructor) {
  Esp32TimerTickSource source(250);

  EXPECT_EQ(source.tickFrequency(), 250U);
}

/** @test tickPeriodUs() computes correctly for 100 Hz. */
TEST(Esp32TimerTickSource, TickPeriodUs100Hz) {
  Esp32TimerTickSource source(100);

  EXPECT_EQ(source.tickPeriodUs(), 10000U);
}

/** @test tickPeriodUs() computes correctly for 1000 Hz. */
TEST(Esp32TimerTickSource, TickPeriodUs1000Hz) {
  Esp32TimerTickSource source(1000);

  EXPECT_EQ(source.tickPeriodUs(), 1000U);
}

/* ----------------------------- Interface Tests ----------------------------- */

/** @test Implements ITickSource interface via base pointer. */
TEST(Esp32TimerTickSource, ImplementsITickSource) {
  Esp32TimerTickSource source(50);
  ITickSource* iface = &source;

  EXPECT_EQ(iface->tickFrequency(), 50U);
  EXPECT_EQ(iface->tickPeriodUs(), 20000U);
  EXPECT_FALSE(iface->isRunning());

  iface->start();
  EXPECT_TRUE(iface->isRunning());

  iface->waitForNextTick();
  EXPECT_EQ(iface->currentTick(), 1U);

  iface->stop();
  EXPECT_FALSE(iface->isRunning());
}

/** @test ackTick() is callable and does not crash. */
TEST(Esp32TimerTickSource, AckTickIsNoOp) {
  Esp32TimerTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1U);
}
