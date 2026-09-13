#ifndef APEX_HORIZON_DEMO_GROUND_VEHICLE_HPP
#define APEX_HORIZON_DEMO_GROUND_VEHICLE_HPP
/**
 * @file GroundVehicle.hpp
 * @brief Kinematic rover component for apex_horizon_demo.
 *
 * The rover drives in a slow circle at constant throttle + constant
 * steering rate. Each `vehicleStep` tick (at `step_hz`, the scheduler
 * entry's rate):
 *   1. Integrates speed up toward `max_speed_m_s` from `throttle_default`.
 *   2. Integrates heading at `steer_rate_deg_s`.
 *   3. Converts (heading, speed) to (lat, lon) deltas using the body's
 *      reference radius (small-angle local-flat approximation).
 *   4. Queries the attached CelestialBody's terrain to clamp altitude
 *      to the ground.
 *   5. Computes terrain slope via 4-point gradient sampling.
 *   6. Casts `lidar_n_rays` rays forward across `lidar_fov_deg`,
 *      marching each in `lidar_step_m` steps; records hit distance
 *      when terrain rises above sensor height.
 *   7. Publishes pose + slope + lidar telemetry.
 *
 * Steering angle and throttle come from an attached drive-command
 * block (`setDriveCommand`, written by a RoverController or by a
 * hardware driver) when one is valid: the plant is then a steered
 * vehicle (heading rate v tan(delta) / wheelbase, speed under
 * acceleration and braking limits). Otherwise the vehicle drives its
 * built-in trajectory (constant throttle + turn rate). A small drive-command
 * interface (`DriveCmd`: HALT / RESUME / SET_THROTTLE) sits above
 * both via the internal command bus — the demo routes the bridge's
 * command sink here, so a paired visualization can halt and resume
 * the vehicle. HALT is a plant-level stop regardless of the block;
 * SET_THROTTLE only shapes the built-in trajectory.
 *
 * @note RT-safe within tick (no allocation); logging is NOT RT-safe.
 */

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleCommand.hpp"
#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleData.hpp"

#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"
#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <system_error>
#include <vector>

namespace appsim {
namespace ground_vehicle {

using ApexStatus = system_core::system_component::Status;

/* ----------------------------- Constants ----------------------------- */

namespace {
using apex::math::vecmat::DEG_TO_RAD;
using apex::math::vecmat::RAD_TO_DEG;
/// Sensor mount height above the ground [m]. Lidar rays travel at
/// (vehicle altitude + this) and hit when terrain rises above that.
constexpr double SENSOR_HEIGHT_M = 1.5;
/// Lateral spacing for terrain-gradient sampling [m]. Two samples
/// north/south + two east/west, all this far from the vehicle.
constexpr double SLOPE_SAMPLE_M = 10.0;
} // namespace

/* ----------------------------- GroundVehicle ----------------------------- */

class GroundVehicle final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. Picked sequentially after WorldQueryProbe (221).
  static constexpr std::uint16_t COMPONENT_ID = 222;
  static constexpr const char* COMPONENT_NAME = "GroundVehicle";

  /// Sequence trace: samples per second while a sequence runs, and the
  /// buffer the 1 Hz drain empties (three seconds of headroom).
  static constexpr std::uint32_t kSeqTraceHz = 20;
  static constexpr std::size_t kSeqTraceReserve = 64;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "GROUND_VEH"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    VEHICLE_STEP = 1, ///< Periodic kinematic step + lidar sweep (at tunables.step_hz).
    TELEMETRY = 2,    ///< Periodic log line (1 Hz typ.).
  };

  /* ----------------------------- Drive commands ----------------------------- */

  /// The drive opcodes (the full surface is RoverOpcode in
  /// GroundVehicleCommand.hpp). Reachable from any internal-bus
  /// source: the demo wires them to the bridge's command sink, so a
  /// paired visualization and the sequence engine drive the vehicle.
  using DriveCmd = RoverOpcode;

  /* ----------------------------- Construction ----------------------------- */

  /// The trace buffer is reserved here, never grown on the RT path.
  GroundVehicle() noexcept { trace_pending_.reserve(kSeqTraceReserve); }
  ~GroundVehicle() override = default;

  GroundVehicle(const GroundVehicle&) = delete;
  GroundVehicle& operator=(const GroundVehicle&) = delete;

  /* ----------------------------- Wiring ----------------------------- */

