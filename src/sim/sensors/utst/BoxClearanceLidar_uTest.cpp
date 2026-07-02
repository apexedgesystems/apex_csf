/**
 * @file BoxClearanceLidar_uTest.cpp
 * @brief Tests for the six-axis box-clearance lidar.
 *
 * Covers the closed-form clearance (centered + offset), the clamp when the
 * sensor leaves the box, the per-beam noise statistics, and SensorBase identity.
 */

#include "src/sim/sensors/inc/BoxClearanceLidar.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using sim::sensors::BoxClearanceLidar;
using sim::sensors::BoxClearanceLidarParams;
using sim::sensors::BoxExtents;
using sim::sensors::SensorKind;

namespace {
constexpr double kTol = 1e-12;
constexpr BoxExtents kBox{4.0, 3.0, 2.5}; // asymmetric extents catch axis mix-ups
} // namespace

/** @test Identity: the sensor is tagged Lidar with its name. */
TEST(BoxClearanceLidarTest, Identity) {
  BoxClearanceLidar s;
  EXPECT_EQ(s.kind(), SensorKind::Lidar);
  EXPECT_STREQ(s.name(), "box_clearance_lidar");
}

/** @test At the box center every clearance equals the half-extent. */
TEST(BoxClearanceLidarTest, CenteredEqualsHalfExtents) {
  BoxClearanceLidar s;
  const auto m = s.measure(0.0, 0.0, 0.0, kBox);
  EXPECT_NEAR(m.pos_x, 4.0, kTol);
  EXPECT_NEAR(m.neg_x, 4.0, kTol);
  EXPECT_NEAR(m.pos_y, 3.0, kTol);
  EXPECT_NEAR(m.neg_y, 3.0, kTol);
  EXPECT_NEAR(m.pos_z, 2.5, kTol);
  EXPECT_NEAR(m.neg_z, 2.5, kTol);
}

/** @test Offset sensor: closed-form clr_pos = half - s, clr_neg = half + s. */
TEST(BoxClearanceLidarTest, OffsetClosedForm) {
  BoxClearanceLidar s;
  const auto m = s.measure(1.0, -0.5, 2.0, kBox);
  EXPECT_NEAR(m.pos_x, 4.0 - 1.0, kTol);    // 3.0
  EXPECT_NEAR(m.neg_x, 4.0 + 1.0, kTol);    // 5.0
  EXPECT_NEAR(m.pos_y, 3.0 - (-0.5), kTol); // 3.5
  EXPECT_NEAR(m.neg_y, 3.0 + (-0.5), kTol); // 2.5
  EXPECT_NEAR(m.pos_z, 2.5 - 2.0, kTol);    // 0.5
  EXPECT_NEAR(m.neg_z, 2.5 + 2.0, kTol);    // 4.5
}

/** @test A sensor past a wall clamps that clearance to zero (never negative). */
TEST(BoxClearanceLidarTest, OutsideBoxClampsToZero) {
  BoxClearanceLidar s;
  const auto m = s.measure(5.0, 0.0, 0.0, kBox); // past the +X wall (4.0)
  EXPECT_NEAR(m.pos_x, 0.0, kTol);               // clamped
  EXPECT_NEAR(m.neg_x, 9.0, kTol);               // 4 + 5
}

/** @test Noise-free is exact regardless of the sampler. */
TEST(BoxClearanceLidarTest, ZeroSigmaIsExact) {
  BoxClearanceLidarParams p;
  p.sigma_m = 0.0;
  BoxClearanceLidar s(p);
  const auto a = s.measure(0.5, 0.5, 0.5, kBox);
  const auto b = s.measure(0.5, 0.5, 0.5, kBox);
  EXPECT_EQ(a.pos_x, b.pos_x); // deterministic, no draws
  EXPECT_NEAR(a.pos_x, 3.5, kTol);
}

/** @test With noise, a beam's error has mean ~0 and std ~sigma. */
TEST(BoxClearanceLidarTest, NoiseStatistics) {
  BoxClearanceLidarParams p;
  p.sigma_m = 0.05;
  BoxClearanceLidar s(p);
  std::vector<double> err;
  err.reserve(100000);
  for (int i = 0; i < 100000; ++i) {
    err.push_back(s.measure(0.0, 0.0, 0.0, kBox).pos_x - 4.0);
  }
  double sum = 0.0;
  for (double x : err) {
    sum += x;
  }
  const double mean = sum / static_cast<double>(err.size());
  double acc = 0.0;
  for (double x : err) {
    acc += (x - mean) * (x - mean);
  }
  const double std = std::sqrt(acc / static_cast<double>(err.size()));
  EXPECT_NEAR(mean, 0.0, 0.002);
  EXPECT_NEAR(std, 0.05, 0.002);
}

/** @test reseed reproduces the noisy stream; params() is accessible. */
TEST(BoxClearanceLidarTest, ReseedReproducesAndParams) {
  BoxClearanceLidarParams p;
  p.sigma_m = 0.1;
  BoxClearanceLidar s(p);
  EXPECT_DOUBLE_EQ(s.params().sigma_m, 0.1);
  const auto a = s.measure(0.0, 0.0, 0.0, kBox);
  s.reseed(p.seed);
  const auto b = s.measure(0.0, 0.0, 0.0, kBox);
  EXPECT_EQ(a.pos_x, b.pos_x);
}
