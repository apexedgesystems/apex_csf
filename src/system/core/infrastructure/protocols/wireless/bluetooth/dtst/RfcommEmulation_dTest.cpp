/**
 * @file RfcommEmulation_dTest.cpp
 * @brief Development tests demonstrating Bluetooth RFCOMM device emulation patterns.
 *
 * This file shows how to:
 *  1. Create an emulated Bluetooth device using RfcommLoopback
 *  2. Build a device model that responds to commands
 *  3. Connect an RfcommAdapter to the emulated device via FD injection
 *  4. Test request/response communication patterns
 *
 * These patterns are useful for:
 *  - Hardware-in-the-loop simulation of Bluetooth devices
 *  - Testing drivers without physical Bluetooth hardware
 *  - Developing and debugging RFCOMM communications
 *  - Model integration testing in simulation environments
 *
 * Key difference from old EmulatedBluetoothDevice:
 *  - No #ifdef ENABLE_BT_SIMULATION needed
 *  - No bool simulate constructor parameter
 *  - Clean separation via FD injection pattern
 *  - RfcommLoopback provides both ends of the connection
 */

#include "RfcommAdapter.hpp"
#include "RfcommLoopback.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

namespace bt = apex::protocols::wireless::bluetooth;
using apex::protocols::TraceDirection;

/* ----------------------------- Emulated Bluetooth Device ----------------------------- */

/**
 * @class EmulatedBluetoothDevice
 * @brief Emulated Bluetooth device for testing.
 *
 * This class emulates a Bluetooth device that:
 *  - Reads commands from the "client" (RfcommAdapter)
 *  - Processes commands and sends responses
 *  - Runs in a background thread to simulate async I/O
 *
 * Unlike the old EmulatedBluetoothDevice:
 *  - Does NOT require #ifdef ENABLE_BT_SIMULATION
 *  - Does NOT require getSimulatedSockFD() accessor
 *  - Uses RfcommLoopback's server-side API directly
 *
 * Usage pattern:
 *  1. Create RfcommLoopback and call open()
 *  2. Create EmulatedBluetoothDevice with the loopback reference
 *  3. Release client FD to RfcommAdapter via loopback.releaseClientFd()
 *  4. Call start() to begin the emulation thread
 *  5. Communicate via RfcommAdapter as if it were a real device
 *  6. Call stop() when done
 */
class EmulatedBluetoothDevice {
public:
  /**
   * @brief Construct emulated device.
   * @param loopback Reference to RfcommLoopback (must remain valid).
   */
  explicit EmulatedBluetoothDevice(bt::RfcommLoopback& loopback) : loopback_(loopback) {}

  ~EmulatedBluetoothDevice() { stop(); }

  EmulatedBluetoothDevice(const EmulatedBluetoothDevice&) = delete;
  EmulatedBluetoothDevice& operator=(const EmulatedBluetoothDevice&) = delete;

  /**
   * @brief Start the emulation thread.
   */
  void start() {
    running_ = true;
    thread_ = std::thread(&EmulatedBluetoothDevice::runLoop, this);
  }

  /**
   * @brief Stop the emulation thread.
   */
  void stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  /**
   * @brief Check if emulator is running.
   */
  bool isRunning() const { return running_.load(std::memory_order_relaxed); }

  /**
   * @brief Get number of commands processed.
   */
  std::size_t commandsProcessed() const { return commandsProcessed_; }

private:
  void runLoop() {
    std::uint8_t buffer[256];

    while (running_.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      bt::Status status = loopback_.serverRead({buffer, sizeof(buffer)}, bytesRead, 100);

      if (status == bt::Status::SUCCESS && bytesRead > 0) {
        processCommand(buffer, bytesRead);
      }
      // WOULD_BLOCK is expected when no data available - just continue polling
    }
  }

