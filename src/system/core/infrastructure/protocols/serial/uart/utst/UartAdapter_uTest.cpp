/**
 * @file UartAdapter_uTest.cpp
 * @brief Unit tests for UartAdapter using PTY pairs for loopback testing.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <thread>

using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::Status;
using apex::protocols::serial::uart::UartAdapter;
using apex::protocols::serial::uart::UartConfig;

/* ----------------------------- Default Construction ----------------------------- */

/** @test UartAdapter construction with path does not open device. */
TEST(UartAdapterTest, ConstructionDoesNotOpen) {
  UartAdapter adapter("/dev/nonexistent_device");
  EXPECT_FALSE(adapter.isOpen());
  EXPECT_EQ(adapter.fd(), -1);
  EXPECT_STREQ(adapter.devicePath(), "/dev/nonexistent_device");
}

/** @test UartAdapter stats are zero after construction. */
TEST(UartAdapterTest, StatsZeroAfterConstruction) {
  UartAdapter adapter("/dev/ttyUSB0");
  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
  EXPECT_EQ(stats.readErrors, 0u);
  EXPECT_EQ(stats.writeErrors, 0u);
}

/* ----------------------------- Configuration Tests ----------------------------- */

/** @test UartAdapter::configure() fails with nonexistent device. */
TEST(UartAdapterTest, ConfigureNonexistentDevice) {
  UartAdapter adapter("/dev/nonexistent_uart_device_12345");
  UartConfig cfg{};
  Status status = adapter.configure(cfg);
  EXPECT_EQ(status, Status::ERROR_CLOSED);
  EXPECT_FALSE(adapter.isOpen());
}

/** @test UartAdapter::configure() with PTY succeeds. */
TEST(UartAdapterTest, ConfigureWithPty) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;

  Status status = adapter.configure(cfg);
  EXPECT_EQ(status, Status::SUCCESS);
  EXPECT_TRUE(adapter.isOpen());
  EXPECT_GE(adapter.fd(), 0);
}

/** @test UartAdapter reconfigure succeeds and maintains open state. */
TEST(UartAdapterTest, ReconfigureSucceeds) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;

  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);
  EXPECT_TRUE(adapter.isOpen());

  cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_9600;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);
  EXPECT_TRUE(adapter.isOpen());
  EXPECT_GE(adapter.fd(), 0);
}

/* ----------------------------- I/O Without Configuration ----------------------------- */

/** @test UartAdapter::read() without configure() returns ERROR_NOT_CONFIGURED. */
TEST(UartAdapterTest, ReadWithoutConfigure) {
  UartAdapter adapter("/dev/ttyUSB0");

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(buffer, sizeof(buffer), bytesRead, 0), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(adapter.stats().readErrors, 1u);
}

/** @test UartAdapter::write() without configure() returns ERROR_NOT_CONFIGURED. */
TEST(UartAdapterTest, WriteWithoutConfigure) {
  UartAdapter adapter("/dev/ttyUSB0");

  std::uint8_t data[] = {0x01, 0x02, 0x03};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(data, sizeof(data), bytesWritten, 0), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(adapter.stats().writeErrors, 1u);
}

/* ----------------------------- I/O Argument Validation ----------------------------- */

/** @test UartAdapter::read() with null buffer returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, ReadNullBuffer) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(nullptr, 64, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::read() with zero size returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, ReadZeroSize) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(buffer, 0, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::write() with null data returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, WriteNullData) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(nullptr, 64, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::write() with zero size returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, WriteZeroSize) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t data[] = {0x01};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(data, 0, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/* ----------------------------- I/O Tests ----------------------------- */

/** @test UartAdapter::read() with timeout and no data returns WOULD_BLOCK. */
TEST(UartAdapterTest, ReadTimeoutWouldBlock) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  Status status = adapter.read(buffer, sizeof(buffer), bytesRead, 10);
  EXPECT_EQ(status, Status::WOULD_BLOCK);
  EXPECT_EQ(bytesRead, 0u);
  EXPECT_EQ(adapter.stats().readWouldBlock, 1u);
}

