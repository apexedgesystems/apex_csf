#ifndef APEX_HORIZON_DEMO_LIDAR_BOX_PRODUCER_HPP
#define APEX_HORIZON_DEMO_LIDAR_BOX_PRODUCER_HPP
/**
 * @file LidarBoxProducer.hpp
 * @brief SW model that drifts a body through the box and measures wall clearance.
 *
 * Each `bodyStep` tick (50 Hz per the scheduler):
 *   1. Advances sim time and evaluates the 3D Lissajous drift + steady yaw
 *      (closed-form in t, so the trajectory is exact at any rate).
 *   2. Clamps the body center to +/-(half - radius) per axis -- the contract
 *      guarantee that every wall clearance stays >= kBodyRadius.
 *   3. Measures the six wall clearances with the sim_sensors BoxClearanceLidar.
 *   4. Publishes the 48-byte LidarBoxFrame OUTPUT block (pose + clearances +
 *      monotonic timestamp) that the ShmRingBridge streams to the consumer.
 *
 * The producer never touches the bridge: it registers OUTPUT and the bridge's
 * TPRM selects this component's (fullUid, OUTPUT) as its source. Scheduler
 * priorities order bodyStep (50) before bridgeStep (40) within each tick, so
 * the bridge always memcpys a freshly-written frame.
 *
 * @note bodyStep is RT-safe (closed-form math + clock_gettime); logging is not.
 */

#include "apps/apex_horizon_demo/lidar_box/producer/inc/LidarBoxTypes.hpp"

#include "src/sim/sensors/inc/BoxClearanceLidar.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <cmath>
#include <cstdint>
#include <ctime>
#include <fmt/format.h>

namespace appsim {
namespace lidar_box {

using ApexStatus = system_core::system_component::Status;

/* ----------------------------- LidarBoxProducer ----------------------------- */

class LidarBoxProducer final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component class ID. 230 keeps clear of the vehicle-demo component range.
  static constexpr std::uint16_t COMPONENT_ID = 230;
  static constexpr const char* COMPONENT_NAME = "LidarBoxProducer";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "LIDAR_BOX"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    BODY_STEP = 1, ///< Drift + clearance + publish (50 Hz).
    TELEMETRY = 2, ///< Periodic log line (1 Hz).
  };

  /* ----------------------------- Construction ----------------------------- */

  /// Tunables carry valid defaults, so the component is configured from
  /// construction; a TPRM load (when present) overrides them before init.
  LidarBoxProducer() noexcept { setConfigured(true); }
  ~LidarBoxProducer() override = default;

  LidarBoxProducer(const LidarBoxProducer&) = delete;
  LidarBoxProducer& operator=(const LidarBoxProducer&) = delete;

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<LidarBoxTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const LidarBoxState& bodyState() const noexcept { return state_.get(); }
  [[nodiscard]] const LidarBoxFrame& frame() const noexcept { return frame_.get(); }

  /* ----------------------------- Tasks ----------------------------- */

