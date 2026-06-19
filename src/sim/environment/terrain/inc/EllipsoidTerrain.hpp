#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_ELLIPSOID_TERRAIN_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_ELLIPSOID_TERRAIN_HPP
/**
 * @file EllipsoidTerrain.hpp
 * @brief Oblate-spheroid terrain model: surface follows an analytic
 *        biaxial ellipsoid of revolution.
 *
 * Body-agnostic. Treats the body as an oblate spheroid with equatorial
 * and polar radii (`r_eq_m`, `r_pol_m`). For geodetic queries, returns
 * H = 0 (samples lie on the ellipsoid by definition). For ECEF queries,
 * returns the geodetic height above the ellipsoid using the standard
 * Bowring iteration.
 *
 * Useful as a higher-fidelity baseline than SphereTerrain when the body
 * has measurable oblateness (Earth, Mars, etc.) but no DEM is available.
 */

#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"

#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- EllipsoidTerrain ----------------------------- */

class EllipsoidTerrain final : public TerrainModelBase {
public:
  EllipsoidTerrain() noexcept = default;
  EllipsoidTerrain(double r_eq_m, double r_pol_m) noexcept : r_eq_m_(r_eq_m), r_pol_m_(r_pol_m) {}

  void setRadii(double r_eq_m, double r_pol_m) noexcept {
    r_eq_m_ = r_eq_m;
    r_pol_m_ = r_pol_m;
  }
  [[nodiscard]] double equatorialRadius() const noexcept { return r_eq_m_; }
  [[nodiscard]] double polarRadius() const noexcept { return r_pol_m_; }
  /// Flattening f = (a - b) / a. Returns 0 for a sphere; ~1/298.26 for WGS84.
  [[nodiscard]] double flattening() const noexcept {
    return (r_eq_m_ > 0.0) ? (r_eq_m_ - r_pol_m_) / r_eq_m_ : 0.0;
  }
  /// First eccentricity squared e^2 = 1 - (b/a)^2.
  [[nodiscard]] double eccentricitySquared() const noexcept {
    return (r_eq_m_ > 0.0) ? 1.0 - (r_pol_m_ * r_pol_m_) / (r_eq_m_ * r_eq_m_) : 0.0;
  }

  /* ----------------------------- TerrainModelBase API ----------------------------- */

  /// Geodetic: ellipsoid surface is H=0.
  [[nodiscard]] Status elevationAt(double /*latRad*/, double /*lonRad*/,
                                   double& H) const noexcept override {
    H = 0.0;
    return Status::SUCCESS;
  }

  /// ECEF: return geodetic height above the ellipsoid via Bowring's
  /// iterative method (3 iterations is sufficient to ~mm at LEO).
  [[nodiscard]] Status elevationAtEcef(const double ecef[3], double& H) const noexcept override {
    if (ecef == nullptr) {
      return Status::ERROR_PARAM_BUFFER_NULL;
    }
    const double X = ecef[0];
    const double Y = ecef[1];
    const double Z = ecef[2];
    const double A = r_eq_m_;
    const double B = r_pol_m_;
    const double E2 = 1.0 - (B * B) / (A * A); // first eccentricity squared
    const double P = std::sqrt(X * X + Y * Y);
    if (P < 1e-12) {
      // Pole: H is just |Z| - r_pol_m_.
      H = std::abs(Z) - B;
      return Status::SUCCESS;
    }
    // Bowring closed-form starting estimate, then 3 iterations.
    double lat = std::atan2(Z, P * (1.0 - E2));
    for (int i = 0; i < 3; ++i) {
      const double SIN_LAT = std::sin(lat);
      const double N = A / std::sqrt(1.0 - E2 * SIN_LAT * SIN_LAT);
      lat = std::atan2(Z + E2 * N * SIN_LAT, P);
    }
    const double SIN_LAT = std::sin(lat);
    const double N = A / std::sqrt(1.0 - E2 * SIN_LAT * SIN_LAT);
    H = P / std::cos(lat) - N;
    return Status::SUCCESS;
  }

  [[nodiscard]] double minLatRad() const noexcept override { return -1.5707963267948966; }
  [[nodiscard]] double maxLatRad() const noexcept override { return 1.5707963267948966; }
  [[nodiscard]] double minLonRad() const noexcept override { return -3.141592653589793; }
  [[nodiscard]] double maxLonRad() const noexcept override { return 3.141592653589793; }
  [[nodiscard]] double resolutionMeters() const noexcept override { return 0.0; }

private:
  double r_eq_m_ = 1.0;
  double r_pol_m_ = 1.0;
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_ELLIPSOID_TERRAIN_HPP