  void processCommand(const std::uint8_t* data, std::size_t len) {
    // Simple command processing: echo back with "ACK: " prefix
    std::vector<std::uint8_t> response;
    response.reserve(5 + len);

    // Add "ACK: " prefix
    const char* prefix = "ACK: ";
    for (const char* p = prefix; *p != '\0'; ++p) {
      response.push_back(static_cast<std::uint8_t>(*p));
    }

    // Echo the command
    for (std::size_t i = 0; i < len; ++i) {
      response.push_back(data[i]);
    }

    // Send response
    std::size_t written = 0;
    (void)loopback_.serverWrite({response.data(), response.size()}, written, 100);

    ++commandsProcessed_;
  }

  bt::RfcommLoopback& loopback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::size_t commandsProcessed_{0};
};

/* ----------------------------- Test Fixture ----------------------------- */

/**
 * @class RfcommEmulationTest
 * @brief Test fixture for RFCOMM emulation tests.
 */
class RfcommEmulationTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(loopback_.open(), bt::Status::SUCCESS);

    // Create emulated device (uses server side of loopback)
    emulator_ = std::make_unique<EmulatedBluetoothDevice>(loopback_);

    // Create adapter with injected FD (uses client side of loopback)
    int clientFd = loopback_.releaseClientFd();
    ASSERT_GE(clientFd, 0);
    adapter_ = std::make_unique<bt::RfcommAdapter>(clientFd);
  }

  void TearDown() override {
    if (emulator_) {
      emulator_->stop();
    }
    adapter_.reset();
    emulator_.reset();
    loopback_.close();
  }

  bt::RfcommLoopback loopback_;
  std::unique_ptr<EmulatedBluetoothDevice> emulator_;
  std::unique_ptr<bt::RfcommAdapter> adapter_;
};

/* ----------------------------- Emulation Tests ----------------------------- */

/** @test Verify basic command-response with emulated device. */
TEST_F(RfcommEmulationTest, BasicCommandResponse) {
  // Start emulator
  emulator_->start();

  // Give emulator thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Send a command
  const char* command = "HELLO";
  std::size_t written = 0;
  bt::Status status =
      adapter_->write({reinterpret_cast<const std::uint8_t*>(command), 5}, written, 100);
  ASSERT_EQ(status, bt::Status::SUCCESS);
  EXPECT_EQ(written, 5u);

  // Read response
  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  status = adapter_->read({buffer, sizeof(buffer)}, bytesRead, 500);
  ASSERT_EQ(status, bt::Status::SUCCESS);
  EXPECT_GT(bytesRead, 0u);

  // Verify response format: "ACK: HELLO"
  std::string response(reinterpret_cast<char*>(buffer), bytesRead);
  EXPECT_EQ(response, "ACK: HELLO");

  // Verify command was processed
  emulator_->stop();
  EXPECT_EQ(emulator_->commandsProcessed(), 1u);
}

/** @test Verify multiple command-response cycles. */
TEST_F(RfcommEmulationTest, MultipleCommands) {
  emulator_->start();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  const std::vector<std::string> commands = {"CMD1", "CMD2", "CMD3"};

  for (const auto& cmd : commands) {
    // Send command
    std::size_t written = 0;
    bt::Status status = adapter_->write(
        {reinterpret_cast<const std::uint8_t*>(cmd.data()), cmd.size()}, written, 100);
    ASSERT_EQ(status, bt::Status::SUCCESS);

    // Read response
    std::uint8_t buffer[64];
    std::size_t bytesRead = 0;
    status = adapter_->read({buffer, sizeof(buffer)}, bytesRead, 500);
    ASSERT_EQ(status, bt::Status::SUCCESS);

    // Verify response
    std::string response(reinterpret_cast<char*>(buffer), bytesRead);
    EXPECT_EQ(response, "ACK: " + cmd);
  }

  emulator_->stop();
  EXPECT_EQ(emulator_->commandsProcessed(), commands.size());
}

