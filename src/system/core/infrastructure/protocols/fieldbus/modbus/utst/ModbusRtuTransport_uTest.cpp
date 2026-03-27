/**
 * @file ModbusRtuTransport_uTest.cpp
 * @brief Unit tests for ModbusRtuTransport using PTY loopback.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusRtuTransport.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <thread>

namespace modbus = apex::protocols::fieldbus::modbus;
using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::UartAdapter;
using apex::protocols::serial::uart::UartConfig;

/* ----------------------------- Test Fixture ----------------------------- */

class ModbusRtuTransportTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create PTY pair for loopback testing
    ASSERT_EQ(pty_.open(), apex::protocols::serial::uart::Status::SUCCESS);

    // Configure UART adapter on slave side
    adapter_ = std::make_unique<UartAdapter>(pty_.slavePath());
    UartConfig cfg;
    ASSERT_EQ(adapter_->configure(cfg), apex::protocols::serial::uart::Status::SUCCESS);

    // Create Modbus RTU transport
    modbus::ModbusRtuConfig rtuConfig;
    rtuConfig.responseTimeoutMs = 100;
    rtuConfig.interFrameDelayUs = 0; // Minimal delay for tests
    transport_ = std::make_unique<modbus::ModbusRtuTransport>(adapter_.get(), rtuConfig, 115200);
  }

  void TearDown() override {
    if (transport_) {
      (void)transport_->close();
    }
    if (adapter_) {
      (void)adapter_->close();
    }
    (void)pty_.close();
  }

  /**
   * @brief Inject a response via the PTY master side.
   */
  void injectResponse(const std::uint8_t* data, std::size_t len) {
    std::size_t written = 0;
    (void)pty_.writeMaster(data, len, written, 100);
  }

  /**
   * @brief Read what was sent via the PTY master side.
   */
  std::size_t readSent(std::uint8_t* buf, std::size_t maxLen) {
    std::size_t bytesRead = 0;
    (void)pty_.readMaster(buf, maxLen, bytesRead, 100);
    return bytesRead;
  }

  PtyPair pty_;
  std::unique_ptr<UartAdapter> adapter_;
  std::unique_ptr<modbus::ModbusRtuTransport> transport_;
};

/* ----------------------------- Construction ----------------------------- */

/** @test Transport can be constructed with valid device. */
TEST_F(ModbusRtuTransportTest, Construction) {
  EXPECT_FALSE(transport_->isOpen());
  EXPECT_NE(transport_->description(), nullptr);
  EXPECT_GT(std::strlen(transport_->description()), 0u);
}

/** @test Transport description includes device path. */
TEST_F(ModbusRtuTransportTest, DescriptionIncludesPath) {
  const std::string DESC = transport_->description();
  EXPECT_NE(DESC.find("RTU:"), std::string::npos);
  EXPECT_NE(DESC.find("/dev/pts/"), std::string::npos);
}

/** @test Inter-frame delay is calculated for 115200 baud. */
TEST_F(ModbusRtuTransportTest, InterFrameDelayCalculation) {
  // At 115200 baud, delay should be around 300us
  const std::uint32_t DELAY = transport_->interFrameDelayUs();
  EXPECT_GT(DELAY, 200);
  EXPECT_LT(DELAY, 500);
}

/* ----------------------------- Open/Close ----------------------------- */

/** @test Transport can be opened and closed. */
TEST_F(ModbusRtuTransportTest, OpenClose) {
  EXPECT_EQ(transport_->open(), modbus::Status::SUCCESS);
  EXPECT_TRUE(transport_->isOpen());

  EXPECT_EQ(transport_->close(), modbus::Status::SUCCESS);
  EXPECT_FALSE(transport_->isOpen());
}

/** @test Opening twice is safe. */
TEST_F(ModbusRtuTransportTest, OpenTwice) {
  EXPECT_EQ(transport_->open(), modbus::Status::SUCCESS);
  EXPECT_EQ(transport_->open(), modbus::Status::SUCCESS);
  EXPECT_TRUE(transport_->isOpen());
}

/** @test Operations fail when not open. */
TEST_F(ModbusRtuTransportTest, OperationsFailWhenClosed) {
  modbus::FrameBuffer buf;
  EXPECT_EQ(transport_->sendRequest(buf, 100), modbus::Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(transport_->receiveResponse(buf, 100), modbus::Status::ERROR_NOT_CONFIGURED);
  EXPECT_EQ(transport_->flush(), modbus::Status::ERROR_NOT_CONFIGURED);
}

/* ----------------------------- Send Request ----------------------------- */

/** @test sendRequest transmits frame correctly. */
TEST_F(ModbusRtuTransportTest, SendRequest) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  // Build a read holding registers request
  modbus::FrameBuffer reqBuf;
  ASSERT_EQ(modbus::buildReadHoldingRegistersRequest(reqBuf, 1, 0x0000, 10),
            modbus::Status::SUCCESS);

  // Send request
  EXPECT_EQ(transport_->sendRequest(reqBuf, 100), modbus::Status::SUCCESS);

  // Read what was sent via PTY master
  std::uint8_t sentData[256];
  const std::size_t SENT_LEN = readSent(sentData, sizeof(sentData));

  EXPECT_EQ(SENT_LEN, reqBuf.length);
  EXPECT_EQ(std::memcmp(sentData, reqBuf.data, reqBuf.length), 0);
}

