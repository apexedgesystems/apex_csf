/**
 * @file ModbusMaster_uTest.cpp
 * @brief Unit tests for ModbusMaster using mock transport.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusMaster.hpp"
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

class ModbusMasterTest : public ::testing::Test {
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
    rtuConfig.interFrameDelayUs = 0;
    transport_ = std::make_unique<modbus::ModbusRtuTransport>(adapter_.get(), rtuConfig, 115200);
    ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

    // Create Modbus master
    modbus::MasterConfig masterConfig;
    master_ = std::make_unique<modbus::ModbusMaster>(transport_.get(), masterConfig);
  }

  void TearDown() override {
    master_.reset();
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
  std::unique_ptr<modbus::ModbusMaster> master_;
};

/* ----------------------------- ModbusResult ----------------------------- */

/** @test ModbusResult success constructor. */
TEST(ModbusResultTest, SuccessConstruction) {
  const modbus::ModbusResult R = modbus::ModbusResult::success();
  EXPECT_TRUE(R.ok());
  EXPECT_FALSE(R.isException());
  EXPECT_EQ(R.status, modbus::Status::SUCCESS);
  EXPECT_EQ(R.exceptionCode, modbus::ExceptionCode::NONE);
}

/** @test ModbusResult error constructor. */
TEST(ModbusResultTest, ErrorConstruction) {
  const modbus::ModbusResult R = modbus::ModbusResult::error(modbus::Status::ERROR_TIMEOUT);
  EXPECT_FALSE(R.ok());
  EXPECT_FALSE(R.isException());
  EXPECT_EQ(R.status, modbus::Status::ERROR_TIMEOUT);
}

/** @test ModbusResult exception constructor. */
TEST(ModbusResultTest, ExceptionConstruction) {
  const modbus::ModbusResult R =
      modbus::ModbusResult::exception(modbus::ExceptionCode::ILLEGAL_FUNCTION);
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.isException());
  EXPECT_EQ(R.status, modbus::Status::ERROR_EXCEPTION);
  EXPECT_EQ(R.exceptionCode, modbus::ExceptionCode::ILLEGAL_FUNCTION);
}

/* ----------------------------- Construction ----------------------------- */

/** @test Master can be constructed with valid transport. */
TEST_F(ModbusMasterTest, Construction) { EXPECT_EQ(master_->transport(), transport_.get()); }

/** @test Master preserves config. */
TEST_F(ModbusMasterTest, ConfigPreserved) {
  modbus::MasterConfig config;
  config.defaultUnitAddress = 42;
  config.validateUnitAddress = false;
  config.validateFunctionCode = false;

  modbus::ModbusMaster master(transport_.get(), config);

  EXPECT_EQ(master.config().defaultUnitAddress, 42);
  EXPECT_FALSE(master.config().validateUnitAddress);
  EXPECT_FALSE(master.config().validateFunctionCode);
}

/* ----------------------------- Read Holding Registers ----------------------------- */

/** @test readHoldingRegisters with valid response. */
TEST_F(ModbusMasterTest, ReadHoldingRegistersSuccess) {
  // Spawn a thread to inject response after request is sent
  std::thread responder([this]() {
    // Wait for request
    std::uint8_t reqBuf[256];
    const std::size_t REQ_LEN = readSent(reqBuf, sizeof(reqBuf));
    EXPECT_GT(REQ_LEN, 0u);

    // Build response: [Unit=1] [FC=0x03] [ByteCount=4] [Reg0=0x1234] [Reg1=0x5678] [CRC]
    std::uint8_t resp[] = {0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00};
    const std::uint16_t CRC = modbus::calculateCrc(resp, 7);
    resp[7] = static_cast<std::uint8_t>(CRC & 0xFF);
    resp[8] = static_cast<std::uint8_t>(CRC >> 8);
    injectResponse(resp, sizeof(resp));
  });

  std::uint16_t values[2] = {0, 0};
  const modbus::ModbusResult RESULT = master_->readHoldingRegisters(1, 0x0000, 2, values, 200);

  responder.join();

  EXPECT_TRUE(RESULT.ok());
  EXPECT_EQ(values[0], 0x1234);
  EXPECT_EQ(values[1], 0x5678);
}

/** @test readHoldingRegisters with null buffer. */
TEST_F(ModbusMasterTest, ReadHoldingRegistersNullBuffer) {
  const modbus::ModbusResult RESULT = master_->readHoldingRegisters(1, 0x0000, 2, nullptr, 100);
  EXPECT_FALSE(RESULT.ok());
  EXPECT_EQ(RESULT.status, modbus::Status::ERROR_INVALID_ARG);
}

/** @test readHoldingRegisters with invalid quantity. */
TEST_F(ModbusMasterTest, ReadHoldingRegistersInvalidQuantity) {
  std::uint16_t values[256];
  const modbus::ModbusResult RESULT = master_->readHoldingRegisters(1, 0x0000, 200, values, 100);
  EXPECT_FALSE(RESULT.ok());
  EXPECT_EQ(RESULT.status, modbus::Status::ERROR_INVALID_ARG);
}

