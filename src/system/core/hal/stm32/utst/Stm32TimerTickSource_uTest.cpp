/**
 * @file Stm32TimerTickSource_uTest.cpp
 * @brief Unit tests for Stm32TimerTickSource (mock mode).
 */

#include "src/system/core/hal/stm32/inc/Stm32TimerTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::stm32::Stm32TimerTickSource;
using executive::lite::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz source. */
TEST(Stm32TimerTickSource_DefaultConstruction, InitialState) {
  Stm32TimerTickSource source;

  EXPECT_EQ(source.tickFrequency(), 100);
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(Stm32TimerTickSource_DefaultConstruction, CustomFrequency) {
  Stm32TimerTickSource source(nullptr, 500);

  EXPECT_EQ(source.tickFrequency(), 500);
}

/** @test 0 Hz is clamped to 1 Hz (defensive). */
TEST(Stm32TimerTickSource_DefaultConstruction, ZeroHzClampedTo1Hz) {
  Stm32TimerTickSource source(nullptr, 0);

  EXPECT_EQ(source.tickFrequency(), 1);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() enables running. */
TEST(Stm32TimerTickSource_Lifecycle, StartEnablesRunning) {
  Stm32TimerTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test stop() disables running. */
TEST(Stm32TimerTickSource_Lifecycle, StopDisablesRunning) {
  Stm32TimerTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test start() resets tick count. */
TEST(Stm32TimerTickSource_Lifecycle, StartResetsTick) {
  Stm32TimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Multiple start/stop cycles work. */
TEST(Stm32TimerTickSource_Lifecycle, MultipleStartsReset) {
  Stm32TimerTickSource source;

  source.start();
  source.waitForNextTick();
  EXPECT_EQ(source.currentTick(), 1);

  source.stop();
  source.start();
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_TRUE(source.isRunning());
}

/** @test stop() from clean state does not crash. */
TEST(Stm32TimerTickSource_Lifecycle, StopFromCleanState) {
  Stm32TimerTickSource source;

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/* ----------------------------- Tick Generation Tests ----------------------------- */

/** @test waitForNextTick() increments tick when running. */
TEST(Stm32TimerTickSource_TickGeneration, WaitIncrementsTickWhenRunning) {
  Stm32TimerTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3);
}

/** @test waitForNextTick() does not increment tick when stopped. */
TEST(Stm32TimerTickSource_TickGeneration, WaitDoesNotIncrementWhenStopped) {
  Stm32TimerTickSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Tick count preserved after stop. */
TEST(Stm32TimerTickSource_TickGeneration, TickCountPreservedAfterStop) {
  Stm32TimerTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2);
}

/** @test Consistent increments over 100 iterations. */
TEST(Stm32TimerTickSource_TickGeneration, ConsistentIncrements) {
  Stm32TimerTickSource source;
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
TEST(Stm32TimerTickSource_Query, TickFrequencyMatchesConstructor) {
  Stm32TimerTickSource source(nullptr, 250);

  EXPECT_EQ(source.tickFrequency(), 250);
}

/** @test tickPeriodUs() computes correctly for 100 Hz. */
TEST(Stm32TimerTickSource_Query, TickPeriodUsCalculation) {
  Stm32TimerTickSource source(nullptr, 100);

  EXPECT_EQ(source.tickPeriodUs(), 10000);
}

/** @test tickPeriodUs() computes correctly for 1000 Hz. */
TEST(Stm32TimerTickSource_Query, TickPeriodUs1000Hz) {
  Stm32TimerTickSource source(nullptr, 1000);

  EXPECT_EQ(source.tickPeriodUs(), 1000);
}

/* ----------------------------- Interface Tests ----------------------------- */

/** @test Implements ITickSource interface via base pointer. */
TEST(Stm32TimerTickSource_Interface, ImplementsITickSource) {
  Stm32TimerTickSource source(nullptr, 50);
  ITickSource* iface = &source;

  EXPECT_EQ(iface->tickFrequency(), 50);
  EXPECT_EQ(iface->tickPeriodUs(), 20000);
  EXPECT_FALSE(iface->isRunning());

  iface->start();
  EXPECT_TRUE(iface->isRunning());

  iface->waitForNextTick();
  EXPECT_EQ(iface->currentTick(), 1);

  iface->stop();
  EXPECT_FALSE(iface->isRunning());
}

/** @test ackTick() is callable and does not crash. */
TEST(Stm32TimerTickSource_Interface, AckTickIsNoOp) {
  Stm32TimerTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1);
}

/** @test isrCallback is callable in mock mode (no-op). */
TEST(Stm32TimerTickSource_Interface, IsrCallbackNoOpInMock) {
  Stm32TimerTickSource::isrCallback();

  // Should not crash or affect state
  Stm32TimerTickSource source;
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test nullptr timer in mock mode does not crash. */
TEST(Stm32TimerTickSource_TimerSpecific, NullTimerInMock) {
  Stm32TimerTickSource source(nullptr, 100);

  source.start();
  source.waitForNextTick();
  source.stop();

  EXPECT_EQ(source.currentTick(), 1);
}
