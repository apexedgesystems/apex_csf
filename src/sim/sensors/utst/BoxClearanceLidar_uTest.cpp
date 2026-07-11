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

/* ----------------------------- rayToWall (slab primitive) ----------------------------- */

/** @test An axis-aligned ray reduces to the closed-form wall distance. */
TEST(BoxClearanceLidarTest, RayToWallAxisAlignedReducesToClosedForm) {
  EXPECT_NEAR(BoxClearanceLidar::rayToWall(1.0, 0.0, 0.0, 1.0, 0.0, 0.0, kBox), 3.0, kTol);
  EXPECT_NEAR(BoxClearanceLidar::rayToWall(1.0, 0.0, 0.0, -1.0, 0.0, 0.0, kBox), 5.0, kTol);
  EXPECT_NEAR(BoxClearanceLidar::rayToWall(0.0, -0.5, 0.0, 0.0, 1.0, 0.0, kBox), 3.5, kTol);
  EXPECT_NEAR(BoxClearanceLidar::rayToWall(0.0, 0.0, 2.0, 0.0, 0.0, -1.0, kBox), 4.5, kTol);
}

/** @test A diagonal ray takes the nearest wall (the slab minimum). */
TEST(BoxClearanceLidarTest, RayToWallDiagonalTakesNearestWall) {
  // From the origin along (1,1,0)/sqrt(2): x wall at t = 4*sqrt(2) ~ 5.657,
  // y wall at t = 3*sqrt(2) ~ 4.243 -> the y wall wins.
  const double kInvSqrt2 = 0.7071067811865476;
  const double t = BoxClearanceLidar::rayToWall(0.0, 0.0, 0.0, kInvSqrt2, kInvSqrt2, 0.0, kBox);
  EXPECT_NEAR(t, 3.0 / kInvSqrt2, 1e-9);
}

/* ----------------------------- measureMounted (body-fixed) ----------------------------- */

/** @test At zero yaw, mounted distances are the world-axis clearances minus the mount. */
TEST(BoxClearanceLidarTest, MountedZeroYawIsClearanceMinusMount) {
  BoxClearanceLidar s;
  const double R = 0.5;
  const auto w = s.measure(1.0, -0.5, 2.0, kBox);
  const auto m = s.measureMounted(1.0, -0.5, 2.0, 0.0, R, kBox);
  EXPECT_NEAR(m.pos_x, w.pos_x - R, kTol);
  EXPECT_NEAR(m.neg_x, w.neg_x - R, kTol);
  EXPECT_NEAR(m.pos_y, w.pos_y - R, kTol);
  EXPECT_NEAR(m.neg_y, w.neg_y - R, kTol);
  EXPECT_NEAR(m.pos_z, w.pos_z - R, kTol);
  EXPECT_NEAR(m.neg_z, w.neg_z - R, kTol);
}

/** @test At yaw = +90 deg, the body X pair ranges along world +/-Y. */
TEST(BoxClearanceLidarTest, MountedQuarterTurnSwapsAxes) {
  BoxClearanceLidar s;
  constexpr double kHalfPi = 1.5707963267948966;
  const double R = 0.5;
  const auto m = s.measureMounted(1.0, -0.5, 0.0, kHalfPi, R, kBox);
  // body +X -> world +Y: distance = (3.0 - (-0.5)) - R = 3.0
  EXPECT_NEAR(m.pos_x, 3.5 - R, 1e-9);
  // body -X -> world -Y: (3.0 + (-0.5)) - R = 2.0
  EXPECT_NEAR(m.neg_x, 2.5 - R, 1e-9);
  // body +Y -> world -X: (4.0 + 1.0) - R = 4.5
  EXPECT_NEAR(m.pos_y, 5.0 - R, 1e-9);
  // body -Y -> world +X: (4.0 - 1.0) - R = 2.5
  EXPECT_NEAR(m.neg_y, 3.0 - R, 1e-9);
}

