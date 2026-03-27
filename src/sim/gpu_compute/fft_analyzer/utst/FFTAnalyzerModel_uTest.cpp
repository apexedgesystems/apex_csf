/**
 * @file FFTAnalyzerModel_uTest.cpp
 * @brief Unit tests for FFTAnalyzerModel (CPU-side behavior).
 */

#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerModel.hpp"

#include <gtest/gtest.h>

using sim::gpu_compute::FFTAnalyzerModel;
using system_core::system_component::Status;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction produces uninitialized model. */
TEST(FFTAnalyzerModel, DefaultConstruction) {
  FFTAnalyzerModel model;
  EXPECT_FALSE(model.isInitialized());
  EXPECT_EQ(model.state().kickCount, 0u);
}

/* ----------------------------- Component Identity ----------------------------- */

/** @test Component ID is 131. */
TEST(FFTAnalyzerModel, ComponentId) {
  FFTAnalyzerModel model;
  EXPECT_EQ(model.componentId(), 131);
}

/** @test Component name is FFTAnalyzerModel. */
TEST(FFTAnalyzerModel, ComponentName) {
  FFTAnalyzerModel model;
  EXPECT_STREQ(model.componentName(), "FFTAnalyzerModel");
}

/** @test Label returns FFT_ANALYZER. */
TEST(FFTAnalyzerModel, Label) {
  FFTAnalyzerModel model;
  EXPECT_STREQ(model.label(), "FFT_ANALYZER");
}

/* ----------------------------- Initialization ----------------------------- */

/** @test init() succeeds. */
TEST(FFTAnalyzerModel, InitSucceeds) {
  FFTAnalyzerModel model;
  auto status = model.init();
  EXPECT_EQ(status, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(model.isInitialized());
}

/** @test Two tasks registered after init. */
TEST(FFTAnalyzerModel, TaskCountIsTwo) {
  FFTAnalyzerModel model;
  (void)model.init();
  EXPECT_EQ(model.taskCount(), 2u);
}

/* ----------------------------- Task Access ----------------------------- */

/** @test Task lookup by KICK UID returns valid task. */
TEST(FFTAnalyzerModel, TaskByUidKick) {
  FFTAnalyzerModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(FFTAnalyzerModel::TaskUid::KICK));
  ASSERT_NE(task, nullptr);
}

/** @test Task lookup by POLL UID returns valid task. */
TEST(FFTAnalyzerModel, TaskByUidPoll) {
  FFTAnalyzerModel model;
  (void)model.init();
  auto* task = model.taskByUid(static_cast<std::uint8_t>(FFTAnalyzerModel::TaskUid::POLL));
  ASSERT_NE(task, nullptr);
}

/* ----------------------------- Kick/Poll Execution ----------------------------- */

/** @test kick() increments kickCount. */
TEST(FFTAnalyzerModel, KickIncrementsCount) {
  FFTAnalyzerModel model;
  (void)model.init();
  EXPECT_EQ(model.kick(), 0);
  EXPECT_EQ(model.state().kickCount, 1u);
}

/** @test poll() returns 0 when no GPU work in flight. */
TEST(FFTAnalyzerModel, PollNoWorkReturnsZero) {
  FFTAnalyzerModel model;
  (void)model.init();
  EXPECT_EQ(model.poll(), 0);
}

/* ----------------------------- Tunable Parameters ----------------------------- */

/** @test Default tunable parameters. */
TEST(FFTAnalyzerModel, DefaultTunableParams) {
  FFTAnalyzerModel model;
  const auto& p = model.tunableParams();
  EXPECT_EQ(p.channelCount, 256u);
  EXPECT_EQ(p.samplesPerChannel, 4096u);
  EXPECT_FLOAT_EQ(p.sampleRateHz, 10000.0f);
  EXPECT_FLOAT_EQ(p.peakThresholdDb, -20.0f);
}
