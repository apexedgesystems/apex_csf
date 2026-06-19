#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_EXPONENTIAL_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_EXPONENTIAL_HPP
/**
 * @file ExponentialAtmosphere.hpp
 * @brief Isothermal exponential atmosphere: rho(h) = rho0 * exp(-h/H).
 *
 * The classical first-order model. Derived from the hydrostatic equation
 *   dP/dz = -rho * g
 * combined with the ideal gas law P = rho * R * T under the simplifying
 * assumptions that T and g are altitude-invariant. Yields:
 *   rho(h) = rho0 * exp(-h / H)
 *   P(h)   = P0   * exp(-h / H)
 *   T(h)   = T0     (constant)
 * where H = R * T / g is the scale height (8.5 km for Earth's troposphere).
 *
 * Body-agnostic. Caller supplies (rho0, T0, H) -- pressure is derived from
 * the ideal gas law (P0 = rho0 * R * T0). For non-Earth bodies, supply the
 * appropriate R_specific.
 *
 * This is the J2 of atmosphere: more capable than constant, much simpler
 * than layered, captures the dominant exponential decay that any first-cut
 * orbital-decay or re-entry estimate needs.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"
#include "src/sim/environment/atmosphere/inc/ConstantAtmosphere.hpp" // shares DEFAULT_GAMMA/R

#include <cmath>
#include <cstdint>
#include <limits>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- Constants ----------------------------- */

/// Earth tropospheric defaults (sea level, ISA-ish).
namespace earth_defaults {
constexpr double RHO0 = 1.225;     ///< Sea-level density [kg/m^3].
constexpr double T0 = 288.15;      ///< 15 C, the ISA reference [K].
constexpr double H_SCALE = 8500.0; ///< Tropospheric scale height [m].
} // namespace earth_defaults

/* ----------------------------- ExponentialAtmosphere ----------------------------- */

/// Isothermal exponential atmosphere. Default construction yields the Earth
/// tropospheric ISA exponential approximation; pass other (rho0, T0, H) for
/// other bodies or other altitude regimes.
class ExponentialAtmosphere final : public AtmosphereModelBase {
public:
  ExponentialAtmosphere() noexcept = default;

  /// Construct with explicit parameters. Returns a "broken" model (queries
  /// will fail) when invariants are violated; check `isInitialized()` after.
  ExponentialAtmosphere(double rho0, double T0, double scaleHeightM, double gamma = DEFAULT_GAMMA,
                        double R_specific = DEFAULT_R_SPECIFIC) noexcept {
    (void)init(rho0, T0, scaleHeightM, gamma, R_specific);
  }

  /// Set parameters with validity checks.
  /// @return `Status::SUCCESS` if all inputs are physical; otherwise a
  ///         specific `ERROR_PARAM_*` naming the offending parameter
  ///         (rho0 >= 0, T0 > 0, H > 0, R_specific > 0). On any error the
  ///         model is left in a "not initialized" state and queries fail
  ///         with `ERROR_NOT_INITIALIZED`.
  [[nodiscard]] Status init(double rho0, double T0, double scaleHeightM,
                            double gamma = DEFAULT_GAMMA,
                            double R_specific = DEFAULT_R_SPECIFIC) noexcept {
    if (rho0 < 0.0) {
      initialized_ = false;
      return Status::ERROR_PARAM_RHO_INVALID;
    }
    if (T0 <= 0.0) {
      initialized_ = false;
      return Status::ERROR_PARAM_TEMP_INVALID;
    }
    if (scaleHeightM <= 0.0) {
      initialized_ = false;
      return Status::ERROR_PARAM_SCALE_INVALID;
    }
    if (R_specific <= 0.0) {
      initialized_ = false;
      return Status::ERROR_PARAM_GAS_CONST_INVALID;
    }
    rho0_ = rho0;
    T0_ = T0;
    H_ = scaleHeightM;
    gamma_ = gamma;
    R_ = R_specific;
    P0_ = rho0_ * R_ * T0_;             // ideal gas law
    a0_ = std::sqrt(gamma_ * R_ * T0_); // speed of sound (isothermal)
    initialized_ = true;
    return Status::SUCCESS;
  }

  [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }
  [[nodiscard]] double rho0() const noexcept { return rho0_; }
  [[nodiscard]] double T0() const noexcept { return T0_; }
  [[nodiscard]] double scaleHeight() const noexcept { return H_; }
  [[nodiscard]] double gamma() const noexcept { return gamma_; }
  [[nodiscard]] double gasConstant() const noexcept { return R_; }

  /* ----------------------------- AtmosphereModelBase API ----------------------------- */

  [[nodiscard]] Status query(double alt_m, double /*lat_rad*/, double /*lon_rad*/,
                             AtmosphereState& s) const noexcept override {
    if (!initialized_) {
      return Status::ERROR_NOT_INITIALIZED;
    }
    if (std::isnan(alt_m)) {
      return Status::ERROR_PARAM_ALT_INVALID;
    }
    const double E = std::exp(-alt_m / H_);
    s.rho = rho0_ * E;
    s.P = P0_ * E;
    s.T = T0_; // isothermal -- T does not vary with altitude
    s.a = a0_; // speed of sound depends only on T (constant here)
    return Status::SUCCESS;
  }

  [[nodiscard]] Status density(double alt_m, double /*lat_rad*/, double /*lon_rad*/,
                               double& rho) const noexcept override {
    if (!initialized_) {
      return Status::ERROR_NOT_INITIALIZED;
    }
    if (std::isnan(alt_m)) {
      return Status::ERROR_PARAM_ALT_INVALID;
    }
    rho = rho0_ * std::exp(-alt_m / H_);
    return Status::SUCCESS;
  }

  /// Valid for all altitudes mathematically (rho -> 0 at h -> inf), but
  /// physically the isothermal assumption breaks above the tropopause.
  /// We keep the bounds permissive; callers that care can clamp.
  [[nodiscard]] double minAltitudeM() const noexcept override {
    return -std::numeric_limits<double>::infinity();
  }
  [[nodiscard]] double maxAltitudeM() const noexcept override {
    return std::numeric_limits<double>::infinity();
  }

private:
  double rho0_ = earth_defaults::RHO0;
  double T0_ = earth_defaults::T0;
  double H_ = earth_defaults::H_SCALE;
  double gamma_ = DEFAULT_GAMMA;
  double R_ = DEFAULT_R_SPECIFIC;
  double P0_ = earth_defaults::RHO0 * DEFAULT_R_SPECIFIC * earth_defaults::T0;
  double a0_ = 340.294;     // sqrt(1.4 * 287.058 * 288.15) approximately
  bool initialized_ = true; // default-constructed object uses Earth defaults
};

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_EXPONENTIAL_HPP
