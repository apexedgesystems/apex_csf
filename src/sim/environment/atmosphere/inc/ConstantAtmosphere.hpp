#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_CONSTANT_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_CONSTANT_HPP
/**
 * @file ConstantAtmosphere.hpp
 * @brief Constant-state atmosphere: rho, T, P are fixed; defaults to vacuum.
 *
 * Body-agnostic baseline. Two important roles:
 *   1. Vacuum sentinel for airless bodies (Moon, asteroids). Default
 *      construction is rho = T = P = 0; `isVacuum()` returns true so
 *      drag computations short-circuit.
 *   2. Hand-tuning a fixed atmosphere for a controlled experiment
 *      (e.g., wind-tunnel sea-level conditions held constant).
 *
 * Mirrors `gravity/ConstantGravityModel.hpp` and `terrain/ConstantTerrain.hpp`
 * in the spirit of the fidelity ladder: simplest possible model, header-only,
 * RT-safe.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereModelBase.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- Constants ----------------------------- */

/// Default specific-heat ratio (used when computing speed of sound for
/// non-vacuum constant atmospheres). Diatomic gas value (air, N2, O2).
constexpr double DEFAULT_GAMMA = 1.4;

/// Default specific gas constant [J/(kg*K)] -- dry air on Earth.
constexpr double DEFAULT_R_SPECIFIC = 287.058;

/* ----------------------------- ConstantAtmosphere ----------------------------- */

/// Returns fixed (rho, T, P) regardless of position. When all three default
/// to zero the model represents vacuum and `isVacuum()` returns true.
class ConstantAtmosphere : public AtmosphereModelBase {
public:
  /// Default construction: vacuum (rho = T = P = 0).
  ConstantAtmosphere() noexcept = default;

  /// Construct with explicit (rho, T, P) and optional gas constants.
  /// Negative inputs are clamped to zero (treated as vacuum semantics).
  ConstantAtmosphere(double rho, double T, double P, double gamma = DEFAULT_GAMMA,
                     double R_specific = DEFAULT_R_SPECIFIC) noexcept
      : rho_(rho < 0.0 ? 0.0 : rho), T_(T < 0.0 ? 0.0 : T), P_(P < 0.0 ? 0.0 : P), gamma_(gamma),
        R_(R_specific) {}

  /// Update the three primary state values in place.
  void setState(double rho, double T, double P) noexcept {
    rho_ = (rho < 0.0 ? 0.0 : rho);
    T_ = (T < 0.0 ? 0.0 : T);
    P_ = (P < 0.0 ? 0.0 : P);
  }

  // Accessors for the held constants. Named distinctly from the base
  // class virtuals (density/pressure/temperature) so the latter remain
  // unshadowed and callable polymorphically.
  [[nodiscard]] double rho() const noexcept { return rho_; }
  [[nodiscard]] double T() const noexcept { return T_; }
  [[nodiscard]] double P() const noexcept { return P_; }
  [[nodiscard]] double gamma() const noexcept { return gamma_; }
  [[nodiscard]] double gasConstant() const noexcept { return R_; }

  /* ----------------------------- AtmosphereModelBase API ----------------------------- */

  [[nodiscard]] Status query(double /*alt_m*/, double /*lat_rad*/, double /*lon_rad*/,
                             AtmosphereState& s) const noexcept override {
    s.rho = rho_;
    s.T = T_;
    s.P = P_;
    // Speed of sound: only meaningful when T > 0. Vacuum case yields 0.
    s.a = (T_ > 0.0) ? std::sqrt(gamma_ * R_ * T_) : 0.0;
    // A vacuum model still returns a valid (zero) state; the warning lets a
    // drag consumer notice it is integrating against nothing and skip the
    // work. The state is filled either way, per WARN semantics.
    return isVacuum() ? Status::WARN_VACUUM_QUERY : Status::SUCCESS;
  }

  /// Vacuum if and only if density is exactly zero. The pressure / temperature
  /// fields may also be zero, but density is the load-bearing one for drag.
  [[nodiscard]] bool isVacuum() const noexcept override { return rho_ == 0.0; }

  /// Constant model is valid at any altitude.
  [[nodiscard]] double minAltitudeM() const noexcept override {
    return -std::numeric_limits<double>::infinity();
  }
  [[nodiscard]] double maxAltitudeM() const noexcept override {
    return std::numeric_limits<double>::infinity();
  }

private:
  double rho_ = 0.0;
  double T_ = 0.0;
  double P_ = 0.0;
  double gamma_ = DEFAULT_GAMMA;
  double R_ = DEFAULT_R_SPECIFIC;
};

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_CONSTANT_HPP
