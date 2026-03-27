/**
 * @file SpiSocketDevice_uTest.cpp
 * @brief Unit tests for SpiSocketDevice (SPI over Unix socketpair).
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiSocketDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

/// Simple SPI peer: reads [len:4][txData:len], writes back txData XOR 0xFF.
static void spiPeer(int fd) {
  while (true) {
    std::uint32_t len = 0;
    auto n = ::read(fd, &len, sizeof(len));
    if (n <= 0) {
      break;
    }

    std::array<std::uint8_t, 1024> buf{};
    std::size_t received = 0;
    while (received < len) {
      n = ::read(fd, buf.data() + received, len - received);
      if (n <= 0) {
        ::close(fd);
        return;
      }
      received += static_cast<std::size_t>(n);
    }

    // XOR all bytes as "response"
    for (std::size_t i = 0; i < len; ++i) {
      buf[i] ^= 0xFF;
    }

    std::size_t sent = 0;
    while (sent < len) {
      n = ::write(fd, buf.data() + sent, len - sent);
      if (n <= 0) {
        ::close(fd);
        return;
      }
      sent += static_cast<std::size_t>(n);
    }
  }
  ::close(fd);
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test SpiSocketDevice with invalid fd reports not open. */
TEST(SpiSocketDeviceTest, InvalidFdNotOpen) {
  apex::protocols::spi::SpiSocketDevice dev(-1, false);
  EXPECT_FALSE(dev.isOpen());
  EXPECT_EQ(dev.fd(), -1);
}

/** @test SpiSocketDevice configure fails on invalid fd. */
TEST(SpiSocketDeviceTest, ConfigureInvalidFd) {
  apex::protocols::spi::SpiSocketDevice dev(-1, false);
  apex::protocols::spi::SpiConfig cfg;
  EXPECT_EQ(dev.configure(cfg), apex::protocols::spi::Status::ERROR_CLOSED);
}

/* ----------------------------- Configure ----------------------------- */

/** @test SpiSocketDevice configure succeeds with valid fd. */
TEST(SpiSocketDeviceTest, ConfigureSuccess) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  EXPECT_EQ(dev.configure(cfg), apex::protocols::spi::Status::SUCCESS);
  EXPECT_TRUE(dev.isOpen());
  ::close(peerFd);
}

/** @test SpiSocketDevice stores config after configure. */
TEST(SpiSocketDeviceTest, ConfigStored) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  cfg.maxSpeedHz = 4000000;
  cfg.mode = apex::protocols::spi::SpiMode::MODE_3;
  (void)dev.configure(cfg);
  EXPECT_EQ(dev.config().maxSpeedHz, 4000000U);
  EXPECT_EQ(dev.config().mode, apex::protocols::spi::SpiMode::MODE_3);
  ::close(peerFd);
}

/* ----------------------------- Transfer ----------------------------- */

/** @test Transfer fails before configure. */
TEST(SpiSocketDeviceTest, TransferBeforeConfigure) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  std::uint8_t tx = 0x42;
  std::uint8_t rx = 0;
  EXPECT_EQ(dev.transfer(&tx, &rx, 1, -1), apex::protocols::spi::Status::ERROR_NOT_CONFIGURED);
  ::close(peerFd);
}

/** @test Full-duplex transfer with peer. */
TEST(SpiSocketDeviceTest, FullDuplexTransfer) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(spiPeer, peerFd);

  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);

  std::array<std::uint8_t, 4> txBuf = {0x01, 0x02, 0x03, 0x04};
  std::array<std::uint8_t, 4> rxBuf = {};
  EXPECT_EQ(dev.transfer(txBuf.data(), rxBuf.data(), 4, -1), apex::protocols::spi::Status::SUCCESS);

  // Peer XORs with 0xFF
  EXPECT_EQ(rxBuf[0], 0xFE);
  EXPECT_EQ(rxBuf[1], 0xFD);
  EXPECT_EQ(rxBuf[2], 0xFC);
  EXPECT_EQ(rxBuf[3], 0xFB);

  (void)dev.close();
  peer.join();
}

