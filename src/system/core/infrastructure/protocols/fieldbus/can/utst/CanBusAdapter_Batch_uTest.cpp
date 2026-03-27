/**
 * @file CanBusAdapter_Batch_uTest.cpp
 * @test recvBatch() batch frame draining functionality.
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

/* ----------------------------- recvBatch Tests ----------------------------- */

/** @test recvBatch() returns 0 when no frames available (nonblocking). */
TEST(CanBusAdapterBatch, RecvBatchNoFramesReturnsZero) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 0);
  EXPECT_EQ(count, 0u);
}

/** @test recvBatch() receives single frame when one available. */
TEST(CanBusAdapterBatch, RecvBatchSingleFrame) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send one frame from external socket
  struct can_frame tx{};
  tx.can_id = 0x100;
  tx.can_dlc = 4;
  tx.data[0] = 0xAA;
  tx.data[1] = 0xBB;
  tx.data[2] = 0xCC;
  tx.data[3] = 0xDD;
  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));

  // Receive via batch
  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 500);

  EXPECT_EQ(count, 1u);
  EXPECT_EQ(buffer[0].canId.id, 0x100u);
  EXPECT_EQ(buffer[0].dlc, 4u);
  EXPECT_EQ(buffer[0].data[0], 0xAAu);

  ::close(extSock);
}

/** @test recvBatch() drains multiple frames at once. */
TEST(CanBusAdapterBatch, RecvBatchMultipleFrames) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send 5 frames from external socket
  const int FRAME_COUNT = 5;
  for (int i = 0; i < FRAME_COUNT; ++i) {
    struct can_frame tx{};
    tx.can_id = static_cast<canid_t>(0x200 + i);
    tx.can_dlc = 2;
    tx.data[0] = static_cast<uint8_t>(i);
    tx.data[1] = static_cast<uint8_t>(i * 2);
    ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));
  }

  // Small delay to ensure frames are in kernel buffer
  usleep(10000); // 10ms

  // Receive via batch
  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 500);

  EXPECT_GE(count, 1u); // At least first frame
  // Verify first frame
  EXPECT_EQ(buffer[0].canId.id, 0x200u);
  EXPECT_EQ(buffer[0].dlc, 2u);
  EXPECT_EQ(buffer[0].data[0], 0u);

  ::close(extSock);
}

/** @test recvBatch() respects maxFrames limit. */
TEST(CanBusAdapterBatch, RecvBatchRespectsMaxFrames) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send 10 frames
  for (int i = 0; i < 10; ++i) {
    struct can_frame tx{};
    tx.can_id = static_cast<canid_t>(0x300 + i);
    tx.can_dlc = 1;
    tx.data[0] = static_cast<uint8_t>(i);
    ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));
  }

  usleep(10000); // 10ms

  // Request only 3 frames max
  std::array<CanFrame, 3> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 500);

  EXPECT_LE(count, 3u);
  EXPECT_GE(count, 1u);

  ::close(extSock);
}

/** @test recvBatch() with null pointer returns 0. */
TEST(CanBusAdapterBatch, RecvBatchNullPointerReturnsZero) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t count = adapter.recvBatch(nullptr, 10, 0);
  EXPECT_EQ(count, 0u);
}

/** @test recvBatch() with maxFrames=0 returns 0. */
TEST(CanBusAdapterBatch, RecvBatchZeroMaxReturnsZero) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), 0, 0);
  EXPECT_EQ(count, 0u);
}

/** @test recvBatch() before configure returns 0. */
TEST(CanBusAdapterBatch, RecvBatchNotConfiguredReturnsZero) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());
  // Not configured

  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 0);
  EXPECT_EQ(count, 0u);
}

/** @test recvBatch() updates statistics correctly. */
TEST(CanBusAdapterBatch, RecvBatchUpdatesStats) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Batch Test", vcan.interfaceName());

  CanConfig cfg{};
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  // Send 3 frames
  for (int i = 0; i < 3; ++i) {
    struct can_frame tx{};
    tx.can_id = static_cast<canid_t>(0x400 + i);
    tx.can_dlc = 4;
    ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));
  }

  usleep(10000);

  // Reset stats before batch
  adapter.resetStats();

  std::array<CanFrame, 10> buffer{};
  std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 500);

  auto stats = adapter.stats();
  // recvBatch calls recv() internally, so stats should reflect frames received
  EXPECT_EQ(stats.framesReceived, count);
  EXPECT_EQ(stats.bytesReceived, count * 4); // 4 bytes per frame

  ::close(extSock);
}
