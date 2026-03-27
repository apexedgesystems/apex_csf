/**
 * @file SweepGenerator_uTest.cpp
 * @brief Unit tests for monte_carlo::SweepGenerator utilities.
 *
 * Notes:
 *  - Tests verify sweep generation correctness and reproducibility.
 *  - Seeded RNG ensures deterministic test results.
 */

#include "src/system/core/monte_carlo/inc/SweepGenerator.hpp"

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <set>
#include <vector>

#include <gtest/gtest.h>

using apex::monte_carlo::cartesianProduct;
using apex::monte_carlo::generateSweep;
using apex::monte_carlo::gridSweep;
using apex::monte_carlo::latinHypercubeSweep;
using apex::monte_carlo::uniformSweep;

/* ----------------------------- gridSweep Tests ----------------------------- */

/** @test gridSweep with zero steps returns empty */
TEST(GridSweepTest, ZeroStepsReturnsEmpty) {
  auto values = gridSweep(0.0, 1.0, 0);
  EXPECT_TRUE(values.empty());
}

/** @test gridSweep with one step returns min */
TEST(GridSweepTest, OneStepReturnsMin) {
  auto values = gridSweep(5.0, 10.0, 1);
  ASSERT_EQ(values.size(), 1U);
  EXPECT_DOUBLE_EQ(values[0], 5.0);
}

/** @test gridSweep with two steps returns min and max */
TEST(GridSweepTest, TwoStepsReturnsMinMax) {
  auto values = gridSweep(0.0, 100.0, 2);
  ASSERT_EQ(values.size(), 2U);
  EXPECT_DOUBLE_EQ(values[0], 0.0);
  EXPECT_DOUBLE_EQ(values[1], 100.0);
}

/** @test gridSweep produces evenly-spaced values */
TEST(GridSweepTest, EvenlySpaced) {
  auto values = gridSweep(0.0, 10.0, 11);
  ASSERT_EQ(values.size(), 11U);

  for (std::uint32_t i = 0; i < 11; ++i) {
    EXPECT_DOUBLE_EQ(values[i], static_cast<double>(i));
  }
}

/** @test gridSweep works with negative range */
TEST(GridSweepTest, NegativeRange) {
  auto values = gridSweep(-10.0, 10.0, 5);
  ASSERT_EQ(values.size(), 5U);
  EXPECT_DOUBLE_EQ(values[0], -10.0);
  EXPECT_DOUBLE_EQ(values[4], 10.0);
}

/* ----------------------------- uniformSweep Tests ----------------------------- */

/** @test uniformSweep produces correct count */
TEST(UniformSweepTest, CorrectCount) {
  auto values = uniformSweep(0.0, 1.0, 100, 42);
  EXPECT_EQ(values.size(), 100U);
}

/** @test uniformSweep values are within range */
TEST(UniformSweepTest, WithinRange) {
  auto values = uniformSweep(-5.0, 5.0, 10000, 42);
  for (const double v : values) {
    EXPECT_GE(v, -5.0);
    EXPECT_LE(v, 5.0);
  }
}

/** @test uniformSweep is reproducible with same seed */
TEST(UniformSweepTest, Reproducible) {
  auto a = uniformSweep(0.0, 1.0, 100, 42);
  auto b = uniformSweep(0.0, 1.0, 100, 42);
  EXPECT_EQ(a, b);
}

/** @test uniformSweep differs with different seeds */
TEST(UniformSweepTest, DifferentSeeds) {
  auto a = uniformSweep(0.0, 1.0, 100, 42);
  auto b = uniformSweep(0.0, 1.0, 100, 99);
  EXPECT_NE(a, b);
}

/* ----------------------------- latinHypercubeSweep Tests ----------------------------- */

/** @test LHS produces correct count */
TEST(LatinHypercubeTest, CorrectCount) {
  auto values = latinHypercubeSweep(0.0, 1.0, 100, 42);
  EXPECT_EQ(values.size(), 100U);
}

/** @test LHS values are within range */
TEST(LatinHypercubeTest, WithinRange) {
  auto values = latinHypercubeSweep(-10.0, 10.0, 1000, 42);
  for (const double v : values) {
    EXPECT_GE(v, -10.0);
    EXPECT_LE(v, 10.0);
  }
}

/** @test LHS is reproducible with same seed */
TEST(LatinHypercubeTest, Reproducible) {
  auto a = latinHypercubeSweep(0.0, 1.0, 100, 42);
  auto b = latinHypercubeSweep(0.0, 1.0, 100, 42);
  EXPECT_EQ(a, b);
}

/** @test LHS covers strata better than uniform (uniqueness in sorted bins) */
TEST(LatinHypercubeTest, StrataCoverage) {
  const std::uint32_t COUNT = 100;
  auto values = latinHypercubeSweep(0.0, 1.0, COUNT, 42);

  // Sort and check that each stratum has exactly one sample
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());

  const double STRATUM = 1.0 / static_cast<double>(COUNT);
  std::set<std::uint32_t> bins;
  for (const double v : sorted) {
    auto bin = static_cast<std::uint32_t>(v / STRATUM);
    if (bin >= COUNT) {
      bin = COUNT - 1;
    }
    bins.insert(bin);
  }

  // Every bin should be occupied (LHS guarantee)
  EXPECT_EQ(bins.size(), COUNT);
}

