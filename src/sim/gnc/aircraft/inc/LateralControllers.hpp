#ifndef APEX_SIM_GNC_AIRCRAFT_LATERAL_CONTROLLERS_HPP
#define APEX_SIM_GNC_AIRCRAFT_LATERAL_CONTROLLERS_HPP
/**
 * @file LateralControllers.hpp
 * @brief Lateral-directional autopilot loops: roll controller, heading hold,
 *        yaw damper.
 *
 * Cascade architecture:
 *
 *   HeadingHold -- phi_ref --> RollController -- aileron --> airframe
 *   YawDamper (independent)   -- rudder --> airframe
 *
 * Sign conventions match the StabilityDerivativeAero model: a positive aileron
 * produces a positive roll moment, so a positive bank error commands positive
 * aileron (no flip). A positive rudder produces a negative yaw moment, so the
 * yaw damper feeds back rudder proportional to yaw rate to oppose Dutch-roll
 * oscillation. Heading error wraps by shortest path (a +358 deg error is treated
 * as -2 deg).
 */

#include "src/sim/gnc/common/inc/PIDLoop.hpp"

namespace sim::gnc::aircraft {

/* ---------------------- RollController ---------------------- */

/**
 * Inner-loop bank-angle controller.
 *
 *   error_rad = phi_ref - phi_actual
 *   aileron   = clamp(PID(error), +/-aileron_limit_rad)
 *
 * No sign flip: a positive bank error commands positive aileron.
 */
class RollController {
public:
  RollController() noexcept {
    setGains(PIDGains{0.80, 0.05, 0.30});
    setAileronLimit(0.35);
  }

  void setGains(const PIDGains& g) noexcept { pid_.setGains(g); }
  void setAileronLimit(double rad) noexcept { aileron_limit_rad_ = rad; }
  void reset() noexcept { pid_.reset(); }

  /**
   * @param bank_ref_rad    commanded bank angle phi
   * @param bank_actual_rad measured bank angle phi
   * @param dt              step (s)
   * @return aileron command (rad), clamped to +/-limit
   */
  [[nodiscard]] double step(double bank_ref_rad, double bank_actual_rad, double dt) noexcept {
    const double error = bank_ref_rad - bank_actual_rad;
    double aileron = pid_.step(error, dt);
    if (aileron > aileron_limit_rad_) {
      aileron = aileron_limit_rad_;
    }
    if (aileron < -aileron_limit_rad_) {
      aileron = -aileron_limit_rad_;
    }
    return aileron;
  }

  [[nodiscard]] const PIDGains& gains() const noexcept { return pid_.gains(); }

private:
  PIDLoop pid_;
  double aileron_limit_rad_ = 0.35;
};

/* ---------------------- HeadingHold ---------------------- */

/**
 * Outer-loop heading controller. Generates a bank-angle reference that
 * RollController then tracks.
 *
 *   error_rad = wrap(psi_ref - psi_actual, [-pi, pi])
 *   phi_ref   = clamp(PID(error), +/-bank_limit_rad)
 *
 * The shortest-path wrap prevents the loop from "going the long way" across the
 * +/-pi discontinuity.
 */
class HeadingHold {
public:
  HeadingHold() noexcept {
    setGains(PIDGains{1.20, 0.05, 0.20});
    setBankLimit(0.45); // ~25 deg
  }

  void setGains(const PIDGains& g) noexcept { pid_.setGains(g); }
  void setBankLimit(double rad) noexcept { bank_limit_rad_ = rad; }
  void reset() noexcept { pid_.reset(); }

