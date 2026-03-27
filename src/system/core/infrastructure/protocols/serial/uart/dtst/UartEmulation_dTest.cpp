/**
 * @file UartEmulation_dTest.cpp
 * @brief Development tests demonstrating UART device emulation patterns.
 *
 * This file shows how to:
 *  1. Create an emulated UART device using PtyPair
 *  2. Build a device model that responds to commands
 *  3. Connect a driver/controller to the emulated device
 *  4. Test bidirectional communication
 *
 * These patterns are useful for:
 *  - Hardware-in-the-loop simulation
 *  - Testing drivers without physical hardware
 *  - Developing and debugging serial protocols
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <thread>

using apex::protocols::serial::uart::PtyPair;
using apex::protocols::serial::uart::Status;
using apex::protocols::serial::uart::UartAdapter;
using apex::protocols::serial::uart::UartConfig;

/* ----------------------------- Emulated Device Base ----------------------------- */

/**
 * @class EmulatedUartDevice
 * @brief Abstract base for emulated UART devices.
 *
 * Subclass this to create device models that respond to commands.
 * The emulated device owns a PtyPair and provides the slave path
 * for drivers to connect to.
 *
 * Usage pattern:
 *  1. Create subclass implementing processCommand()
 *  2. Call start() to begin the device thread
 *  3. Driver connects to slavePath() via UartAdapter
 *  4. Call stop() when done
 */
class EmulatedUartDevice {
public:
  EmulatedUartDevice() = default;
  virtual ~EmulatedUartDevice() { stop(); }

  EmulatedUartDevice(const EmulatedUartDevice&) = delete;
  EmulatedUartDevice& operator=(const EmulatedUartDevice&) = delete;

  /**
   * @brief Initialize the PTY pair.
   * @return true if successful.
   */
  bool init() {
    if (pty_.open() != Status::SUCCESS) {
      return false;
    }
    return true;
  }

  /**
   * @brief Get the slave path for driver connection.
   * @return Path like "/dev/pts/N".
   */
  const char* slavePath() const { return pty_.slavePath(); }

  /**
   * @brief Start the device emulation thread.
   */
  void start() {
    running_ = true;
    thread_ = std::thread(&EmulatedUartDevice::runLoop, this);
  }

  /**
   * @brief Stop the device emulation thread.
   */
  void stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /**
   * @brief Check if device is running.
   */
  bool isRunning() const { return running_; }

protected:
  /**
   * @brief Process a received command and return response.
   * @param command Received command bytes.
   * @param cmdLen Length of command.
   * @param response Buffer for response.
   * @param respCapacity Capacity of response buffer.
   * @return Length of response, or 0 for no response.
   *
   * Override this to implement device-specific behavior.
   */
  virtual std::size_t processCommand(const std::uint8_t* command, std::size_t cmdLen,
                                     std::uint8_t* response, std::size_t respCapacity) = 0;

  PtyPair& pty() { return pty_; }

private:
  void runLoop() {
    std::uint8_t rxBuffer[256];
    std::uint8_t txBuffer[256];

    while (running_) {
      std::size_t bytesRead = 0;
      Status status = pty_.readMaster(rxBuffer, sizeof(rxBuffer), bytesRead, 50);

      if (status == Status::SUCCESS && bytesRead > 0) {
        std::size_t respLen = processCommand(rxBuffer, bytesRead, txBuffer, sizeof(txBuffer));

        if (respLen > 0) {
          std::size_t bytesWritten = 0;
          (void)pty_.writeMaster(txBuffer, respLen, bytesWritten, 100);
        }
      }
    }
  }

