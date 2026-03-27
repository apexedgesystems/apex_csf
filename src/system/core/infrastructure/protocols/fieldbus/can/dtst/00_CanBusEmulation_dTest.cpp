/**
 * @file 00_CanBusEmulation_dTest.cpp
 * @brief dTest: Emulated device ⇄ controller over Linux vcan using SocketCAN.
 *
 * Goals:
 *  - Show end-to-end send/recv via CANBusAdapter on a vcan interface.
 *  - Demonstrate a tiny "device" (raw PF_CAN/SOCK_RAW) replying to a command.
 *  - Keep it deterministic: bounded waits, best-effort setup/teardown, no HW.
 *
 * Prereqs:
 *  - Linux with vcan support, NET_ADMIN privileges (or sudo enabled).
 *
 * Notes:
 *  - Classic CAN only (DLC ≤ 8).
 *  - This is a development test (tutorial-style), excluded from coverage.
 */

#include <gtest/gtest.h>

// Project headers (adapter + vcan helper)
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

// Shared helpers from uTests (createTestCANSocket, testIfName, etc.)
#include "src/system/core/infrastructure/protocols/fieldbus/can/utst/CanAdapter_TestSupport_uTest.hpp"

// C++ STD
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

// Linux / SocketCAN
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>

using apex::protocols::fieldbus::can::CANBusAdapter;
using apex::protocols::fieldbus::can::CanConfig;
using apex::protocols::fieldbus::can::CanFrame;
using apex::protocols::fieldbus::can::CanId;
using apex::protocols::fieldbus::can::Status;
using apex::protocols::fieldbus::can::util::VCanInterface;
using test_support::createTestCANSocket;
using test_support::testIfName;

// -----------------------------------------------------------------------------
// Emulated CAN Device and Controller Simulation
// -----------------------------------------------------------------------------

/**
 * @class EmulatedCANDevice
 * @brief Minimal "device" listening on vcan and replying to "CMD_TEST".
 *
 * Behavior:
 *  - Blocks for one incoming classic CAN frame.
 *  - If payload == "CMD_TEST", replies with "CAN_ACK: Device Ready"
 *    (truncated to 8B as per classic CAN).
 */
class EmulatedCANDevice {
public:
  explicit EmulatedCANDevice(const std::string& interface)
      : interface_(interface), canSockFD_(createTestCANSocket(interface_)) {
    if (canSockFD_ < 0) {
      throw std::runtime_error("EmulatedCANDevice: external CAN socket create failed");
    }
  }

  ~EmulatedCANDevice() {
    if (canSockFD_ >= 0) {
      ::close(canSockFD_);
    }
  }

  void run() {
    // Wait for a single command frame (bounded by the writer’s behavior).
    struct can_frame rxFrame{};
    const ssize_t N = ::read(canSockFD_, &rxFrame, sizeof(rxFrame));
    if (N <= 0) {
      std::cerr << "[Device] No CAN frame or read error: " << std::strerror(errno) << "\n";
      return;
    }

    const std::string CMD(reinterpret_cast<char*>(rxFrame.data), rxFrame.can_dlc);
    std::cout << "[Device] Received: " << CMD << "\n";

    if (CMD == "CMD_TEST") {
      struct can_frame txFrame{};
      txFrame.can_id = 0x000;
      const std::string RESPONSE = "CAN_ACK: Device Ready";
      const size_t RESP_LEN = std::min<size_t>(8, RESPONSE.size());
      txFrame.can_dlc = static_cast<__u8>(RESP_LEN);
      std::memcpy(txFrame.data, RESPONSE.data(), RESP_LEN);

      const ssize_t WN = ::write(canSockFD_, &txFrame, sizeof(txFrame));
      if (WN != static_cast<ssize_t>(sizeof(txFrame))) {
        std::cerr << "[Device] Failed to send response: " << std::strerror(errno) << "\n";
      }
    } else {
      std::cerr << "[Device] Unknown command: " << CMD << "\n";
    }
  }

private:
  std::string interface_;
  int canSockFD_{-1};
};

/**
 * @class CANController
 * @brief Tiny controller layered on CANBusAdapter for request/response.
 */
class CANController {
public:
  explicit CANController(const std::string& interface) : adapter_("CAN Controller", interface) {
    CanConfig cfg{};
    cfg.loopback = true; // ensure local sockets see our TX
    EXPECT_EQ(adapter_.configure(cfg), Status::SUCCESS);
  }

  /**
   * @brief Send an <=8B command and await a single-frame response.
   * @return Response payload (0..8B). Empty string on error.
   */
  std::string communicate(const std::string& cmd) {
    // Build command (<= 8 bytes).
    CanFrame tx{};
    tx.canId = CanId{.id = 0x000, .extended = false, .remote = false, .error = false};
    tx.dlc = static_cast<uint8_t>(std::min<std::size_t>(8, cmd.size()));
    std::memcpy(tx.data.data(), cmd.data(), tx.dlc);

    // Send (bounded wait).
    Status st = adapter_.send(tx, /*timeoutMs=*/500);
    if (st != Status::SUCCESS) {
      std::cerr << "[Controller] send() failed: " << static_cast<int>(st) << "\n";
      return {};
    }

    // Receive (bounded wait).
    CanFrame rx{};
    st = adapter_.recv(rx, /*timeoutMs=*/1000);
    if (st != Status::SUCCESS) {
      std::cerr << "[Controller] recv() failed: " << static_cast<int>(st) << "\n";
      return {};
    }
    return std::string(reinterpret_cast<const char*>(rx.data.data()), rx.dlc);
  }

private:
  CANBusAdapter adapter_;
};

// -----------------------------------------------------------------------------
// dTest: Controller ↔ Emulated Device over vcan
// -----------------------------------------------------------------------------

/**
 * @test ControllerCommunication
 * @brief End-to-end exchange over vcan using SocketCAN.
 *
 * Arrange:
 *   - Ensure vcan exists; start EmulatedCANDevice on that iface.
 * Act:
 *   - Controller sends "CMD_TEST"; device replies with first 8B of "CAN_ACK: Device Ready".
 * Assert:
 *   - Response equals "CAN_ACK:" (truncation demonstrated).
 */
TEST(CanBusEmulation, ControllerCommunication) {
  // Arrange — vcan setup (skip if unavailable)
  VCanInterface vcan(testIfName(), /*autoTeardown=*/false, /*useSudo=*/true);
  if (!vcan.setup()) {
    GTEST_SKIP() << "Requires NET_ADMIN and ip(8); try sudo or set useSudo=true.";
  }
  const std::string IFACE = vcan.interfaceName();

  // Arrange — start device thread
  EmulatedCANDevice device(IFACE);
  std::thread deviceThread([&device]() { device.run(); });

  // Small settle; not correctness-critical
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Act — send command and receive reply
  CANController ctrl(IFACE);
  const std::string RESPONSE = ctrl.communicate("CMD_TEST");

  // Assert — expected truncation to 8 bytes
  EXPECT_EQ(RESPONSE, "CAN_ACK:");

  deviceThread.join();
}