  /**
   * @param heading_ref_rad    commanded heading psi (rad, any range)
   * @param heading_actual_rad measured heading psi (rad, any range)
   * @param dt                 step (s)
   * @return bank reference (rad), clamped to +/-bank_limit
   */
  [[nodiscard]] double step(double heading_ref_rad, double heading_actual_rad, double dt) noexcept {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    double error = heading_ref_rad - heading_actual_rad;
    while (error > kPi) {
      error -= kTwoPi;
    }
    while (error < -kPi) {
      error += kTwoPi;
    }
    double bank = pid_.step(error, dt);
    if (bank > bank_limit_rad_) {
      bank = bank_limit_rad_;
    }
    if (bank < -bank_limit_rad_) {
      bank = -bank_limit_rad_;
    }
    return bank;
  }

  [[nodiscard]] const PIDGains& gains() const noexcept { return pid_.gains(); }

private:
  PIDLoop pid_;
  double bank_limit_rad_ = 0.45;
};

/* ---------------------- YawDamper ---------------------- */

/**
 * Yaw-rate damper for Dutch-roll suppression.
 *
 *   r_hp   = washout(r; tau_w)   // first-order high-pass on body yaw rate
 *   rudder = clamp(K_r * r_hp, +/-rudder_limit_rad)
 *
 * The washout high-pass (time constant tau_w ~ 3 s) passes the Dutch-roll
 * frequency band but rejects the steady yaw rate that accompanies a coordinated
 * turn (psi_dot = g*tan(phi)/V); without it a proportional damper would fight
 * every banked turn.
 *
 * Sign convention: a positive yaw rate r (nose-right) commands a positive rudder
 * (trailing-edge left), which yields a nose-left yaw moment opposing r -- negative
 * feedback.
 *
 * Discretization: backward-Euler high-pass,
 *   y[n] = alpha * (y[n-1] + x[n] - x[n-1]),  alpha = tau_w / (tau_w + dt)
 * alpha -> 1 as tau_w -> infinity; alpha -> 0 as tau_w -> 0 (full attenuation).
 */
class YawDamper {
public:
  YawDamper() = default;

  void setGain(double K_r) noexcept { K_r_ = K_r; }
  void setRudderLimit(double rad) noexcept { rudder_limit_rad_ = rad; }
  void setWashoutTau(double tau_w_s) noexcept { tau_w_s_ = tau_w_s; }
  void reset() noexcept {
    r_prev_ = 0.0;
    r_hp_prev_ = 0.0;
  }

  /**
   * @param yaw_rate_rad_s measured body-axis yaw rate r
   * @param dt             control step (s), used by the washout filter
   * @return rudder command (rad), clamped to +/-rudder_limit
   */
  [[nodiscard]] double step(double yaw_rate_rad_s, double dt) noexcept {
    // High-pass washout: rejects steady-state turn rate, passes Dutch roll.
    // tau_w = 0 disables washout (pass-through to direct proportional feedback).
    double r_hp = yaw_rate_rad_s;
    if (tau_w_s_ > 0.0 && dt > 0.0) {
      const double alpha = tau_w_s_ / (tau_w_s_ + dt);
      r_hp = alpha * (r_hp_prev_ + yaw_rate_rad_s - r_prev_);
    }
    r_hp_prev_ = r_hp;
    r_prev_ = yaw_rate_rad_s;

    double rudder = K_r_ * r_hp;
    if (rudder > rudder_limit_rad_) {
      rudder = rudder_limit_rad_;
    }
    if (rudder < -rudder_limit_rad_) {
      rudder = -rudder_limit_rad_;
    }
    return rudder;
  }

  [[nodiscard]] double gain() const noexcept { return K_r_; }
  [[nodiscard]] double washoutTau() const noexcept { return tau_w_s_; }

private:
  double K_r_ = 0.80;
  double rudder_limit_rad_ = 0.30;
  double tau_w_s_ = 3.0; ///< washout time constant; 0 disables washout
  double r_prev_ = 0.0;
  double r_hp_prev_ = 0.0;
};

} // namespace sim::gnc::aircraft

#endif // APEX_SIM_GNC_AIRCRAFT_LATERAL_CONTROLLERS_HPP
