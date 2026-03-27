/**
 * @file ModbusEmulation_dTest.cpp
 * @brief Development tests demonstrating Modbus device emulation patterns.
 *
 * This file shows how to:
 *  1. Create an emulated Modbus slave device using PtyPair
 *  2. Build a device model that responds to Modbus function codes
 *  3. Connect a ModbusMaster to the emulated device via RTU transport
 *  4. Test read/write operations for registers and coils
 *
 * These patterns are useful for:
 *  - Hardware-in-the-loop simulation of Modbus devices
 *  - Testing drivers without physical hardware
 *  - Developing and debugging Modbus communications
 *  - Model integration testing in simulation environments
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusMaster.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusRtuTransport.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>

namespace modbus = apex::protocols::fieldbus::modbus;
using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::UartAdapter;
using apex::protocols::serial::uart::UartConfig;

/* ----------------------------- Emulated Modbus Slave ----------------------------- */

/**
 * @class EmulatedModbusSlave
 * @brief Emulated Modbus slave device for testing.
 *
 * This class emulates a Modbus slave that:
 *  - Responds to read/write holding register requests (FC 0x03, 0x06, 0x10)
 *  - Responds to read/write coil requests (FC 0x01, 0x05, 0x0F)
 *  - Responds to read input register requests (FC 0x04)
 *  - Responds to read discrete input requests (FC 0x02)
 *  - Returns proper exception responses for invalid requests
 *
 * The emulated slave maintains:
 *  - 16 holding registers (addresses 0-15)
 *  - 16 input registers (addresses 0-15, read-only)
 *  - 32 coils (addresses 0-31)
 *  - 32 discrete inputs (addresses 0-31, read-only)
 *
 * Usage pattern:
 *  1. Create instance and call init()
 *  2. Call start() to begin the slave thread
 *  3. Connect ModbusMaster via RTU transport to slavePath()
 *  4. Execute Modbus transactions
 *  5. Call stop() when done
 */
class EmulatedModbusSlave {
public:
  static constexpr std::uint8_t UNIT_ADDRESS = 1;
  static constexpr std::size_t NUM_HOLDING_REGISTERS = 16;
  static constexpr std::size_t NUM_INPUT_REGISTERS = 16;
  static constexpr std::size_t NUM_COILS = 32;
  static constexpr std::size_t NUM_DISCRETE_INPUTS = 32;

  EmulatedModbusSlave() { initializeData(); }

  ~EmulatedModbusSlave() { stop(); }

  EmulatedModbusSlave(const EmulatedModbusSlave&) = delete;
  EmulatedModbusSlave& operator=(const EmulatedModbusSlave&) = delete;

  /**
   * @brief Initialize the PTY pair for communication.
   * @return true if successful.
   */
  bool init() {
    if (pty_.open() != apex::protocols::serial::uart::Status::SUCCESS) {
      return false;
    }
    return true;
  }

  /**
   * @brief Get the slave path for master connection.
   * @return Path like "/dev/pts/N".
   */
  const char* slavePath() const { return pty_.slavePath(); }

  /**
   * @brief Start the slave emulation thread.
   */
  void start() {
    running_ = true;
    thread_ = std::thread(&EmulatedModbusSlave::runLoop, this);
  }

  /**
   * @brief Stop the slave emulation thread.
   */
  void stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /**
   * @brief Check if slave is running.
   */
  bool isRunning() const { return running_; }

  /**
   * @brief Get a holding register value (for verification).
   */
  std::uint16_t getHoldingRegister(std::size_t addr) const {
    return (addr < NUM_HOLDING_REGISTERS) ? holdingRegisters_[addr] : 0;
  }

  /**
   * @brief Set a holding register value (for test setup).
   */
  void setHoldingRegister(std::size_t addr, std::uint16_t value) {
    if (addr < NUM_HOLDING_REGISTERS) {
      holdingRegisters_[addr] = value;
    }
  }

