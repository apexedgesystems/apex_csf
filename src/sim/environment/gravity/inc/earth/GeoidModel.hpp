#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEOID_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEOID_MODEL_HPP
/**
 * @file GeoidModel.hpp
 * @brief Geoid undulation model using EGM2008 coefficients.
 *
 * Computes the geoid undulation N (separation between the geoid and the
 * reference ellipsoid) at a given geodetic position. This allows conversion
 * between ellipsoid heights (from GPS) and orthometric heights (above MSL).
 *
 *   h = H + N
 *
 * where:
 *   h = ellipsoid height (GPS)
 *   H = orthometric height (above MSL)
 *   N = geoid undulation
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Geodetic.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- GeoidParams ----------------------------- */

/**
 * @brief Parameters for geoid model.
 */
struct GeoidParams {
  double GM = wgs84::GM; ///< Gravitational constant [m^3/s^2].
  double a = wgs84::A;   ///< Reference radius [m].
  int16_t N = 360;       ///< Maximum degree for computation.
};

/* ----------------------------- GeoidModel ----------------------------- */

/**
 * @brief Geoid undulation model using spherical harmonics.
 *
 * Computes geoid undulation N at a geodetic position using the EGM2008
 * disturbing potential coefficients. The basic formula is:
 *
 *   N = T / gamma
 *
 * where T is the disturbing potential and gamma is normal gravity.
 *
 * For higher accuracy, the Zeta-to-N correction can be applied.
 *
 * @note NOT RT-safe: Initialization allocates memory.
 * @note RT-safe after init: O(N^2) evaluation, no allocation.
 */
class GeoidModel {
public:
  GeoidModel() = default;

  /**
   * @brief Initialize with coefficient source.
   *
   * @param src Coefficient source (EGM2008 potential coefficients).
   * @param params Model parameters.
   * @return true on success.
   * @note NOT RT-safe: Allocates coefficient storage.
   */
  [[nodiscard]] bool init(const CoeffSource& src, const GeoidParams& params) noexcept {
    GM_ = params.GM;
    a_ = params.a;

    // Clamp N to source limits
    N_ = params.N;
    if (N_ > src.maxDegree()) {
      N_ = src.maxDegree();
    }
    if (N_ < 0) {
      return false;
    }

    // Allocate and copy coefficients in triangular storage
    const size_t COUNT = triangularCount(N_);
    try {
      C_.resize(COUNT);
      S_.resize(COUNT);
      Pnm_.resize(COUNT);
    } catch (...) {
      return false;
    }

    for (int16_t n = 0; n <= N_; ++n) {
      for (int16_t m = 0; m <= n; ++m) {
        double c = 0.0, s = 0.0;
        src.get(n, m, c, s);
        const size_t IDX = triangularOffset(n, m);
        C_[IDX] = c;
        S_[IDX] = s;
      }
    }

    return true;
  }

  /**
   * @brief Compute geoid undulation at geodetic position.
   *
   * Uses the formula N = T / gamma where T is the disturbing potential.
   *
   * @param lat Geodetic latitude [rad].
   * @param lon Geodetic longitude [rad].
   * @return Geoid undulation N [m].
   * @note RT-safe after init: O(N^2), no allocation.
   */
  double undulation(double lat, double lon) const noexcept {
    // Convert to geocentric coordinates on reference sphere
    // For geoid computation, use mean Earth radius at this latitude
    const double SIN_LAT = std::sin(lat);
    const double COS_LAT = std::cos(lat);

    // Geocentric radius on ellipsoid surface
    const double N_VERT = wgs84::A / std::sqrt(1.0 - wgs84::E2 * SIN_LAT * SIN_LAT);
    const double X = N_VERT * COS_LAT;
    const double Z = N_VERT * (1.0 - wgs84::E2) * SIN_LAT;
    const double R = std::sqrt(X * X + Z * Z);

    // Geocentric latitude
    const double PHI_C = std::atan2(Z, X);
    const double SIN_PHI = std::sin(PHI_C);
    const double COS_PHI = std::cos(PHI_C);

    // Compute normalized associated Legendre functions Pnm(sin(phi))
    computeLegendre(SIN_PHI, COS_PHI);

    // Sum disturbing potential
    // T = (GM/r) * sum_{n=2}^{N} (a/r)^n * sum_{m=0}^{n} Pnm * (Cnm*cos(m*lon) + Snm*sin(m*lon))
    const double A_OVER_R = a_ / R;
    double aOverR_n = A_OVER_R * A_OVER_R; // Start at n=2

    double T = 0.0;

    for (int16_t n = 2; n <= N_; ++n) {
      double sumM = 0.0;
      for (int16_t m = 0; m <= n; ++m) {
        const size_t IDX = triangularOffset(n, m);
        const double P = Pnm_[IDX];
        const double COS_M_LON = std::cos(m * lon);
        const double SIN_M_LON = std::sin(m * lon);
        sumM += P * (C_[IDX] * COS_M_LON + S_[IDX] * SIN_M_LON);
      }
      T += aOverR_n * sumM;
      aOverR_n *= A_OVER_R;
    }

    T *= GM_ / R;

    // Normal gravity at this latitude
    const double GAMMA = normalGravity(lat, 0.0);

    // N = T / gamma
    return T / GAMMA;
  }

