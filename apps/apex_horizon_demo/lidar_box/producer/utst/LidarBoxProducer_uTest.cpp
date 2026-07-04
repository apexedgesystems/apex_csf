/**
 * @file LidarBoxProducer_uTest.cpp
 * @brief Tests for the lidar_box producer: the locked LBOX/v2 wire layout, the
 *        in-box trajectory guarantee, the mounted-distance invariants, the
 *        streamed scene block, and frame publishing.
 */

#include "apps/apex_horizon_demo/lidar_box/producer/inc/LidarBoxProducer.hpp"

#include <cmath>

#include <gtest/gtest.h>

using appsim::lidar_box::kMountRadius;
using appsim::lidar_box::LidarBoxFrame;
using appsim::lidar_box::LidarBoxProducer;
using appsim::lidar_box::LidarBoxTunables;
using sim::sensors::BoxClearanceLidar;
using sim::sensors::BoxExtents;

namespace {

// Independent slab cross-check for a mounted beam (mirrors the contract math,
// not the implementation call path).
double slabBeam(double px, double py, double pz, double dx, double dy, double dz,
                const BoxExtents& box, double mount_r) {
  return BoxClearanceLidar::rayToWall(px, py, pz, dx, dy, dz, box) - mount_r;
}

} // namespace

/* ----------------------------- Wire contract ----------------------------- */

/** @test The frame matches the locked LBOX/v2 layout byte-for-byte. */
TEST(LidarBoxContractTest, FrameLayoutIsLocked) {
  EXPECT_EQ(sizeof(LidarBoxFrame), 64u);
  EXPECT_EQ(alignof(LidarBoxFrame), 8u);
  EXPECT_EQ(offsetof(LidarBoxFrame, pos_x), 0u);
  EXPECT_EQ(offsetof(LidarBoxFrame, yaw_rad), 12u);
  EXPECT_EQ(offsetof(LidarBoxFrame, dist_bx_pos), 16u);
  EXPECT_EQ(offsetof(LidarBoxFrame, dist_bz_neg), 36u);
  EXPECT_EQ(offsetof(LidarBoxFrame, timestamp_ns), 40u);
  EXPECT_EQ(offsetof(LidarBoxFrame, box_half_x), 48u);
  EXPECT_EQ(offsetof(LidarBoxFrame, mount_radius), 60u);
  EXPECT_EQ(appsim::lidar_box::kAppMagic, 0x4C424F58u); // "LBOX"
  EXPECT_EQ(appsim::lidar_box::kAppVersion, 2u);
}

/* ----------------------------- Component identity ----------------------------- */

/** @test Identity fields match the app assignment. */
TEST(LidarBoxProducerTest, Identity) {
  LidarBoxProducer p;
  EXPECT_EQ(p.componentId(), 230);
  EXPECT_STREQ(p.componentName(), "LidarBoxProducer");
  EXPECT_STREQ(p.label(), "LIDAR_BOX");
}

/* ----------------------------- Trajectory guarantees ----------------------------- */

/** @test Over a long run the body center never leaves +/-(half - mount_radius). */
TEST(LidarBoxProducerTest, BodyStaysInsideInsetBox) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  const LidarBoxTunables& t = p.tunables().get();

  const double LIM_X = t.box_half_x_m - t.mount_radius_m;
  const double LIM_Y = t.box_half_y_m - t.mount_radius_m;
  const double LIM_Z = t.box_half_z_m - t.mount_radius_m;

  for (int i = 0; i < 30000; ++i) { // 10 min of sim at 50 Hz
    (void)p.bodyStep();
    const auto& s = p.bodyState();
    ASSERT_LE(std::abs(s.pos_x_m), LIM_X + 1e-12);
    ASSERT_LE(std::abs(s.pos_y_m), LIM_Y + 1e-12);
    ASSERT_LE(std::abs(s.pos_z_m), LIM_Z + 1e-12);
  }
}

