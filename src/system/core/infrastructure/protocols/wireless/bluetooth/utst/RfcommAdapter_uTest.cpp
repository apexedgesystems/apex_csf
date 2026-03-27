/**
 * @file RfcommAdapter_uTest.cpp
 * @brief Unit tests for RfcommAdapter using loopback.
 */

#include "RfcommAdapter.hpp"
#include "RfcommLoopback.hpp"

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace bt = apex::protocols::wireless::bluetooth;
using apex::protocols::TraceDirection;

/* ----------------------------- Test Fixture ----------------------------- */

class RfcommAdapterTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(loopback_.open(), bt::Status::SUCCESS);
    int clientFd = loopback_.releaseClientFd();
    ASSERT_GE(clientFd, 0);
    adapter_ = std::make_unique<bt::RfcommAdapter>(clientFd);
  }

  void TearDown() override {
    adapter_.reset();
    loopback_.close();
  }

  bt::RfcommLoopback loopback_;
  std::unique_ptr<bt::RfcommAdapter> adapter_;
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verify default construction creates unconfigured adapter. */
TEST(RfcommAdapterStandalone, DefaultConstruction) {
  bt::RfcommAdapter adapter;

  EXPECT_FALSE(adapter.isConnected());
  EXPECT_FALSE(adapter.isInjectedFd());
  EXPECT_EQ(adapter.fd(), -1);
  EXPECT_STREQ(adapter.description(), "RFCOMM (not configured)");
}

/* ----------------------------- FD Injection Tests ----------------------------- */

/** @test Verify FD injection constructor marks as connected. */
TEST_F(RfcommAdapterTest, FdInjectionMarksConnected) {
  EXPECT_TRUE(adapter_->isConnected());
  EXPECT_TRUE(adapter_->isInjectedFd());
  EXPECT_GE(adapter_->fd(), 0);
}

/** @test Verify FD injection with invalid FD. */
TEST(RfcommAdapterStandalone, FdInjectionInvalidFd) {
  bt::RfcommAdapter adapter(-1);

  EXPECT_FALSE(adapter.isConnected());
  EXPECT_STREQ(adapter.description(), "RFCOMM (invalid FD)");
}

/* ----------------------------- I/O Tests ----------------------------- */