  PtyPair pty_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

/* ----------------------------- Example: Echo Device ----------------------------- */

/**
 * @class EchoDevice
 * @brief Simple device that echoes back received data.
 *
 * Useful for basic connectivity testing.
 */
class EchoDevice : public EmulatedUartDevice {
protected:
  std::size_t processCommand(const std::uint8_t* command, std::size_t cmdLen,
                             std::uint8_t* response, std::size_t respCapacity) override {
    std::size_t copyLen = (cmdLen < respCapacity) ? cmdLen : respCapacity;
    std::memcpy(response, command, copyLen);
    return copyLen;
  }
};

/* ----------------------------- Example: Command/Response Device ----------------------------- */

/**
 * @class SensorDevice
 * @brief Emulated sensor that responds to ASCII commands.
 *
 * Supported commands:
 *  - "ID?"     -> "SENSOR_V1.0"
 *  - "TEMP?"   -> "TEMP:25.5"
 *  - "STATUS?" -> "OK"
 *  - Other     -> "ERR:UNKNOWN"
 */
class SensorDevice : public EmulatedUartDevice {
protected:
  std::size_t processCommand(const std::uint8_t* command, std::size_t cmdLen,
                             std::uint8_t* response, std::size_t respCapacity) override {
    std::string cmd(reinterpret_cast<const char*>(command), cmdLen);

    const char* resp = nullptr;
    if (cmd == "ID?") {
      resp = "SENSOR_V1.0";
    } else if (cmd == "TEMP?") {
      resp = "TEMP:25.5";
    } else if (cmd == "STATUS?") {
      resp = "OK";
    } else {
      resp = "ERR:UNKNOWN";
    }

    std::size_t respLen = std::strlen(resp);
    if (respLen > respCapacity) {
      respLen = respCapacity;
    }
    std::memcpy(response, resp, respLen);
    return respLen;
  }
};

/* ----------------------------- Example: Binary Protocol Device ----------------------------- */

/**
 * @class BinaryProtocolDevice
 * @brief Emulated device using a simple binary protocol.
 *
 * Protocol format:
 *  Request:  [CMD:1][LEN:1][DATA:LEN]
 *  Response: [STATUS:1][LEN:1][DATA:LEN]
 *
 * Commands:
 *  0x01 - Read register (DATA = register address)
 *  0x02 - Write register (DATA = address + value)
 *  0x03 - Get device info
 */
class BinaryProtocolDevice : public EmulatedUartDevice {
public:
  static constexpr std::uint8_t CMD_READ_REG = 0x01;
  static constexpr std::uint8_t CMD_WRITE_REG = 0x02;
  static constexpr std::uint8_t CMD_GET_INFO = 0x03;

