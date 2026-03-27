/**
 * @file PicoSysTickSource_uTest.cpp
 * @brief Unit tests for PicoSysTickSource (mock mode).
 */

#define APEX_HAL_PICO_MOCK 1

#include "src/system/core/hal/pico/inc/PicoSysTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::pico::PicoSysTickSource;
using executive::lite::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates 100 Hz source. */
TEST(PicoSysTickSource_DefaultConstruction, InitialState) {
  PicoSysTickSource source;

  EXPECT_EQ(source.tickFrequency(), 100);
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(PicoSysTickSource_DefaultConstruction, CustomFrequency) {
  PicoSysTickSource source(200);

  EXPECT_EQ(source.tickFrequency(), 200);
}

/* ----------------------------- Prescaler Calculation ----------------------------- */

/** @test 100 Hz produces prescaler of 10. */
TEST(PicoSysTickSource_Prescaler, Prescaler100Hz) {
  PicoSysTickSource source(100);

  EXPECT_EQ(source.prescaler(), 10);
}

/** @test 1000 Hz produces prescaler of 1 (every SysTick). */
TEST(PicoSysTickSource_Prescaler, Prescaler1000Hz) {
  PicoSysTickSource source(1000);

  EXPECT_EQ(source.prescaler(), 1);
}

/** @test 50 Hz produces prescaler of 20. */
TEST(PicoSysTickSource_Prescaler, Prescaler50Hz) {
  PicoSysTickSource source(50);

  EXPECT_EQ(source.prescaler(), 20);
}

/** @test 10 Hz produces prescaler of 100. */
TEST(PicoSysTickSource_Prescaler, Prescaler10Hz) {
  PicoSysTickSource source(10);

  EXPECT_EQ(source.prescaler(), 100);
}

/** @test 0 Hz is clamped to 1 Hz (defensive). */
TEST(PicoSysTickSource_Prescaler, ZeroHzClampedTo1Hz) {
  PicoSysTickSource source(0);

  EXPECT_EQ(source.tickFrequency(), 1);
  EXPECT_EQ(source.prescaler(), 1000);
}

/** @test Frequency above 1000 Hz gets prescaler of 1. */
TEST(PicoSysTickSource_Prescaler, AboveSysTickRate) {
  PicoSysTickSource source(2000);

  EXPECT_EQ(source.tickFrequency(), 2000);
  EXPECT_EQ(source.prescaler(), 1);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() enables running. */
TEST(PicoSysTickSource_Lifecycle, StartEnablesRunning) {
  PicoSysTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test stop() disables running. */
TEST(PicoSysTickSource_Lifecycle, StopDisablesRunning) {
  PicoSysTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test start() resets tick count. */
TEST(PicoSysTickSource_Lifecycle, StartResetsTick) {
  PicoSysTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Multiple start/stop cycles work. */
TEST(PicoSysTickSource_Lifecycle, MultipleStartsReset) {
  PicoSysTickSource source;

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
TEST(PicoSysTickSource_TickGeneration, WaitIncrementsTickWhenRunning) {
  PicoSysTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3);
}

/** @test waitForNextTick() does not increment tick when stopped. */
TEST(PicoSysTickSource_TickGeneration, WaitDoesNotIncrementWhenStopped) {
  PicoSysTickSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Tick count preserved after stop. */
TEST(PicoSysTickSource_TickGeneration, TickCountPreservedAfterStop) {
  PicoSysTickSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2);
}

/** @test Consistent increments over 100 iterations. */
TEST(PicoSysTickSource_TickGeneration, ConsistentIncrements) {
  PicoSysTickSource source;
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
TEST(PicoSysTickSource_Query, TickFrequencyMatchesConstructor) {
  PicoSysTickSource source(250);

  EXPECT_EQ(source.tickFrequency(), 250);
}

/** @test tickPeriodUs() computes correctly for 100 Hz. */
TEST(PicoSysTickSource_Query, TickPeriodUsCalculation) {
  PicoSysTickSource source(100);

  EXPECT_EQ(source.tickPeriodUs(), 10000);
}

/** @test tickPeriodUs() computes correctly for 1000 Hz. */
TEST(PicoSysTickSource_Query, TickPeriodUs1000Hz) {
  PicoSysTickSource source(1000);

  EXPECT_EQ(source.tickPeriodUs(), 1000);
}

/* ----------------------------- Interface Tests ----------------------------- */

/** @test Implements ITickSource interface via base pointer. */
TEST(PicoSysTickSource_Interface, ImplementsITickSource) {
  PicoSysTickSource source(50);
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
TEST(PicoSysTickSource_Interface, AckTickIsNoOp) {
  PicoSysTickSource source;
  source.start();
  source.waitForNextTick();

  source.ackTick();

  EXPECT_EQ(source.currentTick(), 1);
}

/** @test isrCallback is callable in mock mode (no-op). */
TEST(PicoSysTickSource_Interface, IsrCallbackNoOpInMock) {
  PicoSysTickSource::isrCallback();

  // Should not crash or affect state
  PicoSysTickSource source;
  EXPECT_EQ(source.currentTick(), 0);
}