  /// Set the CelestialBody whose terrain this vehicle drives over.
  /// Pointer must outlive the component. Call before init.
  void setBody(const sim::environment::celestial_body::CelestialBody* body) noexcept {
    body_ = body;
  }
  [[nodiscard]] const sim::environment::celestial_body::CelestialBody* body() const noexcept {
    return body_;
  }

  /// Attach the drive-command block the plant reads each tick (a
  /// controller's OUTPUT or a driver's). Pointer must outlive the
  /// component; nullptr (the default) means the built-in trajectory.
  void setDriveCommand(const GroundVehicleDriveCommand* cmd) noexcept { drive_cmd_ = cmd; }
  [[nodiscard]] const GroundVehicleDriveCommand* driveCommand() const noexcept {
    return drive_cmd_;
  }

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<GroundVehicleTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const GroundVehicleTunables& tunables_const() const noexcept {
    return tunables_.get();
  }
  [[nodiscard]] const GroundVehicleState& vehicleState() const noexcept { return state_.get(); }
  [[nodiscard]] const GroundVehicleTelemetry& telemetry() const noexcept {
    return telemetry_.get();
  }

  /* ----------------------------- Command handling ----------------------------- */

  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    using system_core::system_component::CommandResult;
    const std::uint8_t RC = dispatchCommand(opcode, payload, response);
    // Stamp the result of every rover-range command for the frame;
    // opcodes outside the surface fall through to the base untouched.
    if (opcode >= static_cast<std::uint16_t>(RoverOpcode::HALT) && opcode <= kRoverOpcodeLast) {
      auto& s = state_.get();
      s.last_cmd_opcode = opcode;
      // BUSY is this plant's own code outside CommandResult, so the map
      // is on the raw byte.
      if (RC == static_cast<std::uint8_t>(CommandResult::SUCCESS)) {
        s.last_cmd_result = static_cast<std::uint8_t>(CmdResultCode::ACK);
      } else if (RC == static_cast<std::uint8_t>(CommandResult::EXEC_FAILED)) {
        s.last_cmd_result = static_cast<std::uint8_t>(CmdResultCode::NACK_EXEC_FAILED);
      } else if (RC == kCommandResultBusy) {
        s.last_cmd_result = static_cast<std::uint8_t>(CmdResultCode::NACK_BUSY);
      } else {
        s.last_cmd_result = static_cast<std::uint8_t>(CmdResultCode::NACK_INVALID_ARGUMENT);
      }
    }
    return RC;
  }

  /// Frame bytes 232..: what the plant stamps every tick.
  [[nodiscard]] const std::uint8_t* frameBytes() const noexcept {
    return telemetry_.get().reserved1;
  }

