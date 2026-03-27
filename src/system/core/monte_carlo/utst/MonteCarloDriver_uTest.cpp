/**
 * @file MonteCarloDriver_uTest.cpp
 * @brief Unit tests for monte_carlo::MonteCarloDriver.
 *
 * Notes:
 *  - Tests verify work distribution, result collection, and error handling.
 *  - Thread counts are kept small for test stability.
 */

#include "src/system/core/monte_carlo/inc/MonteCarloDriver.hpp"

#include <cmath>
#include <cstdint>

#include <atomic>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

using apex::monte_carlo::DriverConfig;
using apex::monte_carlo::MonteCarloDriver;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Driver constructs with default config */
TEST(MonteCarloDriverTest, DefaultConstruction) {
  MonteCarloDriver<double, double> driver([](const double& p, std::uint32_t) { return p * 2.0; });

  EXPECT_EQ(driver.config().threadCount, 0U);
  EXPECT_EQ(driver.config().baseSeed, 0U);
}

/** @test Driver constructs with explicit config */
TEST(MonteCarloDriverTest, ExplicitConfig) {
  DriverConfig cfg;
  cfg.threadCount = 4;
  cfg.baseSeed = 42;

  MonteCarloDriver<double, double> driver([](const double& p, std::uint32_t) { return p; }, cfg);

  EXPECT_EQ(driver.config().threadCount, 4U);
  EXPECT_EQ(driver.config().baseSeed, 42U);
}

/* ----------------------------- Execute Tests ----------------------------- */

/** @test Execute with empty params returns empty results */
TEST(MonteCarloDriverTest, EmptyParamsReturnsEmptyResults) {
  MonteCarloDriver<double, double> driver([](const double& p, std::uint32_t) { return p; });

  std::vector<double> params;
  auto results = driver.execute(params);

  EXPECT_EQ(results.totalRuns, 0U);
  EXPECT_EQ(results.completedRuns, 0U);
  EXPECT_TRUE(results.runs.empty());
}

/** @test Execute runs all parameter sets */
TEST(MonteCarloDriverTest, ExecuteRunsAllParams) {
  DriverConfig cfg;
  cfg.threadCount = 2;

  MonteCarloDriver<double, double> driver([](const double& p, std::uint32_t) { return p * 2.0; },
                                          cfg);

  std::vector<double> params = {1.0, 2.0, 3.0, 4.0, 5.0};
  auto results = driver.execute(params);

  EXPECT_EQ(results.totalRuns, 5U);
  EXPECT_EQ(results.completedRuns, 5U);
  EXPECT_EQ(results.failedRuns, 0U);
  EXPECT_EQ(results.threadCount, 2U);
  ASSERT_EQ(results.runs.size(), 5U);

  for (std::size_t i = 0; i < params.size(); ++i) {
    EXPECT_DOUBLE_EQ(results.runs[i], params[i] * 2.0);
  }
}

/** @test Execute preserves run index ordering */
TEST(MonteCarloDriverTest, RunIndexOrdering) {
  DriverConfig cfg;
  cfg.threadCount = 4;

  MonteCarloDriver<int, int> driver(
      [](const int& p, std::uint32_t idx) { return p + static_cast<int>(idx); }, cfg);

  std::vector<int> params(100, 0);
  auto results = driver.execute(params);

  ASSERT_EQ(results.runs.size(), 100U);
  for (std::uint32_t i = 0; i < 100; ++i) {
    EXPECT_EQ(results.runs[i], static_cast<int>(i));
  }
}

/** @test Execute with single thread works correctly */
TEST(MonteCarloDriverTest, SingleThreadExecution) {
  DriverConfig cfg;
  cfg.threadCount = 1;

  MonteCarloDriver<double, double> driver(
      [](const double& p, std::uint32_t) { return std::sqrt(p); }, cfg);

  std::vector<double> params = {4.0, 9.0, 16.0, 25.0};
  auto results = driver.execute(params);

  EXPECT_EQ(results.completedRuns, 4U);
  EXPECT_EQ(results.threadCount, 1U);
  EXPECT_DOUBLE_EQ(results.runs[0], 2.0);
  EXPECT_DOUBLE_EQ(results.runs[1], 3.0);
  EXPECT_DOUBLE_EQ(results.runs[2], 4.0);
  EXPECT_DOUBLE_EQ(results.runs[3], 5.0);
}

/** @test Large run count completes correctly */
TEST(MonteCarloDriverTest, LargeRunCount) {
  DriverConfig cfg;
  cfg.threadCount = 4;

  std::atomic<std::uint32_t> callCount{0};

  MonteCarloDriver<int, int> driver(
      [&callCount](const int& p, std::uint32_t) {
        callCount.fetch_add(1, std::memory_order_relaxed);
        return p;
      },
      cfg);

  const std::uint32_t COUNT = 10000;
  std::vector<int> params(COUNT, 42);
  auto results = driver.execute(params);

  EXPECT_EQ(results.totalRuns, COUNT);
  EXPECT_EQ(results.completedRuns, COUNT);
  EXPECT_EQ(callCount.load(), COUNT);
  EXPECT_GT(results.wallTimeSeconds, 0.0);
}

