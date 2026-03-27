/**
 * @file runningAverage_uTest.cpp
 * @brief Unit tests for apex::helpers::runningAverage online averaging.
 *
 * Coverage:
 *  - First sample sets the average
 *  - Subsequent samples update correctly
 *  - Works from a non-zero starting average
 *  - Handles negative inputs
 *  - Stable with large cycle counts (no overflow, convergence)
 */

#include "src/utilities/helpers/inc/Utilities.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::DoubleEq;

// Fixture ---------------------------------------------------------------------

class RunningAverageTest : public ::testing::Test {};

// -----------------------------------------------------------------------------

/**
 * @test First and subsequent samples update the running average as expected.
 */
TEST_F(RunningAverageTest, SequentialSamples) {
  // Arrange
  double avg = 0.0;

  // Act & Assert
  apex::helpers::runningAverage(avg, 10.0, 0);
  EXPECT_DOUBLE_EQ(avg, 10.0);

  apex::helpers::runningAverage(avg, 20.0, 1);
  EXPECT_DOUBLE_EQ(avg, 15.0);

  apex::helpers::runningAverage(avg, 30.0, 2);
  EXPECT_DOUBLE_EQ(avg, 20.0);
}

/**
 * @test Starting from a non-zero average updates correctly.
 */
TEST_F(RunningAverageTest, NonZeroStartingAverage) {
  // Arrange
  double avg = 50.0;

  // Act
  apex::helpers::runningAverage(avg, 70.0, 0);

  // Assert
  EXPECT_DOUBLE_EQ(avg, 70.0);
}

/**
 * @test Negative inputs are handled correctly.
 */
TEST_F(RunningAverageTest, HandlesNegativeInputs) {
  // Arrange
  double avg = 0.0;

  // Act
  apex::helpers::runningAverage(avg, -10.0, 0);

  // Assert
  EXPECT_DOUBLE_EQ(avg, -10.0);
}

/**
 * @test With large cycle counts the function remains stable and converges.
 */
TEST_F(RunningAverageTest, StableWithLargeCycleCounts) {
  // Arrange
  double avg = 100.0;

  // Act
  apex::helpers::runningAverage(avg, 200.0, 1000000U);

  // Assert: very small adjustment, still close to 100
  EXPECT_NEAR(avg, 100.0001, 1e-3);
}