/** @test readHoldingRegisters with exception response. */
TEST_F(ModbusMasterTest, ReadHoldingRegistersException) {
  std::thread responder([this]() {
    std::uint8_t reqBuf[256];
    (void)readSent(reqBuf, sizeof(reqBuf));

    // Exception response: [Unit=1] [FC=0x83] [ExceptionCode=0x02] [CRC]
    std::uint8_t resp[] = {0x01, 0x83, 0x02, 0x00, 0x00};
    const std::uint16_t CRC = modbus::calculateCrc(resp, 3);
    resp[3] = static_cast<std::uint8_t>(CRC & 0xFF);
    resp[4] = static_cast<std::uint8_t>(CRC >> 8);
    injectResponse(resp, sizeof(resp));
  });

  std::uint16_t values[2] = {0, 0};
  const modbus::ModbusResult RESULT = master_->readHoldingRegisters(1, 0x0000, 2, values, 200);

  responder.join();

  EXPECT_FALSE(RESULT.ok());
  EXPECT_TRUE(RESULT.isException());
  EXPECT_EQ(RESULT.exceptionCode, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
}

/* ----------------------------- Write Single Register ----------------------------- */

/** @test writeSingleRegister with valid response. */
TEST_F(ModbusMasterTest, WriteSingleRegisterSuccess) {
  std::thread responder([this]() {
    std::uint8_t reqBuf[256];
    const std::size_t REQ_LEN = readSent(reqBuf, sizeof(reqBuf));
    EXPECT_GT(REQ_LEN, 0u);

    // Echo response for write single register
    // [Unit=1] [FC=0x06] [Addr=0x0010] [Value=0x1234] [CRC]
    std::uint8_t resp[] = {0x01, 0x06, 0x00, 0x10, 0x12, 0x34, 0x00, 0x00};
    const std::uint16_t CRC = modbus::calculateCrc(resp, 6);
    resp[6] = static_cast<std::uint8_t>(CRC & 0xFF);
    resp[7] = static_cast<std::uint8_t>(CRC >> 8);
    injectResponse(resp, sizeof(resp));
  });

  const modbus::ModbusResult RESULT = master_->writeSingleRegister(1, 0x0010, 0x1234, 200);

  responder.join();

  EXPECT_TRUE(RESULT.ok());
}

/** @test writeSingleRegister with broadcast (no response expected). */
TEST_F(ModbusMasterTest, WriteSingleRegisterBroadcast) {
  // Broadcast should not wait for response
  const modbus::ModbusResult RESULT = master_->writeSingleRegister(0, 0x0010, 0x1234, 100);

  // Should succeed (just sends, no response expected)
  EXPECT_TRUE(RESULT.ok());
}

/* ----------------------------- Write Multiple Registers ----------------------------- */

/** @test writeMultipleRegisters with valid response. */
TEST_F(ModbusMasterTest, WriteMultipleRegistersSuccess) {
  std::thread responder([this]() {
    std::uint8_t reqBuf[256];
    const std::size_t REQ_LEN = readSent(reqBuf, sizeof(reqBuf));
    EXPECT_GT(REQ_LEN, 0u);

    // Response: [Unit=1] [FC=0x10] [Addr=0x0000] [Qty=3] [CRC]
    std::uint8_t resp[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
    const std::uint16_t CRC = modbus::calculateCrc(resp, 6);
    resp[6] = static_cast<std::uint8_t>(CRC & 0xFF);
    resp[7] = static_cast<std::uint8_t>(CRC >> 8);
    injectResponse(resp, sizeof(resp));
  });

  const std::uint16_t VALUES[] = {0x1111, 0x2222, 0x3333};
  const modbus::ModbusResult RESULT = master_->writeMultipleRegisters(1, 0x0000, 3, VALUES, 200);

  responder.join();

  EXPECT_TRUE(RESULT.ok());
}

/** @test writeMultipleRegisters with null values. */
TEST_F(ModbusMasterTest, WriteMultipleRegistersNullValues) {
  const modbus::ModbusResult RESULT = master_->writeMultipleRegisters(1, 0x0000, 3, nullptr, 100);
  EXPECT_FALSE(RESULT.ok());
  EXPECT_EQ(RESULT.status, modbus::Status::ERROR_INVALID_ARG);
}

/* ----------------------------- Write Single Coil ----------------------------- */

/** @test writeSingleCoil ON with valid response. */
TEST_F(ModbusMasterTest, WriteSingleCoilOn) {
  std::thread responder([this]() {
    std::uint8_t reqBuf[256];
    const std::size_t REQ_LEN = readSent(reqBuf, sizeof(reqBuf));
    EXPECT_GT(REQ_LEN, 0u);

    // Echo response: [Unit=1] [FC=0x05] [Addr=0x0000] [Value=0xFF00] [CRC]
    std::uint8_t resp[] = {0x01, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00};
    const std::uint16_t CRC = modbus::calculateCrc(resp, 6);
    resp[6] = static_cast<std::uint8_t>(CRC & 0xFF);
    resp[7] = static_cast<std::uint8_t>(CRC >> 8);
    injectResponse(resp, sizeof(resp));
  });

  const modbus::ModbusResult RESULT = master_->writeSingleCoil(1, 0x0000, true, 200);

  responder.join();

  EXPECT_TRUE(RESULT.ok());
}

/* ----------------------------- Timeout ----------------------------- */

/** @test readHoldingRegisters times out with no response. */
TEST_F(ModbusMasterTest, ReadHoldingRegistersTimeout) {
  std::uint16_t values[2];
  const modbus::ModbusResult RESULT = master_->readHoldingRegisters(1, 0x0000, 2, values, 50);

  // Should timeout or would_block
  EXPECT_FALSE(RESULT.ok());
  EXPECT_TRUE(RESULT.status == modbus::Status::ERROR_TIMEOUT ||
              RESULT.status == modbus::Status::WOULD_BLOCK);
}
