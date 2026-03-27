/**
 * @file CanStats_uTest.cpp
 * @test CanStats struct and CANBusAdapter statistics tracking.
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::CanStats;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

/* ----------------------------- Default Construction ----------------------------- */

/** @test CanStats default construction initializes all counters to zero. */
TEST(CanStatsStruct, DefaultConstruction) {
  CanStats stats{};
  EXPECT_EQ(stats.framesSent, 0u);
  EXPECT_EQ(stats.framesReceived, 0u);
  EXPECT_EQ(stats.errorFrames, 0u);
  EXPECT_EQ(stats.sendWouldBlock, 0u);
  EXPECT_EQ(stats.recvWouldBlock, 0u);
  EXPECT_EQ(stats.sendErrors, 0u);
  EXPECT_EQ(stats.recvErrors, 0u);
  EXPECT_EQ(stats.bytesTransmitted, 0u);
  EXPECT_EQ(stats.bytesReceived, 0u);
}

/* ----------------------------- Helper Methods ----------------------------- */

/** @test CanStats::reset() sets all counters to zero. */
TEST(CanStatsStruct, ResetSetsAllToZero) {
  CanStats stats{};
  stats.framesSent = 100;
  stats.framesReceived = 200;
  stats.errorFrames = 5;
  stats.sendWouldBlock = 10;
  stats.recvWouldBlock = 20;
  stats.sendErrors = 3;
  stats.recvErrors = 4;
  stats.bytesTransmitted = 800;
  stats.bytesReceived = 1600;

  stats.reset();

  EXPECT_EQ(stats.framesSent, 0u);
  EXPECT_EQ(stats.framesReceived, 0u);
  EXPECT_EQ(stats.errorFrames, 0u);
  EXPECT_EQ(stats.sendWouldBlock, 0u);
  EXPECT_EQ(stats.recvWouldBlock, 0u);
  EXPECT_EQ(stats.sendErrors, 0u);
  EXPECT_EQ(stats.recvErrors, 0u);
  EXPECT_EQ(stats.bytesTransmitted, 0u);
  EXPECT_EQ(stats.bytesReceived, 0u);
}

/** @test CanStats::totalFrames() returns sum of sent and received. */
TEST(CanStatsStruct, TotalFramesReturnsSum) {
  CanStats stats{};
  stats.framesSent = 50;
  stats.framesReceived = 75;
  EXPECT_EQ(stats.totalFrames(), 125u);
}

/** @test CanStats::totalErrors() returns sum of all error counters. */
TEST(CanStatsStruct, TotalErrorsReturnsSum) {
  CanStats stats{};
  stats.sendErrors = 3;
  stats.recvErrors = 5;
  stats.errorFrames = 2;
  EXPECT_EQ(stats.totalErrors(), 10u);
}

/** @test CanStats::totalBytes() returns sum of transmitted and received. */
TEST(CanStatsStruct, TotalBytesReturnsSum) {
  CanStats stats{};
  stats.bytesTransmitted = 1000;
  stats.bytesReceived = 2000;
  EXPECT_EQ(stats.totalBytes(), 3000u);
}

/* ----------------------------- Adapter Stats Integration ----------------------------- */

/** @test CANBusAdapter stats() returns zero counters after construction. */
TEST(CanStatsAdapter, StatsZeroAfterConstruction) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  CanStats s = adapter.stats();
  EXPECT_EQ(s.framesSent, 0u);
  EXPECT_EQ(s.framesReceived, 0u);
  EXPECT_EQ(s.bytesTransmitted, 0u);
  EXPECT_EQ(s.bytesReceived, 0u);
}

