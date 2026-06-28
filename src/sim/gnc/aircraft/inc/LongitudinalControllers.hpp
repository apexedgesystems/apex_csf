#ifndef APEX_SIM_GNC_AIRCRAFT_LONGITUDINAL_CONTROLLERS_HPP
#define APEX_SIM_GNC_AIRCRAFT_LONGITUDINAL_CONTROLLERS_HPP
/**
 * @file LongitudinalControllers.hpp
 * @brief Longitudinal autopilot loops: pitch attitude hold, altitude hold,
 *        speed hold.
 *
 * Cascade architecture:
 *
 *   AltitudeHold -- theta_ref --> PitchAttitudeHold -- elevator --> airframe
 *   SpeedHold (independent)      -- throttle --> propulsion
 *
 * Sign conventions match the StabilityDerivativeAero model: a positive elevator
 * deflection (trailing edge down) produces a negative pitch moment, so a
 * pitch-up command drives the elevator negative -- the PitchAttitudeHold wrapper
 * negates the PID output (whose gains act on the canonical error e = ref -
 * actual). A positive throttle command in [0, 1] feeds propulsion directly.
 *
 * Default gains are illustrative transport-class starting values; re-tune
 * per-aircraft via the gain setters.
 */

#include "src/sim/gnc/common/inc/PIDLoop.hpp"

namespace sim::gnc::aircraft {

/* ---------------------- PitchAttitudeHold ---------------------- */

/**
 * Inner-loop pitch attitude controller.
 *
 *   error_rad = pitch_ref_rad - pitch_actual_rad
 *   u_pid     = PID(error)
 *   elevator  = clamp(-u_pid, +/-elevator_limit_rad)
 *
 * The unary minus maps "want more pitch up" (positive error) to a negative
 * elevator deflection (trailing-edge up), which produces a positive pitch
 * moment under the aero sign convention.
 */
class PitchAttitudeHold {
public:
  PitchAttitudeHold() noexcept {
    setGains(PIDGains{1.5, 0.10, 0.40});
    setElevatorLimit(0.35); // ~20 deg
  }

  void setGains(const PIDGains& g) noexcept { pid_.setGains(g); }
  void setElevatorLimit(double rad) noexcept { elevator_limit_rad_ = rad; }
  void reset() noexcept { pid_.reset(); }

  /**
   * @param pitch_ref_rad    commanded pitch attitude
   * @param pitch_actual_rad measured pitch attitude
   * @param dt               step (s)
   * @return elevator command (rad), clamped to +/-limit
   */
  [[nodiscard]] double step(double pitch_ref_rad, double pitch_actual_rad, double dt) noexcept {
    const double error = pitch_ref_rad - pitch_actual_rad;
    const double u = pid_.step(error, dt);
    double elevator = -u;
    if (elevator > elevator_limit_rad_) {
      elevator = elevator_limit_rad_;
    }
    if (elevator < -elevator_limit_rad_) {
      elevator = -elevator_limit_rad_;
    }
    return elevator;
  }

  [[nodiscard]] const PIDGains& gains() const noexcept { return pid_.gains(); }

private:
  PIDLoop pid_;
  double elevator_limit_rad_ = 0.35;
};

/* ---------------------- AltitudeHold ---------------------- */

/**
 * Outer-loop altitude controller. Generates a pitch reference that
 * PitchAttitudeHold then tracks.
 *
 *   error_m   = h_ref_m - h_actual_m
 *   theta_ref = clamp(PID(error), +/-pitch_limit_rad)
 *
 * Default gains are small (Kp ~ 0.0008 rad/m) so a 100 m altitude error commands
 * ~5 deg of pitch -- comfortable for a transport aircraft.
 */
class AltitudeHold {
public:
  AltitudeHold() noexcept {
    setGains(PIDGains{0.0008, 0.00004, 0.0050});
    setPitchLimit(0.15); // ~8.5 deg
  }

  void setGains(const PIDGains& g) noexcept { pid_.setGains(g); }
  void setPitchLimit(double rad) noexcept { pitch_limit_rad_ = rad; }
  void reset() noexcept { pid_.reset(); }

  /**
   * @param altitude_ref_m    commanded altitude (m AMSL)
   * @param altitude_actual_m measured altitude (m AMSL)
   * @param dt                step (s)
   * @return pitch reference (rad), clamped to +/-pitch_limit
   */
  [[nodiscard]] double step(double altitude_ref_m, double altitude_actual_m, double dt) noexcept {
    const double error = altitude_ref_m - altitude_actual_m;
    double theta_ref = pid_.step(error, dt);
    if (theta_ref > pitch_limit_rad_) {
      theta_ref = pitch_limit_rad_;
    }
    if (theta_ref < -pitch_limit_rad_) {
      theta_ref = -pitch_limit_rad_;
    }
    return theta_ref;
  }

  [[nodiscard]] const PIDGains& gains() const noexcept { return pid_.gains(); }

private:
  PIDLoop pid_;
  double pitch_limit_rad_ = 0.15;
};

/* ---------------------- SpeedHold ---------------------- */

/**
 * Speed (auto-throttle) controller.
 *
 *   error_ms = V_ref - V_actual
 *   throttle = clamp(throttle_trim + PID(error), [0, 1])
 *
 * The PID is clamped to [-1, 1]; the trim offset + final saturation happen in
 * the wrapper so anti-windup uses the true output limits.
 */
class SpeedHold {
public:
  SpeedHold() noexcept {
    setGains(PIDGains{0.020, 0.0010, 0.0});
    PIDLimits l;
    l.u_min = -1.0;
    l.u_max = 1.0;
    pid_.setLimits(l);
  }

  void setGains(const PIDGains& g) noexcept { pid_.setGains(g); }
  void setTrimThrottle(double t) noexcept { trim_throttle_ = t; }
  void reset() noexcept { pid_.reset(); }

  /**
   * @param V_ref_ms    commanded true airspeed (m/s)
   * @param V_actual_ms measured true airspeed (m/s)
   * @param dt          step (s)
   * @return throttle command in [0, 1]
   */
  [[nodiscard]] double step(double V_ref_ms, double V_actual_ms, double dt) noexcept {
    const double error = V_ref_ms - V_actual_ms;
    const double u = pid_.step(error, dt);
    double throttle = trim_throttle_ + u;
    if (throttle < 0.0) {
      throttle = 0.0;
    }
    if (throttle > 1.0) {
      throttle = 1.0;
    }
    return throttle;
  }

  [[nodiscard]] const PIDGains& gains() const noexcept { return pid_.gains(); }

private:
  PIDLoop pid_;
  double trim_throttle_ = 0.50;
};

} // namespace sim::gnc::aircraft

#endif // APEX_SIM_GNC_AIRCRAFT_LONGITUDINAL_CONTROLLERS_HPP