/** @test UartAdapter write and read via PTY loopback. */
TEST(UartAdapterTest, WriteReadLoopback) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t txData[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(txData));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(rxBuffer, sizeof(rxBuffer), bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(txData));
  EXPECT_EQ(std::memcmp(rxBuffer, txData, sizeof(txData)), 0);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesTx, sizeof(txData));
  EXPECT_EQ(stats.writesCompleted, 1u);
}

/** @test UartAdapter read from PTY master write. */
TEST(UartAdapterTest, ReadFromMasterWrite) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t txData[] = {0xCA, 0xFE, 0xBA, 0xBE};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(txData));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(txData));
  EXPECT_EQ(std::memcmp(rxBuffer, txData, sizeof(txData)), 0);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesRx, sizeof(txData));
  EXPECT_EQ(stats.readsCompleted, 1u);
}

/* ----------------------------- Flush Tests ----------------------------- */

/** @test UartAdapter::flush() without configure returns ERROR_NOT_CONFIGURED. */
TEST(UartAdapterTest, FlushWithoutConfigure) {
  UartAdapter adapter("/dev/ttyUSB0");
  EXPECT_EQ(adapter.flush(true, true), Status::ERROR_NOT_CONFIGURED);
}

/** @test UartAdapter::flush() with both flags succeeds. */
TEST(UartAdapterTest, FlushBothBuffers) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  EXPECT_EQ(adapter.flush(true, true), Status::SUCCESS);
}

/** @test UartAdapter::flush() with neither flag returns SUCCESS. */
TEST(UartAdapterTest, FlushNeitherBuffer) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  EXPECT_EQ(adapter.flush(false, false), Status::SUCCESS);
}

/* ----------------------------- Close Tests ----------------------------- */

/** @test UartAdapter::close() releases resources. */
TEST(UartAdapterTest, CloseReleasesResources) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);
  EXPECT_TRUE(adapter.isOpen());

  EXPECT_EQ(adapter.close(), Status::SUCCESS);
  EXPECT_FALSE(adapter.isOpen());
  EXPECT_EQ(adapter.fd(), -1);
}

/** @test UartAdapter::close() on unconfigured adapter returns SUCCESS. */
TEST(UartAdapterTest, CloseWhenNotConfigured) {
  UartAdapter adapter("/dev/ttyUSB0");
  EXPECT_EQ(adapter.close(), Status::SUCCESS);
}

/* ----------------------------- Move Semantics ----------------------------- */

/** @test UartAdapter move construction transfers ownership. */
TEST(UartAdapterTest, MoveConstruction) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter1(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter1.configure(cfg), Status::SUCCESS);
  int originalFd = adapter1.fd();

  UartAdapter adapter2(std::move(adapter1));

  EXPECT_FALSE(adapter1.isOpen());
  EXPECT_EQ(adapter1.fd(), -1);

  EXPECT_TRUE(adapter2.isOpen());
  EXPECT_EQ(adapter2.fd(), originalFd);
}

/** @test UartAdapter move assignment transfers ownership. */
TEST(UartAdapterTest, MoveAssignment) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter1(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter1.configure(cfg), Status::SUCCESS);
  int originalFd = adapter1.fd();

  UartAdapter adapter2("/dev/null");
  adapter2 = std::move(adapter1);

  EXPECT_FALSE(adapter1.isOpen());
  EXPECT_TRUE(adapter2.isOpen());
  EXPECT_EQ(adapter2.fd(), originalFd);
}

/* ----------------------------- Stats Tests ----------------------------- */

/** @test UartAdapter::resetStats() clears all counters. */
TEST(UartAdapterTest, ResetStatsClearsCounters) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t txData[] = {0x01, 0x02, 0x03};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  EXPECT_GT(adapter.stats().bytesTx, 0u);

  adapter.resetStats();

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesRx, 0u);
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.readsCompleted, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
}

