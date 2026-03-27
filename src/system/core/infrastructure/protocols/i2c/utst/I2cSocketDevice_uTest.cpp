/**
 * @file I2cSocketDevice_uTest.cpp
 * @brief Unit tests for I2cSocketDevice (I2C over Unix socketpair).
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cSocketDevice.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

/* ----------------------------- Helpers ----------------------------- */

/// Create a socketpair and return both fds.
static std::pair<int, int> makeSocketPair() {
  int fds[2] = {-1, -1};
  EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  return {fds[0], fds[1]};
}

/**
 * @brief Simple I2C peer that implements the wire protocol.
 *
 * Behavior:
 *  - Reads header [addr:2][wLen:2][rLen:2]
 *  - Reads wLen bytes of write data
 *  - If addr == 0x00, responds with NACK (status=1)
 *  - Otherwise ACK (status=0) and rLen bytes of incrementing data
 */
static void i2cPeer(int fd) {
  while (true) {
    struct {
      std::uint16_t addr;
      std::uint16_t wLen;
      std::uint16_t rLen;
    } __attribute__((packed)) header{};

    auto n = ::read(fd, &header, sizeof(header));
    if (n <= 0) {
      break;
    }

    // Read write data (consume and discard)
    if (header.wLen > 0) {
      std::array<std::uint8_t, 1024> wBuf{};
      std::size_t received = 0;
      while (received < header.wLen) {
        n = ::read(fd, wBuf.data() + received, header.wLen - received);
        if (n <= 0) {
          ::close(fd);
          return;
        }
        received += static_cast<std::size_t>(n);
      }
    }

    // NACK for address 0x00
    if (header.addr == 0x00) {
      std::uint8_t status = 1;
      if (::write(fd, &status, 1) < 0) {
        break;
      }
      continue;
    }

    // ACK
    std::uint8_t status = 0;
    if (::write(fd, &status, 1) < 0) {
      break;
    }

    // Send read data: incrementing bytes starting at addr low byte
    if (header.rLen > 0) {
      std::array<std::uint8_t, 1024> rBuf{};
      for (std::size_t i = 0; i < header.rLen; ++i) {
        rBuf[i] = static_cast<std::uint8_t>((header.addr + i) & 0xFF);
      }
      std::size_t sent = 0;
      while (sent < header.rLen) {
        n = ::write(fd, rBuf.data() + sent, header.rLen - sent);
        if (n <= 0) {
          ::close(fd);
          return;
        }
        sent += static_cast<std::size_t>(n);
      }
    }
  }
  ::close(fd);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test I2cSocketDevice with invalid fd reports not open. */
TEST(I2cSocketDeviceTest, InvalidFdNotOpen) {
  apex::protocols::i2c::I2cSocketDevice dev(-1, false);
  EXPECT_FALSE(dev.isOpen());
  EXPECT_EQ(dev.fd(), -1);
}

/** @test I2cSocketDevice configure fails on invalid fd. */
TEST(I2cSocketDeviceTest, ConfigureInvalidFd) {
  apex::protocols::i2c::I2cSocketDevice dev(-1, false);
  apex::protocols::i2c::I2cConfig cfg;
  EXPECT_EQ(dev.configure(cfg), apex::protocols::i2c::Status::ERROR_CLOSED);
}

/* ----------------------------- Configure ----------------------------- */

/** @test Configure succeeds with valid fd. */
TEST(I2cSocketDeviceTest, ConfigureSuccess) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  EXPECT_EQ(dev.configure(cfg), apex::protocols::i2c::Status::SUCCESS);
  EXPECT_TRUE(dev.isOpen());
  ::close(peerFd);
}

/* ----------------------------- Set Slave Address ----------------------------- */

/** @test setSlaveAddress fails before configure. */
TEST(I2cSocketDeviceTest, SetAddrBeforeConfigure) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  EXPECT_EQ(dev.setSlaveAddress(0x50), apex::protocols::i2c::Status::ERROR_NOT_CONFIGURED);
  ::close(peerFd);
}

/** @test setSlaveAddress stores the address. */
TEST(I2cSocketDeviceTest, SetAddrStored) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x68);
  EXPECT_EQ(dev.slaveAddress(), 0x68);
  ::close(peerFd);
}

/* ----------------------------- Read ----------------------------- */

/** @test Read from valid slave address. */
TEST(I2cSocketDeviceTest, ReadSuccess) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(i2cPeer, peerFd);

  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x50);

  std::array<std::uint8_t, 4> buf{};
  std::size_t bytesRead = 0;
  EXPECT_EQ(dev.read(buf.data(), 4, bytesRead, -1), apex::protocols::i2c::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 4U);

  // Peer sends incrementing from addr low byte (0x50)
  EXPECT_EQ(buf[0], 0x50);
  EXPECT_EQ(buf[1], 0x51);
  EXPECT_EQ(buf[2], 0x52);
  EXPECT_EQ(buf[3], 0x53);

  (void)dev.close();
  peer.join();
}

