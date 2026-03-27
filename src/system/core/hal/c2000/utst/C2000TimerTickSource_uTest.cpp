/**
 * @file C2000TimerTickSource_uTest.cpp
 * @brief Unit tests for C2000TimerTickSource (mock mode, host-side).
 *
 * Notes:
 *  - Tests run with APEX_HAL_C2000_MOCK defined (no driverlib).
 *  - Verifies construction, start/stop, and tick behavior.
 */

#define APEX_HAL_C2000_MOCK

#include "src/system/core/hal/c2000/inc/C2000TimerTickSource.hpp"

#include <gtest/gtest.h>

using apex::hal::c2000::C2000TimerTickSource;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Construction with frequency stores frequency */
TEST(C2000TimerTickSource, ConstructionStoresFrequency) {
  C2000TimerTickSource tick(100, 100000000U);
  EXPECT_EQ(tick.tickFrequency(), 100U);
  EXPECT_FALSE(tick.isRunning());
}

/** @test Single-arg constructor uses default sysclk */
TEST(C2000TimerTickSource, SingleArgConstructor) {
  C2000TimerTickSource tick(200);
  EXPECT_EQ(tick.tickFrequency(), 200U);
}

/** @test Zero frequency is clamped to 1 */
TEST(C2000TimerTickSource, ZeroFrequencyClamped) {
  C2000TimerTickSource tick(0);
  EXPECT_EQ(tick.tickFrequency(), 1U);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Start sets running flag */
TEST(C2000TimerTickSource, StartSetsRunning) {
  C2000TimerTickSource tick(100);
  tick.start();
  EXPECT_TRUE(tick.isRunning());
}

/** @test Stop clears running flag */
TEST(C2000TimerTickSource, StopClearsRunning) {
  C2000TimerTickSource tick(100);
  tick.start();
  tick.stop();
  EXPECT_FALSE(tick.isRunning());
}

/** @test Start resets tick count */
TEST(C2000TimerTickSource, StartResetsTick) {
  C2000TimerTickSource tick(100);
  tick.start();
  tick.waitForNextTick();
  EXPECT_EQ(tick.currentTick(), 1U);

  tick.start();
  EXPECT_EQ(tick.currentTick(), 0U);
}

/** @test waitForNextTick increments counter in mock mode */
TEST(C2000TimerTickSource, WaitIncrementsMock) {
  C2000TimerTickSource tick(100);
  tick.start();

  tick.waitForNextTick();
  EXPECT_EQ(tick.currentTick(), 1U);

  tick.waitForNextTick();
  EXPECT_EQ(tick.currentTick(), 2U);
}

/** @test waitForNextTick does not increment when stopped */
TEST(C2000TimerTickSource, WaitWhileStoppedNoIncrement) {
  C2000TimerTickSource tick(100);
  EXPECT_EQ(tick.currentTick(), 0U);

  tick.waitForNextTick();
  EXPECT_EQ(tick.currentTick(), 0U);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Multiple start/stop cycles produce consistent state */
TEST(C2000TimerTickSource, StartStopCycles) {
  C2000TimerTickSource tick(50);

  for (int i = 0; i < 5; ++i) {
    tick.start();
    EXPECT_TRUE(tick.isRunning());
    EXPECT_EQ(tick.currentTick(), 0U);

    tick.waitForNextTick();
    tick.waitForNextTick();
    EXPECT_EQ(tick.currentTick(), 2U);

    tick.stop();
    EXPECT_FALSE(tick.isRunning());
  }
}