/** @test LHS with zero count returns empty */
TEST(LatinHypercubeTest, ZeroCountReturnsEmpty) {
  auto values = latinHypercubeSweep(0.0, 1.0, 0, 42);
  EXPECT_TRUE(values.empty());
}

/* ----------------------------- generateSweep Tests ----------------------------- */

/** @test generateSweep produces correct count */
TEST(GenerateSweepTest, CorrectCount) {
  struct Params {
    double x;
  };

  Params base{0.0};
  auto params = generateSweep<Params>(
      base, 50,
      [](Params& p, std::uint32_t, std::mt19937_64& rng) {
        p.x = std::uniform_real_distribution<>(0.0, 1.0)(rng);
      },
      42);

  EXPECT_EQ(params.size(), 50U);
}

/** @test generateSweep is reproducible */
TEST(GenerateSweepTest, Reproducible) {
  struct Params {
    double x;
  };

  Params base{0.0};
  auto a = generateSweep<Params>(
      base, 100,
      [](Params& p, std::uint32_t, std::mt19937_64& rng) {
        p.x = std::normal_distribution<>(0.0, 1.0)(rng);
      },
      42);

  auto b = generateSweep<Params>(
      base, 100,
      [](Params& p, std::uint32_t, std::mt19937_64& rng) {
        p.x = std::normal_distribution<>(0.0, 1.0)(rng);
      },
      42);

  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i].x, b[i].x);
  }
}

/** @test generateSweep receives correct run index */
TEST(GenerateSweepTest, RunIndexPassedCorrectly) {
  struct Params {
    std::uint32_t idx;
  };

  Params base{0};
  auto params = generateSweep<Params>(
      base, 10, [](Params& p, std::uint32_t idx, std::mt19937_64&) { p.idx = idx; });

  for (std::uint32_t i = 0; i < 10; ++i) {
    EXPECT_EQ(params[i].idx, i);
  }
}

/** @test generateSweep preserves base values for unmutated fields */
TEST(GenerateSweepTest, PreservesBaseValues) {
  struct Params {
    double varied;
    double constant;
  };

  Params base{0.0, 99.0};
  auto params = generateSweep<Params>(
      base, 100,
      [](Params& p, std::uint32_t, std::mt19937_64& rng) {
        p.varied = std::uniform_real_distribution<>(0.0, 1.0)(rng);
      },
      42);

  for (const auto& p : params) {
    EXPECT_DOUBLE_EQ(p.constant, 99.0);
    EXPECT_GE(p.varied, 0.0);
    EXPECT_LE(p.varied, 1.0);
  }
}

/* ----------------------------- cartesianProduct Tests ----------------------------- */

/** @test cartesianProduct produces correct count */
TEST(CartesianProductTest, CorrectCount) {
  struct Params {
    double a;
    double b;
  };

  auto sweepA = gridSweep(0.0, 1.0, 5);
  auto sweepB = gridSweep(0.0, 1.0, 4);

  Params base{0.0, 0.0};
  auto params = cartesianProduct<Params>(
      base, sweepA, sweepB, [](Params& p, double v) { p.a = v; },
      [](Params& p, double v) { p.b = v; });

  EXPECT_EQ(params.size(), 20U);
}

/** @test cartesianProduct covers all combinations */
TEST(CartesianProductTest, AllCombinations) {
  struct Params {
    double a;
    double b;
  };

  auto sweepA = gridSweep(1.0, 3.0, 3);   // {1, 2, 3}
  auto sweepB = gridSweep(10.0, 20.0, 2); // {10, 20}

  Params base{0.0, 0.0};
  auto params = cartesianProduct<Params>(
      base, sweepA, sweepB, [](Params& p, double v) { p.a = v; },
      [](Params& p, double v) { p.b = v; });

  ASSERT_EQ(params.size(), 6U);

  // Verify all 6 combinations exist
  EXPECT_DOUBLE_EQ(params[0].a, 1.0);
  EXPECT_DOUBLE_EQ(params[0].b, 10.0);
  EXPECT_DOUBLE_EQ(params[1].a, 1.0);
  EXPECT_DOUBLE_EQ(params[1].b, 20.0);
  EXPECT_DOUBLE_EQ(params[2].a, 2.0);
  EXPECT_DOUBLE_EQ(params[2].b, 10.0);
  EXPECT_DOUBLE_EQ(params[5].a, 3.0);
  EXPECT_DOUBLE_EQ(params[5].b, 20.0);
}

/** @test cartesianProduct with empty sweep returns empty */
TEST(CartesianProductTest, EmptySweepReturnsEmpty) {
  struct Params {
    double a;
    double b;
  };

  auto sweepA = gridSweep(0.0, 1.0, 5);
  std::vector<double> sweepB; // empty

  Params base{0.0, 0.0};
  auto params = cartesianProduct<Params>(
      base, sweepA, sweepB, [](Params& p, double v) { p.a = v; },
      [](Params& p, double v) { p.b = v; });

  EXPECT_TRUE(params.empty());
}
