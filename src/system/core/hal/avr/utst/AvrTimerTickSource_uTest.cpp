/**
 * @file AvrTimerTickSource_uTest.cpp
 * @brief Unit tests for AvrTimerTickSource (mock mode).
 */

#define APEX_HAL_AVR_MOCK 1

#include "src/system/core/hal/avr/inc/AvrTimerTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::avr::AvrTimerTickSource;
using executive::lite::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz source. */
TEST(AvrTimerTickSource, DefaultConstructionState) {
  AvrTimerTickSource source;

  EXPECT_EQ(source.tickFrequency(), 100U);
  EXPECT_EQ(source.currentTick(), 0U);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(AvrTimerTickSource, CustomFrequency) {
  AvrTimerTickSource source(200);

  EXPECT_EQ(source.tickFrequency(), 200U);
}

/** @test Explicit frequency constructor produces correct value. */
TEST(AvrTimerTickSource, ExplicitFrequency50Hz) {
  AvrTimerTickSource source(50);

  EXPECT_EQ(source.tickFrequency(), 50U);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() enables running. */
TEST(AvrTimerTickSource, StartEnablesRunning) {
  AvrTimerTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0U);
}

/** @test stop() disables running. */
TEST(AvrTimerTickSource, StopDisablesRunning) {
  AvrTimerTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test start() resets tick count. */
TEST(AvrTimerTickSource, StartResetsTick) {
  AvrTimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0U);
}

/** @test Multiple start/stop cycles work. */
TEST(AvrTimerTickSource, MultipleStartStopCycles) {
  AvrTimerTickSource source;

  source.start();
  source.waitForNextTick();
  EXPECT_EQ(source.currentTick(), 1U);

  source.stop();
  source.start();
  EXPECT_EQ(source.currentTick(), 0U);
  EXPECT_TRUE(source.isRunning());
}

/* ----------------------------- Tick Generation Tests ----------------------------- */

/** @test waitForNextTick() increments tick counter. */
TEST(AvrTimerTickSource, WaitIncrementsTick) {
  AvrTimerTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3U);
}

/** @test Tick count preserved after stop. */
TEST(AvrTimerTickSource, TickCountPreservedAfterStop) {
  AvrTimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2U);
}

/** @test Consistent increments over 100 iterations. */
TEST(AvrTimerTickSource, ConsistentIncrements) {
  AvrTimerTickSource source;
  source.start();

  for (int i = 0; i < 100; ++i) {
    const std::uint32_t BEFORE = source.currentTick();
    source.waitForNextTick();
    const std::uint32_t AFTER = source.currentTick();
    EXPECT_EQ(AFTER, BEFORE + 1) << "Increment failed at iteration " << i;
  }
}

/** @test waitForNextTick before start still increments (AVR mock has no guard). */
TEST(AvrTimerTickSource, WaitBeforeStartIncrements) {
  AvrTimerTickSource source;

  source.waitForNextTick();

  // AVR mock unconditionally increments (no running_ guard in mock path)
  EXPECT_EQ(source.currentTick(), 1U);
}

/* ----------------------------- Query Tests ----------------------------- */

/** @test tickFrequency() matches constructor argument. */
TEST(AvrTimerTickSource, TickFrequencyMatchesConstructor) {
  AvrTimerTickSource source(250);

  EXPECT_EQ(source.tickFrequency(), 250U);
}

/** @test tickPeriodUs() computes correctly for 100 Hz. */
TEST(AvrTimerTickSource, TickPeriodUs100Hz) {
  AvrTimerTickSource source(100);

  EXPECT_EQ(source.tickPeriodUs(), 10000U);
}

/** @test tickPeriodUs() computes correctly for 1000 Hz. */
TEST(AvrTimerTickSource, TickPeriodUs1000Hz) {
  AvrTimerTickSource source(1000);

  EXPECT_EQ(source.tickPeriodUs(), 1000U);
}

/** @test tickPeriodUs() computes correctly for 50 Hz. */
TEST(AvrTimerTickSource, TickPeriodUs50Hz) {
  AvrTimerTickSource source(50);

  EXPECT_EQ(source.tickPeriodUs(), 20000U);
}

/* ----------------------------- Interface Tests ----------------------------- */

/** @test Implements ITickSource interface via base pointer. */
TEST(AvrTimerTickSource, ImplementsITickSource) {
  AvrTimerTickSource source(50);
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
TEST(AvrTimerTickSource, AckTickIsNoOp) {
  AvrTimerTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1U);
}

/** @test Multiple ackTick calls are harmless. */
TEST(AvrTimerTickSource, MultipleAckTickCalls) {
  AvrTimerTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();
  source.ackTick();
  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1U);
}