/** @test Wall time and throughput are populated */
TEST(MonteCarloDriverTest, TimingMetrics) {
  DriverConfig cfg;
  cfg.threadCount = 2;

  MonteCarloDriver<int, int> driver([](const int& p, std::uint32_t) { return p; }, cfg);

  std::vector<int> params(1000, 1);
  auto results = driver.execute(params);

  EXPECT_GT(results.wallTimeSeconds, 0.0);
  EXPECT_GT(results.runsPerSecond(), 0.0);
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Failed runs are counted and default-constructed */
TEST(MonteCarloDriverTest, FailedRunsCounted) {
  DriverConfig cfg;
  cfg.threadCount = 2;

  MonteCarloDriver<int, int> driver(
      [](const int& p, std::uint32_t idx) -> int {
        if (idx % 3 == 0) {
          throw std::runtime_error("test failure");
        }
        return p;
      },
      cfg);

  std::vector<int> params(9, 42);
  auto results = driver.execute(params);

  EXPECT_EQ(results.totalRuns, 9U);
  // Runs 0, 3, 6 fail (3 failures)
  EXPECT_EQ(results.failedRuns, 3U);
  EXPECT_EQ(results.completedRuns, 6U);

  // Failed runs get default value
  EXPECT_EQ(results.runs[0], 0);
  EXPECT_EQ(results.runs[3], 0);
  EXPECT_EQ(results.runs[6], 0);

  // Successful runs have correct value
  EXPECT_EQ(results.runs[1], 42);
  EXPECT_EQ(results.runs[2], 42);
}

/* ----------------------------- Single Run ----------------------------- */

/** @test executeSingle runs a single parameter set */
TEST(MonteCarloDriverTest, ExecuteSingle) {
  MonteCarloDriver<double, double> driver(
      [](const double& p, std::uint32_t idx) { return p + static_cast<double>(idx); });

  EXPECT_DOUBLE_EQ(driver.executeSingle(10.0, 5), 15.0);
  EXPECT_DOUBLE_EQ(driver.executeSingle(0.0, 0), 0.0);
}

/* ----------------------------- Struct Types ----------------------------- */

/** @test Works with user-defined struct types */
TEST(MonteCarloDriverTest, StructTypes) {
  struct Params {
    double resistance;
    double capacitance;
  };

  struct Result {
    double tau;
    bool valid;
  };

  DriverConfig cfg;
  cfg.threadCount = 2;

  MonteCarloDriver<Params, Result> driver(
      [](const Params& p, std::uint32_t) -> Result { return {p.resistance * p.capacitance, true}; },
      cfg);

  std::vector<Params> params = {{100.0, 1e-6}, {200.0, 2e-6}, {50.0, 5e-7}};
  auto results = driver.execute(params);

  EXPECT_EQ(results.completedRuns, 3U);
  EXPECT_DOUBLE_EQ(results.runs[0].tau, 100.0 * 1e-6);
  EXPECT_DOUBLE_EQ(results.runs[1].tau, 200.0 * 2e-6);
  EXPECT_TRUE(results.runs[0].valid);
}

/** @test Pool worker count matches config */
TEST(MonteCarloDriverTest, WorkerCountMatchesConfig) {
  DriverConfig cfg;
  cfg.threadCount = 4;

  MonteCarloDriver<int, int> driver([](const int& p, std::uint32_t) { return p; }, cfg);

  EXPECT_EQ(driver.workerCount(), 4U);

  std::vector<int> params = {1, 2, 3};
  auto results = driver.execute(params);

  // All runs complete even with more workers than runs
  EXPECT_EQ(results.completedRuns, 3U);
}

/** @test Multiple execute() calls reuse the pool */
TEST(MonteCarloDriverTest, PoolReusedAcrossExecuteCalls) {
  DriverConfig cfg;
  cfg.threadCount = 2;

  std::atomic<std::uint32_t> totalCalls{0};

  MonteCarloDriver<int, int> driver(
      [&totalCalls](const int& p, std::uint32_t) {
        totalCalls.fetch_add(1, std::memory_order_relaxed);
        return p;
      },
      cfg);

  std::vector<int> params1(100, 1);
  std::vector<int> params2(200, 2);

  auto results1 = driver.execute(params1);
  auto results2 = driver.execute(params2);

  EXPECT_EQ(results1.completedRuns, 100U);
  EXPECT_EQ(results2.completedRuns, 200U);
  EXPECT_EQ(totalCalls.load(), 300U);
}
