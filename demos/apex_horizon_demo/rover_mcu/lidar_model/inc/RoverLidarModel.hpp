#ifndef APEX_HORIZON_DEMO_ROVER_LIDAR_MODEL_HPP
#define APEX_HORIZON_DEMO_ROVER_LIDAR_MODEL_HPP
/**
 * @file RoverLidarModel.hpp
 * @brief The rover's lidar as a hardware model on its own serial wire.
 *
 * The plant sweeps its lidar fan over the terrain; this model turns each
 * sweep into what a small scanning rangefinder would put on a UART: one
 * LIDAR_SCAN frame (scan number, a hit bit and a range per ray) in the
 * board protocol's SLIP + CRC-16 framing. The wire is a real one: the
 * device path names a USB-serial adapter whose TX and RX run to the
 * board's second UART, so the board receives the scan exactly as it
 * would from the sensor. A ray counts as a return only inside the
 * sensor's max_range_m. The port is reopened once a second, so an
 * unplugged adapter recovers without a restart; with enabled = 0 the
 * port is never opened.
 *
 * @note NOT RT-safe in the 1 Hz telemetry task (port open, logging).
 * scanStep() is RT-safe: bounded buffers and non-blocking I/O.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleData.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace appsim {
namespace rover_board {

/* ----------------------------- RoverLidarModelTunables ----------------------------- */

struct RoverLidarModelTunables {
  char device_path[64]{'/', 'd', 'e', 'v', '/', 't', 't', 'y', 'U', 'S', 'B', '0'};
  std::uint32_t baud_rate{BAUD_RATE};
  float max_range_m{50.0F}; ///< Returns beyond this read as no return.
  std::uint8_t enabled{0};  ///< 1 = scans go out on the wire.
  std::uint8_t reserved[3]{};
};
static_assert(sizeof(RoverLidarModelTunables) == 76, "RoverLidarModelTunables is 76 bytes");

/* ----------------------------- RoverLidarModelState ----------------------------- */

struct RoverLidarModelState {
  std::uint64_t scans_sent{0};  ///< LIDAR_SCAN frames written whole.
  std::uint64_t write_short{0}; ///< Scans the port would not take whole.
  std::uint32_t opens{0};       ///< Successful port opens (a replug adds one).
  std::uint16_t scan_seq{0};    ///< The last scan number sent.
  std::uint8_t uart_open{0};
  std::uint8_t last_hit_bits{0}; ///< The last scan's hit bits.
  float last_nearest_m{0.0F};    ///< The last scan's closest return (0 = none).
  std::uint8_t reserved[4]{};
};
static_assert(sizeof(RoverLidarModelState) == 32, "RoverLidarModelState is 32 bytes");

/* ----------------------------- RoverLidarModel ----------------------------- */

class RoverLidarModel final : public system_core::system_component::HwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Sequential after RoverBoardLink (227).
  static constexpr std::uint16_t COMPONENT_ID = 228;
  static constexpr const char* COMPONENT_NAME = "RoverLidarModel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "ROVER_LIDAR"; }

  enum class TaskUid : std::uint8_t {
    SCAN_STEP = 1, ///< Sweep rate: one LIDAR_SCAN from the plant's latest sweep.
    TELEMETRY = 2, ///< 1 Hz: reopen a lost port, log counters.
  };

  /* ----------------------------- Wiring ----------------------------- */

  /// The plant telemetry whose lidar sweep is sent.
  void setSource(const ground_vehicle::GroundVehicleTelemetry* tlm) noexcept { tlm_ = tlm; }

  [[nodiscard]] const RoverLidarModelState& modelState() const noexcept { return state_.get(); }
  [[nodiscard]] system_core::data::TunableParam<RoverLidarModelTunables>& tunables() noexcept {
    return tunables_;
  }

  /**
   * @brief The scan the sensor would report for a sweep.
   * @note RT-safe: O(rays).
   */
  [[nodiscard]] static LidarScan scanFrom(const ground_vehicle::GroundVehicleTelemetry& tlm,
                                          float max_range_m, std::uint16_t seq) noexcept {
    LidarScan scan{};
    scan.scan_seq = seq;
    const std::uint32_t N = std::min<std::uint32_t>(tlm.lidar_n_rays, LIDAR_WIRE_RAYS);
    scan.n_rays = static_cast<std::uint8_t>(N);
    for (std::size_t i = 0; i < LIDAR_WIRE_RAYS; ++i) {
      scan.range_cm[i] = LIDAR_NO_RETURN_CM;
    }
    for (std::uint32_t i = 0; i < N; ++i) {
      const double R = tlm.lidar_range_m[i];
      if (tlm.lidar_hit[i] != 0u && R <= static_cast<double>(max_range_m) && R >= 0.0) {
        scan.hit_bits = static_cast<std::uint8_t>(scan.hit_bits | (1u << i));
        scan.range_cm[i] = static_cast<std::uint16_t>(std::min(std::lround(R * 100.0), 65534L));
      }
    }
    return scan;
  }

  /* ----------------------------- Tasks ----------------------------- */

  std::uint8_t scanStep() noexcept {
    const auto& p = tunables_.get();
    if (p.enabled == 0u || tlm_ == nullptr || !uart_.isOpen()) {
      return 0u;
    }
    auto& s = state_.get();
    const LidarScan SCAN =
        scanFrom(*tlm_, p.max_range_m, static_cast<std::uint16_t>(s.scan_seq + 1u));
    const std::size_t N =
        buildFrame(Opcode::LIDAR_SCAN, &SCAN, sizeof(SCAN), txFrame_.data(), txFrame_.size());
    auto enc = apex::protocols::slip::encode({txFrame_.data(), N}, txSlip_.data(), txSlip_.size());
    if (N == 0u || enc.status != apex::protocols::slip::Status::OK) {
      return 0u;
    }
    std::size_t written = 0;
    const auto ST = uart_.write(txSlip_.data(), enc.bytesProduced, written, 0);
    s.scan_seq = SCAN.scan_seq;
    s.last_hit_bits = SCAN.hit_bits;
    std::uint16_t nearest = LIDAR_NO_RETURN_CM;
    for (std::uint16_t r : SCAN.range_cm) {
      nearest = std::min(nearest, r);
    }
    s.last_nearest_m =
        (nearest == LIDAR_NO_RETURN_CM) ? 0.0F : static_cast<float>(nearest) / 100.0F;
    if (ST == UartStatus::SUCCESS && written == enc.bytesProduced) {
      ++s.scans_sent;
    } else if (ST == UartStatus::SUCCESS || ST == UartStatus::WOULD_BLOCK) {
      ++s.write_short;
    } else {
      closePort("write failed");
    }
    return 0u;
  }

  std::uint8_t telemetryTick() noexcept {
    const auto& p = tunables_.get();
    const auto& s = state_.get();
    if (p.enabled == 0u) {
      return 0u;
    }
    if (!uart_.isOpen()) {
      (void)openPort();
    }
    auto* log = componentLog();
    if (log == nullptr) {
      return 0u;
    }
    if (close_reason_ != nullptr) {
      log->warning(label(), 1u,
                   fmt::format("port closed ({}); reopening once a second", close_reason_));
      close_reason_ = nullptr;
    }
    log->info(label(), fmt::format("open={} sent={} short={} opens={} seq={} hits={:#04x} "
                                   "nearest={:.1f}m",
                                   s.uart_open, s.scans_sent, s.write_short, s.opens, s.scan_seq,
                                   s.last_hit_bits, static_cast<double>(s.last_nearest_m)));
    return 0u;
  }