/** @test Every published mounted distance stays >= 0 (the pod-tip floor). */
TEST(LidarBoxProducerTest, DistancesNeverNegative) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);

  for (int i = 0; i < 30000; ++i) {
    (void)p.bodyStep();
    const auto& f = p.frame();
    ASSERT_GE(f.dist_bx_pos, -1e-6f);
    ASSERT_GE(f.dist_bx_neg, -1e-6f);
    ASSERT_GE(f.dist_by_pos, -1e-6f);
    ASSERT_GE(f.dist_by_neg, -1e-6f);
    ASSERT_GE(f.dist_bz_pos, -1e-6f);
    ASSERT_GE(f.dist_bz_neg, -1e-6f);
  }
}

/* ----------------------------- Mounted-distance invariants ----------------------------- */

/** @test The Z pair sums to the constant chord: 2*half_z - 2*mount_radius. */
TEST(LidarBoxProducerTest, ZPairSumIsConstantChord) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  const LidarBoxTunables& t = p.tunables().get();
  const float EXPECT_SUM = static_cast<float>(2.0 * t.box_half_z_m - 2.0 * t.mount_radius_m);

  for (int i = 0; i < 500; ++i) {
    (void)p.bodyStep();
    const auto& f = p.frame();
    ASSERT_NEAR(f.dist_bz_pos + f.dist_bz_neg, EXPECT_SUM, 1e-4f);
  }
}

/** @test X/Y beams match an independent slab computation at sampled steps. */
TEST(LidarBoxProducerTest, BodyAxisBeamsMatchSlabMath) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  const LidarBoxTunables& t = p.tunables().get();
  const BoxExtents BOX{t.box_half_x_m, t.box_half_y_m, t.box_half_z_m};

  for (int i = 0; i < 2000; ++i) {
    (void)p.bodyStep();
    if (i % 100 != 0) {
      continue;
    }
    const auto& s = p.bodyState();
    const auto& f = p.frame();
    const double C = std::cos(s.yaw_rad);
    const double S = std::sin(s.yaw_rad);
    const double R = t.mount_radius_m;
    EXPECT_NEAR(f.dist_bx_pos, slabBeam(s.pos_x_m, s.pos_y_m, s.pos_z_m, C, S, 0.0, BOX, R), 1e-4);
    EXPECT_NEAR(f.dist_bx_neg, slabBeam(s.pos_x_m, s.pos_y_m, s.pos_z_m, -C, -S, 0.0, BOX, R),
                1e-4);
    EXPECT_NEAR(f.dist_by_pos, slabBeam(s.pos_x_m, s.pos_y_m, s.pos_z_m, -S, C, 0.0, BOX, R), 1e-4);
    EXPECT_NEAR(f.dist_by_neg, slabBeam(s.pos_x_m, s.pos_y_m, s.pos_z_m, S, -C, 0.0, BOX, R), 1e-4);
  }
}

/* ----------------------------- Scene block ----------------------------- */

/** @test The frame streams the tunable-owned scene (box halves + mount). */
TEST(LidarBoxProducerTest, SceneBlockStreamsTunables) {
  LidarBoxProducer p;
  LidarBoxTunables t;
  t.box_half_x_m = 5.0;
  t.box_half_y_m = 2.0;
  t.box_half_z_m = 1.5;
  t.mount_radius_m = 0.25;
  p.tunables().set(t);
  ASSERT_EQ(p.init(), 0u);
  (void)p.bodyStep();
  const auto& f = p.frame();
  EXPECT_NEAR(f.box_half_x, 5.0f, 1e-6f);
  EXPECT_NEAR(f.box_half_y, 2.0f, 1e-6f);
  EXPECT_NEAR(f.box_half_z, 1.5f, 1e-6f);
  EXPECT_NEAR(f.mount_radius, 0.25f, 1e-6f);
  // And the trajectory respects the custom scene's inset.
  for (int i = 0; i < 2000; ++i) {
    (void)p.bodyStep();
    ASSERT_LE(std::abs(p.bodyState().pos_y_m), 2.0 - 0.25 + 1e-12);
    ASSERT_GE(p.frame().dist_by_pos, -1e-6f);
  }
}