/** @test Zero-length transfer succeeds immediately. */
TEST(SpiSocketDeviceTest, ZeroLengthTransfer) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);

  EXPECT_EQ(dev.transfer(nullptr, nullptr, 0, -1), apex::protocols::spi::Status::SUCCESS);
  ::close(peerFd);
}

/** @test Write-only transfer (rxData null). */
TEST(SpiSocketDeviceTest, WriteOnlyTransfer) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(spiPeer, peerFd);

  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);

  std::array<std::uint8_t, 2> txBuf = {0xAA, 0xBB};
  EXPECT_EQ(dev.transfer(txBuf.data(), nullptr, 2, -1), apex::protocols::spi::Status::SUCCESS);

  (void)dev.close();
  peer.join();
}

/** @test Read-only transfer (txData null, sends zeros). */
TEST(SpiSocketDeviceTest, ReadOnlyTransfer) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(spiPeer, peerFd);

  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);

  std::array<std::uint8_t, 3> rxBuf = {};
  EXPECT_EQ(dev.transfer(nullptr, rxBuf.data(), 3, -1), apex::protocols::spi::Status::SUCCESS);

  // Zeros XOR 0xFF = 0xFF
  EXPECT_EQ(rxBuf[0], 0xFF);
  EXPECT_EQ(rxBuf[1], 0xFF);
  EXPECT_EQ(rxBuf[2], 0xFF);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- Statistics ----------------------------- */

/** @test Stats track bytes and transfers. */
TEST(SpiSocketDeviceTest, StatsTracking) {
  auto [driverFd, peerFd] = makeSocketPair();
  std::thread peer(spiPeer, peerFd);

  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);

  std::array<std::uint8_t, 8> txBuf = {};
  std::array<std::uint8_t, 8> rxBuf = {};
  (void)dev.transfer(txBuf.data(), rxBuf.data(), 8, -1);
  (void)dev.transfer(txBuf.data(), rxBuf.data(), 8, -1);

  EXPECT_EQ(dev.stats().bytesTx, 16U);
  EXPECT_EQ(dev.stats().bytesRx, 16U);
  EXPECT_EQ(dev.stats().transfersCompleted, 2U);
  EXPECT_EQ(dev.stats().transferErrors, 0U);

  dev.resetStats();
  EXPECT_EQ(dev.stats().bytesTx, 0U);

  (void)dev.close();
  peer.join();
}

/* ----------------------------- Close ----------------------------- */

/** @test Close then transfer returns error. */
TEST(SpiSocketDeviceTest, TransferAfterClose) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);
  (void)dev.close();

  std::uint8_t tx = 0;
  EXPECT_EQ(dev.transfer(&tx, nullptr, 1, -1), apex::protocols::spi::Status::ERROR_CLOSED);
  ::close(peerFd);
}

/** @test Double close returns error on second call. */
TEST(SpiSocketDeviceTest, DoubleClose) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev.configure(cfg);
  EXPECT_EQ(dev.close(), apex::protocols::spi::Status::SUCCESS);
  EXPECT_EQ(dev.close(), apex::protocols::spi::Status::ERROR_CLOSED);
  ::close(peerFd);
}

/* ----------------------------- Device Path ----------------------------- */

/** @test devicePath returns "socketpair". */
TEST(SpiSocketDeviceTest, DevicePath) {
  apex::protocols::spi::SpiSocketDevice dev(-1, false);
  EXPECT_STREQ(dev.devicePath(), "socketpair");
}

/* ----------------------------- Move Semantics ----------------------------- */

/** @test Move constructor transfers ownership. */
TEST(SpiSocketDeviceTest, MoveConstructor) {
  auto [driverFd, peerFd] = makeSocketPair();
  apex::protocols::spi::SpiSocketDevice dev1(driverFd);
  apex::protocols::spi::SpiConfig cfg;
  (void)dev1.configure(cfg);

  apex::protocols::spi::SpiSocketDevice dev2(std::move(dev1));
  EXPECT_TRUE(dev2.isOpen());
  EXPECT_EQ(dev2.fd(), driverFd);
  EXPECT_FALSE(dev1.isOpen()); // NOLINT(bugprone-use-after-move)
  EXPECT_EQ(dev1.fd(), -1);    // NOLINT(bugprone-use-after-move)

  ::close(peerFd);
}