protected:
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return TprmIngest::DEFAULTS;
    }
    const auto CHECK =
        system_core::system_component::readTprmPayload(PATH, fullUid(), tunables_.get());
    if (CHECK != system_core::system_component::TprmPayloadCheck::OK) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->error(label(), system_core::system_component::toFaultCode(CHECK),
                   fmt::format("TPRM rejected ({}): {}",
                               system_core::system_component::toString(CHECK), PATH.string()));
      }
      return TprmIngest::REJECTED;
    }
    tunables_.get().device_path[sizeof(RoverLidarModelTunables::device_path) - 1u] = '\0';
    return TprmIngest::LOADED;
  }

  [[nodiscard]] bool paramsOptional() const noexcept override { return true; }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;
    registerTask<RoverLidarModel, &RoverLidarModel::scanStep>(
        static_cast<std::uint8_t>(TaskUid::SCAN_STEP), this, "scanStep");
    registerTask<RoverLidarModel, &RoverLidarModel::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");
    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(RoverLidarModelTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(RoverLidarModelState));

    const auto& p = tunables_.get();
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("init: enabled={} device={} baud={} max_range={:.1f}m "
                                     "source={}",
                                     p.enabled, p.device_path, p.baud_rate,
                                     static_cast<double>(p.max_range_m), tlm_ != nullptr));
    }
    if (p.enabled != 0u) {
      (void)openPort();
    }
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  using UartStatus = apex::protocols::serial::uart::Status;

  /// Open and configure the port (NOT RT-safe). Logs the first failure and every success.
  bool openPort() noexcept {
    namespace uart = apex::protocols::serial::uart;
    const auto& p = tunables_.get();
    auto& s = state_.get();
    uart_ = uart::UartAdapter(std::string(p.device_path));
    uart::UartConfig cfg;
    cfg.baudRate = static_cast<uart::BaudRate>(p.baud_rate);
    cfg.dataBits = uart::DataBits::EIGHT;
    cfg.parity = uart::Parity::NONE;
    cfg.stopBits = uart::StopBits::ONE;
    cfg.flowControl = uart::FlowControl::NONE;
    cfg.exclusiveAccess = false;
    auto* log = componentLog();
    const auto ST = uart_.configure(cfg);
    if (ST != UartStatus::SUCCESS) {
      s.uart_open = 0u;
      if (!open_fail_logged_ && log != nullptr) {
        log->warning(label(), 2u,
                     fmt::format("cannot open {} ({}); the board sees no lidar until it opens",
                                 p.device_path, uart::toString(ST)));
      }
      open_fail_logged_ = true;
      return false;
    }
    s.uart_open = 1u;
    ++s.opens;
    open_fail_logged_ = false;
    if (log != nullptr) {
      log->info(label(), fmt::format("port open: {} (open #{})", p.device_path, s.opens));
    }
    return true;
  }

  void closePort(const char* reason) noexcept {
    (void)uart_.close();
    state_.get().uart_open = 0u;
    close_reason_ = reason;
  }

  const ground_vehicle::GroundVehicleTelemetry* tlm_{nullptr};
  apex::protocols::serial::uart::UartAdapter uart_{std::string{}};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> txFrame_{};
  std::array<std::uint8_t, MAX_SLIP_ENCODED> txSlip_{};
  system_core::data::TunableParam<RoverLidarModelTunables> tunables_{};
  system_core::data::State<RoverLidarModelState> state_{};
  bool open_fail_logged_{false};
  const char* close_reason_{nullptr};
};

} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_LIDAR_MODEL_HPP