/** @test Stats are cumulative across multiple operations. */
TEST(UartAdapterTest, StatsCumulative) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t txData[] = {0x01, 0x02, 0x03};
  std::size_t bytesWritten = 0;

  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(adapter.write(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  }

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.writesCompleted, 5u);
  EXPECT_EQ(stats.bytesTx, 15u);
}

/* ----------------------------- Vectored I/O Tests ----------------------------- */

/** @test UartAdapter::writeVectored() without configure returns ERROR_NOT_CONFIGURED. */
TEST(UartAdapterTest, WriteVectoredWithoutConfigure) {
  UartAdapter adapter("/dev/ttyUSB0");

  std::uint8_t buf1[] = {0x01, 0x02};
  std::uint8_t buf2[] = {0x03, 0x04};
  struct iovec iov[2];
  iov[0].iov_base = buf1;
  iov[0].iov_len = sizeof(buf1);
  iov[1].iov_base = buf2;
  iov[1].iov_len = sizeof(buf2);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeVectored(iov, 2, bytesWritten, 0), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(adapter.stats().writeErrors, 1u);
}

/** @test UartAdapter::writeVectored() with null iovec returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, WriteVectoredNullIov) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeVectored(nullptr, 2, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::writeVectored() with zero count returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, WriteVectoredZeroCount) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  struct iovec iov[1];
  std::uint8_t buf[] = {0x01};
  iov[0].iov_base = buf;
  iov[0].iov_len = sizeof(buf);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeVectored(iov, 0, bytesWritten, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::writeVectored() writes multiple buffers in one syscall. */
TEST(UartAdapterTest, WriteVectoredMultipleBuffers) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Header + payload pattern (common in framed protocols)
  std::uint8_t header[] = {0xAA, 0xBB};
  std::uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  struct iovec iov[2];
  iov[0].iov_base = header;
  iov[0].iov_len = sizeof(header);
  iov[1].iov_base = payload;
  iov[1].iov_len = sizeof(payload);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeVectored(iov, 2, bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(header) + sizeof(payload));

  // Verify data arrived at master side
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(rxBuffer, sizeof(rxBuffer), bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(header) + sizeof(payload));
  EXPECT_EQ(rxBuffer[0], 0xAA);
  EXPECT_EQ(rxBuffer[1], 0xBB);
  EXPECT_EQ(rxBuffer[2], 0x01);
  EXPECT_EQ(rxBuffer[5], 0x04);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesTx, sizeof(header) + sizeof(payload));
  EXPECT_EQ(stats.writesCompleted, 1u);
}

/** @test UartAdapter::readVectored() without configure returns ERROR_NOT_CONFIGURED. */
TEST(UartAdapterTest, ReadVectoredWithoutConfigure) {
  UartAdapter adapter("/dev/ttyUSB0");

  std::uint8_t buf1[4];
  std::uint8_t buf2[4];
  struct iovec iov[2];
  iov[0].iov_base = buf1;
  iov[0].iov_len = sizeof(buf1);
  iov[1].iov_base = buf2;
  iov[1].iov_len = sizeof(buf2);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.readVectored(iov, 2, bytesRead, 0), Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(adapter.stats().readErrors, 1u);
}

/** @test UartAdapter::readVectored() with null iovec returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, ReadVectoredNullIov) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.readVectored(nullptr, 2, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::readVectored() with zero count returns ERROR_INVALID_ARG. */
TEST(UartAdapterTest, ReadVectoredZeroCount) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  struct iovec iov[1];
  std::uint8_t buf[4];
  iov[0].iov_base = buf;
  iov[0].iov_len = sizeof(buf);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.readVectored(iov, 0, bytesRead, 0), Status::ERROR_INVALID_ARG);
}

