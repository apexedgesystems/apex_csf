/**
 * @file Accumulator_uTest.cpp
 * @brief Unit tests for apex::math::integration::Accumulator.
 *
 * Notes:
 *  - Tests verify forward Euler and trapezoidal accumulation.
 *  - MultiRateAccumulator tested for multi-sensor fusion.
 */

#include "src/utilities/math/integration/inc/Accumulator.hpp"
#include "src/utilities/math/integration/inc/StateVector.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::Accumulator;
using apex::math::integration::AccumulatorStatus;
using apex::math::integration::MultiRateAccumulator;
using apex::math::integration::State3;
using apex::math::integration::StateVector;

/* -------------------------- Default Construction -------------------------- */

/** @test Accumulator default constructs with zero state. */
TEST(Accumulator, DefaultConstruction) {
  Accumulator<double> acc;
  EXPECT_DOUBLE_EQ(acc.state(), 0.0);
  EXPECT_DOUBLE_EQ(acc.time(), 0.0);
  EXPECT_EQ(acc.sampleCount(), 0u);
}

/** @test Accumulator constructs with initial state. */
TEST(Accumulator, InitialStateConstruction) {
  Accumulator<double> acc(10.0);
  EXPECT_DOUBLE_EQ(acc.state(), 10.0);
  EXPECT_DOUBLE_EQ(acc.time(), 0.0);
}

/* -------------------------- Accumulation ---------------------------------- */

/** @test Forward Euler accumulation. */
TEST(Accumulator, ForwardEulerAccumulation) {
  Accumulator<double> acc(0.0);

  // Constant derivative of 1.0, integrate for 1 second at 0.1s steps
  for (int i = 0; i < 10; ++i) {
    uint8_t status = acc.accumulate(1.0, 0.1);
    EXPECT_EQ(status, static_cast<uint8_t>(AccumulatorStatus::SUCCESS));
  }

  EXPECT_NEAR(acc.state(), 1.0, 1e-10);
  EXPECT_NEAR(acc.time(), 1.0, 1e-10);
  EXPECT_EQ(acc.sampleCount(), 10u);
}

/** @test Trapezoidal accumulation for better accuracy. */
TEST(Accumulator, TrapezoidalAccumulation) {
  Accumulator<double> acc(0.0);

  double prev = 0.0;
  for (int i = 1; i <= 10; ++i) {
    double curr = static_cast<double>(i);
    uint8_t status = acc.accumulateTrapezoidal(prev, curr, 0.1);
    EXPECT_EQ(status, static_cast<uint8_t>(AccumulatorStatus::SUCCESS));
    prev = curr;
  }

  // Trapezoidal integral of linearly increasing derivative
  EXPECT_NEAR(acc.time(), 1.0, 1e-10);
}

/** @test Invalid dt returns error. */
TEST(Accumulator, InvalidDtReturnsError) {
  Accumulator<double> acc(0.0);

  uint8_t status1 = acc.accumulate(1.0, 0.0);
  EXPECT_EQ(status1, static_cast<uint8_t>(AccumulatorStatus::ERROR_INVALID_DT));

  uint8_t status2 = acc.accumulate(1.0, -0.1);
  EXPECT_EQ(status2, static_cast<uint8_t>(AccumulatorStatus::ERROR_INVALID_DT));
}

/* -------------------------- State Management ------------------------------ */

/** @test Reset clears state and counters. */
TEST(Accumulator, Reset) {
  Accumulator<double> acc(10.0);
  acc.accumulate(1.0, 0.1);
  acc.accumulate(1.0, 0.1);

  acc.reset(5.0);

  EXPECT_DOUBLE_EQ(acc.state(), 5.0);
  EXPECT_DOUBLE_EQ(acc.time(), 0.0);
  EXPECT_EQ(acc.sampleCount(), 0u);
}

/** @test setState updates state directly. */
TEST(Accumulator, SetState) {
  Accumulator<double> acc(0.0);
  acc.accumulate(1.0, 0.1);

  acc.setState(100.0);
  EXPECT_DOUBLE_EQ(acc.state(), 100.0);
  EXPECT_NEAR(acc.time(), 0.1, 1e-10); // Time unchanged
}

