/**
 * @file MockPps_uTest.cpp
 * @brief Unit tests for MockPps.
 */

#include "src/system/core/hal/mock/inc/MockPps.hpp"

#include <gtest/gtest.h>

using apex::hal::MockPps;
using apex::hal::PpsConfig;
using apex::hal::PpsEdge;
using apex::hal::PpsStatus;

/* ----------------------------- Lifecycle ----------------------------- */

/** @test Default construction leaves the mock uninitialized. */
TEST(MockPps, DefaultUninitialized) {
  MockPps pps;
  EXPECT_FALSE(pps.isInitialized());
  EXPECT_EQ(pps.pulseCount(), 0U);
  EXPECT_EQ(pps.pendingEdges(), 0U);
}

/** @test readCapture before init returns ERROR_NOT_INIT. */
TEST(MockPps, ReadCaptureUninitializedReturnsError) {
  MockPps pps;
  int64_t ts = -1;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_NOT_INIT);
  EXPECT_EQ(ts, -1); // Untouched on error.
}

/** @test init succeeds and flips the initialized flag. */
TEST(MockPps, InitSucceeds) {
  MockPps pps;
  EXPECT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.isInitialized());
}

/** @test deinit clears the initialized flag. */
TEST(MockPps, DeinitClearsInitialized) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.deinit();
  EXPECT_FALSE(pps.isInitialized());
}

/** @test init stores the configuration verbatim. */
TEST(MockPps, ConfigStored) {
  MockPps pps;
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  ASSERT_EQ(pps.init(cfg), PpsStatus::OK);
  EXPECT_EQ(pps.config().edge, PpsEdge::FALLING);
}

/* ----------------------------- Edge consumption ----------------------------- */

/** @test readCapture with empty queue returns NO_NEW_EDGE. */
TEST(MockPps, NoEdgeReturnsNoNewEdge) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test Single inject + read returns the injected timestamp. */
TEST(MockPps, InjectThenRead) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_TRUE(pps.injectEdge(1'234'567'890));

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 1'234'567'890);
}

/** @test Multiple injects come back in FIFO order. */
TEST(MockPps, InjectFifoOrder) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectEdge(100);
  pps.injectEdge(200);
  pps.injectEdge(300);

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 100);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 200);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 300);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);
}

/** @test Drained queue accepts and replays new injects. */
TEST(MockPps, InjectAfterDrain) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);

  pps.injectEdge(1);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);

  pps.injectEdge(2);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 2);
}

/* ----------------------------- Counters ----------------------------- */

/** @test pulseCount increments on each accepted inject. */
TEST(MockPps, PulseCountTracksInjects) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  for (int i = 0; i < 5; ++i) {
    pps.injectEdge(i);
  }
  EXPECT_EQ(pps.pulseCount(), 5U);
}

/** @test stats.captureCount increments only on OK reads. */
TEST(MockPps, CaptureCountTracksOkReads) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectEdge(1);
  pps.injectEdge(2);
  pps.injectEdge(3);

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK); // captureCount = 1
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK); // captureCount = 2
  // NO_NEW_EDGE is not yet possible; consume the third.
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK); // captureCount = 3
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::NO_NEW_EDGE);

  EXPECT_EQ(pps.stats().captureCount, 3U);
  EXPECT_EQ(pps.stats().errorCount, 0U);
}

/** @test resetStats zeroes counters but keeps pulseCount and queue. */
TEST(MockPps, ResetStatsKeepsPulseAndQueue) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectEdge(1);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
  pps.injectEdge(2); // Pending: 1 edge.

  ASSERT_EQ(pps.stats().captureCount, 1U);
  pps.resetStats();
  EXPECT_EQ(pps.stats().captureCount, 0U);
  EXPECT_EQ(pps.stats().errorCount, 0U);
  EXPECT_EQ(pps.pulseCount(), 2U);   // unchanged
  EXPECT_EQ(pps.pendingEdges(), 1U); // unchanged
}