  /**
   * @brief Get a coil value (for verification).
   */
  bool getCoil(std::size_t addr) const {
    if (addr >= NUM_COILS) {
      return false;
    }
    const std::size_t BYTE_IDX = addr / 8;
    const std::size_t BIT_IDX = addr % 8;
    return (coils_[BYTE_IDX] & (1U << BIT_IDX)) != 0;
  }

  /**
   * @brief Set a coil value (for test setup).
   */
  void setCoil(std::size_t addr, bool value) {
    if (addr >= NUM_COILS) {
      return;
    }
    const std::size_t BYTE_IDX = addr / 8;
    const std::size_t BIT_IDX = addr % 8;
    if (value) {
      coils_[BYTE_IDX] |= static_cast<std::uint8_t>(1U << BIT_IDX);
    } else {
      coils_[BYTE_IDX] &= static_cast<std::uint8_t>(~(1U << BIT_IDX));
    }
  }

  /**
   * @brief Get transaction count for statistics.
   */
  std::uint32_t transactionCount() const { return transactionCount_; }

private:
  void initializeData() {
    // Initialize holding registers with pattern
    for (std::size_t i = 0; i < NUM_HOLDING_REGISTERS; ++i) {
      holdingRegisters_[i] = static_cast<std::uint16_t>(0x1000 + i);
    }

    // Initialize input registers with different pattern
    for (std::size_t i = 0; i < NUM_INPUT_REGISTERS; ++i) {
      inputRegisters_[i] = static_cast<std::uint16_t>(0x2000 + i);
    }

    // Initialize coils: alternating pattern
    std::memset(coils_, 0xAA, sizeof(coils_));

    // Initialize discrete inputs: inverse pattern
    std::memset(discreteInputs_, 0x55, sizeof(discreteInputs_));
  }

  void runLoop() {
    std::uint8_t rxBuffer[modbus::Constants::RTU_MAX_FRAME_SIZE];
    std::uint8_t txBuffer[modbus::Constants::RTU_MAX_FRAME_SIZE];

    while (running_) {
      std::size_t bytesRead = 0;
      auto status = pty_.readMaster(rxBuffer, sizeof(rxBuffer), bytesRead, 50);

      if (status == apex::protocols::serial::uart::Status::SUCCESS && bytesRead >= 4) {
        // Minimum RTU frame: unit + fc + crc (4 bytes)
        std::size_t respLen = processRequest(rxBuffer, bytesRead, txBuffer, sizeof(txBuffer));

        if (respLen > 0) {
          std::size_t bytesWritten = 0;
          (void)pty_.writeMaster(txBuffer, respLen, bytesWritten, 100);
          ++transactionCount_;
        }
      }
    }
  }

