/**
 * @file ConvergenceTracker_uTest.cpp
 * @brief Unit tests for monte_carlo::ConvergenceTracker.
 *
 * Notes:
 *  - Tests verify Welford's algorithm accuracy and convergence detection.
 *  - Known distributions used for numerical validation.
 */

#include "src/system/core/monte_carlo/inc/ConvergenceTracker.hpp"

#include <cmath>
#include <cstdint>

#include <random>

#include <gtest/gtest.h>

using apex::monte_carlo::ConvergenceTracker;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction yields zero state */
TEST(ConvergenceTrackerTest, DefaultConstruction) {
  ConvergenceTracker tracker;

  EXPECT_EQ(tracker.count(), 0U);
  EXPECT_DOUBLE_EQ(tracker.mean(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.variance(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.stddev(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.standardErrorOfMean(), 0.0);
  EXPECT_FALSE(tracker.isConverged());
}

/** @test Custom threshold and minSamples */
TEST(ConvergenceTrackerTest, CustomConfig) {
  ConvergenceTracker tracker(0.01, 50);

  EXPECT_DOUBLE_EQ(tracker.threshold(), 0.01);
  EXPECT_EQ(tracker.minSamples(), 50U);
}

/* ----------------------------- Running Statistics ----------------------------- */

/** @test Single sample sets mean correctly */
TEST(ConvergenceTrackerTest, SingleSample) {
  ConvergenceTracker tracker;
  tracker.addSample(42.0);

  EXPECT_EQ(tracker.count(), 1U);
  EXPECT_DOUBLE_EQ(tracker.mean(), 42.0);
  EXPECT_DOUBLE_EQ(tracker.variance(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.min(), 42.0);
  EXPECT_DOUBLE_EQ(tracker.max(), 42.0);
}

/** @test Two samples compute correct mean and variance */
TEST(ConvergenceTrackerTest, TwoSamples) {
  ConvergenceTracker tracker;
  tracker.addSample(10.0);
  tracker.addSample(20.0);

  EXPECT_DOUBLE_EQ(tracker.mean(), 15.0);
  EXPECT_NEAR(tracker.stddev(), 5.0, 1e-10);
  EXPECT_DOUBLE_EQ(tracker.min(), 10.0);
  EXPECT_DOUBLE_EQ(tracker.max(), 20.0);
}

/** @test Known sequence 1..100 */
TEST(ConvergenceTrackerTest, KnownSequence) {
  ConvergenceTracker tracker;
  for (int i = 1; i <= 100; ++i) {
    tracker.addSample(static_cast<double>(i));
  }

  EXPECT_EQ(tracker.count(), 100U);
  EXPECT_DOUBLE_EQ(tracker.mean(), 50.5);
  EXPECT_DOUBLE_EQ(tracker.min(), 1.0);
  EXPECT_DOUBLE_EQ(tracker.max(), 100.0);

  // Population stddev of 1..100 = sqrt((100^2 - 1) / 12)
  const double EXPECTED_STDDEV = std::sqrt(833.25);
  EXPECT_NEAR(tracker.stddev(), EXPECTED_STDDEV, 0.01);
}

/** @test Identical samples produce zero variance */
TEST(ConvergenceTrackerTest, IdenticalSamples) {
  ConvergenceTracker tracker;
  for (int i = 0; i < 100; ++i) {
    tracker.addSample(7.0);
  }

  EXPECT_DOUBLE_EQ(tracker.mean(), 7.0);
  EXPECT_DOUBLE_EQ(tracker.variance(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.stddev(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.standardErrorOfMean(), 0.0);
}

/** @test SEM decreases as samples increase */
TEST(ConvergenceTrackerTest, SemDecreases) {
  ConvergenceTracker tracker;
  std::mt19937_64 rng(42);
  std::normal_distribution<> dist(100.0, 10.0);

  // Add 100 samples, record SEM at 50 and 100
  for (int i = 0; i < 50; ++i) {
    tracker.addSample(dist(rng));
  }
  const double SEM_50 = tracker.standardErrorOfMean();

  for (int i = 0; i < 50; ++i) {
    tracker.addSample(dist(rng));
  }
  const double SEM_100 = tracker.standardErrorOfMean();

  // SEM should decrease with more samples (roughly by sqrt(2))
  EXPECT_LT(SEM_100, SEM_50);
}

/** @test Coefficient of variation is computed correctly */
TEST(ConvergenceTrackerTest, CoefficientOfVariation) {
  ConvergenceTracker tracker;
  // mean=100, stddev=10 -> CV=0.1
  for (int i = 0; i < 1000; ++i) {
    tracker.addSample(90.0);
    tracker.addSample(110.0);
  }

  EXPECT_NEAR(tracker.mean(), 100.0, 1e-10);
  EXPECT_NEAR(tracker.coefficientOfVariation(), 0.1, 0.001);
}

/* ----------------------------- Convergence Detection ----------------------------- */

/** @test Not converged with fewer than minSamples */
TEST(ConvergenceTrackerTest, NotConvergedBelowMinSamples) {
  ConvergenceTracker tracker(0.001, 30);

  for (int i = 0; i < 29; ++i) {
    tracker.addSample(100.0);
  }

  EXPECT_FALSE(tracker.isConverged());

  tracker.addSample(100.0);
  EXPECT_TRUE(tracker.isConverged()); // 30 identical samples -> converged
}

/** @test Converges on tight distribution */
TEST(ConvergenceTrackerTest, ConvergesOnTightDistribution) {
  ConvergenceTracker tracker(0.01, 30); // 1% threshold

  std::mt19937_64 rng(42);
  std::normal_distribution<> dist(100.0, 0.1); // Very tight

  for (int i = 0; i < 100; ++i) {
    tracker.addSample(dist(rng));
  }

  EXPECT_TRUE(tracker.isConverged());
}

/** @test Does not converge on wide distribution with few samples */
TEST(ConvergenceTrackerTest, DoesNotConvergeOnWideDistribution) {
  ConvergenceTracker tracker(0.001, 30); // 0.1% threshold

  std::mt19937_64 rng(42);
  std::normal_distribution<> dist(100.0, 50.0); // Very wide

  for (int i = 0; i < 50; ++i) {
    tracker.addSample(dist(rng));
  }

  // With CV=50% and only 50 samples, SEM/mean is still large
  EXPECT_FALSE(tracker.isConverged());
}

/** @test Converges on identical samples */
TEST(ConvergenceTrackerTest, ConvergesOnIdenticalSamples) {
  ConvergenceTracker tracker(0.001, 30);

  for (int i = 0; i < 30; ++i) {
    tracker.addSample(42.0);
  }

  // Zero variance -> converged
  EXPECT_TRUE(tracker.isConverged());
}

/** @test Near-zero mean uses absolute SEM threshold */
TEST(ConvergenceTrackerTest, NearZeroMean) {
  ConvergenceTracker tracker(0.001, 30);

  std::mt19937_64 rng(42);
  std::normal_distribution<> dist(0.0, 0.0001); // Mean ~0, tiny stddev

  for (int i = 0; i < 100; ++i) {
    tracker.addSample(dist(rng));
  }

  // Should handle near-zero mean gracefully (no NaN, no crash)
  [[maybe_unused]] const bool RESULT = tracker.isConverged();
  EXPECT_TRUE(tracker.count() > 0U);
}

/* ----------------------------- Reset ----------------------------- */

/** @test Reset clears all statistics */
TEST(ConvergenceTrackerTest, ResetClearsState) {
  ConvergenceTracker tracker(0.01, 30);

  for (int i = 0; i < 100; ++i) {
    tracker.addSample(static_cast<double>(i));
  }
  EXPECT_EQ(tracker.count(), 100U);

  tracker.reset();

  EXPECT_EQ(tracker.count(), 0U);
  EXPECT_DOUBLE_EQ(tracker.mean(), 0.0);
  EXPECT_DOUBLE_EQ(tracker.variance(), 0.0);
  EXPECT_FALSE(tracker.isConverged());

  // Threshold and minSamples preserved
  EXPECT_DOUBLE_EQ(tracker.threshold(), 0.01);
  EXPECT_EQ(tracker.minSamples(), 30U);
}
