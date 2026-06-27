/**
 * @file GaussianSampler_uTest.cpp
 * @brief Tests for the deterministic, portable standard-normal sampler.
 *
 * Covers reproducibility (same seed -> same sequence), seed independence, the
 * Box-Muller pair caching, and the N(0,1) / N(mean,sigma) statistics.
 */

#include "src/sim/sensors/inc/GaussianSampler.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using sim::sensors::GaussianSampler;

namespace {

// Sample mean and (population) standard deviation of a batch of draws.
void meanStd(const std::vector<double>& v, double& mean, double& std) {
  double s = 0.0;
  for (double x : v) {
    s += x;
  }
  mean = s / static_cast<double>(v.size());
  double acc = 0.0;
  for (double x : v) {
    acc += (x - mean) * (x - mean);
  }
  std = std::sqrt(acc / static_cast<double>(v.size()));
}

} // namespace

/** @test The same seed reproduces the same sequence bit-for-bit. */
TEST(GaussianSamplerTest, SameSeedReproducesSequence) {
  GaussianSampler a(12345u);
  GaussianSampler b(12345u);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(a.gaussian(), b.gaussian()); // exact -- determinism is the contract
  }
}

/** @test Reseeding restarts the identical sequence (cache is cleared). */
TEST(GaussianSamplerTest, ReseedRestartsSequence) {
  GaussianSampler s(777u);
  std::vector<double> first;
  first.reserve(10);
  for (int i = 0; i < 10; ++i) {
    first.push_back(s.gaussian());
  }
  s.seed(777u);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(s.gaussian(), first[static_cast<std::size_t>(i)]);
  }
}

/** @test Different seeds produce different sequences. */
TEST(GaussianSamplerTest, DifferentSeedsDiffer) {
  GaussianSampler a(1u);
  GaussianSampler b(2u);
  bool any_diff = false;
  for (int i = 0; i < 16; ++i) {
    if (a.gaussian() != b.gaussian()) {
      any_diff = true;
    }
  }
  EXPECT_TRUE(any_diff);
}

/** @test Over many draws the sample mean ~ 0 and std ~ 1 (standard normal). */
TEST(GaussianSamplerTest, StandardNormalStatistics) {
  GaussianSampler s(2024u);
  std::vector<double> v;
  v.reserve(200000);
  for (int i = 0; i < 200000; ++i) {
    v.push_back(s.gaussian());
  }
  double mean = 0.0;
  double std = 0.0;
  meanStd(v, mean, std);
  EXPECT_NEAR(mean, 0.0, 0.03);
  EXPECT_NEAR(std, 1.0, 0.03);
}

/** @test gaussian(mean, sigma) shifts and scales the standard normal. */
TEST(GaussianSamplerTest, ShiftedScaledStatistics) {
  GaussianSampler s(99u);
  std::vector<double> v;
  v.reserve(200000);
  for (int i = 0; i < 200000; ++i) {
    v.push_back(s.gaussian(5.0, 2.0));
  }
  double mean = 0.0;
  double std = 0.0;
  meanStd(v, mean, std);
  EXPECT_NEAR(mean, 5.0, 0.05);
  EXPECT_NEAR(std, 2.0, 0.05);
}

/** @test The cached second sample of each Box-Muller pair is consumed next. */
TEST(GaussianSamplerTest, PairCachingConsumesBothSamples) {
  // Two samplers seeded identically: drawing 2 from one must equal 2 from the
  // other -- the cached path (2nd draw) is exercised and must stay in sequence.
  GaussianSampler a(55u);
  GaussianSampler b(55u);
  const double a0 = a.gaussian();
  const double a1 = a.gaussian(); // cached path
  const double b0 = b.gaussian();
  const double b1 = b.gaussian(); // cached path
  EXPECT_EQ(a0, b0);
  EXPECT_EQ(a1, b1);
  EXPECT_NE(a0, a1); // the pair's two normals are independent
}
