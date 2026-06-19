#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_MODEL_BASE_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_MODEL_BASE_HPP
/**
 * @file AtmosphereModelBase.hpp
 * @brief Abstract interface for atmosphere models.
 *
 * Atmosphere models report local fluid state (density, pressure, temperature,
 * speed of sound) at a position above a celestial body. Altitude is measured
 * above the body's reference surface (the same convention used by terrain).
 * Lat/lon are accepted by the interface but most analytic models ignore them
 * (they only depend on altitude). Empirical models like NRLMSISE-00 use them.
 *
 * Implementations range from the trivial (Constant, Vacuum) through analytic
 * (Exponential) to physical (Layered/USSA76) and eventually empirical
 * (NRLMSISE-00 -- deferred).
 *
 * The base also exposes `isVacuum()` so consumers (drag computations) can
 * short-circuit when there is no atmosphere to integrate against.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- AtmosphereState ----------------------------- */

/// Bundle of fluid-state quantities returned by `query()`. Derived quantities
/// (e.g., dynamic viscosity, mean free path) are intentionally omitted and can
/// be added later without breaking the base class API.
struct AtmosphereState {
  double rho = 0.0; ///< Density [kg/m^3].
  double P = 0.0;   ///< Pressure [Pa].
  double T = 0.0;   ///< Temperature [K].
  double a = 0.0;   ///< Speed of sound [m/s] (sqrt(gamma * R_specific * T)).
};

/* ----------------------------- AtmosphereModelBase ----------------------------- */

/// Abstract base. All concrete atmosphere models (Constant, Exponential,
/// Layered, ...) implement `query()` and `min/maxAltitudeM()`. Convenience
/// accessors (`density()`, `pressure()`, ...) are default-implemented in
/// terms of `query()` and may be overridden if a faster code path exists.
class AtmosphereModelBase {
public:
  virtual ~AtmosphereModelBase() = default;

  /// Compute the full atmospheric state at altitude `alt_m` above the
  /// body's reference surface. `lat_rad` / `lon_rad` are accepted for
  /// future geographic-variation models; analytic models ignore them.
  /// Returns `Status::SUCCESS` on a valid sample; `ERROR_NOT_INITIALIZED`
  /// when the model holds no data; `WARN_OUT_OF_VALID_RANGE` when the
  /// altitude is outside the model's documented validity; `ERROR_PARAM_*`
  /// for a degenerate query. On any non-SUCCESS result `s` is left
  /// unmodified, so a caller that ignores the status sees its own prior
  /// contents (or its zero-initialized default), never a half-filled
  /// bundle. RT-safety depends on the implementation.
  [[nodiscard]] virtual Status query(double alt_m, double lat_rad, double lon_rad,
                                     AtmosphereState& s) const noexcept = 0;

  /// Convenience: density-only. Drag computations are the hot path; an
  /// override that skips P/T/a costs less than the bundle. Default impl
  /// pulls from `query()`. `rho` is left unmodified on any non-SUCCESS.
  [[nodiscard]] virtual Status density(double alt_m, double lat_rad, double lon_rad,
                                       double& rho) const noexcept {
    AtmosphereState s;
    const Status st = query(alt_m, lat_rad, lon_rad, s);
    if (!isSuccess(st)) {
      return st;
    }
    rho = s.rho;
    return st;
  }

  /// Convenience: pressure-only. `P` is left unmodified on any non-SUCCESS.
  [[nodiscard]] virtual Status pressure(double alt_m, double lat_rad, double lon_rad,
                                        double& P) const noexcept {
    AtmosphereState s;
    const Status st = query(alt_m, lat_rad, lon_rad, s);
    if (!isSuccess(st)) {
      return st;
    }
    P = s.P;
    return st;
  }

  /// Convenience: temperature-only. `T` is left unmodified on any non-SUCCESS.
  [[nodiscard]] virtual Status temperature(double alt_m, double lat_rad, double lon_rad,
                                           double& T) const noexcept {
    AtmosphereState s;
    const Status st = query(alt_m, lat_rad, lon_rad, s);
    if (!isSuccess(st)) {
      return st;
    }
    T = s.T;
    return st;
  }

  /// True when this model represents vacuum (density is zero everywhere).
  /// Drag computations should skip integration entirely when this is true.
  /// Default false; ConstantAtmosphere with rho=0 returns true.
  [[nodiscard]] virtual bool isVacuum() const noexcept { return false; }

  /// Validity altitude range (meters above the body's reference surface).
  /// Outside this range `query()` may return false (out of validity warning).
  [[nodiscard]] virtual double minAltitudeM() const noexcept = 0;
  [[nodiscard]] virtual double maxAltitudeM() const noexcept = 0;

  /// Convenience: is `alt_m` within this model's documented validity?
  [[nodiscard]] bool isInValidRange(double alt_m) const noexcept {
    return alt_m >= minAltitudeM() && alt_m <= maxAltitudeM();
  }
};

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_MODEL_BASE_HPP