/** @test Verify adapter statistics are updated during emulation. */
TEST_F(RfcommEmulationTest, StatisticsTracking) {
  emulator_->start();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Initial stats should be zero (or minimal from setup)
  adapter_->resetStats();

  // Send a command
  const char* command = "TEST";
  std::size_t written = 0;
  (void)adapter_->write({reinterpret_cast<const std::uint8_t*>(command), 4}, written, 100);

  // Read response
  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  (void)adapter_->read({buffer, sizeof(buffer)}, bytesRead, 500);

  // Check statistics
  const auto& stats = adapter_->stats();
  EXPECT_EQ(stats.bytesTx, 4u);
  EXPECT_GT(stats.bytesRx, 0u);
  EXPECT_EQ(stats.writesCompleted, 1u);
  EXPECT_EQ(stats.readsCompleted, 1u);

  emulator_->stop();
}

/** @test Verify ByteTrace captures emulation traffic. */
TEST_F(RfcommEmulationTest, ByteTraceCapture) {
  emulator_->start();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Track traced bytes
  std::vector<std::uint8_t> txBytes;
  std::vector<std::uint8_t> rxBytes;

  auto callback = [](TraceDirection dir, const std::uint8_t* data, std::size_t len,
                     void* userData) noexcept {
    auto* vectors =
        static_cast<std::pair<std::vector<std::uint8_t>*, std::vector<std::uint8_t>*>*>(userData);
    if (dir == TraceDirection::TX) {
      vectors->first->insert(vectors->first->end(), data, data + len);
    } else {
      vectors->second->insert(vectors->second->end(), data, data + len);
    }
  };

  auto userData = std::make_pair(&txBytes, &rxBytes);
  adapter_->attachTrace(callback, &userData);
  adapter_->setTraceEnabled(true);

  // Send and receive
  const char* command = "TRACE";
  std::size_t written = 0;
  (void)adapter_->write({reinterpret_cast<const std::uint8_t*>(command), 5}, written, 100);

  std::uint8_t buffer[64];
  std::size_t bytesRead = 0;
  (void)adapter_->read({buffer, sizeof(buffer)}, bytesRead, 500);

  // Verify traces captured
  EXPECT_EQ(txBytes.size(), 5u);
  EXPECT_GT(rxBytes.size(), 0u);

  // Verify TX content
  std::string txStr(txBytes.begin(), txBytes.end());
  EXPECT_EQ(txStr, "TRACE");

  adapter_->detachTrace();
  emulator_->stop();
}

/** @test Verify emulator handles connection without client. */
TEST_F(RfcommEmulationTest, EmulatorStartsWithoutClient) {
  // Start emulator before adapter sends anything
  emulator_->start();
  EXPECT_TRUE(emulator_->isRunning());

  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Emulator should still be running (polling for data)
  EXPECT_TRUE(emulator_->isRunning());
  EXPECT_EQ(emulator_->commandsProcessed(), 0u);

  emulator_->stop();
  EXPECT_FALSE(emulator_->isRunning());
}

/* ----------------------------- Binary Protocol Example ----------------------------- */

/**
 * @class BinaryProtocolEmulator
 * @brief Example emulator for a binary protocol device.
 *
 * Demonstrates how to emulate a device with a simple binary protocol:
 *  - Request: [CMD:1][LEN:1][DATA:N]
 *  - Response: [STATUS:1][LEN:1][DATA:N]
 */
class BinaryProtocolEmulator {
public:
  static constexpr std::uint8_t CMD_READ = 0x01;
  static constexpr std::uint8_t CMD_WRITE = 0x02;
  static constexpr std::uint8_t CMD_STATUS = 0x03;

  static constexpr std::uint8_t STATUS_OK = 0x00;
  static constexpr std::uint8_t STATUS_ERROR = 0xFF;

  explicit BinaryProtocolEmulator(bt::RfcommLoopback& loopback) : loopback_(loopback) {
    // Initialize some registers
    std::memset(registers_, 0, sizeof(registers_));
  }

  void start() {
    running_ = true;
    thread_ = std::thread(&BinaryProtocolEmulator::runLoop, this);
  }

  void stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  void runLoop() {
    std::uint8_t buffer[256];

    while (running_.load(std::memory_order_relaxed)) {
      std::size_t bytesRead = 0;
      bt::Status status = loopback_.serverRead({buffer, sizeof(buffer)}, bytesRead, 100);

      if (status == bt::Status::SUCCESS && bytesRead >= 2) {
        processFrame(buffer, bytesRead);
      }
    }
  }

