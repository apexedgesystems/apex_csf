#ifndef APEX_HORIZON_DEMO_ROVER_BOARD_LIDAR_HPP
#define APEX_HORIZON_DEMO_ROVER_BOARD_LIDAR_HPP
/**
 * @file RoverBoardLidar.hpp
 * @brief What the board knows about its lidar sensor.
 *
 * The sensor sends one LidarScan per sweep on the board's second UART.
 * The board keeps the last scan and when it arrived, and stamps what it
 * saw into every CONTROL_CMD: the sensor state (UP while scans keep
 * coming, STALE once they stop for LIDAR_STALE_MS, NEVER before the
 * first), the scan number, the hit bits and the closest return. No HAL,
 * no allocation, so the firmware and the host tests share it.
 */

#include "demos/apex_horizon_demo/rover_mcu/board/inc/RoverBoardProtocol.hpp"

#include <stdint.h>

namespace appsim {
namespace rover_board {

class RoverBoardLidar {
public:
  /**
   * @brief Take one scan received at now_ms.
   * @note RT-safe: O(rays).
   */
  void onScan(const LidarScan& scan, uint32_t now_ms) noexcept {
    const uint8_t N =
        (scan.n_rays <= LIDAR_WIRE_RAYS) ? scan.n_rays : static_cast<uint8_t>(LIDAR_WIRE_RAYS);
    uint16_t nearest_cm = LIDAR_NO_RETURN_CM;
    for (uint8_t i = 0; i < N; ++i) {
      if (scan.range_cm[i] < nearest_cm) {
        nearest_cm = scan.range_cm[i];
      }
    }
    if (nearest_cm == LIDAR_NO_RETURN_CM) {
      nearest_m_ = LIDAR_NEAREST_NONE;
    } else {
      const uint32_t M = nearest_cm / 100u;
      nearest_m_ = static_cast<uint8_t>(M > 254u ? 254u : M);
    }
    const uint8_t MASK =
        (N >= 8u) ? static_cast<uint8_t>(0xFFu) : static_cast<uint8_t>((1u << N) - 1u);
    hit_bits_ = static_cast<uint8_t>(scan.hit_bits & MASK);
    scan_seq_ = scan.scan_seq;
    last_ms_ = now_ms;
    ++scans_;
  }

  /// A frame from the sensor that failed its CRC or its length check.
  void onBadFrame() noexcept { ++bad_frames_; }

  /**
   * @brief Stamp the sensor's picture at now_ms into a command.
   * @note RT-safe: O(1).
   */
  void fill(BoardCommand& cmd, uint32_t now_ms) const noexcept {
    cmd.lidar_state = state(now_ms);
    cmd.lidar_scan_seq = scan_seq_;
    cmd.lidar_hit_bits = hit_bits_;
    cmd.lidar_nearest_m = nearest_m_;
  }

  [[nodiscard]] uint8_t state(uint32_t now_ms) const noexcept {
    if (scans_ == 0u) {
      return LIDAR_NEVER;
    }
    return (now_ms - last_ms_ <= LIDAR_STALE_MS) ? LIDAR_UP : LIDAR_STALE;
  }

  [[nodiscard]] uint32_t scans() const noexcept { return scans_; }
  [[nodiscard]] uint32_t badFrames() const noexcept { return bad_frames_; }

private:
  uint32_t scans_{0};
  uint32_t bad_frames_{0};
  uint32_t last_ms_{0};
  uint16_t scan_seq_{0};
  uint8_t hit_bits_{0};
  uint8_t nearest_m_{LIDAR_NEAREST_NONE};
};

} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_BOARD_LIDAR_HPP
