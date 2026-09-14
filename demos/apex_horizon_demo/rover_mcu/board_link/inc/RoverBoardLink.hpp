#ifndef APEX_HORIZON_DEMO_ROVER_BOARD_LINK_HPP
#define APEX_HORIZON_DEMO_ROVER_BOARD_LINK_HPP
/**
 * @file RoverBoardLink.hpp
 * @brief Host side of the serial link to the rover controller on the board.
 *
 * Each link tick the driver drains the board's replies (CONTROL_CMD,
 * HEARTBEAT) into a snapshot the RoverController forwards to the plant,
 * judges the link (UP while commands keep arriving, LOST once they stop
 * for link_timeout_ms, NEVER until the first), then sends the rover's
 * pose with the mode and leg the RoverController resolved as one
 * STATE_UPDATE. A port that fails or
 * disappears is closed and reopened once a second, so unplugging and
 * replugging the board recovers without a restart. With enabled = 0 the
 * port is never opened and the controller keeps the host law.
 *
 * @note NOT RT-safe in doInit() and the 1 Hz telemetry task (port open,
 * logging). linkStep() is RT-safe: bounded buffers and non-blocking I/O.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/rover_controller/inc/RoverController.hpp"
#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/DriverBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace appsim {
namespace rover_board {

/* ----------------------------- RoverBoardLinkTunables ----------------------------- */

struct RoverBoardLinkTunables {
  char device_path[64]{'/', 'd', 'e', 'v', '/', 't', 't', 'y', 'A', 'C', 'M', '0'};
  std::uint32_t baud_rate{BAUD_RATE};
  std::uint16_t link_timeout_ms{500};
  std::uint8_t enabled{0}; ///< 1 = the board computes the drive.
  std::uint8_t reserved{0};
};
static_assert(sizeof(RoverBoardLinkTunables) == 72, "RoverBoardLinkTunables is 72 bytes");

/* ----------------------------- RoverBoardLinkState ----------------------------- */

struct RoverBoardLinkState {
  std::uint64_t tx_count{0};   ///< STATE_UPDATE frames written.
  std::uint64_t rx_count{0};   ///< CONTROL_CMD frames accepted.
  std::uint64_t heartbeats{0}; ///< HEARTBEAT frames accepted.
  std::uint64_t crc_errors{0}; ///< Frames refused by the CRC or the length check.
  std::uint32_t seq_gaps{0};   ///< Commands missing between consecutive sequence numbers.
  std::uint32_t opens{0};      ///< Successful port opens (a replug adds one).
  std::uint32_t board_cycles{0};
  std::uint32_t board_steps{0};
  std::uint32_t board_overhead_us{0};
  std::uint16_t last_rx_seq{0};
  std::uint8_t link_state{LINK_NEVER};
  std::uint8_t uart_open{0};
  std::uint8_t load_pct{0};
  std::uint8_t reserved[3]{};
  std::uint32_t board_restarts{0}; ///< Command sequence restarted from 1 (the board rebooted).
};
static_assert(sizeof(RoverBoardLinkState) == 64, "RoverBoardLinkState is 64 bytes");

/* ----------------------------- RoverBoardLink ----------------------------- */