/** @test sendRequest updates statistics. */
TEST_F(ModbusRtuTransportTest, SendRequestUpdatesStats) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  modbus::FrameBuffer reqBuf;
  ASSERT_EQ(modbus::buildReadHoldingRegistersRequest(reqBuf, 1, 0, 1), modbus::Status::SUCCESS);

  transport_->resetStats();
  EXPECT_EQ(transport_->sendRequest(reqBuf, 100), modbus::Status::SUCCESS);

  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.requestsSent, 1);
  EXPECT_EQ(STATS.bytesTx, reqBuf.length);
}

/** @test sendRequest rejects too-short frame. */
TEST_F(ModbusRtuTransportTest, SendRequestRejectsTooShort) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  modbus::FrameBuffer buf;
  buf.length = 2; // Too short (minimum is 4)

  EXPECT_EQ(transport_->sendRequest(buf, 100), modbus::Status::ERROR_INVALID_ARG);
}

/* ----------------------------- Receive Response ----------------------------- */

/** @test receiveResponse receives and validates a normal response. */
TEST_F(ModbusRtuTransportTest, ReceiveNormalResponse) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  // Prepare a valid read holding registers response
  // [Unit=1] [FC=0x03] [ByteCount=2] [Reg0=0x1234] [CRC]
  std::uint8_t response[] = {0x01, 0x03, 0x02, 0x12, 0x34, 0x00, 0x00};
  const std::uint16_t CRC = modbus::calculateCrc(response, 5);
  response[5] = static_cast<std::uint8_t>(CRC & 0xFF);
  response[6] = static_cast<std::uint8_t>(CRC >> 8);

  // Inject response
  injectResponse(response, sizeof(response));

  // Receive
  modbus::FrameBuffer respBuf;
  const modbus::Status S = transport_->receiveResponse(respBuf, 100);
  EXPECT_EQ(S, modbus::Status::SUCCESS);
  EXPECT_EQ(respBuf.length, sizeof(response));

  // Verify stats
  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.responsesReceived, 1);
  EXPECT_EQ(STATS.bytesRx, sizeof(response));
}

/** @test receiveResponse detects exception response. */
TEST_F(ModbusRtuTransportTest, ReceiveExceptionResponse) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  // Prepare exception response: [Unit=1] [FC=0x83] [ExceptionCode=0x02] [CRC]
  std::uint8_t response[] = {0x01, 0x83, 0x02, 0x00, 0x00};
  const std::uint16_t CRC = modbus::calculateCrc(response, 3);
  response[3] = static_cast<std::uint8_t>(CRC & 0xFF);
  response[4] = static_cast<std::uint8_t>(CRC >> 8);

  injectResponse(response, sizeof(response));

  modbus::FrameBuffer respBuf;
  const modbus::Status S = transport_->receiveResponse(respBuf, 100);
  EXPECT_EQ(S, modbus::Status::ERROR_EXCEPTION);

  // Stats should track exception
  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.exceptionsReceived, 1);
}

/** @test receiveResponse detects CRC error. */
TEST_F(ModbusRtuTransportTest, ReceiveCrcError) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  // Prepare response with bad CRC
  const std::uint8_t RESPONSE[] = {0x01, 0x03, 0x02, 0x12, 0x34, 0xFF, 0xFF};
  injectResponse(RESPONSE, sizeof(RESPONSE));

  modbus::FrameBuffer respBuf;
  const modbus::Status S = transport_->receiveResponse(respBuf, 100);
  EXPECT_EQ(S, modbus::Status::ERROR_CRC);

  // Stats should track CRC error
  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.crcErrors, 1);
}

/** @test receiveResponse times out with no data. */
TEST_F(ModbusRtuTransportTest, ReceiveTimeout) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  modbus::FrameBuffer respBuf;
  const modbus::Status S = transport_->receiveResponse(respBuf, 10); // Short timeout

  // Should be timeout or would_block
  EXPECT_TRUE(S == modbus::Status::ERROR_TIMEOUT || S == modbus::Status::WOULD_BLOCK);
}

/* ----------------------------- Statistics ----------------------------- */

/** @test Stats are initially zero. */
TEST_F(ModbusRtuTransportTest, StatsInitiallyZero) {
  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.requestsSent, 0);
  EXPECT_EQ(STATS.responsesReceived, 0);
  EXPECT_EQ(STATS.bytesTx, 0);
  EXPECT_EQ(STATS.bytesRx, 0);
  EXPECT_EQ(STATS.crcErrors, 0);
  EXPECT_EQ(STATS.timeouts, 0);
}

/** @test resetStats clears all counters. */
TEST_F(ModbusRtuTransportTest, ResetStats) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

  modbus::FrameBuffer buf;
  ASSERT_EQ(modbus::buildReadHoldingRegistersRequest(buf, 1, 0, 1), modbus::Status::SUCCESS);
  (void)transport_->sendRequest(buf, 100);

  transport_->resetStats();

  const modbus::ModbusStats STATS = transport_->stats();
  EXPECT_EQ(STATS.requestsSent, 0);
  EXPECT_EQ(STATS.bytesTx, 0);
}

/* ----------------------------- Flush ----------------------------- */

/** @test flush() succeeds when open. */
TEST_F(ModbusRtuTransportTest, Flush) {
  ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);
  EXPECT_EQ(transport_->flush(), modbus::Status::SUCCESS);
}
