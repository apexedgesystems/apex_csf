/**
 * @file BatchStatsModel_uTest.cpp
 * @brief Unit tests for BatchStatsModel (CPU-side behavior).
 */

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsModel.hpp"

#include <gtest/gtest.h>

using sim::gpu_compute::BatchStatsModel;
using system_core::system_component::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction produces uninitialized model. */
TEST(BatchStatsModel, DefaultConstruction) {
  BatchStatsModel model;
  EXPECT_FALSE(model.isInitialized());
  EXPECT_EQ(model.state().kickCount, 0u);
  EXPECT_EQ(model.state().completeCount, 0u);
}

/* ----------------------------- Component Identity ----------------------------- */

/** @test Component ID is 132. */
TEST(BatchStatsModel, ComponentId) {
  BatchStatsModel model;
  EXPECT_EQ(model.componentId(), 132);
}

/** @test Component name is BatchStatsModel. */
TEST(BatchStatsModel, ComponentName) {
  BatchStatsModel model;
  EXPECT_STREQ(model.componentName(), "BatchStatsModel");
}

/** @test Label returns BATCH_STATS. */
TEST(BatchStatsModel, Label) {
  BatchStatsModel model;
  EXPECT_STREQ(model.label(), "BATCH_STATS");
}

/* ----------------------------- Initialization ----------------------------- */

/** @test init() succeeds. */
TEST(BatchStatsModel, InitSucceeds) {
  BatchStatsModel model;
  auto status = model.init();
  EXPECT_EQ(status, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(model.isInitialized());
}

/** @test Two tasks registered after init. */
TEST(BatchStatsModel, TaskCountIsTwo) {
  BatchStatsModel model;
  (void)model.init();
  EXPECT_EQ(model.taskCount(), 2u);
}

/* ----------------------------- Task Access ----------------------------- */

/** @test Task lookup by KICK UID returns valid task. */
TEST(BatchStatsModel, TaskByUidKick) {
  BatchStatsModel model;
  (void)model.init();

  auto* task = model.taskByUid(static_cast<std::uint8_t>(BatchStatsModel::TaskUid::KICK));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup by POLL UID returns valid task. */
TEST(BatchStatsModel, TaskByUidPoll) {
  BatchStatsModel model;
  (void)model.init();

  auto* task = model.taskByUid(static_cast<std::uint8_t>(BatchStatsModel::TaskUid::POLL));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup for unknown UID returns nullptr. */
TEST(BatchStatsModel, TaskByUidUnknownReturnsNull) {
  BatchStatsModel model;
  (void)model.init();
  EXPECT_EQ(model.taskByUid(99), nullptr);
}

/* ----------------------------- Kick/Poll Execution ----------------------------- */

/** @test kick() increments kickCount regardless of GPU availability. */
TEST(BatchStatsModel, KickIncrementsCount) {
  BatchStatsModel model;
  (void)model.init();

  auto status = model.kick();
  EXPECT_EQ(status, 0);
  EXPECT_EQ(model.state().kickCount, 1u);

  status = model.kick();
  EXPECT_EQ(status, 0);
  EXPECT_EQ(model.state().kickCount, 2u);
}

/** @test poll() returns 0 when no GPU work in flight. */
TEST(BatchStatsModel, PollNoWorkReturnsZero) {
  BatchStatsModel model;
  (void)model.init();

  auto status = model.poll();
  EXPECT_EQ(status, 0);
}

/** @test kick via task interface works. */
TEST(BatchStatsModel, KickViaTaskInterface) {
  BatchStatsModel model;
  (void)model.init();

  auto* task = model.taskByUid(static_cast<std::uint8_t>(BatchStatsModel::TaskUid::KICK));
  ASSERT_NE(task, nullptr);

  auto status = task->execute();
  EXPECT_EQ(status, 0);
  EXPECT_EQ(model.state().kickCount, 1u);
}

/* ----------------------------- Tunable Parameters ----------------------------- */

/** @test Default tunable parameters have expected values. */
TEST(BatchStatsModel, DefaultTunableParams) {
  BatchStatsModel model;
  const auto& p = model.tunableParams();
  EXPECT_EQ(p.elementCount, 1u << 20);
  EXPECT_EQ(p.groupSize, 4096u);
  EXPECT_EQ(p.histogramBins, 64u);
  EXPECT_FLOAT_EQ(p.histogramMin, -10.0f);
  EXPECT_FLOAT_EQ(p.histogramMax, 10.0f);
}
