/**
 * @file FreeRunningSource_uTest.cpp
 * @brief Unit tests for FreeRunningSource.
 */

#include "src/system/core/executive/mcu/inc/FreeRunningSource.hpp"

#include <gtest/gtest.h>

using executive::mcu::FreeRunningSource;
using executive::mcu::ITickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates source with expected state. */
TEST(FreeRunningSource_DefaultConstruction, InitialState) {
  FreeRunningSource source;

  EXPECT_EQ(source.tickFrequency(), 100);
  EXPECT_EQ(source.currentTick(), 0);
  EXPECT_FALSE(source.isRunning());
}

/** @test Custom frequency is applied. */
TEST(FreeRunningSource_DefaultConstruction, CustomFrequency) {
  FreeRunningSource source(200);

  EXPECT_EQ(source.tickFrequency(), 200);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() resets tick count and enables running. */
TEST(FreeRunningSource_Lifecycle, StartResetsAndEnables) {
  FreeRunningSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test stop() disables running. */
TEST(FreeRunningSource_Lifecycle, StopDisablesRunning) {
  FreeRunningSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test Multiple start() calls reset tick count. */
TEST(FreeRunningSource_Lifecycle, MultipleStartsReset) {
  FreeRunningSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.start();

  EXPECT_EQ(source.currentTick(), 0);
}

/* ----------------------------- Tick Generation Tests ----------------------------- */

/** @test waitForNextTick() increments tick when running. */
TEST(FreeRunningSource_TickGeneration, WaitIncrementsTickWhenRunning) {
  FreeRunningSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 3);
}

/** @test waitForNextTick() does not increment tick when stopped. */
TEST(FreeRunningSource_TickGeneration, WaitDoesNotIncrementWhenStopped) {
  FreeRunningSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test Tick count preserved after stop. */
TEST(FreeRunningSource_TickGeneration, TickCountPreservedAfterStop) {
  FreeRunningSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.stop();

  EXPECT_EQ(source.currentTick(), 2);
}

/* ----------------------------- Test Helper Tests ----------------------------- */

/** @test reset() clears tick count. */
TEST(FreeRunningSource_TestHelpers, ResetClearsTicks) {
  FreeRunningSource source;
  source.start();
  source.waitForNextTick();
  source.waitForNextTick();

  source.reset();

  EXPECT_EQ(source.currentTick(), 0);
}

/** @test setTickCount() sets specific value. */
TEST(FreeRunningSource_TestHelpers, SetTickCount) {
  FreeRunningSource source;

  source.setTickCount(1000);

  EXPECT_EQ(source.currentTick(), 1000);
}

/** @test setTickCount() to max value works. */
TEST(FreeRunningSource_TestHelpers, SetTickCountMax) {
  FreeRunningSource source;

  source.setTickCount(0xFFFFFFFF);

  EXPECT_EQ(source.currentTick(), 0xFFFFFFFF);
}

/* ----------------------------- ITickSource Interface Tests ----------------------------- */

/** @test FreeRunningSource implements ITickSource interface. */
TEST(FreeRunningSource_Interface, ImplementsITickSource) {
  FreeRunningSource source(50);
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

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Repeated tick generation produces consistent increments. */
TEST(FreeRunningSource_Determinism, ConsistentIncrements) {
  FreeRunningSource source;
  source.start();

  for (int i = 0; i < 100; ++i) {
    const std::uint32_t BEFORE = source.currentTick();
    source.waitForNextTick();
    const std::uint32_t AFTER = source.currentTick();
    EXPECT_EQ(AFTER, BEFORE + 1) << "Increment failed at iteration " << i;
  }
}
