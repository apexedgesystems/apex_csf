#ifndef APEX_HIL_DEMO_DRIVER_HPP
#define APEX_HIL_DEMO_DRIVER_HPP
/**
 * @file HilDriver.hpp
 * @brief UART/SLIP driver for HIL communication with a flight controller.
 *
 * Sends VehicleState to and receives ControlCmd from a serial device.
 * The device may be real hardware (/dev/ttyACM0) or an emulated HW_MODEL
 * behind a PTY (/dev/pts/X). The driver does not know or care which.
 *
 * Two scheduled tasks:
 *   - sendState (50 Hz): SLIP-encode VehicleState + CRC, write to UART
 *   - recvCommand (50 Hz): Read UART, SLIP-decode, validate CRC, extract ControlCmd
 *
 * Wire format: [SLIP_END][opcode:1][payload:N][CRC-16:2][SLIP_END]
 *
 * @note NOT RT-safe in doInit() (opens serial device). Tasks are RT-safe.
 */

#include "apps/apex_hil_demo/common/inc/HilConfig.hpp"
#include "apps/apex_hil_demo/common/inc/HilProtocol.hpp"
#include "apps/apex_hil_demo/common/inc/VehicleState.hpp"
#include "apps/apex_hil_demo/driver/inc/HilDriverData.hpp"

#include "src/system/core/infrastructure/system_component/apex/inc/ModelData.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/DriverBase.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <fmt/format.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace appsim {
namespace driver {

using system_core::data::Output;
using system_core::data::State;
using system_core::system_component::DriverBase;

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Status codes for HilDriver.
 *
 * Extends system_component::Status::EOE_SYSTEM_COMPONENT.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0,

  // Errors ------------------------------------------------------------------
  ERROR_UART_OPEN =
      static_cast<std::uint8_t>(system_core::system_component::Status::EOE_SYSTEM_COMPONENT),
  ERROR_COMM_LOST,

  // Warnings ----------------------------------------------------------------
  WARN_COMM_RESTORED,

  // Marker ------------------------------------------------------------------
  EOE_HIL_DRIVER
};

/* ----------------------------- HilDriver ----------------------------- */

/**
 * @class HilDriver
 * @brief Serial driver for HIL flight controller communication.
 *
 * componentId = 122 (0x7A)
 * Multi-instance: instance 0 = real STM32, instance 1 = emulated
 *
 * The driver receives a pointer to the latest VehicleState from the plant
 * model (set via setStateSource). It encodes and transmits this state,
 * then reads back ControlCmd responses.
 *
 * @note NOT thread-safe. Tasks run on the same executor thread.
 */
