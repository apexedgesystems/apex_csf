/**
 * @file ConvFilterModel_uTest.cpp
 * @brief Unit tests for ConvFilterModel (CPU-side behavior).
 */

#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterModel.hpp"

#include <gtest/gtest.h>

using sim::gpu_compute::ConvFilterModel;
using system_core::system_component::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction produces uninitialized model. */
TEST(ConvFilterModel, DefaultConstruction) {
  ConvFilterModel model;
  EXPECT_FALSE(model.isInitialized());
  EXPECT_EQ(model.state().kickCount, 0u);
}

/* ----------------------------- Component Identity ----------------------------- */

/** @test Component ID is 130. */
TEST(ConvFilterModel, ComponentId) {
  ConvFilterModel model;
  EXPECT_EQ(model.componentId(), 130);
}

/** @test Component name is ConvFilterModel. */
TEST(ConvFilterModel, ComponentName) {
  ConvFilterModel model;
  EXPECT_STREQ(model.componentName(), "ConvFilterModel");
}

/** @test Label returns CONV_FILTER. */
TEST(ConvFilterModel, Label) {
  ConvFilterModel model;
  EXPECT_STREQ(model.label(), "CONV_FILTER");
}

/* ----------------------------- Initialization ----------------------------- */

/** @test init() succeeds. */
TEST(ConvFilterModel, InitSucceeds) {
  ConvFilterModel model;
  auto status = model.init();
  EXPECT_EQ(status, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(model.isInitialized());
}

/** @test Two tasks registered after init. */
TEST(ConvFilterModel, TaskCountIsTwo) {
  ConvFilterModel model;
  (void)model.init();
  EXPECT_EQ(model.taskCount(), 2u);
}

/* ----------------------------- Task Access ----------------------------- */

/** @test Task lookup by KICK UID returns valid task. */
TEST(ConvFilterModel, TaskByUidKick) {
  ConvFilterModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(ConvFilterModel::TaskUid::KICK));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup by POLL UID returns valid task. */
TEST(ConvFilterModel, TaskByUidPoll) {
  ConvFilterModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(ConvFilterModel::TaskUid::POLL));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup for unknown UID returns nullptr. */
TEST(ConvFilterModel, TaskByUidUnknownReturnsNull) {
  ConvFilterModel model;
  (void)model.init();
  EXPECT_EQ(model.taskByUid(99), nullptr);
}

/* ----------------------------- Kick/Poll Execution ----------------------------- */

/** @test kick() increments kickCount regardless of GPU availability. */
TEST(ConvFilterModel, KickIncrementsCount) {
  ConvFilterModel model;
  (void)model.init();

  EXPECT_EQ(model.kick(), 0);
  EXPECT_EQ(model.state().kickCount, 1u);

  EXPECT_EQ(model.kick(), 0);
  EXPECT_EQ(model.state().kickCount, 2u);
}

/** @test poll() returns 0 when no GPU work in flight. */
TEST(ConvFilterModel, PollNoWorkReturnsZero) {
  ConvFilterModel model;
  (void)model.init();
  EXPECT_EQ(model.poll(), 0);
}

/* ----------------------------- Tunable Parameters ----------------------------- */

/** @test Default tunable parameters have expected values. */
TEST(ConvFilterModel, DefaultTunableParams) {
  ConvFilterModel model;
  const auto& p = model.tunableParams();
  EXPECT_EQ(p.imageWidth, 2048u);
  EXPECT_EQ(p.imageHeight, 2048u);
  EXPECT_EQ(p.kernelRadius, 3u);
  EXPECT_EQ(p.kernelType, 0u);
  EXPECT_FLOAT_EQ(p.gaussianSigma, 1.5f);
}