/** @test Read from NACK address returns ERROR_NACK. */
TEST(I2cSocketDeviceTest, ReadNack) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(i2cPeer, peerFd);

  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x00); // Peer NACKs address 0x00

  std::array<std::uint8_t, 2> buf{};
  std::size_t bytesRead = 0;
  EXPECT_EQ(dev.read(buf.data(), 2, bytesRead, -1), apex::protocols::i2c::Status::ERROR_NACK);
  EXPECT_EQ(bytesRead, 0U);
  EXPECT_EQ(dev.stats().nackCount, 1U);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- Write ----------------------------- */

/** @test Write to valid slave address. */
TEST(I2cSocketDeviceTest, WriteSuccess) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(i2cPeer, peerFd);

  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x50);

  std::array<std::uint8_t, 3> data = {0x10, 0x20, 0x30};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(dev.write(data.data(), 3, bytesWritten, -1), apex::protocols::i2c::Status::SUCCESS);
  EXPECT_EQ(bytesWritten, 3U);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- WriteRead ----------------------------- */

/** @test WriteRead (register read pattern). */
TEST(I2cSocketDeviceTest, WriteReadSuccess) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(i2cPeer, peerFd);

  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x68);

  std::uint8_t regAddr = 0x0D;
  std::array<std::uint8_t, 2> readBuf{};
  std::size_t bytesRead = 0;
  EXPECT_EQ(dev.writeRead(&regAddr, 1, readBuf.data(), 2, bytesRead, -1),
            apex::protocols::i2c::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 2U);

  // Peer sends incrementing from addr low byte (0x68)
  EXPECT_EQ(readBuf[0], 0x68);
  EXPECT_EQ(readBuf[1], 0x69);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- Statistics ----------------------------- */

/** @test Stats track bytes and operations. */
TEST(I2cSocketDeviceTest, StatsTracking) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(i2cPeer, peerFd);

  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x50);

  // Write 3 bytes
  std::array<std::uint8_t, 3> wData = {1, 2, 3};
  std::size_t bw = 0;
  (void)dev.write(wData.data(), 3, bw, -1);

  // Read 4 bytes
  std::array<std::uint8_t, 4> rBuf{};
  std::size_t br = 0;
  (void)dev.read(rBuf.data(), 4, br, -1);

  EXPECT_EQ(dev.stats().bytesTx, 3U);
  EXPECT_EQ(dev.stats().bytesRx, 4U);
  EXPECT_EQ(dev.stats().writesCompleted, 1U);
  EXPECT_EQ(dev.stats().readsCompleted, 1U);

  dev.resetStats();
  EXPECT_EQ(dev.stats().bytesTx, 0U);
  EXPECT_EQ(dev.stats().bytesRx, 0U);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- Close ----------------------------- */

/** @test Operations fail after close. */
TEST(I2cSocketDeviceTest, OperationsAfterClose) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.setSlaveAddress(0x50);
  (void)dev.close();

  std::uint8_t buf = 0;
  std::size_t n = 0;
  EXPECT_EQ(dev.read(&buf, 1, n, -1), apex::protocols::i2c::Status::ERROR_CLOSED);
  EXPECT_EQ(dev.write(&buf, 1, n, -1), apex::protocols::i2c::Status::ERROR_CLOSED);
  ::close(peerFd);
}

/** @test Double close returns error. */
TEST(I2cSocketDeviceTest, DoubleClose) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev.configure(cfg);
  EXPECT_EQ(dev.close(), apex::protocols::i2c::Status::SUCCESS);
  EXPECT_EQ(dev.close(), apex::protocols::i2c::Status::ERROR_CLOSED);
  ::close(peerFd);
}

/* ----------------------------- Device Path ----------------------------- */

/** @test devicePath returns "socketpair". */
TEST(I2cSocketDeviceTest, DevicePath) {
  apex::protocols::i2c::I2cSocketDevice dev(-1, false);
  EXPECT_STREQ(dev.devicePath(), "socketpair");
}

/* ----------------------------- Move Semantics ----------------------------- */

/** @test Move constructor transfers ownership. */
TEST(I2cSocketDeviceTest, MoveConstructor) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::i2c::I2cSocketDevice dev1(driverFd);
  apex::protocols::i2c::I2cConfig cfg;
  (void)dev1.configure(cfg);

  apex::protocols::i2c::I2cSocketDevice dev2(std::move(dev1));
  EXPECT_TRUE(dev2.isOpen());
  EXPECT_EQ(dev2.fd(), driverFd);
  EXPECT_FALSE(dev1.isOpen()); // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(dev1.fd(), -1);    // NOLINT(bugprone-use-after-move)

  ::close(peerFd);
}