private:
  /// A sequence owns the drive while seq_state is a running id (not
  /// idle, not a halt reason).
  [[nodiscard]] static bool sequenceOwnsDrive(const GroundVehicleState& s) noexcept {
    return s.seq_state != 0u && (s.seq_state & kSeqStateHaltBit) == 0u;
  }

  [[nodiscard]] std::uint8_t dispatchCommand(std::uint16_t opcode,
                                             apex::compat::rospan<std::uint8_t> payload,
                                             std::vector<std::uint8_t>& response) noexcept {
    using system_core::system_component::CommandResult;
    auto& s = state_.get();

    switch (static_cast<RoverOpcode>(opcode)) {
    case RoverOpcode::HALT:
      s.commanded_halt = 1u;
      // A halt is attributed on the frame: manual unless a recovery
      // sequence already named its reason.
      if ((s.seq_state & kSeqStateHaltBit) == 0u) {
        s.seq_state = kSeqStateManualHalt;
        s.waypoint_total = 0u;
        s.active_waypoint = 0u;
      }
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);

    case RoverOpcode::RESUME:
      s.commanded_halt = 0u;
      s.throttle_override_pct = 255u;
      if ((s.seq_state & kSeqStateHaltBit) != 0u) {
        s.seq_state = 0u; // the halt is over; a running sequence would re-stamp itself
      }
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);

    case RoverOpcode::SET_THROTTLE: {
      if (payload.size() < 1u) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      const std::uint8_t PCT = payload[0];
      if (PCT > 100u) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
      s.throttle_override_pct = PCT;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case RoverOpcode::SET_MODE:
    case RoverOpcode::SET_MODE_SEQ: {
      if (s.commanded_halt != 0u) {
        return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED); // halted: RESUME first
      }
      if (payload.size() < sizeof(RoverCmdSetMode)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      if (payload[0] > kDriveModeMax) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
      if (static_cast<RoverOpcode>(opcode) == RoverOpcode::SET_MODE && sequenceOwnsDrive(s)) {
        return kCommandResultBusy;
      }
      s.commanded_mode = payload[0];
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case RoverOpcode::SET_TARGET_REL:
    case RoverOpcode::SET_TARGET_ABS:
    case RoverOpcode::SET_TARGET_REL_SEQ:
    case RoverOpcode::SET_TARGET_ABS_SEQ: {
      if (payload.size() < sizeof(RoverCmdSetTarget)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      RoverCmdSetTarget t{};
      std::memcpy(&t, payload.data(), sizeof(t));
      if (!std::isfinite(t.a_m) || !std::isfinite(t.b_m) || std::fabs(t.a_m) > kTargetAbsMaxM ||
          std::fabs(t.b_m) > kTargetAbsMaxM) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
      if (s.commanded_halt != 0u) {
        return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED); // the mode refuses
      }
      const auto OP = static_cast<RoverOpcode>(opcode);
      const bool FROM_WIRE =
          (OP == RoverOpcode::SET_TARGET_REL) || (OP == RoverOpcode::SET_TARGET_ABS);
      if (FROM_WIRE && sequenceOwnsDrive(s)) {
        return kCommandResultBusy;
      }
      s.target_kind =
          (OP == RoverOpcode::SET_TARGET_REL || OP == RoverOpcode::SET_TARGET_REL_SEQ) ? 1u : 2u;
      s.target_a_m = t.a_m;
      s.target_b_m = t.b_m;
      ++s.target_seq;
      if (s.seq_state != 0u && s.active_waypoint < 255u) {
        ++s.active_waypoint;
      }
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case RoverOpcode::SET_LED: {
      if (payload.size() < sizeof(RoverCmdSetLed)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      const std::uint8_t LAMP = payload[0];
      const std::uint8_t COLOUR = payload[1];
      const std::uint8_t RATE = payload[2];
      if (LAMP < 1u || LAMP > kLampCount || COLOUR > kLedColourMax || RATE > kLedRateMax) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
      if (s.commanded_halt != 0u) {
        // The halt lamps are the alarm; a halt sequence lights them
        // before it latches the halt, and nothing repaints them until
        // RESUME.
        return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
      }
      s.led_colour[LAMP - 1u] = COLOUR;
      s.led_rate[LAMP - 1u] = RATE;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case RoverOpcode::SET_SEQ_STATE: {
      if (payload.size() < sizeof(RoverCmdSeqState)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      if (s.commanded_halt != 0u) {
        // The halt reason stays on the frame until RESUME: a sequence
        // started (or finishing) while halted cannot bracket itself
        // over it.
        return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
      }
      s.seq_state = payload[0];
      s.waypoint_total = payload[1];
      s.active_waypoint = 0u;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }
    }
    return SwModelBase::handleCommand(opcode, payload, response);
  }

public:
  /* ----------------------------- Tasks ----------------------------- */

  /// One kinematic step + lidar sweep. Returns 0 unconditionally.
  std::uint8_t vehicleStep() noexcept {
    auto& s = state_.get();
    auto& tlm = telemetry_.get();
    const auto& p = tunables_.get();

    if (body_ == nullptr || !body_->isReady()) {
      ++s.tick_count;
      return 0u;
    }

    // First tick: latch the boot pose into telemetry (doInit already
    // seeded it so watchpoints never read zeros; this re-asserts it
    // with the body's radius known). Subsequent ticks integrate from
    // telemetry's pose.
    if (s.initialized == 0u) {
      seedBootPose(tlm, p);
      s.initialized = 1u;
    }

    // 1 + 2: integrate speed and heading at the scheduled step rate
    // (the tunable must match the scheduler entry for this task).
    const double DT = 1.0 / static_cast<double>(std::max<std::uint32_t>(p.step_hz, 1u));
    // Throttle and steering resolve in priority order: HALT forces the
    // target speed to zero and freezes the heading; a valid attached
    // drive-command block supplies both; otherwise the built-in
    // trajectory does (with an active SET_THROTTLE override replacing
    // its default throttle).
    const bool DRIVEN = (drive_cmd_ != nullptr) && (drive_cmd_->valid != 0u);
    if (DRIVEN) {
      // The steered vehicle: speed follows the throttle target under
      // the acceleration and braking limits; heading turns at
      // v tan(delta) / wheelbase, so a stationary rover cannot pivot
      // and the tightest turn is wheelbase / tan(max_steer).
      const double TARGET_SPEED =
          (s.commanded_halt != 0u)
              ? 0.0
              : std::clamp(drive_cmd_->throttle_frac, 0.0, 1.0) * p.max_speed_m_s;
      const double DV = TARGET_SPEED - tlm.speed_m_s;
      const double LIMIT = (DV >= 0.0 ? p.accel_m_s2 : p.decel_m_s2) * DT;
      tlm.speed_m_s += std::clamp(DV, -LIMIT, LIMIT);
      if (s.commanded_halt == 0u && p.wheelbase_m > 0.0) {
        const double DELTA =
            std::clamp(drive_cmd_->steer_angle_deg, -p.max_steer_deg, p.max_steer_deg) * DEG_TO_RAD;
        const double RATE_DEG_S = tlm.speed_m_s * std::tan(DELTA) / p.wheelbase_m * RAD_TO_DEG;
        tlm.heading_deg = std::fmod(tlm.heading_deg + RATE_DEG_S * DT + 360.0, 360.0);
      }
    } else {
      // The undriven plant: its built-in constant-rate circle with a
      // first-order speed approach (95 % per second), and a HALT that
      // freezes the heading while the coast-down runs out.
      const double THROTTLE = (s.throttle_override_pct <= 100u)
                                  ? static_cast<double>(s.throttle_override_pct) / 100.0
                                  : p.throttle_default;
      const double TARGET_SPEED = (s.commanded_halt != 0u) ? 0.0 : THROTTLE * p.max_speed_m_s;
      constexpr double TAU_S = 1.0;
      tlm.speed_m_s += (TARGET_SPEED - tlm.speed_m_s) * (DT / TAU_S);
      if (s.commanded_halt == 0u) {
        tlm.heading_deg = std::fmod(tlm.heading_deg + p.steer_rate_deg_s * DT + 360.0, 360.0);
      }
    }

    // 3: convert (heading, speed) to lat/lon delta on the body's
    // reference radius. ref_radius_m comes from CelestialBody telemetry.
    const double R = body_->telemetry().reference_radius_m;
    if (R > 0.0) {
      const double HEAD_RAD = tlm.heading_deg * DEG_TO_RAD;
      const double V_NORTH_M_S = tlm.speed_m_s * std::cos(HEAD_RAD);
      const double V_EAST_M_S = tlm.speed_m_s * std::sin(HEAD_RAD);
      // Local-flat approximation: 1 deg lat = R * pi/180; 1 deg lon at
      // latitude phi = R * cos(phi) * pi/180. For our small patch this
      // is plenty accurate.
      const double LAT_RAD = tlm.pos_lat_deg * DEG_TO_RAD;
      const double M_PER_DEG_LAT = R * DEG_TO_RAD;
      const double M_PER_DEG_LON = R * std::cos(LAT_RAD) * DEG_TO_RAD;
      tlm.pos_lat_deg += (V_NORTH_M_S * DT) / M_PER_DEG_LAT;
      tlm.pos_lon_deg += (V_EAST_M_S * DT) / M_PER_DEG_LON;
    }
    ++s.step_count;

    // 4: clamp altitude to ground. Out-of-coverage flagged but doesn't
    // block the step.
    const double LAT_RAD_NOW = tlm.pos_lat_deg * DEG_TO_RAD;
    const double LON_RAD_NOW = tlm.pos_lon_deg * DEG_TO_RAD;
    double H = 0.0;
    const bool TERRAIN_OK = body_->terrain()->elevationAt(LAT_RAD_NOW, LON_RAD_NOW, H) ==
                            sim::environment::terrain::Status::SUCCESS;
    tlm.is_off_terrain = TERRAIN_OK ? 0u : 1u;
    if (TERRAIN_OK) {
      tlm.ground_elevation_m = H;
      tlm.pos_alt_m = H; // ride on the surface
    }

    // 5: slope from 4-point gradient. Sample N/S and E/W of the
    // vehicle position; convert to slope angle + azimuth.
    if (TERRAIN_OK && R > 0.0) {
      const double D_LAT = SLOPE_SAMPLE_M / (R * DEG_TO_RAD) * DEG_TO_RAD;
      const double D_LON = SLOPE_SAMPLE_M / (R * std::cos(LAT_RAD_NOW) * DEG_TO_RAD) * DEG_TO_RAD;
      double H_N = H, H_S = H, H_E = H, H_W = H;
      (void)body_->terrain()->elevationAt(LAT_RAD_NOW + D_LAT, LON_RAD_NOW, H_N);
      (void)body_->terrain()->elevationAt(LAT_RAD_NOW - D_LAT, LON_RAD_NOW, H_S);
      (void)body_->terrain()->elevationAt(LAT_RAD_NOW, LON_RAD_NOW + D_LON, H_E);
      (void)body_->terrain()->elevationAt(LAT_RAD_NOW, LON_RAD_NOW - D_LON, H_W);
      const double DH_DN = (H_N - H_S) / (2.0 * SLOPE_SAMPLE_M); // dH/dy (north)
      const double DH_DE = (H_E - H_W) / (2.0 * SLOPE_SAMPLE_M); // dH/dx (east)
      const double SLOPE_TAN = std::sqrt(DH_DN * DH_DN + DH_DE * DH_DE);
      tlm.slope_deg = std::atan(SLOPE_TAN) * RAD_TO_DEG;
      tlm.slope_azimuth_deg = std::fmod(std::atan2(DH_DE, DH_DN) * RAD_TO_DEG + 360.0, 360.0);
      tlm.is_slipping = (tlm.slope_deg > p.max_slope_deg) ? 1u : 0u;
    } else {
      tlm.slope_deg = 0.0;
      tlm.slope_azimuth_deg = 0.0;
      tlm.is_slipping = 0u;
    }

    // 6: lidar sweep. N rays across fov_deg, centered on vehicle heading;
    // decimated to every lidar_divisor-th step.
    const std::uint32_t LIDAR_DIV = std::max<std::uint32_t>(p.lidar_divisor, 1u);
    if ((s.step_count % LIDAR_DIV) == 1u % LIDAR_DIV) {
      sweepLidar(tlm, p, R);
    }
    tlm.lidar_n_rays = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);

    // 6b: lamps. A steady colour is on; a strobed one toggles every half
    // period, the period being the step rate over the rate code's
    // frequency (never below two steps, so any rate reads as a blink).
    for (std::size_t i = 0; i < 2u; ++i) {
      const std::uint8_t COLOUR = s.led_colour[i];
      const std::uint8_t RATE = std::min<std::uint8_t>(s.led_rate[i], kLedRateMax);
      if (COLOUR == 0u) {
        s.led_on[i] = 0u;
        s.led_phase[i] = 0u;
        continue;
      }
      if (RATE == 0u) {
        s.led_on[i] = 1u;
        s.led_phase[i] = 0u;
        continue;
      }
      // kLedRateFrames is the period at 100 Hz; scale to this step rate.
      const std::uint32_t PERIOD =
          std::max<std::uint32_t>(2u, (kLedRateFrames[RATE] * p.step_hz) / 100u);
      const std::uint32_t HALF = PERIOD / 2u;
      s.led_on[i] = (s.led_phase[i] < HALF) ? 1u : 0u;
      s.led_phase[i] = static_cast<std::uint16_t>((s.led_phase[i] + 1u) % PERIOD);
    }

    // 7: stamp wire-format header fields. The bridge memcpys this whole
    // struct — The consumer uses timestamp_ns + tick to detect dropped frames and
    // measure end-to-end latency.
    // Stamp simulated state time on the tick grid, anchored to the
    // monotonic clock once at the first published tick: state and
    // stamp agree exactly, so scheduler jitter never reaches the
    // wire (10 Hz grid: consecutive stamps differ by exactly
    // 100 ms).
    if (t0_ns_ == 0u) {
      t0_ns_ = static_cast<std::uint64_t>(apex::helpers::cpu::getMonotonicNs());
      t0_tick_ = s.tick_count;
    }
    const std::uint64_t DT_NS = static_cast<std::uint64_t>(DT * 1.0e9);
    tlm.timestamp_ns = t0_ns_ + (s.tick_count - t0_tick_) * DT_NS;
    tlm.tick = s.tick_count;

    // 8: drive/command truth into the frame's reserved tail (byte map
    // in GroundVehicleCommand.hpp). LED bits and board bytes are
    // stamped by their own features.
    auto* fb = tlm.reserved1;
    fb[FB_CONTROLLER_MODE] = (s.commanded_halt != 0u) ? kFrameModeHalted
                             : DRIVEN                 ? drive_cmd_->mode
                                                      : static_cast<std::uint8_t>(1u);
    fb[FB_SEQ_STATE] = s.seq_state;
    fb[FB_ACTIVE_WAYPOINT] = s.active_waypoint;
    fb[FB_WAYPOINT_TOTAL] = s.waypoint_total;
    fb[FB_LED_BITS] = static_cast<std::uint8_t>((s.led_on[0] != 0u ? 0x01u : 0u) |
                                                (s.led_on[1] != 0u ? 0x02u : 0u));
    fb[FB_LED1_COLOUR] = s.led_colour[0];
    fb[FB_LED1_RATE] = s.led_rate[0];
    fb[FB_LED2_COLOUR] = s.led_colour[1];
    fb[FB_LED2_RATE] = s.led_rate[1];
    fb[FB_LAST_CMD_RESULT] = s.last_cmd_result;
    fb[FB_LAST_CMD_OPCODE_LO] = static_cast<std::uint8_t>(s.last_cmd_opcode & 0xFFu);
    fb[FB_LAST_CMD_OPCODE_HI] = static_cast<std::uint8_t>(s.last_cmd_opcode >> 8u);

    // 9: sequence trace. While seq_state names a running sequence,
    // sample the drive-relevant channels at 20 Hz into a bounded
    // buffer that telemetryTick drains to the log (the RT path never
    // allocates: the buffer is reserved at init; overflow counts).
    captureSeqTrace(s, tlm, p, DT);
    ++s.tick_count;
    return 0u;
  }

  /// Samples pending in the trace buffer (drained by telemetryTick).
  [[nodiscard]] std::size_t seqTracePending() const noexcept { return trace_pending_.size(); }

  /// Periodic log line summarizing pose + nearest lidar hit, and the
  /// drain of the sequence trace (fixed grep-able shape: the offline
  /// plots parse these lines).
  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log == nullptr) {
      return 0u;
    }
    {
      auto& s = state_.get();
      for (const auto& ts : trace_pending_) {
        log->info(label(), fmt::format("SEQTRACE seq={} wp={}/{} t={:.2f} n={:+.3f} e={:+.3f} "
                                       "hdg={:.2f} v={:.3f} lidar={:.1f} slope={:.2f} led={}",
                                       ts.seq, ts.wp, ts.wp_total, ts.t_s, ts.north_m, ts.east_m,
                                       ts.heading_deg, ts.speed_m_s, ts.lidar_nearest_m,
                                       ts.slope_deg, ts.led_bits));
      }
      trace_pending_.clear();
      if (s.trace_ended != 0u) {
        s.trace_ended = 0u;
        log->info(label(), fmt::format("SEQTRACE end dropped={}", s.trace_dropped));
        s.trace_dropped = 0u;
      }
    }
    const auto& tlm = telemetry_.get();
    const auto& p = tunables_.get();
    // Find nearest lidar hit for the log summary.
    double nearest = p.lidar_max_range_m;
    int nearest_ray = -1;
    const std::uint32_t N = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);
    for (std::uint32_t i = 0; i < N; ++i) {
      if (tlm.lidar_hit[i] != 0u && tlm.lidar_range_m[i] < nearest) {
        nearest = tlm.lidar_range_m[i];
        nearest_ray = static_cast<int>(i);
      }
    }
    log->info(label(),
              fmt::format("tick={} {} lat={:.5f} lon={:.5f} hdg={:6.2f} "
                          "spd={:5.2f} grnd_H={:7.1f}m slope={:5.2f}deg "
                          "slip={} lidar_nearest={:.1f}m@ray{}",
                          tlm.tick, p.body_label, tlm.pos_lat_deg, tlm.pos_lon_deg, tlm.heading_deg,
                          tlm.speed_m_s, tlm.ground_elevation_m, tlm.slope_deg,
                          tlm.is_slipping ? "yes" : "no", nearest, nearest_ray));
    return 0u;
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  /// Optional TPRM tunable load. Typed-reject reader: a size or
  /// identity mismatch is a loud classified fault, not a silent
  /// fallback to defaults.
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return TprmIngest::DEFAULTS; // Tunables stay at defaults; init can still proceed.
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
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
    }
    return TprmIngest::LOADED;
  }

  /** @brief Defaults are a designed configuration for this demo component. */
  [[nodiscard]] bool paramsOptional() const noexcept override { return true; }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<GroundVehicle, &GroundVehicle::vehicleStep>(
        static_cast<std::uint8_t>(TaskUid::VEHICLE_STEP), this, "vehicleStep");
    registerTask<GroundVehicle, &GroundVehicle::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(GroundVehicleTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(GroundVehicleState));
    registerData(DataCategory::OUTPUT, "telemetry", &telemetry_.get(),
                 sizeof(GroundVehicleTelemetry));
    seedBootPose(telemetry_.get(), tunables_.get());

    auto* log = componentLog();
    if (log != nullptr) {
      const auto& p = tunables_.get();
      log->info(label(), fmt::format("init: body={} init_pos=({:.4f}, {:.4f}) heading={:.1f}deg "
                                     "lidar={}rays/{:.0f}deg/{:.0f}m body_attached={}",
                                     p.body_label, p.init_lat_deg, p.init_lon_deg,
                                     p.init_heading_deg, p.lidar_n_rays, p.lidar_fov_deg,
                                     p.lidar_max_range_m, body_ != nullptr));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  /* ----------------------------- Boot pose ----------------------------- */

  /// Write the boot pose and a benign sensor picture (no lidar hits,
  /// level, on terrain) into telemetry. Called at init, before any
  /// task runs, because the action engine's watchpoints evaluate the
  /// OUTPUT block from the executive thread starting at tick 0 -- a
  /// zero-initialized block reads as "outside the geofence with an
  /// obstacle at 0 m" and would fire every boundary at boot.
  void seedBootPose(GroundVehicleTelemetry& tlm, const GroundVehicleTunables& p) const noexcept {
    const double R0 = (body_ != nullptr) ? body_->telemetry().reference_radius_m : 0.0;
    if (p.init_from_grid != 0u && R0 > 0.0) {
      // Grid boot: the anchor plus (north, east) metres, projected
      // about the anchor latitude as the step integrates.
      const double M_PER_DEG_LAT = R0 * DEG_TO_RAD;
      const double M_PER_DEG_LON = R0 * std::cos(p.anchor_lat_deg * DEG_TO_RAD) * DEG_TO_RAD;
      tlm.pos_lat_deg = p.anchor_lat_deg + p.init_north_m / M_PER_DEG_LAT;
      tlm.pos_lon_deg = p.anchor_lon_deg + p.init_east_m / M_PER_DEG_LON;
    } else if (p.init_from_grid != 0u) {
      tlm.pos_lat_deg = p.anchor_lat_deg;
      tlm.pos_lon_deg = p.anchor_lon_deg;
    } else {
      tlm.pos_lat_deg = p.init_lat_deg;
      tlm.pos_lon_deg = p.init_lon_deg;
    }
    tlm.heading_deg = p.init_heading_deg;
    tlm.speed_m_s = 0.0;
    tlm.slope_deg = 0.0;
    tlm.is_slipping = 0u;
    tlm.is_off_terrain = 0u;
    tlm.lidar_n_rays = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);
    for (std::uint32_t i = 0; i < MAX_LIDAR_RAYS; ++i) {
      tlm.lidar_range_m[i] = p.lidar_max_range_m;
      tlm.lidar_hit[i] = 0u;
    }
  }

  /* ----------------------------- Sequence trace ----------------------------- */

  void captureSeqTrace(GroundVehicleState& s, const GroundVehicleTelemetry& tlm,
                       const GroundVehicleTunables& p, double dt) noexcept {
    const bool RUNNING = (s.seq_state != 0u);
    if (!RUNNING) {
      if (s.trace_active != 0u) {
        s.trace_active = 0u;
        s.trace_ended = 1u;
      }
      return;
    }
    if (s.trace_active == 0u) {
      s.trace_active = 1u;
      s.trace_t_s = 0.0;
      s.trace_decim = 0u;
    }
    const std::uint32_t DECIM = std::max<std::uint32_t>(p.step_hz / kSeqTraceHz, 1u);
    if (s.trace_decim == 0u) {
      if (trace_pending_.size() < trace_pending_.capacity()) {
        const double R0 = (body_ != nullptr) ? body_->telemetry().reference_radius_m : 0.0;
        double n = 0.0, e = 0.0;
        if (R0 > 0.0) {
          n = (tlm.pos_lat_deg - p.anchor_lat_deg) * R0 * DEG_TO_RAD;
          e = (tlm.pos_lon_deg - p.anchor_lon_deg) * R0 * std::cos(p.anchor_lat_deg * DEG_TO_RAD) *
              DEG_TO_RAD;
        }
        double nearest = p.lidar_max_range_m;
        const std::uint32_t N = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);
        for (std::uint32_t i = 0; i < N; ++i) {
          if (tlm.lidar_hit[i] != 0u && tlm.lidar_range_m[i] < nearest) {
            nearest = tlm.lidar_range_m[i];
          }
        }
        trace_pending_.push_back(GroundVehicleSeqTraceSample{
            s.trace_t_s, s.seq_state, s.active_waypoint, s.waypoint_total,
            static_cast<std::uint8_t>((s.led_on[0] != 0u ? 1u : 0u) |
                                      (s.led_on[1] != 0u ? 2u : 0u)),
            n, e, tlm.heading_deg, tlm.speed_m_s, nearest, tlm.slope_deg});
      } else {
        ++s.trace_dropped;
      }
    }
    s.trace_decim = (s.trace_decim + 1u) % DECIM;
    s.trace_t_s += dt;
  }

  /* ----------------------------- Lidar helper ----------------------------- */

  /// Cast `tunables.lidar_n_rays` rays forward and update telemetry.
  /// Sensor altitude = vehicle altitude + SENSOR_HEIGHT_M; rays travel
  /// horizontally; hit when terrain elevation > sensor altitude.
  void sweepLidar(GroundVehicleTelemetry& tlm, const GroundVehicleTunables& p,
                  double R_ref_m) const noexcept {
    const std::uint32_t N = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);
    if (N == 0u || R_ref_m <= 0.0) {
      return;
    }
    const double SENSOR_ALT = tlm.pos_alt_m + SENSOR_HEIGHT_M;
    const double LAT_RAD = tlm.pos_lat_deg * DEG_TO_RAD;
    const double M_PER_DEG_LAT = R_ref_m * DEG_TO_RAD;
    const double M_PER_DEG_LON = R_ref_m * std::cos(LAT_RAD) * DEG_TO_RAD;

    // Half-FOV in deg; ray i is at angular offset (i / (N-1) - 0.5)*FOV
    // from heading (or 0 if N==1).
    const double FOV = p.lidar_fov_deg;
    for (std::uint32_t i = 0; i < N; ++i) {
      const double FRAC = (N == 1u) ? 0.0 : (static_cast<double>(i) / (N - 1u) - 0.5);
      const double RAY_HEAD_DEG = std::fmod(tlm.heading_deg + FRAC * FOV + 360.0, 360.0);
      const double RAY_HEAD_RAD = RAY_HEAD_DEG * DEG_TO_RAD;
      const double DLAT_PER_M = std::cos(RAY_HEAD_RAD) / M_PER_DEG_LAT;
      const double DLON_PER_M = std::sin(RAY_HEAD_RAD) / M_PER_DEG_LON;

      // March forward in step_m increments until hit or max range.
      tlm.lidar_hit[i] = 0u;
      tlm.lidar_range_m[i] = p.lidar_max_range_m;
      const double STEP = (p.lidar_step_m > 0.0) ? p.lidar_step_m : 5.0;
      for (double r = STEP; r <= p.lidar_max_range_m; r += STEP) {
        const double SAMPLE_LAT_DEG = tlm.pos_lat_deg + DLAT_PER_M * r;
        const double SAMPLE_LON_DEG = tlm.pos_lon_deg + DLON_PER_M * r;
        const double SAMPLE_LAT_RAD = SAMPLE_LAT_DEG * DEG_TO_RAD;
        const double SAMPLE_LON_RAD = SAMPLE_LON_DEG * DEG_TO_RAD;
        double sample_h = 0.0;
        if (body_->terrain()->elevationAt(SAMPLE_LAT_RAD, SAMPLE_LON_RAD, sample_h) !=
            sim::environment::terrain::Status::SUCCESS) {
          // Out of coverage: ray "exits the world"; treat as max range.
          break;
        }
        if (sample_h > SENSOR_ALT) {
          tlm.lidar_hit[i] = 1u;
          tlm.lidar_range_m[i] = r;
          break;
        }
      }
    }
  }

  const sim::environment::celestial_body::CelestialBody* body_{nullptr};
  const GroundVehicleDriveCommand* drive_cmd_{nullptr};
  std::vector<GroundVehicleSeqTraceSample> trace_pending_{};

  /// Timestamp grid anchor: monotonic time of the first published tick
  /// and its tick number; stamps advance from there in exact DT steps.
  std::uint64_t t0_ns_ = 0;
  std::uint64_t t0_tick_ = 0;
  system_core::data::TunableParam<GroundVehicleTunables> tunables_{};
  system_core::data::State<GroundVehicleState> state_{};
  system_core::data::Output<GroundVehicleTelemetry> telemetry_{};
};

} // namespace ground_vehicle
} // namespace appsim

#endif // APEX_HORIZON_DEMO_GROUND_VEHICLE_HPP