  std::size_t processRequest(const std::uint8_t* request, std::size_t reqLen,
                             std::uint8_t* response, std::size_t respCapacity) {
    // Validate minimum length and CRC
    if (reqLen < 4 || respCapacity < 5) {
      return 0;
    }

    // Verify CRC
    const std::uint16_t RECEIVED_CRC = static_cast<std::uint16_t>(request[reqLen - 2]) |
                                       (static_cast<std::uint16_t>(request[reqLen - 1]) << 8);
    const std::uint16_t CALC_CRC = modbus::calculateCrc(request, reqLen - 2);
    if (RECEIVED_CRC != CALC_CRC) {
      return 0; // Silent discard on CRC error
    }

    // Check unit address
    const std::uint8_t UNIT = request[0];
    if (UNIT != UNIT_ADDRESS && UNIT != 0) {
      return 0; // Not for us
    }

    // Parse function code and dispatch
    const std::uint8_t FC = request[1];
    std::size_t pduLen = 0;

    switch (FC) {
    case static_cast<std::uint8_t>(modbus::FunctionCode::READ_COILS):
      pduLen = handleReadCoils(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::READ_DISCRETE_INPUTS):
      pduLen = handleReadDiscreteInputs(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::READ_HOLDING_REGISTERS):
      pduLen = handleReadHoldingRegisters(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::READ_INPUT_REGISTERS):
      pduLen = handleReadInputRegisters(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_SINGLE_COIL):
      pduLen = handleWriteSingleCoil(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_SINGLE_REGISTER):
      pduLen = handleWriteSingleRegister(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_MULTIPLE_COILS):
      pduLen = handleWriteMultipleCoils(request, reqLen, response, respCapacity);
      break;

    case static_cast<std::uint8_t>(modbus::FunctionCode::WRITE_MULTIPLE_REGISTERS):
      pduLen = handleWriteMultipleRegisters(request, reqLen, response, respCapacity);
      break;

    default:
      pduLen = buildExceptionResponse(response, respCapacity, UNIT, FC,
                                      modbus::ExceptionCode::ILLEGAL_FUNCTION);
      break;
    }

    // Broadcast: no response
    if (UNIT == 0) {
      return 0;
    }

    // Append CRC
    if (pduLen > 0 && pduLen + 2 <= respCapacity) {
      const std::uint16_t CRC = modbus::calculateCrc(response, pduLen);
      response[pduLen] = static_cast<std::uint8_t>(CRC & 0xFF);
      response[pduLen + 1] = static_cast<std::uint8_t>(CRC >> 8);
      return pduLen + 2;
    }

    return 0;
  }

  std::size_t handleReadHoldingRegisters(const std::uint8_t* request, std::size_t reqLen,
                                         std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) { // unit + fc + addr(2) + qty(2) + crc(2)
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    // Validate quantity
    if (QUANTITY == 0 || QUANTITY > 125) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    // Validate address range
    if (START_ADDR + QUANTITY > NUM_HOLDING_REGISTERS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    // Build response
    const std::size_t BYTE_COUNT = QUANTITY * 2;
    if (3 + BYTE_COUNT > respCapacity) {
      return 0;
    }

    response[0] = UNIT;
    response[1] = FC;
    response[2] = static_cast<std::uint8_t>(BYTE_COUNT);

    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      const std::uint16_t VAL = holdingRegisters_[START_ADDR + i];
      response[3 + i * 2] = static_cast<std::uint8_t>(VAL >> 8);
      response[3 + i * 2 + 1] = static_cast<std::uint8_t>(VAL & 0xFF);
    }

    return 3 + BYTE_COUNT;
  }

  std::size_t handleReadInputRegisters(const std::uint8_t* request, std::size_t reqLen,
                                       std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    if (QUANTITY == 0 || QUANTITY > 125) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    if (START_ADDR + QUANTITY > NUM_INPUT_REGISTERS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    const std::size_t BYTE_COUNT = QUANTITY * 2;
    if (3 + BYTE_COUNT > respCapacity) {
      return 0;
    }

    response[0] = UNIT;
    response[1] = FC;
    response[2] = static_cast<std::uint8_t>(BYTE_COUNT);

    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      const std::uint16_t VAL = inputRegisters_[START_ADDR + i];
      response[3 + i * 2] = static_cast<std::uint8_t>(VAL >> 8);
      response[3 + i * 2 + 1] = static_cast<std::uint8_t>(VAL & 0xFF);
    }

    return 3 + BYTE_COUNT;
  }

  std::size_t handleReadCoils(const std::uint8_t* request, std::size_t reqLen,
                              std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    if (QUANTITY == 0 || QUANTITY > 2000) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    if (START_ADDR + QUANTITY > NUM_COILS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    const std::size_t BYTE_COUNT = (QUANTITY + 7) / 8;
    if (3 + BYTE_COUNT > respCapacity) {
      return 0;
    }

    response[0] = UNIT;
    response[1] = FC;
    response[2] = static_cast<std::uint8_t>(BYTE_COUNT);

    // Pack coil values into response bytes
    std::memset(&response[3], 0, BYTE_COUNT);
    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      const std::size_t COIL_ADDR = START_ADDR + i;
      const std::size_t SRC_BYTE = COIL_ADDR / 8;
      const std::size_t SRC_BIT = COIL_ADDR % 8;
      const bool VALUE = (coils_[SRC_BYTE] & (1U << SRC_BIT)) != 0;

      const std::size_t DST_BYTE = i / 8;
      const std::size_t DST_BIT = i % 8;
      if (VALUE) {
        response[3 + DST_BYTE] |= static_cast<std::uint8_t>(1U << DST_BIT);
      }
    }

    return 3 + BYTE_COUNT;
  }