/** @test UartAdapter::readVectored() with timeout and no data returns WOULD_BLOCK. */
TEST(UartAdapterTest, ReadVectoredTimeoutWouldBlock) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t buf1[4];
  std::uint8_t buf2[4];
  struct iovec iov[2];
  iov[0].iov_base = buf1;
  iov[0].iov_len = sizeof(buf1);
  iov[1].iov_base = buf2;
  iov[1].iov_len = sizeof(buf2);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.readVectored(iov, 2, bytesRead, 10), Status::WOULD_BLOCK);
  EXPECT_EQ(bytesRead, 0u);
  EXPECT_EQ(adapter.stats().readWouldBlock, 1u);
}

/** @test UartAdapter::readVectored() reads into multiple buffers. */
TEST(UartAdapterTest, ReadVectoredMultipleBuffers) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Send data from master (header + payload)
  std::uint8_t txData[] = {0xAA, 0xBB, 0x01, 0x02, 0x03, 0x04};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(txData));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Read into header + payload buffers
  std::uint8_t header[2] = {0};
  std::uint8_t payload[4] = {0};
  struct iovec iov[2];
  iov[0].iov_base = header;
  iov[0].iov_len = sizeof(header);
  iov[1].iov_base = payload;
  iov[1].iov_len = sizeof(payload);

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.readVectored(iov, 2, bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(txData));

  // Verify scatter into separate buffers
  EXPECT_EQ(header[0], 0xAA);
  EXPECT_EQ(header[1], 0xBB);
  EXPECT_EQ(payload[0], 0x01);
  EXPECT_EQ(payload[1], 0x02);
  EXPECT_EQ(payload[2], 0x03);
  EXPECT_EQ(payload[3], 0x04);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesRx, sizeof(txData));
  EXPECT_EQ(stats.readsCompleted, 1u);
}

/* ----------------------------- Span API Tests ----------------------------- */

/** @test UartAdapter::write(bytes_span) writes data via span. */
TEST(UartAdapterTest, WriteSpan) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::array<std::uint8_t, 4> txData = {0xDE, 0xAD, 0xBE, 0xEF};
  apex::compat::bytes_span txSpan(txData.data(), txData.size());

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(txSpan, bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, txData.size());

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(pty.readMaster(rxBuffer, sizeof(rxBuffer), bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, txData.size());
  EXPECT_EQ(std::memcmp(rxBuffer, txData.data(), txData.size()), 0);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesTx, txData.size());
  EXPECT_EQ(stats.writesCompleted, 1u);
}

/** @test UartAdapter::read(mutable_bytes_span) reads data via span. */
TEST(UartAdapterTest, ReadSpan) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t txData[] = {0xCA, 0xFE, 0xBA, 0xBE};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(pty.writeMaster(txData, sizeof(txData), bytesWritten, 100), Status::SUCCESS);
  EXPECT_EQ(bytesWritten, sizeof(txData));

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::array<std::uint8_t, 64> rxBuffer{};
  apex::compat::mutable_bytes_span rxSpan(rxBuffer.data(), rxBuffer.size());

  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxSpan, bytesRead, 100), Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(txData));
  EXPECT_EQ(std::memcmp(rxBuffer.data(), txData, sizeof(txData)), 0);

  const auto& stats = adapter.stats();
  EXPECT_EQ(stats.bytesRx, sizeof(txData));
  EXPECT_EQ(stats.readsCompleted, 1u);
}