class RoverBoardLink final : public system_core::system_component::DriverBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Sequential after RoverController (226).
  static constexpr std::uint16_t COMPONENT_ID = 227;
  static constexpr const char* COMPONENT_NAME = "RoverBoardLink";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "ROVER_BOARD_LINK"; }

  enum class TaskUid : std::uint8_t {
    LINK_STEP = 1, ///< Link rate: drain replies, judge the link, send the state.
    TELEMETRY = 2, ///< 1 Hz: reopen a lost port, log counters and transitions.
  };

  RoverBoardLink() noexcept {
    decodeCfg_.maxFrameSize = MAX_FRAME_PAYLOAD;
    decodeCfg_.allowEmptyFrame = false;
    decodeCfg_.dropUntilEnd = true;
    decodeCfg_.requireTrailingEnd = true;
  }

  /* ----------------------------- Wiring ----------------------------- */

  /// The plant whose state is sent and the controller whose grid it uses.
  void setSources(const ground_vehicle::GroundVehicle* rover,
                  const rover_controller::RoverController* controller) noexcept {
    rover_ = rover;
    controller_ = controller;
  }

  [[nodiscard]] const BoardLinkSnapshot& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] const RoverBoardLinkState& linkState() const noexcept { return state_.get(); }
  [[nodiscard]] system_core::data::TunableParam<RoverBoardLinkTunables>& tunables() noexcept {
    return tunables_;
  }

  /* ----------------------------- Tasks ----------------------------- */

  std::uint8_t linkStep() noexcept {
    // The tunable is read every step so a registry write takes effect live.
    snapshot_.enabled = tunables_.get().enabled;
    if (snapshot_.enabled == 0u) {
      return 0u;
    }
    if (uart_.isOpen()) {
      drain();
    }
    judgeLink();
    if (uart_.isOpen()) {
      sendState();
    }
    return 0u;
  }

  std::uint8_t telemetryTick() noexcept {
    const auto& p = tunables_.get();
    auto& s = state_.get();
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
    if (s.link_state != logged_link_state_) {
      static constexpr const char* NAMES[] = {"NEVER", "UP", "LOST"};
      const auto* FROM = NAMES[logged_link_state_ < 3u ? logged_link_state_ : 0u];
      const auto* TO = NAMES[s.link_state < 3u ? s.link_state : 0u];
      if (s.link_state == LINK_LOST) {
        log->warning(
            label(), 2u,
            fmt::format("link {} -> {} (no command for {} ms)", FROM, TO, p.link_timeout_ms));
      } else {
        log->info(label(), fmt::format("link {} -> {}", FROM, TO));
      }
      logged_link_state_ = s.link_state;
    }
    log->info(label(),
              fmt::format(
                  "link={} open={} tx={} rx={} hb={} crc={} gaps={} opens={} restarts={} | board: "
                  "cycles={} steps={} tick={}us load={}% | cmd: mode={} steer={:+.1f} "
                  "thr={:.3f} arrived={} target_seq={}",
                  s.link_state, s.uart_open, s.tx_count, s.rx_count, s.heartbeats, s.crc_errors,
                  s.seq_gaps, s.opens, s.board_restarts, s.board_cycles, s.board_steps,
                  s.board_overhead_us, s.load_pct, snapshot_.cmd.mode,
                  static_cast<double>(snapshot_.cmd.steer_deg),
                  static_cast<double>(snapshot_.cmd.throttle_frac), snapshot_.cmd.arrived,
                  snapshot_.cmd.target_seq));
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
    tunables_.get().device_path[sizeof(RoverBoardLinkTunables::device_path) - 1u] = '\0';
    return TprmIngest::LOADED;
  }

  [[nodiscard]] bool paramsOptional() const noexcept override { return true; }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;
    registerTask<RoverBoardLink, &RoverBoardLink::linkStep>(
        static_cast<std::uint8_t>(TaskUid::LINK_STEP), this, "linkStep");
    registerTask<RoverBoardLink, &RoverBoardLink::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");
    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(RoverBoardLinkTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(RoverBoardLinkState));

    const auto& p = tunables_.get();
    snapshot_.enabled = p.enabled;
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("init: enabled={} device={} baud={} timeout={}ms sources={}",
                                     p.enabled, p.device_path, p.baud_rate, p.link_timeout_ms,
                                     (rover_ != nullptr && controller_ != nullptr)));
    }
    if (p.enabled != 0u) {
      (void)openPort();
    }
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  using UartStatus = apex::protocols::serial::uart::Status;

  [[nodiscard]] static std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

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
        log->warning(label(), 3u,
                     fmt::format("cannot open {} ({}); the rover gets zero drive until the board "
                                 "answers",
                                 p.device_path, uart::toString(ST)));
      }
      open_fail_logged_ = true;
      return false;
    }
    (void)uart_.flush(true, false);
    decodeState_.reset();
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

  void drain() noexcept {
    for (int guard = 0; guard < 8; ++guard) {
      std::size_t n = 0;
      const auto ST = uart_.read(rxRaw_.data(), rxRaw_.size(), n, 0);
      if (ST == UartStatus::WOULD_BLOCK || (ST == UartStatus::SUCCESS && n == 0u)) {
        return;
      }
      if (ST != UartStatus::SUCCESS) {
        closePort("read failed");
        return;
      }
      feed(n);
      if (n < rxRaw_.size()) {
        return;
      }
    }
  }

  void feed(std::size_t bytesRead) noexcept {
    std::size_t pos = 0;
    while (pos < bytesRead) {
      const std::size_t PREV_LEN = decodeState_.frameLen;
      auto result = apex::protocols::slip::decodeChunk(
          decodeState_, decodeCfg_, {rxRaw_.data() + pos, bytesRead - pos},
          rxDecoded_.data() + PREV_LEN, rxDecoded_.size() - PREV_LEN);
      pos += result.bytesConsumed;
      if (result.frameCompleted) {
        processFrame(rxDecoded_.data(), PREV_LEN + result.bytesProduced);
      }
      if (result.status == apex::protocols::slip::Status::OUTPUT_FULL) {
        decodeState_.reset();
        continue;
      }
      if (result.bytesConsumed == 0u) {
        break;
      }
    }
  }

  void processFrame(const std::uint8_t* data, std::size_t len) noexcept {
    auto& s = state_.get();
    const ParsedFrame F = parseFrame(data, len);
    if (!F.ok) {
      ++s.crc_errors;
      return;
    }
    if (F.opcode == Opcode::CONTROL_CMD && F.payload_len == sizeof(BoardCommand)) {
      BoardCommand cmd{};
      std::memcpy(&cmd, F.payload, sizeof(cmd));
      if (s.rx_count > 0u && cmd.seq_num != static_cast<std::uint16_t>(s.last_rx_seq + 1u)) {
        // A board that reboots numbers its commands from 1 again: that
        // is a restart, not a run of lost frames.
        if (cmd.seq_num <= s.last_rx_seq) {
          ++s.board_restarts;
        } else {
          s.seq_gaps += static_cast<std::uint16_t>(cmd.seq_num - s.last_rx_seq - 1u);
        }
      }
      s.last_rx_seq = cmd.seq_num;
      ++s.rx_count;
      snapshot_.cmd = cmd;
      snapshot_.board_tick = cmd.seq_num;
      last_rx_ns_ = nowNs();
    } else if (F.opcode == Opcode::HEARTBEAT && F.payload_len == sizeof(BoardHeartbeat)) {
      BoardHeartbeat hb{};
      std::memcpy(&hb, F.payload, sizeof(hb));
      ++s.heartbeats;
      s.board_cycles = hb.cycle_count;
      s.board_steps = hb.step_count;
      s.board_overhead_us = hb.overhead_us;
      s.load_pct = hb.load_pct;
      snapshot_.load_pct = hb.load_pct;
    } else {
      ++s.crc_errors;
    }
  }

  void judgeLink() noexcept {
    auto& s = state_.get();
    const std::int64_t TIMEOUT_NS =
        static_cast<std::int64_t>(tunables_.get().link_timeout_ms) * 1000000;
    const bool FRESH = (s.rx_count > 0u) && (nowNs() - last_rx_ns_ <= TIMEOUT_NS);
    s.link_state = FRESH ? LINK_UP : (s.rx_count > 0u ? LINK_LOST : LINK_NEVER);
    snapshot_.link_state = s.link_state;
  }

  void sendState() noexcept {
    if (rover_ == nullptr || controller_ == nullptr) {
      return;
    }
    const auto& TLM = rover_->telemetry();
    const auto& VS = rover_->vehicleState();
    const auto& CS = controller_->controllerState();
    double north = 0.0;
    double east = 0.0;
    controller_->gridPosition(north, east);
    BoardState st{};
    st.north_m = static_cast<float>(north);
    st.east_m = static_cast<float>(east);
    st.heading_deg = static_cast<float>(TLM.heading_deg);
    st.speed_m_s = static_cast<float>(TLM.speed_m_s);
    st.max_speed_m_s = static_cast<float>(rover_->tunables_const().max_speed_m_s);
    st.target_north_m = static_cast<float>(CS.target_north_m);
    st.target_east_m = static_cast<float>(CS.target_east_m);
    st.start_north_m = static_cast<float>(CS.leg_start_north_m);
    st.start_east_m = static_cast<float>(CS.leg_start_east_m);
    st.mode = static_cast<std::uint8_t>(controller_->mode());
    st.target_valid = CS.target_valid;
    st.halt = (VS.commanded_halt != 0u) ? 1u : 0u;
    st.led1_colour = VS.led_colour[0];
    st.led1_rate = VS.led_rate[0];
    st.led2_colour = VS.led_colour[1];
    st.led2_rate = VS.led_rate[1];
    st.target_seq = CS.target_seq;
    st.seq_num = ++tx_seq_;

    const std::size_t N =
        buildFrame(Opcode::STATE_UPDATE, &st, sizeof(st), txFrame_.data(), txFrame_.size());
    auto enc = apex::protocols::slip::encode({txFrame_.data(), N}, txSlip_.data(), txSlip_.size());
    if (N == 0u || enc.status != apex::protocols::slip::Status::OK) {
      return;
    }
    std::size_t written = 0;
    const auto ST = uart_.write(txSlip_.data(), enc.bytesProduced, written, 0);
    if (ST == UartStatus::SUCCESS && written == enc.bytesProduced) {
      ++state_.get().tx_count;
    } else if (ST != UartStatus::SUCCESS && ST != UartStatus::WOULD_BLOCK) {
      closePort("write failed");
    }
  }

  const ground_vehicle::GroundVehicle* rover_{nullptr};
  const rover_controller::RoverController* controller_{nullptr};
  apex::protocols::serial::uart::UartAdapter uart_{std::string{}};
  apex::protocols::slip::DecodeState decodeState_{};
  apex::protocols::slip::DecodeConfig decodeCfg_{};
  std::array<std::uint8_t, 512> rxRaw_{};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> rxDecoded_{};
  std::array<std::uint8_t, MAX_FRAME_PAYLOAD> txFrame_{};
  std::array<std::uint8_t, MAX_SLIP_ENCODED> txSlip_{};
  BoardLinkSnapshot snapshot_{};
  system_core::data::TunableParam<RoverBoardLinkTunables> tunables_{};
  system_core::data::State<RoverBoardLinkState> state_{};
  std::int64_t last_rx_ns_{0};
  std::uint16_t tx_seq_{0};
  std::uint8_t logged_link_state_{LINK_NEVER};
  bool open_fail_logged_{false};
  const char* close_reason_{nullptr};
};

} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_BOARD_LINK_HPP
