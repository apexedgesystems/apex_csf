/**
 * @file LidarBoxProducer_uTest.cpp
 * @brief Tests for the lidar_box producer: wire-contract layout, the in-box
 *        trajectory guarantee, clearance consistency, and frame publishing.
 */

#include "apps/apex_horizon_demo/lidar_box/producer/inc/LidarBoxProducer.hpp"

#include <cmath>

#include <gtest/gtest.h>

using appsim::lidar_box::kBodyRadius;
using appsim::lidar_box::kBoxHalfX;
using appsim::lidar_box::kBoxHalfY;
using appsim::lidar_box::kBoxHalfZ;
using appsim::lidar_box::LidarBoxFrame;
using appsim::lidar_box::LidarBoxProducer;
using appsim::lidar_box::LidarBoxTunables;

/* ----------------------------- Wire contract ----------------------------- */

/** @test The frame matches the locked LBOX/v1 layout byte-for-byte. */
TEST(LidarBoxContractTest, FrameLayoutIsLocked) {
  EXPECT_EQ(sizeof(LidarBoxFrame), 48u);
  EXPECT_EQ(alignof(LidarBoxFrame), 8u);
  EXPECT_EQ(offsetof(LidarBoxFrame, pos_x), 0u);
  EXPECT_EQ(offsetof(LidarBoxFrame, yaw_rad), 12u);
  EXPECT_EQ(offsetof(LidarBoxFrame, clr_pos_x), 16u);
  EXPECT_EQ(offsetof(LidarBoxFrame, clr_neg_z), 36u);
  EXPECT_EQ(offsetof(LidarBoxFrame, timestamp_ns), 40u);
  EXPECT_EQ(appsim::lidar_box::kAppMagic, 0x4C424F58u); // "LBOX"
  EXPECT_EQ(appsim::lidar_box::kAppVersion, 1u);
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

/** @test Over a long run the body center never leaves +/-(half - radius). */
TEST(LidarBoxProducerTest, BodyStaysInsideInsetBox) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);

  const double LIM_X = static_cast<double>(kBoxHalfX - kBodyRadius);
  const double LIM_Y = static_cast<double>(kBoxHalfY - kBodyRadius);
  const double LIM_Z = static_cast<double>(kBoxHalfZ - kBodyRadius);

  for (int i = 0; i < 30000; ++i) { // 10 min of sim at 50 Hz
    (void)p.bodyStep();
    const auto& s = p.bodyState();
    ASSERT_LE(std::abs(s.pos_x_m), LIM_X + 1e-12);
    ASSERT_LE(std::abs(s.pos_y_m), LIM_Y + 1e-12);
    ASSERT_LE(std::abs(s.pos_z_m), LIM_Z + 1e-12);
  }
}

/** @test Every published clearance is >= kBodyRadius (the inset guarantee). */
TEST(LidarBoxProducerTest, ClearancesNeverBelowBodyRadius) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);

  for (int i = 0; i < 30000; ++i) {
    (void)p.bodyStep();
    const auto& f = p.frame();
    ASSERT_GE(f.clr_pos_x, kBodyRadius - 1e-6f);
    ASSERT_GE(f.clr_neg_x, kBodyRadius - 1e-6f);
    ASSERT_GE(f.clr_pos_y, kBodyRadius - 1e-6f);
    ASSERT_GE(f.clr_neg_y, kBodyRadius - 1e-6f);
    ASSERT_GE(f.clr_pos_z, kBodyRadius - 1e-6f);
    ASSERT_GE(f.clr_neg_z, kBodyRadius - 1e-6f);
  }
}

/** @test Opposite clearances sum to the box size (closed-form consistency). */
TEST(LidarBoxProducerTest, OppositeClearancesSumToBoxSize) {
  LidarBoxProducer p;
  ASSERT_EQ(p.init(), 0u);
  (void)p.bodyStep();
  const auto& f = p.frame();
  EXPECT_NEAR(f.clr_pos_x + f.clr_neg_x, 2.0f * kBoxHalfX, 1e-4f);
  EXPECT_NEAR(f.clr_pos_y + f.clr_neg_y, 2.0f * kBoxHalfY, 1e-4f);
  EXPECT_NEAR(f.clr_pos_z + f.clr_neg_z, 2.0f * kBoxHalfZ, 1e-4f);
}

/** @test The frame matches the state, and the clearances match the pose. */
TEST(LidarBoxProducerTest, FrameMatchesStateAndPose) {
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
  // Ideal lidar (default sigma = 0): clearance is exactly half - pos.
  EXPECT_NEAR(f.clr_pos_x, kBoxHalfX - f.pos_x, 1e-4f);
  EXPECT_NEAR(f.clr_neg_y, kBoxHalfY + f.pos_y, 1e-4f);
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
  EXPECT_NEAR(p.frame().clr_pos_z, kBoxHalfZ, 1e-4f);
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

  const double LIM_Y = static_cast<double>(kBoxHalfY - kBodyRadius);
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