/** @test UartAdapter span API integrates with std::array. */
TEST(UartAdapterTest, SpanFromArray) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  // Write using array-backed span
  std::array<std::uint8_t, 3> txData = {0x01, 0x02, 0x03};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(
      adapter.write(apex::compat::bytes_span(txData.data(), txData.size()), bytesWritten, 100),
      Status::SUCCESS);
  EXPECT_EQ(bytesWritten, 3u);

  // Wait for data to arrive at master
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Read from master and echo back
  std::uint8_t echoBuffer[32];
  std::size_t masterRead = 0;
  EXPECT_EQ(pty.readMaster(echoBuffer, sizeof(echoBuffer), masterRead, 100), Status::SUCCESS);
  EXPECT_EQ(masterRead, 3u);

  // Write echo back
  std::size_t masterWritten = 0;
  EXPECT_EQ(pty.writeMaster(echoBuffer, masterRead, masterWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Read using array-backed span
  std::array<std::uint8_t, 32> rxData{};
  std::size_t bytesRead = 0;
  EXPECT_EQ(
      adapter.read(apex::compat::mutable_bytes_span(rxData.data(), rxData.size()), bytesRead, 100),
      Status::SUCCESS);
  EXPECT_EQ(bytesRead, 3u);
  EXPECT_EQ(rxData[0], 0x01);
  EXPECT_EQ(rxData[1], 0x02);
  EXPECT_EQ(rxData[2], 0x03);
}

/* ----------------------------- ByteTrace Tests ----------------------------- */

using apex::protocols::ByteTrace;
using apex::protocols::formatBytesHex;
using apex::protocols::formatTraceMessage;
using apex::protocols::TraceDirection;

namespace {

// Test callback that captures trace data
struct TraceCapture {
  TraceDirection lastDir{};
  std::vector<std::uint8_t> lastData;
  std::size_t callCount{0};

  static void callback(TraceDirection dir, const std::uint8_t* data, std::size_t len,
                       void* userData) noexcept {
    auto* self = static_cast<TraceCapture*>(userData);
    self->lastDir = dir;
    self->lastData.assign(data, data + len);
    ++self->callCount;
  }
};

} // namespace

/** @test ByteTrace is disabled by default. */
TEST(UartAdapterTest, ByteTraceDisabledByDefault) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  EXPECT_FALSE(adapter.traceEnabled());
  EXPECT_FALSE(adapter.traceAttached());
}

/** @test ByteTrace callback not invoked when not attached. */
TEST(UartAdapterTest, ByteTraceNoCallbackWhenNotAttached) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  adapter.setTraceEnabled(true); // Enable but no callback attached

  std::uint8_t txData[] = {0xAA, 0xBB};
  std::size_t written = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), written, 100), Status::SUCCESS);

  // No crash, no callback
  EXPECT_TRUE(adapter.traceEnabled());
  EXPECT_FALSE(adapter.traceAttached());
}

/** @test ByteTrace callback not invoked when disabled. */
TEST(UartAdapterTest, ByteTraceNoCallbackWhenDisabled) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  TraceCapture capture;
  adapter.attachTrace(TraceCapture::callback, &capture);
  // Don't enable - should not invoke callback

  std::uint8_t txData[] = {0xAA, 0xBB};
  std::size_t written = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), written, 100), Status::SUCCESS);

  EXPECT_EQ(capture.callCount, 0u);
  EXPECT_TRUE(adapter.traceAttached());
  EXPECT_FALSE(adapter.traceEnabled());
}

/** @test ByteTrace captures TX data on write. */
TEST(UartAdapterTest, ByteTraceCapturesTx) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  TraceCapture capture;
  adapter.attachTrace(TraceCapture::callback, &capture);
  adapter.setTraceEnabled(true);

  std::uint8_t txData[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::size_t written = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), written, 100), Status::SUCCESS);

  EXPECT_EQ(capture.callCount, 1u);
  EXPECT_EQ(capture.lastDir, TraceDirection::TX);
  ASSERT_EQ(capture.lastData.size(), sizeof(txData));
  EXPECT_EQ(capture.lastData[0], 0xDE);
  EXPECT_EQ(capture.lastData[1], 0xAD);
  EXPECT_EQ(capture.lastData[2], 0xBE);
  EXPECT_EQ(capture.lastData[3], 0xEF);
}