/** @test Verify write and read round-trip via loopback. */
TEST_F(RfcommAdapterTest, WriteReadRoundTrip) {
  // Write from adapter
  const std::uint8_t writeData[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::size_t written = 0;
  ASSERT_EQ(adapter_->write({writeData, sizeof(writeData)}, written, 100), bt::Status::SUCCESS);
  EXPECT_EQ(written, sizeof(writeData));

  // Read from loopback server side
  std::uint8_t readBuf[16];
  std::size_t bytesRead = 0;
  ASSERT_EQ(loopback_.serverRead({readBuf, sizeof(readBuf)}, bytesRead, 100), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(writeData));
  EXPECT_EQ(std::memcmp(readBuf, writeData, sizeof(writeData)), 0);
}

/** @test Verify read receives data from loopback. */
TEST_F(RfcommAdapterTest, ReadFromLoopback) {
  // Write to loopback server side
  const std::uint8_t writeData[] = {0xCA, 0xFE, 0xBA, 0xBE};
  std::size_t written = 0;
  ASSERT_EQ(loopback_.serverWrite({writeData, sizeof(writeData)}, written, 100),
            bt::Status::SUCCESS);

  // Read from adapter
  std::uint8_t readBuf[16];
  std::size_t bytesRead = 0;
  ASSERT_EQ(adapter_->read({readBuf, sizeof(readBuf)}, bytesRead, 100), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, sizeof(writeData));
  EXPECT_EQ(std::memcmp(readBuf, writeData, sizeof(writeData)), 0);
}

/** @test Verify read returns WOULD_BLOCK with no data and timeout=0. */
TEST_F(RfcommAdapterTest, ReadWouldBlock) {
  std::uint8_t buf[16];
  std::size_t bytesRead = 0;

  bt::Status status = adapter_->read({buf, sizeof(buf)}, bytesRead, 0);
  EXPECT_EQ(status, bt::Status::WOULD_BLOCK);
  EXPECT_EQ(bytesRead, 0u);
}

/** @test Verify write with empty data succeeds. */
TEST_F(RfcommAdapterTest, WriteEmptyData) {
  std::size_t written = 999;
  apex::compat::bytes_span empty;
  bt::Status status = adapter_->write(empty, written, 0);
  EXPECT_EQ(status, bt::Status::SUCCESS);
  EXPECT_EQ(written, 0u);
}

/** @test Verify read with empty buffer succeeds. */
TEST_F(RfcommAdapterTest, ReadEmptyBuffer) {
  std::size_t bytesRead = 999;
  apex::compat::mutable_bytes_span empty;
  bt::Status status = adapter_->read(empty, bytesRead, 0);
  EXPECT_EQ(status, bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 0u);
}

/** @test Verify large transfer. */
TEST_F(RfcommAdapterTest, LargeTransfer) {
  std::vector<std::uint8_t> writeData(4096);
  for (std::size_t i = 0; i < writeData.size(); ++i) {
    writeData[i] = static_cast<std::uint8_t>(i & 0xFF);
  }

  // Write in chunks
  std::size_t totalWritten = 0;
  while (totalWritten < writeData.size()) {
    std::size_t written = 0;
    apex::compat::bytes_span chunk{writeData.data() + totalWritten,
                                   writeData.size() - totalWritten};
    bt::Status status = adapter_->write(chunk, written, 100);
    ASSERT_TRUE(status == bt::Status::SUCCESS || status == bt::Status::WOULD_BLOCK)
        << bt::toString(status);
    totalWritten += written;
  }

  // Read back in chunks
  std::vector<std::uint8_t> readData(4096);
  std::size_t totalRead = 0;
  while (totalRead < readData.size()) {
    std::size_t bytesRead = 0;
    apex::compat::mutable_bytes_span chunk{readData.data() + totalRead,
                                           readData.size() - totalRead};
    bt::Status status = loopback_.serverRead(chunk, bytesRead, 100);
    ASSERT_TRUE(status == bt::Status::SUCCESS || status == bt::Status::WOULD_BLOCK)
        << bt::toString(status);
    totalRead += bytesRead;
  }

  EXPECT_EQ(writeData, readData);
}

/* ----------------------------- Statistics Tests ----------------------------- */

/** @test Verify statistics are updated on I/O. */
TEST_F(RfcommAdapterTest, StatisticsUpdated) {
  const std::uint8_t data[] = {0x01, 0x02, 0x03};
  std::size_t written = 0;

  ASSERT_EQ(adapter_->write({data, sizeof(data)}, written, 100), bt::Status::SUCCESS);

  const auto& stats = adapter_->stats();
  EXPECT_EQ(stats.bytesTx, sizeof(data));
  EXPECT_EQ(stats.writesCompleted, 1u);
}

/** @test Verify resetStats clears counters. */
TEST_F(RfcommAdapterTest, ResetStats) {
  const std::uint8_t data[] = {0x01, 0x02, 0x03};
  std::size_t written = 0;
  ASSERT_EQ(adapter_->write({data, sizeof(data)}, written, 100), bt::Status::SUCCESS);

  adapter_->resetStats();

  const auto& stats = adapter_->stats();
  EXPECT_EQ(stats.bytesTx, 0u);
  EXPECT_EQ(stats.writesCompleted, 0u);
}

/* ----------------------------- ByteTrace Tests ----------------------------- */

std::vector<std::uint8_t> g_traceData;
TraceDirection g_traceDir;

void testTraceCallback(TraceDirection dir, const std::uint8_t* data, std::size_t len,
                       void* /*userData*/) noexcept {
  g_traceDir = dir;
  g_traceData.assign(data, data + len);
}

/** @test Verify ByteTrace captures TX data. */
TEST_F(RfcommAdapterTest, ByteTraceCaptures) {
  g_traceData.clear();

  adapter_->attachTrace(testTraceCallback);
  adapter_->setTraceEnabled(true);

  const std::uint8_t data[] = {0xAA, 0xBB, 0xCC};
  std::size_t written = 0;
  ASSERT_EQ(adapter_->write({data, sizeof(data)}, written, 100), bt::Status::SUCCESS);

  EXPECT_EQ(g_traceDir, TraceDirection::TX);
  EXPECT_EQ(g_traceData.size(), sizeof(data));
  EXPECT_EQ(std::memcmp(g_traceData.data(), data, sizeof(data)), 0);
}

/** @test Verify ByteTrace captures RX data. */
TEST_F(RfcommAdapterTest, ByteTraceCapturesRx) {
  g_traceData.clear();

  adapter_->attachTrace(testTraceCallback);
  adapter_->setTraceEnabled(true);

  // Send data from loopback
  const std::uint8_t data[] = {0x11, 0x22, 0x33};
  std::size_t written = 0;
  ASSERT_EQ(loopback_.serverWrite({data, sizeof(data)}, written, 100), bt::Status::SUCCESS);

  // Read via adapter
  std::uint8_t buf[16];
  std::size_t bytesRead = 0;
  ASSERT_EQ(adapter_->read({buf, sizeof(buf)}, bytesRead, 100), bt::Status::SUCCESS);

  EXPECT_EQ(g_traceDir, TraceDirection::RX);
  EXPECT_EQ(g_traceData.size(), sizeof(data));
}

/** @test Verify trace disabled by default. */
TEST_F(RfcommAdapterTest, TraceDisabledByDefault) {
  g_traceData.clear();
  g_traceData.push_back(0xFF); // Sentinel

  adapter_->attachTrace(testTraceCallback);
  // Note: setTraceEnabled NOT called

  const std::uint8_t data[] = {0x01};
  std::size_t written = 0;
  ASSERT_EQ(adapter_->write({data, sizeof(data)}, written, 100), bt::Status::SUCCESS);

  // Trace should not have been invoked
  EXPECT_EQ(g_traceData.size(), 1u);
  EXPECT_EQ(g_traceData[0], 0xFF);
}

/* ----------------------------- Disconnect Tests ----------------------------- */

/** @test Verify disconnect closes connection. */
TEST_F(RfcommAdapterTest, DisconnectClosesConnection) {
  EXPECT_TRUE(adapter_->isConnected());

  ASSERT_EQ(adapter_->disconnect(), bt::Status::SUCCESS);

  EXPECT_FALSE(adapter_->isConnected());
  EXPECT_EQ(adapter_->fd(), -1);
}

/** @test Verify read fails after disconnect. */
TEST_F(RfcommAdapterTest, ReadFailsAfterDisconnect) {
  ASSERT_EQ(adapter_->disconnect(), bt::Status::SUCCESS);

  std::uint8_t buf[16];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter_->read({buf, sizeof(buf)}, bytesRead, 0), bt::Status::ERROR_NOT_CONNECTED);
}

/** @test Verify write fails after disconnect. */
TEST_F(RfcommAdapterTest, WriteFailsAfterDisconnect) {
  ASSERT_EQ(adapter_->disconnect(), bt::Status::SUCCESS);

  const std::uint8_t data[] = {0x01};
  std::size_t written = 0;
  EXPECT_EQ(adapter_->write({data, sizeof(data)}, written, 0), bt::Status::ERROR_NOT_CONNECTED);
}

/* ----------------------------- Configuration Tests ----------------------------- */

/** @test Verify configure stores config for injected FD adapter. */
TEST_F(RfcommAdapterTest, ConfigureStoresConfig) {
  bt::RfcommConfig cfg;
  cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
  cfg.channel = 5;

  ASSERT_EQ(adapter_->configure(cfg), bt::Status::SUCCESS);

  EXPECT_EQ(adapter_->config().channel, 5u);
  EXPECT_TRUE(adapter_->config().remoteAddress.isValid());
}
