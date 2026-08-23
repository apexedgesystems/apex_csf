#ifndef APEX_HORIZON_DEMO_GROUND_VEHICLE_HPP
#define APEX_HORIZON_DEMO_GROUND_VEHICLE_HPP
/**
 * @file GroundVehicle.hpp
 * @brief Kinematic rover component for apex_horizon_demo.
 *
 * The rover drives in a slow circle at constant throttle + constant
 * steering rate. Each `vehicleStep` tick (10 Hz, matching executive
 * fundamental):
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
 * The default drive is autonomous (constant throttle + turn). A small
 * drive-command interface (`DriveCmd`: HALT / RESUME / SET_THROTTLE)
 * overrides it via the internal command bus — the demo routes the
 * bridge's command sink here, so a paired visualization can halt and
 * resume the vehicle. A future `RoverController` component could
 * replace the baked-in autonomy through the same seam.
 *
 * @note RT-safe within tick (no allocation); logging is NOT RT-safe.
 */

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

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "GROUND_VEH"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    VEHICLE_STEP = 1, ///< Periodic kinematic step + lidar sweep (10 Hz, matches exec fundamental).
    TELEMETRY = 2,    ///< Periodic log line (1 Hz typ.).
  };

  /* ----------------------------- Drive commands ----------------------------- */

  /// Component-specific command opcodes (0x0100+ range). Reachable from
  /// any internal-bus source: the demo wires them to the bridge's
  /// command sink, so a paired visualization can drive the vehicle.
  enum class DriveCmd : std::uint16_t {
    HALT = 0x0100,         ///< No payload. Target speed -> 0 (first-order coast-down).
    RESUME = 0x0101,       ///< No payload. Clear HALT and any throttle override.
    SET_THROTTLE = 0x0102, ///< Payload: 1 byte, throttle percent 0-100.
  };

  /* ----------------------------- Construction ----------------------------- */

  GroundVehicle() noexcept = default;
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

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<GroundVehicleTunables>& tunables() noexcept {
    return tunables_;
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
    auto& s = state_.get();

    switch (static_cast<DriveCmd>(opcode)) {
    case DriveCmd::HALT:
      s.commanded_halt = 1u;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);

    case DriveCmd::RESUME:
      s.commanded_halt = 0u;
      s.throttle_override_pct = 255u;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);

    case DriveCmd::SET_THROTTLE: {
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

    default:
      return SwModelBase::handleCommand(opcode, payload, response);
    }
  }

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

    // First tick: pull init pose from tunables into telemetry. After
    // that, subsequent ticks integrate from telemetry's pose.
    if (s.initialized == 0u) {
      tlm.pos_lat_deg = p.init_lat_deg;
      tlm.pos_lon_deg = p.init_lon_deg;
      tlm.heading_deg = p.init_heading_deg;
      tlm.speed_m_s = 0.0;
      s.initialized = 1u;
    }

    // 1 + 2: integrate speed and heading. dt is hardcoded for the MVP
    // (matches the 10 Hz scheduler entry, which is also the exec
    // fundamental); future could query the executive for the real dt.
    constexpr double DT = 1.0 / 10.0;
    // Throttle resolves in priority order: HALT forces target speed to
    // zero; an active SET_THROTTLE override replaces the default.
    const double THROTTLE = (s.throttle_override_pct <= 100u)
                                ? static_cast<double>(s.throttle_override_pct) / 100.0
                                : p.throttle_default;
    const double TARGET_SPEED = (s.commanded_halt != 0u) ? 0.0 : THROTTLE * p.max_speed_m_s;
    // Simple first-order approach: 95% per second time constant.
    constexpr double TAU_S = 1.0;
    tlm.speed_m_s += (TARGET_SPEED - tlm.speed_m_s) * (DT / TAU_S);
    // A halted vehicle holds its heading (wheels stop steering); the
    // coast-down still moves it along the frozen heading until speed
    // decays to zero.
    if (s.commanded_halt == 0u) {
      tlm.heading_deg = std::fmod(tlm.heading_deg + p.steer_rate_deg_s * DT + 360.0, 360.0);
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

    // 6: lidar sweep. N rays across fov_deg, centered on vehicle heading.
    sweepLidar(tlm, p, R);
    tlm.lidar_n_rays = std::min<std::uint32_t>(p.lidar_n_rays, MAX_LIDAR_RAYS);

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
    constexpr std::uint64_t DT_NS = static_cast<std::uint64_t>(DT * 1.0e9);
    tlm.timestamp_ns = t0_ns_ + (s.tick_count - t0_tick_) * DT_NS;
    tlm.tick = s.tick_count;
    ++s.tick_count;
    return 0u;
  }

  /// Periodic log line summarizing pose + nearest lidar hit.
  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log == nullptr) {
      return 0u;
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
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    const std::filesystem::path PATH = tprmDir / fmt::format("{:06x}.tprm", fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return true; // Tunables stay at defaults; init can still proceed.
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
      return false;
    }
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("loadTprm: tunables loaded from {}", PATH.string()));
    }
    return true;
  }

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
