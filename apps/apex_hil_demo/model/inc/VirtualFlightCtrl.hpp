#ifndef APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_HPP
#define APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_HPP
/**
 * @file VirtualFlightCtrl.hpp
 * @brief Software emulation of the STM32 flight controller as an HW_MODEL.
 *
 * Wraps the same FlightController algorithm used on the STM32 firmware,
 * but runs on the POSIX host behind a PTY. The framework creates the
 * PTY automatically based on the declared TransportKind::SERIAL_232.
 * A HilDriver instance connects to the slave end of the PTY and
 * communicates identically to how it would with real hardware.
 *
 * Scheduled task:
 *   - controlStep (50 Hz): Read SLIP-framed VehicleState from transport,
 *     run FlightController, send SLIP-framed ControlCmd back.
 *
 * @note NOT RT-safe in doInit(). controlStep() is RT-safe.
 */

#include "apps/apex_hil_demo/common/inc/HilConfig.hpp"
#include "apps/apex_hil_demo/common/inc/HilProtocol.hpp"
#include "apps/apex_hil_demo/common/inc/VehicleState.hpp"
#include "apps/apex_hil_demo/firmware/inc/FlightController.hpp"
#include "apps/apex_hil_demo/model/inc/VirtualFlightCtrlData.hpp"

#include "src/system/core/infrastructure/system_component/apex/inc/ModelData.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/TransportKind.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <fmt/format.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace appsim {
namespace model {

using system_core::data::State;
using system_core::system_component::HwModelBase;
using system_core::system_component::Status;
using system_core::system_component::TransportKind;

/* ----------------------------- VirtualFlightCtrl ----------------------------- */

/**
 * @class VirtualFlightCtrl
 * @brief Emulated flight controller (HW_MODEL, SERIAL_232 transport).
 *
 * componentId = 121 (0x79)
 * fullUid = 0x7900 (single instance)
 *
 * The framework auto-provisions a PTY pair during registration. The
 * model gets the master fd for transportRead()/transportWrite(). A
 * HilDriver opens the slave path as a standard UartAdapter.
 * Communication uses SLIP framing + CRC-16 identical to the real
 * STM32 firmware.
 */
class VirtualFlightCtrl final : public HwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 121;
  static constexpr const char* COMPONENT_NAME = "VirtualFlightCtrl";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    CONTROL_STEP = 1, ///< 50 Hz: process state, compute control, respond.
    TELEMETRY = 2     ///< 1 Hz: status logging.
  };

  /* ----------------------------- Transport ----------------------------- */

  /**
   * @brief Declare SERIAL_232 transport.
   *
   * The framework provisions a PTY pair during registration. The master
   * fd is set as the transport, the slave path goes to the DRIVER.
   */
  [[nodiscard]] TransportKind transportKind() const noexcept override {
    return TransportKind::SERIAL_232;
  }

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    controller_.init(100.0F); // Default target altitude
    controller_.setMode(hil::ControlMode::HOLD_ALT);

    registerTask<VirtualFlightCtrl, &VirtualFlightCtrl::controlStep>(
        static_cast<std::uint8_t>(TaskUid::CONTROL_STEP), this, "controlStep");
    registerTask<VirtualFlightCtrl, &VirtualFlightCtrl::telemetry>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(VfcState));

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("Init: transportFd={}, hasTransport={}", transportFd(),
                                     hasTransport()));
    }

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Process incoming state and send control response (50 Hz).
   *
   * 1. Read bytes from transport (non-blocking)
   * 2. SLIP decode
   * 3. Verify CRC, extract VehicleState
   * 4. Run FlightController::computeControl()
   * 5. SLIP encode ControlCmd + CRC
   * 6. Write to transport
   *
   * @return 0 on success.
   * @note RT-safe: Bounded buffer operations + syscalls.
   */
  std::uint8_t controlStep() noexcept {
    if (!hasTransport()) {
      return 0;
    }

    // Read available bytes from transport
    std::size_t bytesRead = 0;
    transportRead(rxRaw_.data(), rxRaw_.size(), bytesRead);
    ++state_.get().pollCount;
    if (bytesRead == 0) {
      return 0;
    }

    // Feed into SLIP decoder
    auto result = apex::protocols::slip::decodeChunk(
        decodeState_, decodeCfg_, {rxRaw_.data(), bytesRead}, rxDecoded_.data(), rxDecoded_.size());

    if (!result.frameCompleted || result.bytesProduced == 0) {
      return 0;
    }

    // Process the decoded frame
    if (!processIncoming(rxDecoded_.data(), result.bytesProduced)) {
      return 0;
    }

    // Compute control, stamp sequence
    hil::ControlCmd cmd = controller_.computeControl();
    cmd.seqNum = ++txSeqNum_;
    cmd.ackSeq = lastRxSeq_;

    // Send response
    sendControlCmd(cmd);

    return 0;
  }

  /**
   * @brief Log VFC status (1 Hz).
   * @return 0 on success.
   * @note NOT RT-safe: Uses fmt::format for logging.
   */
  std::uint8_t telemetry() noexcept {
    auto* log = componentLog();
    if (log != nullptr) {
      const auto& s = state_.get();
      log->info(label(), fmt::format("rx={} tx={} polls={} crcErr={}", s.rxCount, s.txCount,
                                     s.pollCount, s.crcErrors));
    }
    return 0;
  }

  [[nodiscard]] const char* label() const noexcept override { return "VIRT_FC"; }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] const hil::FlightController& controller() const noexcept { return controller_; }
  [[nodiscard]] std::uint32_t rxCount() const noexcept { return state_.get().rxCount; }
  [[nodiscard]] std::uint32_t txCount() const noexcept { return state_.get().txCount; }
  [[nodiscard]] const VfcState& state() const noexcept { return state_.get(); }