  std::size_t handleReadDiscreteInputs(const std::uint8_t* request, std::size_t reqLen,
                                       std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    if (QUANTITY == 0 || QUANTITY > 2000) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    if (START_ADDR + QUANTITY > NUM_DISCRETE_INPUTS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    const std::size_t BYTE_COUNT = (QUANTITY + 7) / 8;
    if (3 + BYTE_COUNT > respCapacity) {
      return 0;
    }

    response[0] = UNIT;
    response[1] = FC;
    response[2] = static_cast<std::uint8_t>(BYTE_COUNT);

    std::memset(&response[3], 0, BYTE_COUNT);
    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      const std::size_t INPUT_ADDR = START_ADDR + i;
      const std::size_t SRC_BYTE = INPUT_ADDR / 8;
      const std::size_t SRC_BIT = INPUT_ADDR % 8;
      const bool VALUE = (discreteInputs_[SRC_BYTE] & (1U << SRC_BIT)) != 0;

      const std::size_t DST_BYTE = i / 8;
      const std::size_t DST_BIT = i % 8;
      if (VALUE) {
        response[3 + DST_BYTE] |= static_cast<std::uint8_t>(1U << DST_BIT);
      }
    }

    return 3 + BYTE_COUNT;
  }

  std::size_t handleWriteSingleRegister(const std::uint8_t* request, std::size_t reqLen,
                                        std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t REG_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t VALUE = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    if (REG_ADDR >= NUM_HOLDING_REGISTERS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    // Write the register
    holdingRegisters_[REG_ADDR] = VALUE;

    // Echo response
    if (respCapacity < 6) {
      return 0;
    }
    std::memcpy(response, request, 6);
    return 6;
  }

  std::size_t handleWriteSingleCoil(const std::uint8_t* request, std::size_t reqLen,
                                    std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 8) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t COIL_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t VALUE = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];

    if (COIL_ADDR >= NUM_COILS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    // Validate value (0x0000 = OFF, 0xFF00 = ON)
    if (VALUE != 0x0000 && VALUE != 0xFF00) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    // Write the coil
    const std::size_t BYTE_IDX = COIL_ADDR / 8;
    const std::size_t BIT_IDX = COIL_ADDR % 8;
    if (VALUE == 0xFF00) {
      coils_[BYTE_IDX] |= static_cast<std::uint8_t>(1U << BIT_IDX);
    } else {
      coils_[BYTE_IDX] &= static_cast<std::uint8_t>(~(1U << BIT_IDX));
    }

    // Echo response
    if (respCapacity < 6) {
      return 0;
    }
    std::memcpy(response, request, 6);
    return 6;
  }

  std::size_t handleWriteMultipleRegisters(const std::uint8_t* request, std::size_t reqLen,
                                           std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 9) { // unit + fc + addr(2) + qty(2) + bytecount + crc(2)
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];
    const std::uint8_t BYTE_COUNT = request[6];

    if (QUANTITY == 0 || QUANTITY > 123 || BYTE_COUNT != QUANTITY * 2) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    if (START_ADDR + QUANTITY > NUM_HOLDING_REGISTERS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    if (reqLen < static_cast<std::size_t>(7 + BYTE_COUNT + 2)) {
      return 0;
    }

    // Write the registers
    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      holdingRegisters_[START_ADDR + i] =
          (static_cast<std::uint16_t>(request[7 + i * 2]) << 8) | request[7 + i * 2 + 1];
    }

