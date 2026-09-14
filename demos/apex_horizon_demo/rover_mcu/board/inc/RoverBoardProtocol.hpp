#ifndef APEX_HORIZON_DEMO_ROVER_BOARD_PROTOCOL_HPP
#define APEX_HORIZON_DEMO_ROVER_BOARD_PROTOCOL_HPP
/**
 * @file RoverBoardProtocol.hpp
 * @brief The wire between the rover's plant (host) and its controller (board).
 *
 * Every tick of the link the host sends the rover's pose, the mode and
 * leg its controller resolved, the halt and the lamps as one
 * STATE_UPDATE frame;
 * the board answers each with one CONTROL_CMD carrying the steering
 * angle and throttle the plant consumes through its drive-command seam,
 * plus what it adopted. Once a second the board sends a HEARTBEAT with
 * its cycle count, step count and load. The lidar sensor speaks the
 * same framing on the board's second UART: one LIDAR_SCAN per sweep,
 * and the board reports what it last saw in every CONTROL_CMD. Frames are
 * [opcode][payload][CRC-16/XMODEM big-endian] inside SLIP, the shape the
 * HIL demo pinned; this header is the single definition both sides
 * compile, and it depends on nothing but <stdint.h>.
 */

#include <stddef.h>
#include <stdint.h>

namespace appsim {
namespace rover_board {

/* ----------------------------- Link ----------------------------- */

inline constexpr uint32_t BAUD_RATE = 115200;
inline constexpr size_t MAX_FRAME_PAYLOAD = 64; ///< opcode + payload + crc.
inline constexpr size_t MAX_SLIP_ENCODED = MAX_FRAME_PAYLOAD * 2 + 2;

/* ----------------------------- Opcodes ----------------------------- */

enum class Opcode : uint8_t {
  STATE_UPDATE = 0x10, ///< host -> board: BoardState.
  CONTROL_CMD = 0x20,  ///< board -> host: BoardCommand.
  HEARTBEAT = 0x30,    ///< board -> host: BoardHeartbeat.
  LIDAR_SCAN = 0x40,   ///< sensor -> board (its own wire): LidarScan.
};

/* ----------------------------- Codes shared with the frame ----------------------------- */

/// Drive modes as the ROVR/2 frame and SET_MODE carry them.
inline constexpr uint8_t MODE_HOLD = 0;
inline constexpr uint8_t MODE_TRAJECTORY = 1;
inline constexpr uint8_t MODE_WAYPOINT = 2;

/* ----------------------------- BoardState ----------------------------- */

/**
 * host -> board, every link tick. 48 bytes.
 *
 * The mode and the leg are the ones the host controller resolved (a
 * relative target already turned into an absolute target and the
 * position the leg starts from), so the board keeps no command state a
 * reset could lose: a board that restarts mid-leg reads the same leg
 * from the next frame.
 */
struct BoardState {
  float north_m{0.0F}; ///< Grid position about the anchor.
  float east_m{0.0F};
  float heading_deg{0.0F}; ///< Clockwise from north.
  float speed_m_s{0.0F};
  float max_speed_m_s{8.0F};  ///< The plant's full-throttle speed.
  float target_north_m{0.0F}; ///< The leg's end, grid metres.
  float target_east_m{0.0F};
  float start_north_m{0.0F}; ///< Where the leg starts (the line runs start -> target).
  float start_east_m{0.0F};
  uint8_t mode{0};         ///< MODE_* in effect.
  uint8_t target_valid{0}; ///< 1 once a target is set.
  uint8_t halt{0};         ///< 1 while the plant is halted.
  uint8_t led1_colour{0};
  uint8_t led1_rate{0};
  uint8_t led2_colour{0};
  uint8_t led2_rate{0};
  uint8_t reserved0{0};
  uint16_t target_seq{0}; ///< Bumps on every new leg; the board resets its arrival on change.
  uint16_t seq_num{0};    ///< Host sequence number.
};
static_assert(sizeof(BoardState) == 48, "BoardState is 48 bytes on the wire");

/* ----------------------------- LidarScan ----------------------------- */

inline constexpr size_t LIDAR_WIRE_RAYS = 8;
/// range_cm of a ray with no return inside the sensor's range.
inline constexpr uint16_t LIDAR_NO_RETURN_CM = 0xFFFF;

/// sensor -> board, one per sweep. 20 bytes. Ray 0 is the left edge of
/// the fan, ray LIDAR_WIRE_RAYS - 1 the right edge.
struct LidarScan {
  uint16_t scan_seq{0};                 ///< Bumps every sweep.
  uint8_t n_rays{0};                    ///< Valid rays (<= LIDAR_WIRE_RAYS).
  uint8_t hit_bits{0};                  ///< bit i: ray i returned inside the sensor's range.
  uint16_t range_cm[LIDAR_WIRE_RAYS]{}; ///< LIDAR_NO_RETURN_CM without a return.
};
static_assert(sizeof(LidarScan) == 20, "LidarScan is 20 bytes on the wire");

/// Sensor states as the board reports them and the ROVR/2 frame carries them.
inline constexpr uint8_t LIDAR_NEVER = 0; ///< No scan since the board booted.
inline constexpr uint8_t LIDAR_UP = 1;    ///< Scans arriving.
inline constexpr uint8_t LIDAR_STALE = 2; ///< Scans stopped for LIDAR_STALE_MS.
inline constexpr uint32_t LIDAR_STALE_MS = 500;
/// lidar_nearest_m when no ray returned.
inline constexpr uint8_t LIDAR_NEAREST_NONE = 255;

/* ----------------------------- BoardCommand ----------------------------- */

/// board -> host, one per BoardState. 32 bytes.
struct BoardCommand {
  float steer_deg{0.0F};
  float throttle_frac{0.0F};
  float cross_track_m{0.0F};
  float distance_m{0.0F};
  uint8_t mode{0};                  ///< MODE_* in effect on the board.
  uint8_t arrived{0};               ///< Arrival latch for the adopted target.
  uint8_t led_bits{0};              ///< bit0 lamp1 on, bit1 lamp2 on (the board's own strobe).
  uint8_t lidar_state{LIDAR_NEVER}; ///< LIDAR_* as the board judges its sensor.
  uint16_t target_seq{0};           ///< The target the board is driving to.
  uint16_t seq_num{0};              ///< Board sequence number.
  uint16_t ack_seq{0};              ///< The BoardState.seq_num this answers.
  uint16_t lidar_scan_seq{0};       ///< The last scan the board received.
  uint8_t lidar_hit_bits{0};        ///< That scan's hit bits.
  uint8_t lidar_nearest_m{LIDAR_NEAREST_NONE}; ///< Its closest return, whole metres (254 cap).
  uint16_t reserved0{0};
};
static_assert(sizeof(BoardCommand) == 32, "BoardCommand is 32 bytes on the wire");

/* ----------------------------- BoardHeartbeat ----------------------------- */

/// board -> host, 1 Hz. 16 bytes.
struct BoardHeartbeat {
  uint32_t cycle_count{0}; ///< Executive cycles since boot.
  uint32_t step_count{0};  ///< Controller steps since boot.
  uint32_t overhead_us{0}; ///< Last tick's task time.
  uint8_t load_pct{0};     ///< overhead / tick period.
  uint8_t reserved[3]{};
};
static_assert(sizeof(BoardHeartbeat) == 16, "BoardHeartbeat is 16 bytes on the wire");

/* ----------------------------- BoardLinkSnapshot ----------------------------- */

/// Link states as the ROVR/2 frame's board_link byte carries them.
inline constexpr uint8_t LINK_NEVER = 0;
inline constexpr uint8_t LINK_UP = 1;
inline constexpr uint8_t LINK_LOST = 2;

/// What the host's link driver publishes each tick for the controller:
/// the board's latest command and the link's health.
struct BoardLinkSnapshot {
  BoardCommand cmd{};
  uint8_t enabled{0};             ///< 1 when the board computes the drive (TPRM).
  uint8_t link_state{LINK_NEVER}; ///< LINK_*.
  uint8_t load_pct{0};            ///< From the last heartbeat.
  uint8_t reserved{0};
  uint16_t board_tick{0}; ///< The last command's sequence number (moves at the link rate).
};

/* ----------------------------- Codec ----------------------------- */

/// CRC-16/XMODEM (poly 0x1021, init 0), bitwise so both sides share it.
inline uint16_t crc16Xmodem(const uint8_t* data, size_t len) noexcept {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int b = 0; b < 8; ++b) {
      crc = static_cast<uint16_t>((crc & 0x8000u) != 0u ? (crc << 1) ^ 0x1021u : (crc << 1));
    }
  }
  return crc;
}

