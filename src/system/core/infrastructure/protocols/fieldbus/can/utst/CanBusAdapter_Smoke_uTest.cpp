/**
 * @file CanBusAdapter_Smoke_uTest.cpp
 * @test “Happy path” send/recv using virtual CAN (vcan).
 */

#include "CanAdapter_TestSupport_uTest.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <vector>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

/** @test Adapter → external socket: frame fields and payload match. */
TEST(CanBusAdapterSmoke, CreationAndBasicWriteRead) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  const char* msg = "Hello";
  CanFrame tx{};
  tx.canId = CanId{.id = 0x000, .extended = false, .remote = false, .error = false};
  tx.dlc = static_cast<uint8_t>(std::strlen(msg));
  std::memcpy(tx.data.data(), msg, tx.dlc);

  // Act
  EXPECT_EQ(adapter.send(tx, /*timeoutMs*/ 500), Status::SUCCESS);

  // Assert (read on external socket)
  struct can_frame rx{};
  ssize_t n = ::read(extSock, &rx, sizeof(rx));
  ASSERT_EQ(n, (ssize_t)sizeof(rx));
  EXPECT_EQ(rx.can_id, 0x000u);
  EXPECT_EQ(rx.can_dlc, tx.dlc);
  EXPECT_EQ(std::memcmp(rx.data, tx.data.data(), tx.dlc), 0);

  ::close(extSock);
}

/** @test External socket → adapter: adapter recv() returns SUCCESS and matches fields. */
TEST(CanBusAdapterSmoke, SlaveToMasterRead) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  struct can_frame tx{};
  tx.can_id = 0x123;
  const char* payload = "CAN!";
  tx.can_dlc = static_cast<__u8>(std::strlen(payload));
  std::memcpy(tx.data, payload, tx.can_dlc);

  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));

  // Act
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ 500);

  // Assert
  EXPECT_EQ(st, Status::SUCCESS);
  EXPECT_FALSE(rx.canId.extended);
  EXPECT_EQ(rx.canId.id, 0x123u);
  EXPECT_EQ(rx.dlc, tx.can_dlc);
  EXPECT_EQ(std::memcmp(rx.data.data(), tx.data, rx.dlc), 0);

  ::close(extSock);
}

/** @test Adapter → external: zero-length payload (dlc=0) transmits cleanly. */
TEST(CanBusAdapterSmoke, WriteReadZeroLengthPayload) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  CanFrame tx{};
  tx.canId = CanId{.id = 0x055, .extended = false, .remote = false, .error = false};
  tx.dlc = 0; // no payload

  // Act
  EXPECT_EQ(adapter.send(tx, /*timeoutMs*/ 500), Status::SUCCESS);

  // Assert
  struct can_frame rx{};
  ASSERT_EQ(::read(extSock, &rx, sizeof(rx)), (ssize_t)sizeof(rx));
  EXPECT_EQ(rx.can_id, 0x055u);
  EXPECT_EQ(rx.can_dlc, 0);
  ::close(extSock);
}

/** @test Adapter → external: extended identifier frame (29-bit) transmitted correctly. */
TEST(CanBusAdapterSmoke, WriteReadExtendedId) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  const uint32_t EXT_ID = 0x1ABCDE3; // within 29-bit range
  const std::vector<uint8_t> PAYLOAD = {0xCA, 0xFE, 0xBA, 0xBE};

  CanFrame tx{};
  tx.canId = CanId{.id = EXT_ID, .extended = true, .remote = false, .error = false};
  tx.dlc = static_cast<uint8_t>(PAYLOAD.size());
  std::memcpy(tx.data.data(), PAYLOAD.data(), tx.dlc);

  // Act
  EXPECT_EQ(adapter.send(tx, /*timeoutMs*/ 500), Status::SUCCESS);

  // Assert
  struct can_frame rx{};
  ASSERT_EQ(::read(extSock, &rx, sizeof(rx)), (ssize_t)sizeof(rx));
  // driver encodes extended IDs with CAN_EFF_FLAG; the raw id bits sit under CAN_EFF_MASK
  EXPECT_TRUE((rx.can_id & CAN_EFF_FLAG) != 0);
  EXPECT_EQ(rx.can_id & CAN_EFF_MASK, EXT_ID);
  EXPECT_EQ(rx.can_dlc, tx.dlc);
  EXPECT_EQ(std::memcmp(rx.data, tx.data.data(), tx.dlc), 0);

  ::close(extSock);
}

/** @test Adapter → external: remote (RTR) frame is encoded correctly. */
TEST(CanBusAdapterSmoke, WriteReadRemoteFrame) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange
  CanConfig cfg;
  cfg.loopback = true;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  CanFrame tx{};
  tx.canId = CanId{.id = 0x222, .extended = false, .remote = true, .error = false};
  tx.dlc = 4; // RTR frames carry a length, no payload

  // Act
  EXPECT_EQ(adapter.send(tx, /*timeoutMs*/ 500), Status::SUCCESS);

  // Assert
  struct can_frame rx{};
  ASSERT_EQ(::read(extSock, &rx, sizeof(rx)), (ssize_t)sizeof(rx));
  EXPECT_TRUE((rx.can_id & CAN_RTR_FLAG) != 0);
  EXPECT_EQ(rx.can_id & CAN_SFF_MASK, 0x222u);
  EXPECT_EQ(rx.can_dlc, 4);

  ::close(extSock);
}

/** @test Filters drop unmatched frames → recv() WOULD_BLOCK within bounded wait. */
TEST(CanBusAdapterSmoke, FiltersDropUnmatchedFramesWouldBlock) {
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "vcan kernel module not available";
  }
  CANBusAdapter adapter("Smoke CAN", vcan.interfaceName());

  // Arrange: only accept ID 0x321, send 0x123 externally.
  CanConfig cfg;
  cfg.loopback = true;
  cfg.filters.push_back({/*id*/ 0x321, /*mask*/ 0x7FF, /*extended*/ false});
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  int extSock = createTestCANSocket(vcan.interfaceName());
  ASSERT_GE(extSock, 0);

  struct can_frame tx{};
  tx.can_id = 0x123; // does NOT match the filter above
  tx.can_dlc = 2;
  tx.data[0] = 0xAB;
  tx.data[1] = 0xCD;
  ASSERT_EQ(::write(extSock, &tx, sizeof(tx)), (ssize_t)sizeof(tx));

  // Act: adapter should not see the frame due to filter → WOULD_BLOCK.
  CanFrame rx{};
  Status st = adapter.recv(rx, /*timeoutMs*/ 200);
  EXPECT_EQ(st, Status::WOULD_BLOCK);

  ::close(extSock);
}