  /**
   * @brief Compute geoid undulation at geodetic position.
   *
   * @param geo Geodetic coordinates.
   * @return Geoid undulation N [m].
   * @note RT-safe after init: O(N^2), no allocation.
   */
  double undulation(const GeodeticCoord& geo) const noexcept {
    return undulation(geo.lat, geo.lon);
  }

  /**
   * @brief Convert ellipsoid height to orthometric height.
   *
   *   H = h - N
   *
   * @param lat Geodetic latitude [rad].
   * @param lon Geodetic longitude [rad].
   * @param ellipsoidHeight Height above ellipsoid [m].
   * @return Orthometric height (above MSL) [m].
   * @note RT-safe after init: O(N^2), no allocation.
   */
  double ellipsoidToOrthometric(double lat, double lon, double ellipsoidHeight) const noexcept {
    return ellipsoidHeight - undulation(lat, lon);
  }

  /**
   * @brief Convert orthometric height to ellipsoid height.
   *
   *   h = H + N
   *
   * @param lat Geodetic latitude [rad].
   * @param lon Geodetic longitude [rad].
   * @param orthometricHeight Orthometric height (above MSL) [m].
   * @return Height above ellipsoid [m].
   * @note RT-safe after init: O(N^2), no allocation.
   */
  double orthometricToEllipsoid(double lat, double lon, double orthometricHeight) const noexcept {
    return orthometricHeight + undulation(lat, lon);
  }

  /// @brief Maximum degree.
  int16_t maxDegree() const noexcept { return N_; }

private:
  double GM_ = wgs84::GM;
  double a_ = wgs84::A;
  int16_t N_ = 0;

  std::vector<double> C_;           ///< Cosine coefficients (triangular).
  std::vector<double> S_;           ///< Sine coefficients (triangular).
  mutable std::vector<double> Pnm_; ///< Legendre scratch space.

  static constexpr size_t triangularOffset(int16_t n, int16_t m) noexcept {
    return static_cast<size_t>(n) * static_cast<size_t>(n + 1) / 2 + static_cast<size_t>(m);
  }

  static constexpr size_t triangularCount(int16_t N) noexcept {
    return static_cast<size_t>(N + 1) * static_cast<size_t>(N + 2) / 2;
  }

  /**
   * @brief Compute normalized associated Legendre functions.
   *
   * Uses standard recurrence relations for fully-normalized Pnm.
   */
  void computeLegendre(double sinPhi, double cosPhi) const noexcept {
    // P00 = 1
    Pnm_[0] = 1.0;

    if (N_ < 1) {
      return;
    }

    // P10 = sqrt(3) * sin(phi)
    // P11 = sqrt(3) * cos(phi)
    Pnm_[triangularOffset(1, 0)] = std::sqrt(3.0) * sinPhi;
    Pnm_[triangularOffset(1, 1)] = std::sqrt(3.0) * cosPhi;

    // Recurrence for higher degrees
    for (int16_t n = 2; n <= N_; ++n) {
      for (int16_t m = 0; m <= n; ++m) {
        const size_t IDX = triangularOffset(n, m);

        if (m == n) {
          // Pnn = sqrt((2n+1)/(2n)) * cos(phi) * P_{n-1,n-1}
          const double FACTOR = std::sqrt(static_cast<double>(2 * n + 1) / (2 * n));
          Pnm_[IDX] = FACTOR * cosPhi * Pnm_[triangularOffset(n - 1, n - 1)];
        } else if (m == n - 1) {
          // P_{n,n-1} = sqrt(2n+1) * sin(phi) * P_{n-1,n-1}
          const double FACTOR = std::sqrt(static_cast<double>(2 * n + 1));
          Pnm_[IDX] = FACTOR * sinPhi * Pnm_[triangularOffset(n - 1, n - 1)];
        } else {
          // General recurrence:
          // Pnm = a_nm * sin(phi) * P_{n-1,m} - b_nm * P_{n-2,m}
          const double N2 = static_cast<double>(n * n);
          const double M2 = static_cast<double>(m * m);
          const double A = std::sqrt((4.0 * N2 - 1.0) / (N2 - M2));
          const double B =
              std::sqrt(((n - 1.0) * (n - 1.0) - M2) / (4.0 * (n - 1.0) * (n - 1.0) - 1.0));
          Pnm_[IDX] = A * (sinPhi * Pnm_[triangularOffset(n - 1, m)] -
                           B * Pnm_[triangularOffset(n - 2, m)]);
        }
      }
    }
  }
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_GEOID_MODEL_HPP