/** @test Correction adds to state. */
TEST(Accumulator, Correction) {
  Accumulator<double> acc(10.0);
  acc.correct(5.0);
  EXPECT_DOUBLE_EQ(acc.state(), 15.0);
}

/* -------------------------- Statistics ------------------------------------ */

/** @test Average dt calculation. */
TEST(Accumulator, AverageDt) {
  Accumulator<double> acc(0.0);
  acc.accumulate(1.0, 0.1);
  acc.accumulate(1.0, 0.2);
  acc.accumulate(1.0, 0.3);

  EXPECT_NEAR(acc.averageDt(), 0.2, 1e-10);
}

/** @test Average dt returns 0 with no samples. */
TEST(Accumulator, AverageDtNoSamples) {
  Accumulator<double> acc(0.0);
  EXPECT_DOUBLE_EQ(acc.averageDt(), 0.0);
}

/* -------------------------- StateVector Integration ----------------------- */

/** @test Accumulator works with StateVector. */
TEST(Accumulator, StateVectorIntegration) {
  Accumulator<State3> acc(State3{0.0, 0.0, 0.0});

  // Constant acceleration in x direction
  State3 accel{1.0, 0.0, 0.0};
  for (int i = 0; i < 10; ++i) {
    acc.accumulate(accel, 0.1);
  }

  EXPECT_NEAR(acc.state()[0], 1.0, 1e-10);
  EXPECT_NEAR(acc.state()[1], 0.0, 1e-10);
  EXPECT_NEAR(acc.state()[2], 0.0, 1e-10);
}

/* ---------------------- MultiRateAccumulator ------------------------------ */

/** @test MultiRateAccumulator default construction. */
TEST(MultiRateAccumulator, DefaultConstruction) {
  MultiRateAccumulator<double, 2> acc;
  EXPECT_DOUBLE_EQ(acc.state(), 0.0);
}

/** @test MultiRateAccumulator with multiple sources. */
TEST(MultiRateAccumulator, MultipleSources) {
  MultiRateAccumulator<double, 2> acc(0.0);

  // Source 0: high rate (400 Hz -> dt = 0.0025)
  for (int i = 0; i < 40; ++i) {
    acc.accumulate(0, 1.0, 0.0025);
  }

  // Source 1: low rate (10 Hz -> dt = 0.1)
  for (int i = 0; i < 1; ++i) {
    acc.accumulate(1, 2.0, 0.1);
  }

  // Total: 40 * 1.0 * 0.0025 + 1 * 2.0 * 0.1 = 0.1 + 0.2 = 0.3
  EXPECT_NEAR(acc.state(), 0.3, 1e-10);

  EXPECT_EQ(acc.sourceStats(0).sampleCount, 40u);
  EXPECT_EQ(acc.sourceStats(1).sampleCount, 1u);
}

/** @test MultiRateAccumulator invalid source returns error. */
TEST(MultiRateAccumulator, InvalidSourceReturnsError) {
  MultiRateAccumulator<double, 2> acc(0.0);
  uint8_t status = acc.accumulate(5, 1.0, 0.1); // Invalid source index
  EXPECT_EQ(status, static_cast<uint8_t>(AccumulatorStatus::ERROR_OVERFLOW));
}

/** @test MultiRateAccumulator reset clears all. */
TEST(MultiRateAccumulator, Reset) {
  MultiRateAccumulator<double, 2> acc(10.0);
  acc.accumulate(0, 1.0, 0.1);
  acc.accumulate(1, 2.0, 0.1);

  acc.reset(0.0);

  EXPECT_DOUBLE_EQ(acc.state(), 0.0);
  EXPECT_EQ(acc.sourceStats(0).sampleCount, 0u);
  EXPECT_EQ(acc.sourceStats(1).sampleCount, 0u);
}

/** @test MultiRateAccumulator correction. */
TEST(MultiRateAccumulator, Correction) {
  MultiRateAccumulator<State3, 2> acc(State3{0.0, 0.0, 0.0});

  // Accumulate IMU data
  acc.accumulate(0, State3{1.0, 0.0, 0.0}, 0.01);

  // Apply GPS correction
  acc.correct(State3{0.5, 0.0, 0.0});

  EXPECT_NEAR(acc.state()[0], 0.51, 1e-10);
}
