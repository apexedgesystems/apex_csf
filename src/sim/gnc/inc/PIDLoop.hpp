#ifndef APEX_SIM_GNC_PID_LOOP_HPP
#define APEX_SIM_GNC_PID_LOOP_HPP
/**
 * @file PIDLoop.hpp
 * @brief Generic single-input single-output PID controller primitive.
 *
 *   u(t) = Kp * e(t) + Ki * integral(e) + Kd * de/dt
 *
 * Discretized with backward-Euler for the integral and a first difference for
 * the derivative:
 *
 *   integral   += e * dt
 *   derivative  = (e - e_prev) / dt
 *   u           = Kp*e + Ki*integral + Kd*derivative
 *
 * Output is clamped to [u_min, u_max]; on saturation the integral update is
 * reverted (conditional integration) to prevent windup. This primitive is the
 * shared building block the named control loops compose.
 */

namespace sim::gnc {

struct PIDGains {
  double Kp = 0.0;
  double Ki = 0.0;
  double Kd = 0.0;
};

/** Output clamps + anti-windup behavior. Defaults disable clamping. */
struct PIDLimits {
  double u_min = -1e30;
  double u_max = +1e30;
};

class PIDLoop {
public:
  PIDLoop() = default;
  PIDLoop(const PIDGains& g, const PIDLimits& l = {}) noexcept : gains_(g), limits_(l) {}

  /** Reset integral + previous-error history. */
  void reset() noexcept {
    integral_ = 0.0;
    prev_err_ = 0.0;
    has_prev_ = false;
  }

  void setGains(const PIDGains& g) noexcept { gains_ = g; }
  void setLimits(const PIDLimits& l) noexcept { limits_ = l; }

  /**
   * Compute u given the current error and time step.
   *
   * Anti-windup: if the unclamped output would saturate, the tentative integral
   * update is reverted so the integral does not grow while the output is pinned.
   */
  [[nodiscard]] double step(double error, double dt) noexcept {
    const double p_term = gains_.Kp * error;
    const double d_term = has_prev_ ? gains_.Kd * (error - prev_err_) / dt : 0.0;

    // Tentative integral update; reverted if we saturate.
    const double prev_integral = integral_;
    integral_ += error * dt;
    const double i_term = gains_.Ki * integral_;

    double u = p_term + i_term + d_term;

    if (u > limits_.u_max) {
      integral_ = prev_integral;
      u = limits_.u_max;
    } else if (u < limits_.u_min) {
      integral_ = prev_integral;
      u = limits_.u_min;
    }

    prev_err_ = error;
    has_prev_ = true;
    return u;
  }

  // Inspection helpers for tests + telemetry.
  [[nodiscard]] double integral() const noexcept { return integral_; }
  [[nodiscard]] const PIDGains& gains() const noexcept { return gains_; }
  [[nodiscard]] const PIDLimits& limits() const noexcept { return limits_; }

private:
  PIDGains gains_{};
  PIDLimits limits_{};
  double integral_ = 0.0;
  double prev_err_ = 0.0;
  bool has_prev_ = false;
};

} // namespace sim::gnc

#endif // APEX_SIM_GNC_PID_LOOP_HPP