/* ----------------------------- Error injection ----------------------------- */

/** @test injectError makes the next reads return ERROR_DEVICE. */
TEST(MockPps, InjectErrorReturnsErrorDevice) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectEdge(42);
  pps.injectError(2);

  int64_t ts = 0;
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);

  // Error budget exhausted; the queued edge becomes available.
  EXPECT_EQ(pps.readCapture(ts), PpsStatus::OK);
  EXPECT_EQ(ts, 42);
}

/** @test Error reads do not consume queued edges. */
TEST(MockPps, InjectErrorDoesNotConsumeEdges) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectEdge(1);
  pps.injectError(1);

  ASSERT_EQ(pps.pendingEdges(), 1U);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);
  EXPECT_EQ(pps.pendingEdges(), 1U); // Still queued.
}

/** @test Error reads bump errorCount but not captureCount. */
TEST(MockPps, ErrorCountTracksFailedReads) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  pps.injectError(3);

  int64_t ts = 0;
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE);
  }
  EXPECT_EQ(pps.stats().errorCount, 3U);
  EXPECT_EQ(pps.stats().captureCount, 0U);
}

/* ----------------------------- Series + capacity ----------------------------- */

/** @test injectEdgeSeries lays down evenly spaced edges. */
TEST(MockPps, InjectEdgeSeries) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);

  // 4 edges at 1 Hz starting at t=10s.
  EXPECT_EQ(pps.injectEdgeSeries(10'000'000'000LL, 1'000'000'000LL, 4), 4U);

  int64_t ts = 0;
  for (int64_t i = 0; i < 4; ++i) {
    ASSERT_EQ(pps.readCapture(ts), PpsStatus::OK);
    EXPECT_EQ(ts, 10'000'000'000LL + i * 1'000'000'000LL);
  }
}

/** @test injectEdge fails when the queue is full; injectEdgeSeries reports
 *        partial acceptance. */
TEST(MockPps, QueueFullRejectsAdditional) {
  MockPps pps;
  ASSERT_EQ(pps.init({}), PpsStatus::OK);

  for (size_t i = 0; i < MockPps::MAX_PENDING_EDGES; ++i) {
    EXPECT_TRUE(pps.injectEdge(static_cast<int64_t>(i)));
  }
  EXPECT_FALSE(pps.injectEdge(999));
  EXPECT_EQ(pps.pendingEdges(), MockPps::MAX_PENDING_EDGES);

  // injectEdgeSeries reports actual accepted count when capacity runs out.
  pps.resetAll();
  ASSERT_EQ(pps.init({}), PpsStatus::OK);
  EXPECT_EQ(pps.injectEdgeSeries(0, 1, MockPps::MAX_PENDING_EDGES + 5),
            static_cast<uint32_t>(MockPps::MAX_PENDING_EDGES));
}

/* ----------------------------- resetAll ----------------------------- */

/** @test resetAll clears queue, counts, and config; init state is untouched. */
TEST(MockPps, ResetAllClears) {
  MockPps pps;
  PpsConfig cfg;
  cfg.edge = PpsEdge::FALLING;
  ASSERT_EQ(pps.init(cfg), PpsStatus::OK);
  pps.injectEdge(1);
  pps.injectError(1);
  int64_t ts = 0;
  ASSERT_EQ(pps.readCapture(ts), PpsStatus::ERROR_DEVICE); // bumps errorCount

  pps.resetAll();

  EXPECT_TRUE(pps.isInitialized()); // still initialized
  EXPECT_EQ(pps.pulseCount(), 0U);
  EXPECT_EQ(pps.pendingEdges(), 0U);
  EXPECT_EQ(pps.errorBudget(), 0U);
  EXPECT_EQ(pps.stats().captureCount, 0U);
  EXPECT_EQ(pps.stats().errorCount, 0U);
  EXPECT_EQ(pps.config().edge, PpsEdge::RISING); // back to default
}
