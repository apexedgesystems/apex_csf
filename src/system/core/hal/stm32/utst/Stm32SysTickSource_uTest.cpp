/**
 * @file Stm32SysTickSource_uTest.cpp
 * @brief Unit tests for Stm32SysTickSource (mock mode).
 */

#include "src/system/core/hal/stm32/inc/Stm32SysTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::stm32::Stm32SysTickSource;
using executive::mcu::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz source. */
TEST(Stm32SysTickSource_DefaultConstruction, InitialState) {
  Stm32SysTickSource source;

  EXPECT_EQ(source.tickFrequency(), 100);
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(Stm32SysTickSource_DefaultConstruction, CustomFrequency) {
  Stm32SysTickSource source(200);

  EXPECT_EQ(source.tickFrequency(), 200);
}

/* ----------------------------- Prescaler Calculation ----------------------------- */

/** @test 100 Hz produces prescaler of 10. */
TEST(Stm32SysTickSource_Prescaler, Prescaler100Hz) {
  Stm32SysTickSource source(100);

  EXPECT_EQ(source.prescaler(), 10);
}

/** @test 1000 Hz produces prescaler of 1 (every SysTick). */
TEST(Stm32SysTickSource_Prescaler, Prescaler1000Hz) {
  Stm32SysTickSource source(1000);

  EXPECT_EQ(source.prescaler(), 1);
}

/** @test 50 Hz produces prescaler of 20. */
TEST(Stm32SysTickSource_Prescaler, Prescaler50Hz) {
  Stm32SysTickSource source(50);

  EXPECT_EQ(source.prescaler(), 20);
}

/** @test 10 Hz produces prescaler of 100. */
TEST(Stm32SysTickSource_Prescaler, Prescaler10Hz) {
  Stm32SysTickSource source(10);

  EXPECT_EQ(source.prescaler(), 100);
}

/** @test 0 Hz is clamped to 1 Hz (defensive). */
TEST(Stm32SysTickSource_Prescaler, ZeroHzClampedTo1Hz) {
  Stm32SysTickSource source(0);

  EXPECT_EQ(source.tickFrequency(), 1);
  EXPECT_EQ(source.prescaler(), 1000);
}

/** @test Frequency above 1000 Hz gets prescaler of 1. */
TEST(Stm32SysTickSource_Prescaler, AboveSysTickRate) {
  Stm32SysTickSource source(2000);

  EXPECT_EQ(source.tickFrequency(), 2000);
  EXPECT_EQ(source.prescaler(), 1);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() enables running. */
TEST(Stm32SysTickSource_Lifecycle, StartEnablesRunning) {
  Stm32SysTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test stop() disables running. */
TEST(Stm32SysTickSource_Lifecycle, StopDisablesRunning) {
  Stm32SysTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test start() resets tick count. */
TEST(Stm32SysTickSource_Lifecycle, StartResetsTick) {
  Stm32SysTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Multiple start/stop cycles work. */
TEST(Stm32SysTickSource_Lifecycle, MultipleStartsReset) {
  Stm32SysTickSource source;

  source.start();
  source.waitForNextTick();
  EXPECT_EQ(source.currentTick(), 1);

  source.stop();
  source.start();
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_TRUE(source.isRunning());
}

/* ----------------------------- Tick Generation Tests ----------------------------- */

/** @test waitForNextTick() increments tick when running. */
TEST(Stm32SysTickSource_TickGeneration, WaitIncrementsTickWhenRunning) {
  Stm32SysTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3);
}

/** @test waitForNextTick() does not increment tick when stopped. */
TEST(Stm32SysTickSource_TickGeneration, WaitDoesNotIncrementWhenStopped) {
  Stm32SysTickSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Tick count preserved after stop. */
TEST(Stm32SysTickSource_TickGeneration, TickCountPreservedAfterStop) {
  Stm32SysTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2);
}

/** @test Consistent increments over 100 iterations. */
TEST(Stm32SysTickSource_TickGeneration, ConsistentIncrements) {
  Stm32SysTickSource source;
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
TEST(Stm32SysTickSource_Query, TickFrequencyMatchesConstructor) {
  Stm32SysTickSource source(250);

  EXPECT_EQ(source.tickFrequency(), 250);
}

/** @test tickPeriodUs() computes correctly for 100 Hz. */
TEST(Stm32SysTickSource_Query, TickPeriodUsCalculation) {
  Stm32SysTickSource source(100);

  EXPECT_EQ(source.tickPeriodUs(), 10000);
}

/** @test tickPeriodUs() computes correctly for 1000 Hz. */
TEST(Stm32SysTickSource_Query, TickPeriodUs1000Hz) {
  Stm32SysTickSource source(1000);

  EXPECT_EQ(source.tickPeriodUs(), 1000);
}

/* ----------------------------- Interface Tests ----------------------------- */

/** @test Implements ITickSource interface via base pointer. */
TEST(Stm32SysTickSource_Interface, ImplementsITickSource) {
  Stm32SysTickSource source(50);
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
TEST(Stm32SysTickSource_Interface, AckTickIsNoOp) {
  Stm32SysTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1);
}

/** @test isrCallback is callable in mock mode (no-op). */
TEST(Stm32SysTickSource_Interface, IsrCallbackNoOpInMock) {
  Stm32SysTickSource::isrCallback();

  // Should not crash or affect state
  Stm32SysTickSource source;
  EXPECT_EQ(source.currentTick(), 0);
}