/* ----------------------------- Frame consistency ----------------------------- */

/** @test The frame pose matches the state. */
TEST(LidarBoxProducerTest, FrameMatchesState) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  for (int i = 0; i < 100; ++i) {
    (void)p.bodyStep();
  }
  const auto& s = p.bodyState();
  const auto& f = p.frame();
  EXPECT_NEAR(f.pos_x, static_cast<float>(s.pos_x_m), 1e-6f);
  EXPECT_NEAR(f.pos_y, static_cast<float>(s.pos_y_m), 1e-6f);
  EXPECT_NEAR(f.pos_z, static_cast<float>(s.pos_z_m), 1e-6f);
  EXPECT_NEAR(f.yaw_rad, static_cast<float>(s.yaw_rad), 1e-6f);
}

/** @test Yaw stays wrapped to [-pi, pi]; timestamps are monotonic. */
TEST(LidarBoxProducerTest, YawWrapsAndTimestampsAdvance) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  std::uint64_t prev_ts = 0;
  for (int i = 0; i < 2000; ++i) { // 40 s: several yaw revolutions at 0.35 rad/s
    (void)p.bodyStep();
    const auto& f = p.frame();
    ASSERT_LE(std::abs(f.yaw_rad), 3.1415927f + 1e-5f);
    ASSERT_GE(f.timestamp_ns, prev_ts);
    prev_ts = f.timestamp_ns;
  }
}

/** @test A zero amplitude fraction pins that axis at the box center. */
TEST(LidarBoxProducerTest, ZeroAmplitudePinsAxis) {
  LidarBoxProducer p;
  LidarBoxTunables t;
  t.amp_frac_z = 0.0;
  p.tunables().set(t);
  ASSERT_EQ(p.init(), 0u);
  for (int i = 0; i < 500; ++i) {
    (void)p.bodyStep();
  }
  EXPECT_NEAR(p.frame().pos_z, 0.0f, 1e-6f);
  // Centered Z pod: distance = half_z - mount_radius.
  EXPECT_NEAR(p.frame().dist_bz_pos, static_cast<float>(t.box_half_z_m - t.mount_radius_m), 1e-4f);
}

/** @test Out-of-range amplitude fractions clamp to [0, 1]; negative yaw wraps. */
TEST(LidarBoxProducerTest, FractionClampsAndNegativeYawWrap) {
  LidarBoxProducer p;
  LidarBoxTunables t;
  t.amp_frac_x = -0.5;     // clamps to 0 -> axis pinned
  t.amp_frac_y = 1.5;      // clamps to 1 -> full travel budget, still in-box
  t.yaw_rate_rad_s = -0.9; // exercises the negative wrap branch
  p.tunables().set(t);
  ASSERT_EQ(p.init(), 0u);

  const double LIM_Y = t.box_half_y_m - t.mount_radius_m;
  for (int i = 0; i < 2000; ++i) { // 40 s: several negative yaw revolutions
    (void)p.bodyStep();
    const auto& s = p.bodyState();
    ASSERT_NEAR(s.pos_x_m, 0.0, 1e-12);            // pinned by the 0-clamp
    ASSERT_LE(std::abs(s.pos_y_m), LIM_Y + 1e-12); // 1-clamp respects the budget
    ASSERT_LE(std::abs(p.frame().yaw_rad), 3.1415927f + 1e-5f);
  }
}

/** @test telemetryTick is callable and returns success (log wiring is optional). */
TEST(LidarBoxProducerTest, TelemetryTickSucceeds) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  (void)p.bodyStep();
  EXPECT_EQ(p.telemetryTick(), 0u);
}

/** @test The seed default constants match the tunable defaults. */
TEST(LidarBoxContractTest, SeedDefaultsMatchTunables) {
  LidarBoxTunables t;
  EXPECT_NEAR(t.box_half_x_m, static_cast<double>(appsim::lidar_box::kBoxHalfX), 1e-12);
  EXPECT_NEAR(t.mount_radius_m, static_cast<double>(kMountRadius), 1e-12);
}
