/**
 * @file CanBusAdapter_Timeouts_uTest.cpp
 * @test Timeout semantics and delayed producer behavior.
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <thread>
#include <vector>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

/** @test Bounded wait with no data → WOULD_BLOCK. */
TEST(CanBusAdapterTimeouts, ReadTimeoutWouldBlock) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Timeout CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Act
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ 500);

  // Assert
  EXPECT_EQ(st, Status::WOULD_BLOCK);
}

/** @test A delayed external write is picked up by adapter::recv() within the wait window. */
TEST(CanBusAdapterTimeouts, DelayedWrite) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Timeout CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  std::vector<uint8_t> testData = {'D', 'E', 'L', 'A', 'Y'};
  std::thread writer([extSock, testData]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    struct can_frame frame{};
    frame.can_id = 0x000;
    frame.can_dlc = static_cast<__u8>(testData.size());
    std::memcpy(frame.data, testData.data(), frame.can_dlc);
    ssize_t n = -1;
    do {
      n = ::write(extSock, &frame, sizeof(frame));
    } while (n < 0 && errno == EINTR);
    (void)n;
  });

  // Act
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ 500);

  // Assert
  EXPECT_EQ(st, Status::SUCCESS);
  EXPECT_EQ(rx.dlc, testData.size());
  EXPECT_EQ(std::memcmp(rx.data.data(), testData.data(), rx.dlc), 0);

  writer.join();
  ::close(extSock);
}

/** @test Nonblocking read (timeoutMs=0) with no data → immediate WOULD_BLOCK. */
TEST(CanBusAdapterTimeouts, ReadNonblockingZeroTimeout) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Timeout CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Act
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ 0);

  // Assert
  EXPECT_EQ(st, Status::WOULD_BLOCK);
}

/** @test Block-forever read (timeoutMs=-1) returns SUCCESS once a delayed writer sends. */
TEST(CanBusAdapterTimeouts, ReadBlocksForeverUntilData) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Timeout CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  const std::vector<uint8_t> TEST_DATA = {0xBE, 0xEF};
  std::thread writer([extSock, TEST_DATA]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    struct can_frame frame{};
    frame.can_id = 0x7FF; // max standard ID
    frame.can_dlc = static_cast<__u8>(TEST_DATA.size());
    std::memcpy(frame.data, TEST_DATA.data(), frame.can_dlc);
    ssize_t n = -1;
    do {
      n = ::write(extSock, &frame, sizeof(frame));
    } while (n < 0 && errno == EINTR);
    (void)n;
  });

  // Act
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ -1);

  // Assert
  EXPECT_EQ(st, Status::SUCCESS);
  EXPECT_EQ(rx.dlc, TEST_DATA.size());
  EXPECT_EQ(std::memcmp(rx.data.data(), TEST_DATA.data(), rx.dlc), 0);

  writer.join();
  ::close(extSock);
}

/**
 * @test Late arrival scenario:
 *       First bounded wait expires with WOULD_BLOCK; a subsequent wait succeeds.
 */
TEST(CanBusAdapterTimeouts, LateArrivalThenSuccess) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Timeout CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  const std::vector<uint8_t> TEST_DATA = {1, 2, 3, 4};
  std::thread writer([extSock, TEST_DATA]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    struct can_frame frame{};
    frame.can_id = 0x100;
    frame.can_dlc = static_cast<__u8>(TEST_DATA.size());
    std::memcpy(frame.data, TEST_DATA.data(), frame.can_dlc);
    ssize_t n = -1;
    do {
      n = ::write(extSock, &frame, sizeof(frame));
    } while (n < 0 && errno == EINTR);
    (void)n;
  });

  // Act 1: short timeout → WOULD_BLOCK
  CanFrame rx{};
  Status st1 = adapter.recv(rx, /*timeoutMs*/ 50);
  EXPECT_EQ(st1, Status::WOULD_BLOCK);

  // Act 2: longer timeout → SUCCESS after writer fires
  Status st2 = adapter.recv(rx, /*timeoutMs*/ 300);
  EXPECT_EQ(st2, Status::SUCCESS);
  EXPECT_EQ(rx.dlc, TEST_DATA.size());
  EXPECT_EQ(std::memcmp(rx.data.data(), TEST_DATA.data(), rx.dlc), 0);

  writer.join();
  ::close(extSock);
}