    // Response: unit + fc + startAddr + quantity
    if (respCapacity < 6) {
      return 0;
    }
    response[0] = UNIT;
    response[1] = FC;
    response[2] = request[2];
    response[3] = request[3];
    response[4] = request[4];
    response[5] = request[5];
    return 6;
  }

  std::size_t handleWriteMultipleCoils(const std::uint8_t* request, std::size_t reqLen,
                                       std::uint8_t* response, std::size_t respCapacity) {
    if (reqLen < 9) {
      return 0;
    }

    const std::uint8_t UNIT = request[0];
    const std::uint8_t FC = request[1];
    const std::uint16_t START_ADDR = (static_cast<std::uint16_t>(request[2]) << 8) | request[3];
    const std::uint16_t QUANTITY = (static_cast<std::uint16_t>(request[4]) << 8) | request[5];
    const std::uint8_t BYTE_COUNT = request[6];

    const std::size_t EXPECTED_BYTES = (QUANTITY + 7) / 8;
    if (QUANTITY == 0 || QUANTITY > 1968 || BYTE_COUNT != EXPECTED_BYTES) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_VALUE);
    }

    if (START_ADDR + QUANTITY > NUM_COILS) {
      return buildExceptionResponse(response, respCapacity, UNIT, FC,
                                    modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    }

    if (reqLen < static_cast<std::size_t>(7 + BYTE_COUNT + 2)) {
      return 0;
    }

    // Write the coils
    for (std::uint16_t i = 0; i < QUANTITY; ++i) {
      const std::size_t SRC_BYTE = i / 8;
      const std::size_t SRC_BIT = i % 8;
      const bool VALUE = (request[7 + SRC_BYTE] & (1U << SRC_BIT)) != 0;

      const std::size_t DST_ADDR = START_ADDR + i;
      const std::size_t DST_BYTE = DST_ADDR / 8;
      const std::size_t DST_BIT = DST_ADDR % 8;
      if (VALUE) {
        coils_[DST_BYTE] |= static_cast<std::uint8_t>(1U << DST_BIT);
      } else {
        coils_[DST_BYTE] &= static_cast<std::uint8_t>(~(1U << DST_BIT));
      }
    }

    // Response
    if (respCapacity < 6) {
      return 0;
    }
    response[0] = UNIT;
    response[1] = FC;
    response[2] = request[2];
    response[3] = request[3];
    response[4] = request[4];
    response[5] = request[5];
    return 6;
  }

  std::size_t buildExceptionResponse(std::uint8_t* response, std::size_t respCapacity,
                                     std::uint8_t unit, std::uint8_t fc,
                                     modbus::ExceptionCode exCode) {
    if (respCapacity < 3) {
      return 0;
    }
    response[0] = unit;
    response[1] = fc | 0x80; // Exception flag
    response[2] = static_cast<std::uint8_t>(exCode);
    return 3;
  }

  PtyPair pty_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint32_t> transactionCount_{0};

  std::uint16_t holdingRegisters_[NUM_HOLDING_REGISTERS];
  std::uint16_t inputRegisters_[NUM_INPUT_REGISTERS];
  std::uint8_t coils_[(NUM_COILS + 7) / 8];
  std::uint8_t discreteInputs_[(NUM_DISCRETE_INPUTS + 7) / 8];
};

/* ----------------------------- Test Fixture ----------------------------- */

/**
 * @class ModbusEmulationTest
 * @brief Test fixture for Modbus emulation tests.
 *
 * Sets up:
 *  - Emulated Modbus slave
 *  - UART adapter connected to slave
 *  - Modbus RTU transport
 *  - Modbus master for issuing commands
 */
class ModbusEmulationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Initialize and start emulated slave
    ASSERT_TRUE(slave_.init());
    slave_.start();

    // Allow slave thread to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Configure UART adapter connected to slave
    adapter_ = std::make_unique<UartAdapter>(slave_.slavePath());
    UartConfig cfg;
    cfg.exclusiveAccess = false;
    ASSERT_EQ(adapter_->configure(cfg), apex::protocols::serial::uart::Status::SUCCESS);

    // Create RTU transport
    modbus::ModbusRtuConfig rtuConfig;
    rtuConfig.responseTimeoutMs = 200;
    rtuConfig.interFrameDelayUs = 0;
    transport_ = std::make_unique<modbus::ModbusRtuTransport>(adapter_.get(), rtuConfig, 115200);
    ASSERT_EQ(transport_->open(), modbus::Status::SUCCESS);

    // Create master
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
    slave_.stop();
  }

  EmulatedModbusSlave slave_;
  std::unique_ptr<UartAdapter> adapter_;
  std::unique_ptr<modbus::ModbusRtuTransport> transport_;
  std::unique_ptr<modbus::ModbusMaster> master_;
};

/* ----------------------------- Read Holding Registers ----------------------------- */

/** @test Read single holding register from emulated slave. */
TEST_F(ModbusEmulationTest, ReadSingleHoldingRegister) {
  std::uint16_t value = 0;
  const modbus::ModbusResult RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 1, &value, 500);

  EXPECT_TRUE(RESULT.ok()) << "Status: " << static_cast<int>(RESULT.status);
  EXPECT_EQ(value, 0x1000); // Initial value at address 0
}

/** @test Read multiple holding registers from emulated slave. */
TEST_F(ModbusEmulationTest, ReadMultipleHoldingRegisters) {
  std::uint16_t values[4] = {0};
  const modbus::ModbusResult RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 4, values, 500);

  EXPECT_TRUE(RESULT.ok()) << "Status: " << static_cast<int>(RESULT.status);
  EXPECT_EQ(values[0], 0x1000);
  EXPECT_EQ(values[1], 0x1001);
  EXPECT_EQ(values[2], 0x1002);
  EXPECT_EQ(values[3], 0x1003);
}

/** @test Read holding registers with offset. */
TEST_F(ModbusEmulationTest, ReadHoldingRegistersWithOffset) {
  std::uint16_t values[2] = {0};
  const modbus::ModbusResult RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 5, 2, values, 500);

  EXPECT_TRUE(RESULT.ok());
  EXPECT_EQ(values[0], 0x1005);
  EXPECT_EQ(values[1], 0x1006);
}

/** @test Read holding registers returns exception for invalid address. */
TEST_F(ModbusEmulationTest, ReadHoldingRegistersInvalidAddress) {
  std::uint16_t values[4] = {0};
  const modbus::ModbusResult RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 14, 4, values, 500);

  EXPECT_FALSE(RESULT.ok());
  EXPECT_TRUE(RESULT.isException());
  EXPECT_EQ(RESULT.exceptionCode, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
}

/* ----------------------------- Read Input Registers ----------------------------- */

/** @test Read input registers from emulated slave. */
TEST_F(ModbusEmulationTest, ReadInputRegisters) {
  std::uint16_t values[3] = {0};
  const modbus::ModbusResult RESULT =
      master_->readInputRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 3, values, 500);

  EXPECT_TRUE(RESULT.ok()) << "Status: " << static_cast<int>(RESULT.status);
  EXPECT_EQ(values[0], 0x2000);
  EXPECT_EQ(values[1], 0x2001);
  EXPECT_EQ(values[2], 0x2002);
}

/* ----------------------------- Write Single Register ----------------------------- */

/** @test Write single register to emulated slave. */
TEST_F(ModbusEmulationTest, WriteSingleRegister) {
  // Write new value
  const modbus::ModbusResult WRITE_RESULT =
      master_->writeSingleRegister(EmulatedModbusSlave::UNIT_ADDRESS, 5, 0xABCD, 500);
  EXPECT_TRUE(WRITE_RESULT.ok());

  // Read back to verify
  std::uint16_t value = 0;
  const modbus::ModbusResult READ_RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 5, 1, &value, 500);
  EXPECT_TRUE(READ_RESULT.ok());
  EXPECT_EQ(value, 0xABCD);
}