  /// One kinematic step + clearance measurement + frame publish.
  std::uint8_t bodyStep() noexcept {
    const auto& p = tunables_.get();
    auto& s = state_.get();
    auto& f = frame_.get();

    s.t_s += p.dt_s;

    // Closed-form Lissajous drift; amplitude is a clamped fraction of the
    // per-axis travel budget so the inset guarantee holds by construction.
    s.pos_x_m = amplitude(p.amp_frac_x, kHalfX) * std::sin(p.omega_x_rad_s * s.t_s);
    s.pos_y_m = amplitude(p.amp_frac_y, kHalfY) * std::sin(p.omega_y_rad_s * s.t_s + p.phase_y_rad);
    s.pos_z_m = amplitude(p.amp_frac_z, kHalfZ) * std::sin(p.omega_z_rad_s * s.t_s + p.phase_z_rad);

    // Steady yaw, wrapped to [-pi, pi] so the float in the frame stays exact.
    s.yaw_rad = wrapPi(s.yaw_rad + p.yaw_rate_rad_s * p.dt_s);

    const auto CLR = lidar_.measure(s.pos_x_m, s.pos_y_m, s.pos_z_m, kBox);

    f.pos_x = static_cast<float>(s.pos_x_m);
    f.pos_y = static_cast<float>(s.pos_y_m);
    f.pos_z = static_cast<float>(s.pos_z_m);
    f.yaw_rad = static_cast<float>(s.yaw_rad);
    f.clr_pos_x = static_cast<float>(CLR.pos_x);
    f.clr_neg_x = static_cast<float>(CLR.neg_x);
    f.clr_pos_y = static_cast<float>(CLR.pos_y);
    f.clr_neg_y = static_cast<float>(CLR.neg_y);
    f.clr_pos_z = static_cast<float>(CLR.pos_z);
    f.clr_neg_z = static_cast<float>(CLR.neg_z);
    f.timestamp_ns = monotonicNs();

    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

  /// 1 Hz health log line.
  std::uint8_t telemetryTick() noexcept {
    auto* log = componentLog();
    if (log != nullptr) {
      const auto& s = state_.get();
      const auto& f = frame_.get();
      log->info(label(),
                fmt::format("t={:.1f}s pos=({:+.2f},{:+.2f},{:+.2f}) yaw={:+.2f} "
                            "clr x=({:.2f}/{:.2f}) y=({:.2f}/{:.2f}) z=({:.2f}/{:.2f})",
                            s.t_s, f.pos_x, f.pos_y, f.pos_z, f.yaw_rad, f.clr_pos_x, f.clr_neg_x,
                            f.clr_pos_y, f.clr_neg_y, f.clr_pos_z, f.clr_neg_z));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<LidarBoxProducer, &LidarBoxProducer::bodyStep>(
        static_cast<std::uint8_t>(TaskUid::BODY_STEP), this, "bodyStep");
    registerTask<LidarBoxProducer, &LidarBoxProducer::telemetryTick>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    registerData(DataCategory::TUNABLE_PARAM, "tunables", &tunables_.get(),
                 sizeof(LidarBoxTunables));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(LidarBoxState));
    registerData(DataCategory::OUTPUT, "frame", &frame_.get(), sizeof(LidarBoxFrame));

    // The lidar's noise setting comes from the tunables (loaded before init).
    const auto& p = tunables_.get();
    sim::sensors::BoxClearanceLidarParams lp;
    lp.sigma_m = p.sigma_m;
    lidar_ = sim::sensors::BoxClearanceLidar(lp);

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(),
                fmt::format("init: box=({:.1f},{:.1f},{:.1f}) inset={:.1f} dt={:.3f}s "
                            "omega=({:.2f},{:.2f},{:.2f}) yaw_rate={:.2f} sigma={:.3f}",
                            kBoxHalfX, kBoxHalfY, kBoxHalfZ, kBodyRadius, p.dt_s, p.omega_x_rad_s,
                            p.omega_y_rad_s, p.omega_z_rad_s, p.yaw_rate_rad_s, p.sigma_m));
    }
    return static_cast<std::uint8_t>(ApexStatus::SUCCESS);
  }

private:
  /* ----------------------------- Helpers ----------------------------- */

  /// Per-axis drift amplitude: a [0,1]-clamped fraction of (half - radius).
  [[nodiscard]] static double amplitude(double frac, double half) noexcept {
    const double F = frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac);
    return F * (half - static_cast<double>(kBodyRadius));
  }

  /// Wrap an angle to [-pi, pi].
  [[nodiscard]] static double wrapPi(double a) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    while (a > kPi) {
      a -= kTwoPi;
    }
    while (a < -kPi) {
      a += kTwoPi;
    }
    return a;
  }

  /// Monotonic nanosecond stamp for the frame.
  [[nodiscard]] static std::uint64_t monotonicNs() noexcept {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
  }

  // Double mirrors of the float wire constants (the model runs in double).
  static constexpr double kHalfX = static_cast<double>(kBoxHalfX);
  static constexpr double kHalfY = static_cast<double>(kBoxHalfY);
  static constexpr double kHalfZ = static_cast<double>(kBoxHalfZ);
  static constexpr sim::sensors::BoxExtents kBox{kHalfX, kHalfY, kHalfZ};

  /* ----------------------------- Apex data registration ----------------------------- */

  system_core::data::TunableParam<LidarBoxTunables> tunables_{};
  system_core::data::State<LidarBoxState> state_{};
  system_core::data::Output<LidarBoxFrame> frame_{};

  sim::sensors::BoxClearanceLidar lidar_{};
};

} // namespace lidar_box
} // namespace appsim

#endif // APEX_HORIZON_DEMO_LIDAR_BOX_PRODUCER_HPP