/** @test ByteTrace captures RX data on read. */
TEST(UartAdapterTest, ByteTraceCapturesRx) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  TraceCapture capture;
  adapter.attachTrace(TraceCapture::callback, &capture);
  adapter.setTraceEnabled(true);

  // Inject data from master
  std::uint8_t txData[] = {0xCA, 0xFE};
  std::size_t written = 0;
  EXPECT_EQ(pty.writeMaster(txData, sizeof(txData), written, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 100), Status::SUCCESS);

  EXPECT_EQ(capture.callCount, 1u);
  EXPECT_EQ(capture.lastDir, TraceDirection::RX);
  ASSERT_EQ(capture.lastData.size(), sizeof(txData));
  EXPECT_EQ(capture.lastData[0], 0xCA);
  EXPECT_EQ(capture.lastData[1], 0xFE);
}

/** @test ByteTrace can be detached. */
TEST(UartAdapterTest, ByteTraceDetach) {
  PtyPair pty;
  ASSERT_EQ(pty.open(), Status::SUCCESS);

  UartAdapter adapter(pty.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  TraceCapture capture;
  adapter.attachTrace(TraceCapture::callback, &capture);
  adapter.setTraceEnabled(true);

  // First write - should trace
  std::uint8_t txData[] = {0x01};
  std::size_t written = 0;
  EXPECT_EQ(adapter.write(txData, sizeof(txData), written, 100), Status::SUCCESS);
  EXPECT_EQ(capture.callCount, 1u);

  // Detach
  adapter.detachTrace();
  EXPECT_FALSE(adapter.traceAttached());
  EXPECT_FALSE(adapter.traceEnabled());

  // Second write - should not trace
  EXPECT_EQ(adapter.write(txData, sizeof(txData), written, 100), Status::SUCCESS);
  EXPECT_EQ(capture.callCount, 1u); // Still 1
}

/** @test formatBytesHex formats bytes correctly. */
TEST(UartAdapterTest, FormatBytesHex) {
  char buffer[64];
  std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};

  std::size_t len = formatBytesHex(data, sizeof(data), buffer, sizeof(buffer));

  EXPECT_STREQ(buffer, "DE AD BE EF");
  EXPECT_EQ(len, 11u); // "DE AD BE EF" = 11 chars
}

/** @test formatBytesHex truncates long data. */
TEST(UartAdapterTest, FormatBytesHexTruncates) {
  char buffer[128];
  std::uint8_t data[64];
  for (std::size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<std::uint8_t>(i);
  }

  std::size_t len = formatBytesHex(data, sizeof(data), buffer, sizeof(buffer), 4);

  EXPECT_STREQ(buffer, "00 01 02 03 ...");
  EXPECT_EQ(len, 15u); // "00 01 02 03 ..." = 15 chars
}

/** @test formatBytesHex handles empty data. */
TEST(UartAdapterTest, FormatBytesHexEmpty) {
  char buffer[64];
  std::uint8_t data[1] = {0};

  std::size_t len = formatBytesHex(data, 0, buffer, sizeof(buffer));

  EXPECT_STREQ(buffer, "");
  EXPECT_EQ(len, 0u);
}

/** @test formatTraceMessage formats complete trace output. */
TEST(UartAdapterTest, FormatTraceMessage) {
  char buffer[256];
  std::uint8_t data[] = {0xAB, 0xCD};

  std::size_t len =
      formatTraceMessage(TraceDirection::TX, data, sizeof(data), buffer, sizeof(buffer), "UART");

  EXPECT_STREQ(buffer, "[UART] TX (2 bytes): AB CD");
  EXPECT_GT(len, 0u);
}

/** @test formatTraceMessage handles RX direction. */
TEST(UartAdapterTest, FormatTraceMessageRx) {
  char buffer[256];
  std::uint8_t data[] = {0x01, 0x02, 0x03};

  std::size_t len =
      formatTraceMessage(TraceDirection::RX, data, sizeof(data), buffer, sizeof(buffer), "TEST");

  EXPECT_STREQ(buffer, "[TEST] RX (3 bytes): 01 02 03");
  EXPECT_GT(len, 0u);
}
