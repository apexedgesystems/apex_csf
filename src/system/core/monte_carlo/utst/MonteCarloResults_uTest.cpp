/**
 * @file MonteCarloResults_uTest.cpp
 * @brief Unit tests for monte_carlo::MonteCarloResults and ScalarStats.
 *
 * Notes:
 *  - Tests verify statistics computation, percentiles, and edge cases.
 *  - Floating-point comparisons use EXPECT_NEAR for numerical stability.
 */

#include "src/system/core/monte_carlo/inc/MonteCarloResults.hpp"

#include <cmath>
#include <cstdint>

#include <vector>

#include <gtest/gtest.h>

using apex::monte_carlo::computeStats;
using apex::monte_carlo::extractAndCompute;
using apex::monte_carlo::MonteCarloResults;
using apex::monte_carlo::ScalarStats;

/* ----------------------------- Default Construction ----------------------------- */

/** @test ScalarStats default-constructs to zero */
TEST(ScalarStatsTest, DefaultConstruction) {
  ScalarStats stats;
  EXPECT_DOUBLE_EQ(stats.mean, 0.0);
  EXPECT_DOUBLE_EQ(stats.stddev, 0.0);
  EXPECT_DOUBLE_EQ(stats.min, 0.0);
  EXPECT_DOUBLE_EQ(stats.max, 0.0);
  EXPECT_DOUBLE_EQ(stats.median, 0.0);
  EXPECT_EQ(stats.count, 0U);
}

/** @test MonteCarloResults default-constructs to empty */
TEST(MonteCarloResultsTest, DefaultConstruction) {
  MonteCarloResults<double> results;
  EXPECT_TRUE(results.runs.empty());
  EXPECT_EQ(results.totalRuns, 0U);
  EXPECT_EQ(results.completedRuns, 0U);
  EXPECT_EQ(results.failedRuns, 0U);
  EXPECT_DOUBLE_EQ(results.wallTimeSeconds, 0.0);
  EXPECT_DOUBLE_EQ(results.runsPerSecond(), 0.0);
}

/* ----------------------------- computeStats Tests ----------------------------- */

/** @test computeStats on empty span returns zero stats */
TEST(ComputeStatsTest, EmptyInput) {
  std::vector<double> empty;
  auto stats = computeStats(empty);
  EXPECT_EQ(stats.count, 0U);
  EXPECT_DOUBLE_EQ(stats.mean, 0.0);
}

/** @test computeStats on single value */
TEST(ComputeStatsTest, SingleValue) {
  std::vector<double> values = {42.0};
  auto stats = computeStats(values);

  EXPECT_EQ(stats.count, 1U);
  EXPECT_DOUBLE_EQ(stats.mean, 42.0);
  EXPECT_DOUBLE_EQ(stats.stddev, 0.0);
  EXPECT_DOUBLE_EQ(stats.min, 42.0);
  EXPECT_DOUBLE_EQ(stats.max, 42.0);
  EXPECT_DOUBLE_EQ(stats.median, 42.0);
  EXPECT_DOUBLE_EQ(stats.p05, 42.0);
  EXPECT_DOUBLE_EQ(stats.p95, 42.0);
}

/** @test computeStats on two values */
TEST(ComputeStatsTest, TwoValues) {
  std::vector<double> values = {10.0, 20.0};
  auto stats = computeStats(values);

  EXPECT_EQ(stats.count, 2U);
  EXPECT_DOUBLE_EQ(stats.mean, 15.0);
  EXPECT_DOUBLE_EQ(stats.min, 10.0);
  EXPECT_DOUBLE_EQ(stats.max, 20.0);
  EXPECT_DOUBLE_EQ(stats.median, 15.0);
  EXPECT_NEAR(stats.stddev, 5.0, 1e-10);
}

/** @test computeStats on known distribution */
TEST(ComputeStatsTest, KnownDistribution) {
  // 1 through 100
  std::vector<double> values;
  values.reserve(100);
  for (int i = 1; i <= 100; ++i) {
    values.push_back(static_cast<double>(i));
  }

  auto stats = computeStats(values);

  EXPECT_EQ(stats.count, 100U);
  EXPECT_DOUBLE_EQ(stats.mean, 50.5);
  EXPECT_DOUBLE_EQ(stats.min, 1.0);
  EXPECT_DOUBLE_EQ(stats.max, 100.0);
  EXPECT_DOUBLE_EQ(stats.median, 50.5);

  // Population stddev of 1..100
  const double EXPECTED_STDDEV = std::sqrt(833.25); // (100^2 - 1) / 12
  EXPECT_NEAR(stats.stddev, EXPECTED_STDDEV, 0.01);
}

/** @test computeStats percentiles are monotonic */
TEST(ComputeStatsTest, PercentilesMonotonic) {
  std::vector<double> values;
  values.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    values.push_back(static_cast<double>(i));
  }

  auto stats = computeStats(values);

  EXPECT_LE(stats.min, stats.p05);
  EXPECT_LE(stats.p05, stats.p25);
  EXPECT_LE(stats.p25, stats.median);
  EXPECT_LE(stats.median, stats.p75);
  EXPECT_LE(stats.p75, stats.p95);
  EXPECT_LE(stats.p95, stats.max);
}

/** @test computeStats handles identical values */
TEST(ComputeStatsTest, IdenticalValues) {
  std::vector<double> values(50, 7.0);
  auto stats = computeStats(values);

  EXPECT_EQ(stats.count, 50U);
  EXPECT_DOUBLE_EQ(stats.mean, 7.0);
  EXPECT_DOUBLE_EQ(stats.stddev, 0.0);
  EXPECT_DOUBLE_EQ(stats.min, 7.0);
  EXPECT_DOUBLE_EQ(stats.max, 7.0);
  EXPECT_DOUBLE_EQ(stats.median, 7.0);
}

/* ----------------------------- extractAndCompute Tests ----------------------------- */

/** @test extractAndCompute extracts field from struct results */
TEST(ExtractAndComputeTest, ExtractsField) {
  struct Result {
    double voltage;
    double current;
  };

  std::vector<Result> runs = {{1.0, 0.1}, {2.0, 0.2}, {3.0, 0.3}};

  auto voltageStats = extractAndCompute<Result>(runs, [](const Result& r) { return r.voltage; });

  EXPECT_EQ(voltageStats.count, 3U);
  EXPECT_DOUBLE_EQ(voltageStats.mean, 2.0);
  EXPECT_DOUBLE_EQ(voltageStats.min, 1.0);
  EXPECT_DOUBLE_EQ(voltageStats.max, 3.0);

  auto currentStats = extractAndCompute<Result>(runs, [](const Result& r) { return r.current; });

  EXPECT_NEAR(currentStats.mean, 0.2, 1e-10);
}

/* ----------------------------- runsPerSecond Tests ----------------------------- */

/** @test runsPerSecond computes correctly */
TEST(MonteCarloResultsTest, RunsPerSecond) {
  MonteCarloResults<int> results;
  results.completedRuns = 1000;
  results.wallTimeSeconds = 2.0;

  EXPECT_DOUBLE_EQ(results.runsPerSecond(), 500.0);
}

/** @test runsPerSecond returns zero for zero time */
TEST(MonteCarloResultsTest, RunsPerSecondZeroTime) {
  MonteCarloResults<int> results;
  results.completedRuns = 100;
  results.wallTimeSeconds = 0.0;

  EXPECT_DOUBLE_EQ(results.runsPerSecond(), 0.0);
}