/**
 * @brief Lay [opcode][payload][crc] into out. Returns the byte count, 0 if
 * out is too small.
 */
inline size_t buildFrame(Opcode opcode, const void* payload, size_t payload_len, uint8_t* out,
                         size_t out_cap) noexcept {
  const size_t LEN = 1 + payload_len + 2;
  if (out_cap < LEN) {
    return 0;
  }
  out[0] = static_cast<uint8_t>(opcode);
  const uint8_t* src = static_cast<const uint8_t*>(payload);
  for (size_t i = 0; i < payload_len; ++i) {
    out[1 + i] = src[i];
  }
  const uint16_t CRC_VALUE = crc16Xmodem(out, 1 + payload_len);
  out[1 + payload_len] = static_cast<uint8_t>(CRC_VALUE >> 8);
  out[2 + payload_len] = static_cast<uint8_t>(CRC_VALUE & 0xFFu);
  return LEN;
}

/// A decoded frame: opcode and payload view, valid only when ok.
struct ParsedFrame {
  bool ok{false};
  Opcode opcode{Opcode::STATE_UPDATE};
  const uint8_t* payload{nullptr};
  size_t payload_len{0};
};

/// Check the CRC of a de-SLIPped frame and split it.
inline ParsedFrame parseFrame(const uint8_t* data, size_t len) noexcept {
  ParsedFrame f{};
  if (len < 3) {
    return f;
  }
  const size_t BODY = len - 2;
  const uint16_t EXPECTED =
      static_cast<uint16_t>((static_cast<uint16_t>(data[BODY]) << 8) | data[BODY + 1]);
  if (crc16Xmodem(data, BODY) != EXPECTED) {
    return f;
  }
  f.ok = true;
  f.opcode = static_cast<Opcode>(data[0]);
  f.payload = data + 1;
  f.payload_len = BODY - 1;
  return f;
}

} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_BOARD_PROTOCOL_HPP