/** @test Write single register returns exception for invalid address. */
TEST_F(ModbusEmulationTest, WriteSingleRegisterInvalidAddress) {
  const modbus::ModbusResult RESULT =
      master_->writeSingleRegister(EmulatedModbusSlave::UNIT_ADDRESS, 100, 0x1234, 500);

  EXPECT_FALSE(RESULT.ok());
  EXPECT_TRUE(RESULT.isException());
  EXPECT_EQ(RESULT.exceptionCode, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
}

/* ----------------------------- Write Multiple Registers ----------------------------- */

/** @test Write multiple registers to emulated slave. */
TEST_F(ModbusEmulationTest, WriteMultipleRegisters) {
  const std::uint16_t VALUES[] = {0x1111, 0x2222, 0x3333};
  const modbus::ModbusResult WRITE_RESULT =
      master_->writeMultipleRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 2, 3, VALUES, 500);
  EXPECT_TRUE(WRITE_RESULT.ok());

  // Read back to verify
  std::uint16_t readValues[3] = {0};
  const modbus::ModbusResult READ_RESULT =
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 2, 3, readValues, 500);
  EXPECT_TRUE(READ_RESULT.ok());
  EXPECT_EQ(readValues[0], 0x1111);
  EXPECT_EQ(readValues[1], 0x2222);
  EXPECT_EQ(readValues[2], 0x3333);
}

/* ----------------------------- Read Coils ----------------------------- */

/** @test Read coils from emulated slave. */
TEST_F(ModbusEmulationTest, ReadCoils) {
  std::uint8_t values[1] = {0};
  const modbus::ModbusResult RESULT =
      master_->readCoils(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, values, 500);

  EXPECT_TRUE(RESULT.ok()) << "Status: " << static_cast<int>(RESULT.status);
  // Initial pattern is 0xAA (alternating bits)
  EXPECT_EQ(values[0], 0xAA);
}

/* ----------------------------- Read Discrete Inputs ----------------------------- */

/** @test Read discrete inputs from emulated slave. */
TEST_F(ModbusEmulationTest, ReadDiscreteInputs) {
  std::uint8_t values[1] = {0};
  const modbus::ModbusResult RESULT =
      master_->readDiscreteInputs(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, values, 500);

  EXPECT_TRUE(RESULT.ok()) << "Status: " << static_cast<int>(RESULT.status);
  // Initial pattern is 0x55 (inverse of coils)
  EXPECT_EQ(values[0], 0x55);
}

/* ----------------------------- Write Single Coil ----------------------------- */

/** @test Write single coil to emulated slave. */
TEST_F(ModbusEmulationTest, WriteSingleCoil) {
  // Initial coil 0 should be OFF (0xAA pattern means bit 0 = 0)
  // Write coil 0 ON
  const modbus::ModbusResult WRITE_RESULT =
      master_->writeSingleCoil(EmulatedModbusSlave::UNIT_ADDRESS, 0, true, 500);
  EXPECT_TRUE(WRITE_RESULT.ok());

  // Read back to verify
  std::uint8_t values[1] = {0};
  const modbus::ModbusResult READ_RESULT =
      master_->readCoils(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, values, 500);
  EXPECT_TRUE(READ_RESULT.ok());
  // 0xAA | 0x01 = 0xAB
  EXPECT_EQ(values[0], 0xAB);
}