  void processFrame(const std::uint8_t* data, std::size_t len) {
    std::uint8_t cmd = data[0];
    std::uint8_t dataLen = data[1];

    if (len < static_cast<std::size_t>(2 + dataLen)) {
      sendError();
      return;
    }

    switch (cmd) {
    case CMD_READ:
      handleRead(data + 2, dataLen);
      break;
    case CMD_WRITE:
      handleWrite(data + 2, dataLen);
      break;
    case CMD_STATUS:
      handleStatus();
      break;
    default:
      sendError();
      break;
    }
  }

  void handleRead(const std::uint8_t* data, std::size_t len) {
    if (len < 2) {
      sendError();
      return;
    }
    std::uint8_t addr = data[0];
    std::uint8_t count = data[1];

    if (addr + count > sizeof(registers_)) {
      sendError();
      return;
    }

    std::uint8_t response[258];
    response[0] = STATUS_OK;
    response[1] = count;
    std::memcpy(response + 2, registers_ + addr, count);

    std::size_t written = 0;
    (void)loopback_.serverWrite({response, static_cast<std::size_t>(2 + count)}, written, 100);
  }

  void handleWrite(const std::uint8_t* data, std::size_t len) {
    if (len < 2) {
      sendError();
      return;
    }
    std::uint8_t addr = data[0];
    std::uint8_t count = data[1];

    if (addr + count > sizeof(registers_) || len < static_cast<std::size_t>(2 + count)) {
      sendError();
      return;
    }

    std::memcpy(registers_ + addr, data + 2, count);

    std::uint8_t response[2] = {STATUS_OK, 0};
    std::size_t written = 0;
    (void)loopback_.serverWrite({response, 2}, written, 100);
  }

  void handleStatus() {
    std::uint8_t response[3] = {STATUS_OK, 1, 0x42}; // Device ID
    std::size_t written = 0;
    (void)loopback_.serverWrite({response, 3}, written, 100);
  }

  void sendError() {
    std::uint8_t response[2] = {STATUS_ERROR, 0};
    std::size_t written = 0;
    (void)loopback_.serverWrite({response, 2}, written, 100);
  }

  bt::RfcommLoopback& loopback_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::uint8_t registers_[256];
};

/** @test Verify binary protocol emulation. */
TEST_F(RfcommEmulationTest, BinaryProtocolEmulation) {
  // Create binary protocol emulator (shares loopback with fixture)
  BinaryProtocolEmulator binEmulator(loopback_);
  binEmulator.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Write some data: CMD_WRITE, len=4, addr=0, count=2, data=[0xAA, 0xBB]
  std::uint8_t writeCmd[] = {0x02, 4, 0, 2, 0xAA, 0xBB};
  std::size_t written = 0;
  ASSERT_EQ(adapter_->write({writeCmd, sizeof(writeCmd)}, written, 100), bt::Status::SUCCESS);

  // Read response
  std::uint8_t response[16];
  std::size_t bytesRead = 0;
  ASSERT_EQ(adapter_->read({response, sizeof(response)}, bytesRead, 500), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 2u);
  EXPECT_EQ(response[0], 0x00); // STATUS_OK
  EXPECT_EQ(response[1], 0x00); // No data

  // Read back: CMD_READ, len=2, addr=0, count=2
  std::uint8_t readCmd[] = {0x01, 2, 0, 2};
  ASSERT_EQ(adapter_->write({readCmd, sizeof(readCmd)}, written, 100), bt::Status::SUCCESS);

  ASSERT_EQ(adapter_->read({response, sizeof(response)}, bytesRead, 500), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 4u);
  EXPECT_EQ(response[0], 0x00); // STATUS_OK
  EXPECT_EQ(response[1], 2);    // 2 bytes of data
  EXPECT_EQ(response[2], 0xAA);
  EXPECT_EQ(response[3], 0xBB);

  binEmulator.stop();
}