/** @test CANBusAdapter send() increments framesSent and bytesTransmitted. */
TEST(CanStatsAdapter, SendIncrementsCounters) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send a frame with 4 bytes payload
  CanFrame tx{};
  tx.canId = CanId{.id = 0x100, .extended = false, .remote = false, .error = false};
  tx.dlc = 4;
  tx.data = {0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0};

  EXPECT_EQ(adapter.send(tx, 500), Status::SUCCESS);

  CanStats s = adapter.stats();
  EXPECT_EQ(s.framesSent, 1u);
  EXPECT_EQ(s.bytesTransmitted, 4u);

  // Send another frame with 8 bytes
  tx.dlc = 8;
  EXPECT_EQ(adapter.send(tx, 500), Status::SUCCESS);

  s = adapter.stats();
  EXPECT_EQ(s.framesSent, 2u);
  EXPECT_EQ(s.bytesTransmitted, 12u);

  ::close(extSock);
}

/** @test CANBusAdapter recv() increments framesReceived and bytesReceived. */
TEST(CanStatsAdapter, RecvIncrementsCounters) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send from external socket
  struct can_frame tx{};
  tx.can_id = 0x200;
  tx.can_dlc = 3;
  tx.data[0] = 0xAA;
  tx.data[1] = 0xBB;
  tx.data[2] = 0xCC;
  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));

  // Receive on adapter
  CanFrame rx{};
  EXPECT_EQ(adapter.recv(rx, 500), Status::SUCCESS);

  CanStats s = adapter.stats();
  EXPECT_EQ(s.framesReceived, 1u);
  EXPECT_EQ(s.bytesReceived, 3u);

  // Send another frame
  tx.can_dlc = 6;
  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));
  EXPECT_EQ(adapter.recv(rx, 500), Status::SUCCESS);

  s = adapter.stats();
  EXPECT_EQ(s.framesReceived, 2u);
  EXPECT_EQ(s.bytesReceived, 9u);

  ::close(extSock);
}

/** @test CANBusAdapter recv() with no data increments recvWouldBlock. */
TEST(CanStatsAdapter, RecvWouldBlockIncrementsCounter) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Try to receive with no data available (nonblocking)
  CanFrame rx{};
  Status st = adapter.recv(rx, 0); // timeoutMs=0 = nonblocking
  EXPECT_EQ(st, Status::WOULD_BLOCK);

  CanStats s = adapter.stats();
  EXPECT_EQ(s.recvWouldBlock, 1u);

  // Try again
  st = adapter.recv(rx, 0);
  EXPECT_EQ(st, Status::WOULD_BLOCK);

  s = adapter.stats();
  EXPECT_EQ(s.recvWouldBlock, 2u);
}

/** @test CANBusAdapter resetStats() clears all counters. */
TEST(CanStatsAdapter, ResetStatsClearsCounters) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Generate some activity
  CanFrame tx{};
  tx.canId = CanId{.id = 0x300, .extended = false, .remote = false, .error = false};
  tx.dlc = 5;
  EXPECT_EQ(adapter.send(tx, 500), Status::SUCCESS);

  CanStats s = adapter.stats();
  EXPECT_GT(s.framesSent, 0u);

  // Reset stats
  adapter.resetStats();

  s = adapter.stats();
  EXPECT_EQ(s.framesSent, 0u);
  EXPECT_EQ(s.framesReceived, 0u);
  EXPECT_EQ(s.bytesTransmitted, 0u);
  EXPECT_EQ(s.bytesReceived, 0u);
  EXPECT_EQ(s.sendWouldBlock, 0u);
  EXPECT_EQ(s.recvWouldBlock, 0u);

  ::close(extSock);
}

/** @test CANBusAdapter stats are cumulative across multiple operations. */
TEST(CanStatsAdapter, StatsCumulative) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Stats Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send 5 frames
  CanFrame tx{};
  tx.canId = CanId{.id = 0x400, .extended = false, .remote = false, .error = false};
  tx.dlc = 2;

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(adapter.send(tx, 500), Status::SUCCESS);
  }

  CanStats s = adapter.stats();
  EXPECT_EQ(s.framesSent, 5u);
  EXPECT_EQ(s.bytesTransmitted, 10u); // 5 * 2 bytes

  ::close(extSock);
}