  static constexpr std::uint8_t STATUS_OK = 0x00;
  static constexpr std::uint8_t STATUS_ERR = 0xFF;

protected:
  std::size_t processCommand(const std::uint8_t* command, std::size_t cmdLen,
                             std::uint8_t* response, std::size_t respCapacity) override {
    if (cmdLen < 2 || respCapacity < 2) {
      return 0;
    }

    std::uint8_t cmd = command[0];
    std::uint8_t len = command[1];

    if (cmdLen < static_cast<std::size_t>(2 + len)) {
      response[0] = STATUS_ERR;
      response[1] = 0;
      return 2;
    }

    switch (cmd) {
    case CMD_READ_REG: {
      if (len >= 1) {
        std::uint8_t regAddr = command[2];
        if (regAddr < sizeof(registers_)) {
          response[0] = STATUS_OK;
          response[1] = 1;
          response[2] = registers_[regAddr];
          return 3;
        }
      }
      response[0] = STATUS_ERR;
      response[1] = 0;
      return 2;
    }

    case CMD_WRITE_REG: {
      if (len >= 2) {
        std::uint8_t regAddr = command[2];
        std::uint8_t value = command[3];
        if (regAddr < sizeof(registers_)) {
          registers_[regAddr] = value;
          response[0] = STATUS_OK;
          response[1] = 0;
          return 2;
        }
      }
      response[0] = STATUS_ERR;
      response[1] = 0;
      return 2;
    }

    case CMD_GET_INFO: {
      response[0] = STATUS_OK;
      response[1] = 4;
      response[2] = 0xDE;
      response[3] = 0xAD;
      response[4] = 0xBE;
      response[5] = 0xEF;
      return 6;
    }

    default:
      response[0] = STATUS_ERR;
      response[1] = 0;
      return 2;
    }
  }

private:
  std::uint8_t registers_[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                                 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0xFF};
};

/* ----------------------------- Tests ----------------------------- */

/** @test Echo device returns sent data. */
TEST(UartEmulationTest, EchoDevice) {
  EchoDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  const char* testMsg = "Hello, Echo!";
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeAscii(testMsg, bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);
  EXPECT_EQ(bytesRead, std::strlen(testMsg));
  EXPECT_EQ(std::memcmp(rxBuffer, testMsg, bytesRead), 0);

  device.stop();
}

/** @test Sensor device responds to ID query. */
TEST(UartEmulationTest, SensorDeviceId) {
  SensorDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeAscii("ID?", bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);

  std::string response(reinterpret_cast<char*>(rxBuffer), bytesRead);
  EXPECT_EQ(response, "SENSOR_V1.0");

  device.stop();
}

/** @test Sensor device responds to TEMP query. */
TEST(UartEmulationTest, SensorDeviceTemp) {
  SensorDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeAscii("TEMP?", bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);

  std::string response(reinterpret_cast<char*>(rxBuffer), bytesRead);
  EXPECT_EQ(response, "TEMP:25.5");

  device.stop();
}

/** @test Sensor device handles unknown command. */
TEST(UartEmulationTest, SensorDeviceUnknown) {
  SensorDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.writeAscii("FOOBAR", bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);

  std::string response(reinterpret_cast<char*>(rxBuffer), bytesRead);
  EXPECT_EQ(response, "ERR:UNKNOWN");

  device.stop();
}

/** @test Binary protocol device read register. */
TEST(UartEmulationTest, BinaryProtocolReadReg) {
  BinaryProtocolDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t cmd[] = {BinaryProtocolDevice::CMD_READ_REG, 0x01, 0x00};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(cmd, sizeof(cmd), bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);

  ASSERT_GE(bytesRead, 3u);
  EXPECT_EQ(rxBuffer[0], BinaryProtocolDevice::STATUS_OK);
  EXPECT_EQ(rxBuffer[1], 1);
  EXPECT_EQ(rxBuffer[2], 0x10);

  device.stop();
}

/** @test Binary protocol device write and read back register. */
TEST(UartEmulationTest, BinaryProtocolWriteReadReg) {
  BinaryProtocolDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t writeCmd[] = {BinaryProtocolDevice::CMD_WRITE_REG, 0x02, 0x05, 0xAB};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(writeCmd, sizeof(writeCmd), bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);
  EXPECT_EQ(rxBuffer[0], BinaryProtocolDevice::STATUS_OK);

  std::uint8_t readCmd[] = {BinaryProtocolDevice::CMD_READ_REG, 0x01, 0x05};
  EXPECT_EQ(adapter.write(readCmd, sizeof(readCmd), bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);
  ASSERT_GE(bytesRead, 3u);
  EXPECT_EQ(rxBuffer[0], BinaryProtocolDevice::STATUS_OK);
  EXPECT_EQ(rxBuffer[2], 0xAB);

  device.stop();
}

/** @test Binary protocol device get info. */
TEST(UartEmulationTest, BinaryProtocolGetInfo) {
  BinaryProtocolDevice device;
  ASSERT_TRUE(device.init());
  device.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  UartAdapter adapter(device.slavePath());
  UartConfig cfg{};
  cfg.exclusiveAccess = false;
  ASSERT_EQ(adapter.configure(cfg), Status::SUCCESS);

  std::uint8_t cmd[] = {BinaryProtocolDevice::CMD_GET_INFO, 0x00};
  std::size_t bytesWritten = 0;
  EXPECT_EQ(adapter.write(cmd, sizeof(cmd), bytesWritten, 100), Status::SUCCESS);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::uint8_t rxBuffer[64];
  std::size_t bytesRead = 0;
  EXPECT_EQ(adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 200), Status::SUCCESS);

  ASSERT_GE(bytesRead, 6u);
  EXPECT_EQ(rxBuffer[0], BinaryProtocolDevice::STATUS_OK);
  EXPECT_EQ(rxBuffer[1], 4);
  EXPECT_EQ(rxBuffer[2], 0xDE);
  EXPECT_EQ(rxBuffer[3], 0xAD);
  EXPECT_EQ(rxBuffer[4], 0xBE);
  EXPECT_EQ(rxBuffer[5], 0xEF);

  device.stop();
}