/** @test A pod touching its wall reads exactly zero (the contact definition). */
TEST(BoxClearanceLidarTest, MountedPodAtWallReadsZero) {
  BoxClearanceLidar s;
  const double R = 0.5;
  // Body center at the +X inset limit: pod tip exactly on the wall.
  const auto m = s.measureMounted(kBox.half_x - R, 0.0, 0.0, 0.0, R, kBox);
  EXPECT_NEAR(m.pos_x, 0.0, kTol);
}

/** @test Mounted distances reproduce hand-derived golden vectors.
 *
 * The expected values are pencil-and-paper ground truth from the slab
 * geometry -- not the output of any implementation -- so an error coded
 * identically on both ends of the wire (a mis-signed rotation, a wrong slab
 * minimum) still fails here. The same table is asserted verbatim by the
 * consumer's suite; 1e-4 m is the agreed cross-suite tolerance.
 */
TEST(BoxClearanceLidarTest, MountedGoldenVectorsMatchHandDerivedTruth) {
  constexpr double kR = 0.5;
  constexpr double kHalfPi = 1.5707963267948966;
  constexpr double kQuarterPi = 0.7853981633974483;
  // Yaw 45 deg at the center: both in-plane rays meet the Y slabs first,
  // t = 3 / (sqrt(2)/2) = 3*sqrt(2), minus the mount.
  const double kDiag = 3.0 * std::sqrt(2.0) - kR; // 3.7426407
  struct GoldenRow {
    double x, y, z, yaw;
    double exp[6]; // bx+, bx-, by+, by-, bz+, bz-
  };
  const GoldenRow kRows[] = {
      {0.0, 0.0, 0.0, 0.0, {3.5, 3.5, 2.5, 2.5, 2.0, 2.0}},     // half - mount
      {1.5, -1.0, 0.75, 0.0, {2.0, 5.0, 3.5, 1.5, 1.25, 2.75}}, // per-axis offsets, both signs
      {0.0, 0.0, 0.0, kHalfPi, {2.5, 2.5, 3.5, 3.5, 2.0, 2.0}}, // axis rotation sign/swap
      {0.0, 0.0, 0.0, kQuarterPi, {kDiag, kDiag, kDiag, kDiag, 2.0, 2.0}}, // slab-min selection
  };

  BoxClearanceLidar s;
  for (const auto& r : kRows) {
    const auto m = s.measureMounted(r.x, r.y, r.z, r.yaw, kR, kBox);
    const double got[6] = {m.pos_x, m.neg_x, m.pos_y, m.neg_y, m.pos_z, m.neg_z};
    for (int i = 0; i < 6; ++i) {
      EXPECT_NEAR(got[i], r.exp[i], 1e-4)
          << "row (" << r.x << "," << r.y << "," << r.z << ", yaw=" << r.yaw << ") beam " << i;
    }
  }
}

/** @test Inside the inset region every mounted reading stays non-negative. */
TEST(BoxClearanceLidarTest, MountedFloorIsZeroInsideInset) {
  BoxClearanceLidar s;
  const double R = 0.5;
  for (int iy = 0; iy < 16; ++iy) { // yaw sweep
    const double yaw = 0.4 * static_cast<double>(iy);
    for (int ip = 0; ip < 8; ++ip) { // positions toward the inset corner
      const double f = static_cast<double>(ip) / 7.0;
      const auto m = s.measureMounted(f * (kBox.half_x - R), f * (kBox.half_y - R),
                                      f * (kBox.half_z - R), yaw, R, kBox);
      ASSERT_GE(m.pos_x, -kTol);
      ASSERT_GE(m.neg_x, -kTol);
      ASSERT_GE(m.pos_y, -kTol);
      ASSERT_GE(m.neg_y, -kTol);
      ASSERT_GE(m.pos_z, -kTol);
      ASSERT_GE(m.neg_z, -kTol);
    }
  }
}