private:
  /* ----------------------------- Frame Processing ----------------------------- */

  /**
   * @brief Process a decoded SLIP frame.
   * @return true if a valid VehicleState was extracted.
   */
  bool processIncoming(const std::uint8_t* data, std::size_t len) noexcept {
    // Minimum: opcode(1) + CRC(2)
    if (len < 3) {
      return false;
    }

    // Verify CRC
    const std::size_t PAYLOAD_LEN = len - 2;
    apex::checksums::crc::Crc16XmodemTable crc;
    const std::uint16_t EXPECTED =
        (static_cast<std::uint16_t>(data[PAYLOAD_LEN]) << 8) | data[PAYLOAD_LEN + 1];
    std::uint16_t computed = 0;
    crc.calculate(data, PAYLOAD_LEN, computed);
    if (EXPECTED != computed) {
      ++state_.get().crcErrors;
      return false;
    }

    // Dispatch
    const auto OPCODE = static_cast<hil::HilOpcode>(data[0]);
    if (OPCODE == hil::HilOpcode::STATE_UPDATE && PAYLOAD_LEN == 1 + sizeof(hil::VehicleState)) {
      hil::VehicleState state{};
      std::memcpy(&state, data + 1, sizeof(hil::VehicleState));
      lastRxSeq_ = state.seqNum;
      controller_.updateState(state);
      ++state_.get().rxCount;
      return true;
    }

    if (OPCODE == hil::HilOpcode::CMD_START) {
      controller_.setMode(hil::ControlMode::HOLD_ALT);
    } else if (OPCODE == hil::HilOpcode::CMD_STOP) {
      controller_.setMode(hil::ControlMode::IDLE);
    }

    return false;
  }

  /**
   * @brief Encode and send ControlCmd over transport.
   */
  void sendControlCmd(const hil::ControlCmd& cmd) noexcept {
    // Build: [opcode:1][ControlCmd][CRC-16:2]
    constexpr std::size_t PAYLOAD_SIZE = 1 + sizeof(hil::ControlCmd);
    std::array<std::uint8_t, PAYLOAD_SIZE + 2> msgBuf{};

    msgBuf[0] = static_cast<std::uint8_t>(hil::HilOpcode::CONTROL_CMD);
    std::memcpy(msgBuf.data() + 1, &cmd, sizeof(hil::ControlCmd));

    apex::checksums::crc::Crc16XmodemTable crc;
    std::uint16_t crcVal = 0;
    crc.calculate(msgBuf.data(), PAYLOAD_SIZE, crcVal);
    msgBuf[PAYLOAD_SIZE] = static_cast<std::uint8_t>(crcVal >> 8);
    msgBuf[PAYLOAD_SIZE + 1] = static_cast<std::uint8_t>(crcVal & 0xFF);

    // SLIP encode
    constexpr std::size_t SLIP_BUF_SIZE = (PAYLOAD_SIZE + 2) * 2 + 2;
    std::array<std::uint8_t, SLIP_BUF_SIZE> slipBuf{};
    auto result = apex::protocols::slip::encode({msgBuf.data(), PAYLOAD_SIZE + 2}, slipBuf.data(),
                                                slipBuf.size());

    if (result.status == apex::protocols::slip::Status::OK) {
      std::size_t written = 0;
      transportWrite(slipBuf.data(), result.bytesProduced, written);
      ++state_.get().txCount;
    }
  }

  /* ----------------------------- State ----------------------------- */

  hil::FlightController controller_;
  State<VfcState> state_{};
  std::uint16_t txSeqNum_{0};
  std::uint16_t lastRxSeq_{0};

  // SLIP decoder state
  apex::protocols::slip::DecodeState decodeState_{};
  apex::protocols::slip::DecodeConfig decodeCfg_{};

  // Buffers
  std::array<std::uint8_t, hil::MAX_SLIP_ENCODED> rxRaw_{};
  std::array<std::uint8_t, hil::MAX_FRAME_PAYLOAD> rxDecoded_{};
};

} // namespace model
} // namespace appsim

#endif // APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_HPP
