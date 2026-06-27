#ifndef APEX_SIM_GNC_GUST_ALLEVIATION_HPP
#define APEX_SIM_GNC_GUST_ALLEVIATION_HPP
/**
 * @file GustAlleviation.hpp
 * @brief Longitudinal gust alleviation: vertical-gust feedforward to elevator.
 *
 * A measured vertical gust w_g induces an apparent angle-of-attack change
 * d_alpha = w_g / V, which produces extra lift dL = q*S*CL_alpha*d_alpha. To
 * cancel the gust's effect on lift, command an elevator deflection whose own
 * lift increment cancels it:
 *
 *   CL_alpha * d_alpha + CL_delta_e * elevator = 0
 *   elevator = -(CL_alpha / CL_delta_e) * (w_g / V)
 *
 * This is open-loop feedforward -- it depends only on the measured gust, not on
 * aircraft state -- and complements a closed-loop pitch attitude hold.
 *
 * Sign: with CL_alpha > 0 and CL_delta_e > 0 the gain ratio K is positive. An
 * upward gust (w_g > 0, lift up) commands a negative elevator (trailing-edge up)
 * to cancel; the leading minus sign handles this.
 *
 * The authority fraction (default 1.0) scales or disables the feedforward
 * without removing it from the loop.
 */

namespace sim::gnc {

struct GustAlleviationParams {
  /// Aero gain ratio CL_alpha / CL_delta_e (illustrative transport-class value).
  double K_alpha_over_delta = 14.6;

  /// Authority fraction: 0 = disabled, 1 = full feedforward, >1 = overdrive.
  double gust_authority_pct = 1.0;

  /// Output saturation (rad).
  double elevator_limit_rad = 0.35;
};

class GustAlleviation {
public:
  explicit GustAlleviation(const GustAlleviationParams& p = {}) noexcept : p_(p) {}

  /**
   * Feedforward elevator command for a given vertical gust.
   *
   * @param w_g_m_s vertical gust velocity (body-frame w_g)
   * @param V_m_s   true airspeed
   * @return elevator command (rad), clamped to +/-limit
   */
  [[nodiscard]] double step(double w_g_m_s, double V_m_s) const noexcept {
    if (V_m_s < 1.0) {
      return 0.0; // cannot normalize at vanishing V
    }
    const double d_alpha = w_g_m_s / V_m_s;
    double elevator = -p_.gust_authority_pct * p_.K_alpha_over_delta * d_alpha;
    if (elevator > p_.elevator_limit_rad) {
      elevator = p_.elevator_limit_rad;
    }
    if (elevator < -p_.elevator_limit_rad) {
      elevator = -p_.elevator_limit_rad;
    }
    return elevator;
  }

  void setParams(const GustAlleviationParams& p) noexcept { p_ = p; }
  [[nodiscard]] const GustAlleviationParams& params() const noexcept { return p_; }

private:
  GustAlleviationParams p_;
};

} // namespace sim::gnc

#endif // APEX_SIM_GNC_GUST_ALLEVIATION_HPP
