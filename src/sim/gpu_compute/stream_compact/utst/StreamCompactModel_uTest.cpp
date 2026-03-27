/**
 * @file StreamCompactModel_uTest.cpp
 * @brief Unit tests for StreamCompactModel (CPU-side behavior).
 */

#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactModel.hpp"

#include <gtest/gtest.h>

using sim::gpu_compute::StreamCompactModel;
using system_core::system_component::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction produces uninitialized model. */
TEST(StreamCompactModel, DefaultConstruction) {
  StreamCompactModel model;
  EXPECT_FALSE(model.isInitialized());
  EXPECT_EQ(model.state().kickCount, 0u);
}

/* ----------------------------- Component Identity ----------------------------- */

/** @test Component ID is 133. */
TEST(StreamCompactModel, ComponentId) {
  StreamCompactModel model;
  EXPECT_EQ(model.componentId(), 133);
}

/** @test Component name is StreamCompactModel. */
TEST(StreamCompactModel, ComponentName) {
  StreamCompactModel model;
  EXPECT_STREQ(model.componentName(), "StreamCompactModel");
}

/** @test Label returns STREAM_COMPACT. */
TEST(StreamCompactModel, Label) {
  StreamCompactModel model;
  EXPECT_STREQ(model.label(), "STREAM_COMPACT");
}

/* ----------------------------- Initialization ----------------------------- */

/** @test init() succeeds. */
TEST(StreamCompactModel, InitSucceeds) {
  StreamCompactModel model;
  auto status = model.init();
  EXPECT_EQ(status, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(model.isInitialized());
}

/** @test Two tasks registered after init. */
TEST(StreamCompactModel, TaskCountIsTwo) {
  StreamCompactModel model;
  (void)model.init();
  EXPECT_EQ(model.taskCount(), 2u);
}

/* ----------------------------- Task Access ----------------------------- */

/** @test Task lookup by KICK UID returns valid task. */
TEST(StreamCompactModel, TaskByUidKick) {
  StreamCompactModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(StreamCompactModel::TaskUid::KICK));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup by POLL UID returns valid task. */
TEST(StreamCompactModel, TaskByUidPoll) {
  StreamCompactModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(StreamCompactModel::TaskUid::POLL));
  ASSERT_NE(task, nullptr);
}

/* ----------------------------- Kick/Poll Execution ----------------------------- */

/** @test kick() increments kickCount. */
TEST(StreamCompactModel, KickIncrementsCount) {
  StreamCompactModel model;
  (void)model.init();
  EXPECT_EQ(model.kick(), 0);
  EXPECT_EQ(model.state().kickCount, 1u);
}

/** @test poll() returns 0 when no GPU work in flight. */
TEST(StreamCompactModel, PollNoWorkReturnsZero) {
  StreamCompactModel model;
  (void)model.init();
  EXPECT_EQ(model.poll(), 0);
}

/* ----------------------------- Tunable Parameters ----------------------------- */

/** @test Default tunable parameters. */
TEST(StreamCompactModel, DefaultTunableParams) {
  StreamCompactModel model;
  const auto& p = model.tunableParams();
  EXPECT_EQ(p.fieldWidth, 2048u);
  EXPECT_EQ(p.fieldHeight, 2048u);
  EXPECT_FLOAT_EQ(p.threshold, 0.5f);
  EXPECT_EQ(p.classCount, 8u);
}