/** @test Write single coil OFF. */
TEST_F(ModbusEmulationTest, WriteSingleCoilOff) {
  // Initial coil 1 should be ON (0xAA pattern means bit 1 = 1)
  // Write coil 1 OFF
  const modbus::ModbusResult WRITE_RESULT =
      master_->writeSingleCoil(EmulatedModbusSlave::UNIT_ADDRESS, 1, false, 500);
  EXPECT_TRUE(WRITE_RESULT.ok());

  // Read back to verify
  std::uint8_t values[1] = {0};
  const modbus::ModbusResult READ_RESULT =
      master_->readCoils(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, values, 500);
  EXPECT_TRUE(READ_RESULT.ok());
  // 0xAA & ~0x02 = 0xA8
  EXPECT_EQ(values[0], 0xA8);
}

/* ----------------------------- Write Multiple Coils ----------------------------- */

/** @test Write multiple coils to emulated slave. */
TEST_F(ModbusEmulationTest, WriteMultipleCoils) {
  // Write 8 coils starting at address 0 with pattern 0x55
  const std::uint8_t VALUES[] = {0x55};
  const modbus::ModbusResult WRITE_RESULT =
      master_->writeMultipleCoils(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, VALUES, 500);
  EXPECT_TRUE(WRITE_RESULT.ok());

  // Read back to verify
  std::uint8_t readValues[1] = {0};
  const modbus::ModbusResult READ_RESULT =
      master_->readCoils(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, readValues, 500);
  EXPECT_TRUE(READ_RESULT.ok());
  EXPECT_EQ(readValues[0], 0x55);
}

/* ----------------------------- Multi-Transaction Sequence ----------------------------- */

/** @test Demonstrate typical polling sequence. */
TEST_F(ModbusEmulationTest, TypicalPollingSequence) {
  // This demonstrates how a model might poll a Modbus device

  // 1. Read device status (input registers)
  std::uint16_t statusRegs[2] = {0};
  EXPECT_TRUE(
      master_->readInputRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 2, statusRegs, 500).ok());

  // 2. Read process values (holding registers)
  std::uint16_t processValues[4] = {0};
  EXPECT_TRUE(
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 4, processValues, 500)
          .ok());

  // 3. Read digital inputs (discrete inputs)
  std::uint8_t digitalInputs[1] = {0};
  EXPECT_TRUE(
      master_->readDiscreteInputs(EmulatedModbusSlave::UNIT_ADDRESS, 0, 8, digitalInputs, 500)
          .ok());

  // 4. Write control setpoint
  EXPECT_TRUE(
      master_->writeSingleRegister(EmulatedModbusSlave::UNIT_ADDRESS, 10, 0x1234, 500).ok());

  // 5. Write control outputs
  EXPECT_TRUE(master_->writeSingleCoil(EmulatedModbusSlave::UNIT_ADDRESS, 0, true, 500).ok());

  // Verify transaction count
  EXPECT_GE(slave_.transactionCount(), 5u);
}

/** @test Demonstrate batch write with verification. */
TEST_F(ModbusEmulationTest, BatchWriteWithVerification) {
  // Write setpoints
  const std::uint16_t SETPOINTS[] = {100, 200, 300, 400, 500};
  EXPECT_TRUE(
      master_->writeMultipleRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 5, SETPOINTS, 500)
          .ok());

  // Verify all written values
  std::uint16_t readback[5] = {0};
  EXPECT_TRUE(
      master_->readHoldingRegisters(EmulatedModbusSlave::UNIT_ADDRESS, 0, 5, readback, 500).ok());

  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(readback[i], SETPOINTS[i]);
  }
}

/* ----------------------------- Error Handling ----------------------------- */

/** @test Wrong unit address results in timeout (no response). */
TEST_F(ModbusEmulationTest, WrongUnitAddressTimeout) {
  std::uint16_t value = 0;
  const modbus::ModbusResult RESULT =
      master_->readHoldingRegisters(99, 0, 1, &value, 100); // Wrong unit address

  EXPECT_FALSE(RESULT.ok());
  // Should timeout or would_block since slave ignores requests for wrong unit
  EXPECT_TRUE(RESULT.status == modbus::Status::ERROR_TIMEOUT ||
              RESULT.status == modbus::Status::WOULD_BLOCK);
}