class HilDriver final : public DriverBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 122;
  static constexpr const char* COMPONENT_NAME = "HilDriver";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    SEND_STATE = 1,   ///< 50 Hz: send VehicleState to device.
    RECV_COMMAND = 2, ///< 50 Hz: read ControlCmd from device.
    TELEMETRY = 3     ///< 1 Hz: status logging.
  };

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct driver with device path.
   * @param devicePath Path to serial device (e.g., "/dev/ttyACM0").
   */
  explicit HilDriver(std::string devicePath) noexcept
      : devicePath_{std::move(devicePath)}, uart_{devicePath_} {}

  /**
   * @brief Construct driver with default device path (set via TPRM).
   */
  HilDriver() noexcept : uart_{devicePath_} {}

  ~HilDriver() override = default;

  /* ----------------------------- Lifecycle ----------------------------- */

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Configure UART
    apex::protocols::serial::uart::UartConfig cfg;
    cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_115200;
    cfg.dataBits = apex::protocols::serial::uart::DataBits::EIGHT;
    cfg.parity = apex::protocols::serial::uart::Parity::NONE;
    cfg.stopBits = apex::protocols::serial::uart::StopBits::ONE;
    cfg.flowControl = apex::protocols::serial::uart::FlowControl::NONE;
    cfg.exclusiveAccess = false; // PTYs do not support flock

    if (!devicePath_.empty()) {
      auto uartStatus = uart_.configure(cfg);
      auto* log = componentLog();
      if (uartStatus != apex::protocols::serial::uart::Status::SUCCESS) {
        setStatus(static_cast<std::uint8_t>(Status::ERROR_UART_OPEN));
        setLastError("UART configure failed");
        if (log != nullptr) {
          log->warning(label(), static_cast<std::uint8_t>(Status::ERROR_UART_OPEN),
                       fmt::format("UART configure failed for {}", devicePath_));
        }
        // Not fatal -- device may not exist yet. Track misses until available.
      } else {
        // Flush RX buffer to discard stale bytes from prior session (e.g., after execve).
        (void)uart_.flush(true, false);
        state_.get().uartOpen = 1;
        if (log != nullptr) {
          log->info(label(), fmt::format("UART open: {} (fd={})", devicePath_, uart_.isOpen()));
        }
      }
    }

    // Register tasks
    registerTask<HilDriver, &HilDriver::sendState>(static_cast<std::uint8_t>(TaskUid::SEND_STATE),
                                                   this, "sendState");
    registerTask<HilDriver, &HilDriver::recvCommand>(
        static_cast<std::uint8_t>(TaskUid::RECV_COMMAND), this, "recvCommand");
    registerTask<HilDriver, &HilDriver::telemetry>(static_cast<std::uint8_t>(TaskUid::TELEMETRY),
                                                   this, "telemetry");

    // Register data for registry integration
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(DriverState));
    registerData(DataCategory::OUTPUT, "controlCmd", &controlOutput_.get(),
                 sizeof(hil::ControlCmd));

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Encode and send VehicleState to device (50 Hz).
   * @return 0 on success.
   * @note RT-safe: Bounded buffer operations + syscall.
   */
  std::uint8_t sendState() noexcept {
    if (!uart_.isOpen() || stateSource_ == nullptr) {
      ++state_.get().txMisses;
      return 0;
    }

    // Build message: [opcode:1][VehicleState][CRC-16:2]
    hil::VehicleState stateMsg = *stateSource_;
    stateMsg.seqNum = ++state_.get().txSeq;
    stateMsg.ackSeq = state_.get().rxSeq;

    constexpr std::size_t PAYLOAD_SIZE = 1 + sizeof(hil::VehicleState);
    std::array<std::uint8_t, PAYLOAD_SIZE + 2> msgBuf{};

    msgBuf[0] = static_cast<std::uint8_t>(hil::HilOpcode::STATE_UPDATE);
    std::memcpy(msgBuf.data() + 1, &stateMsg, sizeof(hil::VehicleState));

    // CRC-16/XMODEM over opcode + payload
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
      auto writeStatus = uart_.write(slipBuf.data(), result.bytesProduced, written, 0);
      if (writeStatus == apex::protocols::serial::uart::Status::SUCCESS) {
        if (written < result.bytesProduced) {
          ++state_.get().txMisses; // Partial write counts as miss.
        }
        ++state_.get().txCount;
      } else {
        ++state_.get().txMisses;
      }
    }

    return 0;
  }

  /**
   * @brief Read and decode ControlCmd from device (50 Hz).
   * @return 0 on success.
   * @note RT-safe: Bounded buffer operations + syscall.
   */
  std::uint8_t recvCommand() noexcept {
    if (!uart_.isOpen()) {
      ++state_.get().rxMisses;
      return 0;
    }

    // Read available bytes
    std::size_t bytesRead = 0;
    auto status = uart_.read(rxRaw_.data(), rxRaw_.size(), bytesRead, 0);
    if (status != apex::protocols::serial::uart::Status::SUCCESS || bytesRead == 0) {
      ++state_.get().rxMisses;
      return 0;
    }

    // Feed into streaming SLIP decoder (may contain multiple frames per read)
    std::size_t pos = 0;
    while (pos < bytesRead) {
      const std::size_t PREV_LEN = decodeState_.frameLen;
      auto result = apex::protocols::slip::decodeChunk(
          decodeState_, decodeCfg_, {rxRaw_.data() + pos, bytesRead - pos},
          rxDecoded_.data() + PREV_LEN, rxDecoded_.size() - PREV_LEN);

      pos += result.bytesConsumed;

      if (result.frameCompleted) {
        const std::size_t FRAME_LEN = PREV_LEN + result.bytesProduced;
        processFrame(rxDecoded_.data(), FRAME_LEN);
      }

      if (result.bytesConsumed == 0) {
        break;
      }
    }

    return 0;
  }

  /**
   * @brief Log driver status and check comm health (1 Hz).
   * @return 0 on success.
   * @note NOT RT-safe: Uses fmt::format for logging.
   */
  std::uint8_t telemetry() noexcept {
    auto* log = componentLog();
    auto& s = state_.get();

    // Comm watchdog: detect stale RX. If rxCount hasn't changed in
    // commWatchdogSec_ consecutive 1 Hz checks, the link is dead.
    if (s.uartOpen != 0 && s.hasCmd != 0) {
      if (s.rxCount == lastWatchdogRx_) {
        ++commSilentSec_;
        if (commSilentSec_ >= commWatchdogSec_ && s.commLost == 0) {
          s.commLost = 1;
          ++s.commLostCount;
          setStatus(static_cast<std::uint8_t>(Status::ERROR_COMM_LOST));
          setLastError("COMM LOST");
          if (log != nullptr) {
            log->error(label(), static_cast<std::uint8_t>(Status::ERROR_COMM_LOST),
                       fmt::format("COMM LOST: no RX for {}s (last rx={}, crcErr={})",
                                   commSilentSec_, s.rxCount, s.crcErrors));
          }
        }
      } else {
        // Link is alive. Clear fault if it was set (recovery after reset).
        if (s.commLost != 0) {
          s.commLost = 0;
          setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
          setLastError(nullptr);
          if (log != nullptr) {
            log->info(label(), "COMM RESTORED");
          }
        }
        commSilentSec_ = 0;
      }
      lastWatchdogRx_ = s.rxCount;
    }

    if (log != nullptr) {
      if (lastHeartbeat_.cycleCount > 0) {
        log->info(label(), fmt::format("tx={} rx={} txMiss={} rxMiss={} crcErr={} "
                                       "seq={}/{} gaps={} "
                                       "stm32Cyc={} stm32Steps={} stm32Us={}{}",
                                       s.txCount, s.rxCount, s.txMisses, s.rxMisses, s.crcErrors,
                                       s.txSeq, s.rxSeq, s.seqGaps, lastHeartbeat_.cycleCount,
                                       lastHeartbeat_.stepCount, lastHeartbeat_.overheadUs,
                                       s.commLost != 0 ? " COMM_LOST" : ""));
      } else {
        log->info(label(),
                  fmt::format("tx={} rx={} txMiss={} rxMiss={} crcErr={} "
                              "seq={}/{} gaps={} uartOpen={}{}",
                              s.txCount, s.rxCount, s.txMisses, s.rxMisses, s.crcErrors, s.txSeq,
                              s.rxSeq, s.seqGaps, s.uartOpen, s.commLost != 0 ? " COMM_LOST" : ""));
      }
    }
    return 0;
  }

  [[nodiscard]] const char* label() const noexcept override { return "HIL_DRIVER"; }

  /* ----------------------------- TPRM ----------------------------- */

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
    std::filesystem::path tprmPath = tprmDir / filename;

    if (!std::filesystem::exists(tprmPath)) {
      return false;
    }

    // TPRM for driver contains device path as first 128 bytes (null-terminated)
    struct DriverTprm {
      char devicePath[128];
    };

    std::string error;
    DriverTprm loaded{};
    if (apex::helpers::files::hex2cpp(tprmPath.string(), loaded, error)) {
      // Only override devicePath if TPRM provides a non-empty path.
      // Empty path means the executive wires the device dynamically (PTY).
      if (loaded.devicePath[0] != '\0') {
        devicePath_ = loaded.devicePath;
        uart_ = apex::protocols::serial::uart::UartAdapter(devicePath_);
      }
      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(),
                  fmt::format("TPRM devicePath: {} (active: {})", loaded.devicePath, devicePath_));
      }
      return true;
    }
    return false;
  }

  /* ----------------------------- Data Interface ----------------------------- */

  /**
   * @brief Set pointer to latest VehicleState from plant model.
   * @param state Pointer to VehicleState (must outlive this driver).
   */
  void setStateSource(const hil::VehicleState* state) noexcept { stateSource_ = state; }

  /**
   * @brief Get the last received ControlCmd.
   * @return Most recent control command.
   */
  [[nodiscard]] const hil::ControlCmd& lastCommand() const noexcept { return controlOutput_.get(); }

  /**
   * @brief Check if a valid command has been received.
   * @return true if at least one valid ControlCmd was decoded.
   */
  [[nodiscard]] bool hasCommand() const noexcept { return state_.get().hasCmd != 0; }

  /**
   * @brief Get transmit count.
   */
  [[nodiscard]] std::uint32_t txCount() const noexcept { return state_.get().txCount; }

  /**
   * @brief Get receive count.
   */
  [[nodiscard]] std::uint32_t rxCount() const noexcept { return state_.get().rxCount; }

  /**
   * @brief Get driver state for external inspection.
   */
  [[nodiscard]] const DriverState& driverState() const noexcept { return state_.get(); }

  /**
   * @brief Set device path (used by executive for transport wiring).
   * @param path Device path string.
   */
  void setDevicePath(const std::string& path) noexcept {
    devicePath_ = path;
    uart_ = apex::protocols::serial::uart::UartAdapter(devicePath_);
  }

private:
  /* ----------------------------- Frame Processing ----------------------------- */

  void processFrame(const std::uint8_t* data, std::size_t len) noexcept {
    // Minimum: opcode(1) + CRC(2)
    if (len < 3) {
      return;
    }

    // Verify CRC-16/XMODEM
    const std::size_t PAYLOAD_LEN = len - 2;
    apex::checksums::crc::Crc16XmodemTable crc;
    const std::uint16_t EXPECTED =
        (static_cast<std::uint16_t>(data[PAYLOAD_LEN]) << 8) | data[PAYLOAD_LEN + 1];
    std::uint16_t computed = 0;
    crc.calculate(data, PAYLOAD_LEN, computed);

    if (EXPECTED != computed) {
      ++state_.get().crcErrors;
      return;
    }

    // Dispatch by opcode
    const auto OPCODE = static_cast<hil::HilOpcode>(data[0]);
    switch (OPCODE) {
    case hil::HilOpcode::CONTROL_CMD:
      if (PAYLOAD_LEN == 1 + sizeof(hil::ControlCmd)) {
        std::memcpy(&controlOutput_.get(), data + 1, sizeof(hil::ControlCmd));
        const std::uint16_t SEQ = controlOutput_.get().seqNum;
        const std::uint16_t PREV = state_.get().rxSeq;
        if (state_.get().hasCmd && SEQ != static_cast<std::uint16_t>(PREV + 1)) {
          state_.get().seqGaps += static_cast<std::uint16_t>(SEQ - PREV - 1);
        }
        state_.get().rxSeq = SEQ;
        state_.get().hasCmd = 1;
        ++state_.get().rxCount;
      }
      break;
    case hil::HilOpcode::HEARTBEAT:
      if (PAYLOAD_LEN == 1 + sizeof(hil::HeartbeatData)) {
        std::memcpy(&lastHeartbeat_, data + 1, sizeof(hil::HeartbeatData));
      }
      break;
    default:
      break;
    }
  }

  /* ----------------------------- State ----------------------------- */

  std::string devicePath_;
  apex::protocols::serial::uart::UartAdapter uart_;

  const hil::VehicleState* stateSource_ = nullptr;
  hil::HeartbeatData lastHeartbeat_{};

  State<DriverState> state_{};
  Output<hil::ControlCmd> controlOutput_{};

  // Comm watchdog: detect link loss when rxCount stops advancing.
  std::uint32_t commWatchdogSec_ = 5; ///< Seconds of silence before COMM_LOST (TPRM-configurable).
  std::uint32_t lastWatchdogRx_ = 0;
  std::uint32_t commSilentSec_ = 0;

  // SLIP decoder state
  apex::protocols::slip::DecodeState decodeState_{};
  apex::protocols::slip::DecodeConfig decodeCfg_{};

  // Buffers
  std::array<std::uint8_t, hil::MAX_SLIP_ENCODED> rxRaw_{};
  std::array<std::uint8_t, hil::MAX_FRAME_PAYLOAD> rxDecoded_{};
};

} // namespace driver
} // namespace appsim

#endif // APEX_HIL_DEMO_DRIVER_HPP
