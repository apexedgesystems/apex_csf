#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_ZONAL_GRAVITY_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_ZONAL_GRAVITY_MODEL_HPP
/**
 * @file ZonalGravityModel.hpp
 * @brief Zonal-only (m=0) gravity model for axisymmetric field approximation.
 *
 * Uses only zonal harmonics (Cn0 terms) which capture the rotationally
 * symmetric part of Earth's gravity field. Faster than full spherical
 * harmonics while more accurate than J2-only for high-fidelity applications.
 */

#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- ZonalParams ----------------------------- */

/**
 * @brief Parameters for zonal gravity model.
 */
struct ZonalParams {
  double GM = wgs84::GM; ///< Gravitational constant [m^3/s^2].
  double a = wgs84::A;   ///< Reference radius (equatorial) [m].
  int16_t N = 6;         ///< Maximum degree (2-20 typical, max 20 built-in).
};

/* ----------------------------- ZonalGravityModel ----------------------------- */

/**
 * @brief Zonal-only gravity model with configurable max degree.
 *
 * Computes gravitational potential using only zonal (m=0) harmonics:
 *   V = (GM/r) * sum_{n=0}^{N} (a/r)^n * Cn0 * Pn(sin(phi))
 *
 * where Pn is the Legendre polynomial and sin(phi) = z/r.
 *
 * Built-in coefficients up to N=20 from EGM2008. For higher degrees,
 * use the full Egm2008Model.
 *
 * @note RT-safe after init(): No allocation in hot path, O(N) operations.
 */
class ZonalGravityModel final : public GravityModelBase {
public:
  /// Maximum supported degree with built-in coefficients.
  static constexpr int16_t MAX_BUILTIN_DEGREE = 20;

  ZonalGravityModel() noexcept = default;

  /**
   * @brief Initialize with parameters.
   * @param params Zonal model parameters.
   * @return true if N <= MAX_BUILTIN_DEGREE.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool init(const ZonalParams& params) noexcept {
    if (params.N < 0 || params.N > MAX_BUILTIN_DEGREE) {
      return false;
    }
    GM_ = params.GM;
    a_ = params.a;
    N_ = params.N;
    return true;
  }

  /**
   * @brief Initialize with default WGS84 parameters and specified degree.
   * @param N Maximum degree (2-20).
   * @return true if N <= MAX_BUILTIN_DEGREE.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] bool init(int16_t N = 6) noexcept {
    ZonalParams p;
    p.N = N;
    return init(p);
  }

  /**
   * @brief Compute zonal gravitational potential.
   * @param r Position in ECEF [m].
   * @param V Output potential [m^2/s^2].
   * @return true unless |r| == 0.
   * @note RT-safe: No allocation, O(N).
   */
  bool potential(const double r[3], double& V) const noexcept override {
    const double R2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
    if (R2 == 0.0) {
      V = 0.0;
      return false;
    }

    const double RMAG = std::sqrt(R2);
    const double INVR = 1.0 / RMAG;
    const double A_OVER_R = a_ * INVR;

    // sin(phi) = z/r (geocentric latitude)
    const double U = r[2] * INVR;

    // Compute Legendre polynomials P0..PN via recurrence
    // P0 = 1, P1 = u, Pn = ((2n-1)*u*P_{n-1} - (n-1)*P_{n-2}) / n
    double Pnm1 = 1.0; // P_{n-1} starting with P0
    double Pn = U;     // P_n starting with P1

    // Sum: 1 + sum_{n=2}^{N} (a/r)^n * Cn0 * Pn
    // Start with central term (C00 = 1 normalized, but using un-normalized Jn)
    double sum = 1.0;

    double aOverR_n = A_OVER_R * A_OVER_R; // (a/r)^2

    for (int16_t n = 2; n <= N_; ++n) {
      // Recurrence for Pn
      if (n > 1) {
        const double PNEW = ((2 * n - 1) * U * Pn - (n - 1) * Pnm1) / n;
        Pnm1 = Pn;
        Pn = PNEW;
      }

      // Add contribution: -(a/r)^n * Jn * Pn
      // Note: Jn = -sqrt(2n+1) * Cn0 for normalized coefficients
      // We use pre-computed un-normalized Jn values
      sum -= aOverR_n * Jn(n) * Pn;

      aOverR_n *= A_OVER_R; // (a/r)^{n+1}
    }

    V = (GM_ * INVR) * sum;
    return true;
  }

  /**
   * @brief Compute zonal gravitational acceleration via numeric gradient.
   * @param r Position in ECEF [m].
   * @param a Output acceleration [m/s^2].
   * @return true unless |r| == 0.
   * @note RT-safe: No allocation, O(N).
   */
  bool acceleration(const double r[3], double a[3]) const noexcept override {
    // Use central difference for gradient: a = -grad(V)
    constexpr double H = 1.0; // 1 meter step

    double Vxp = 0.0, Vxm = 0.0;
    double Vyp = 0.0, Vym = 0.0;
    double Vzp = 0.0, Vzm = 0.0;

    double rp[3], rm[3];

    // dV/dx
    rp[0] = r[0] + H;
    rp[1] = r[1];
    rp[2] = r[2];
    rm[0] = r[0] - H;
    rm[1] = r[1];
    rm[2] = r[2];
    if (!potential(rp, Vxp) || !potential(rm, Vxm)) {
      a[0] = a[1] = a[2] = 0.0;
      return false;
    }

    // dV/dy
    rp[0] = r[0];
    rp[1] = r[1] + H;
    rp[2] = r[2];
    rm[0] = r[0];
    rm[1] = r[1] - H;
    rm[2] = r[2];
    if (!potential(rp, Vyp) || !potential(rm, Vym)) {
      a[0] = a[1] = a[2] = 0.0;
      return false;
    }

    // dV/dz
    rp[0] = r[0];
    rp[1] = r[1];
    rp[2] = r[2] + H;
    rm[0] = r[0];
    rm[1] = r[1];
    rm[2] = r[2] - H;
    if (!potential(rp, Vzp) || !potential(rm, Vzm)) {
      a[0] = a[1] = a[2] = 0.0;
      return false;
    }

    // a = grad(V) for V = GM/r (positive potential convention)
    // V decreases with distance, so dV/dx < 0 at positive x gives negative ax
    const double INV_2H = 0.5 / H;
    a[0] = (Vxp - Vxm) * INV_2H;
    a[1] = (Vyp - Vym) * INV_2H;
    a[2] = (Vzp - Vzm) * INV_2H;

    return true;
  }

  /**
   * @brief Returns configured maximum degree.
   * @note RT-safe: O(1).
   */
  int16_t maxDegree() const noexcept override { return N_; }

private:
  double GM_ = wgs84::GM;
  double a_ = wgs84::A;
  int16_t N_ = 6;

  /**
   * @brief Get un-normalized Jn coefficient for degree n.
   *
   * EGM2008 zonal coefficients (un-normalized form).
   * J_n = -sqrt(2n+1) * C_{n0} for fully-normalized coefficients.
   */
  static double Jn(int16_t n) noexcept {
    // Un-normalized Jn values from EGM2008
    // Positive Jn means negative C_{n0} contribution to potential
    static constexpr std::array<double, 21> JN_TABLE = {{
        0.0,                // J0 (unused, C00=1 handled separately)
        0.0,                // J1 (Earth center of mass at origin)
        1.0826358191967e-3, // J2
        -2.5323516555e-6,   // J3
        -1.6196723715e-6,   // J4
        -2.2730247936e-7,   // J5
        5.4068202549e-7,    // J6
        -3.5235228055e-7,   // J7
        -2.0477893620e-7,   // J8
        -1.2002797418e-7,   // J9
        -2.4616587284e-7,   // J10
        -2.4330917982e-7,   // J11
        1.8770925752e-7,    // J12
        1.9723922274e-7,    // J13
        -1.4591071610e-8,   // J14
        1.5222710889e-8,    // J15
        -5.4525052934e-8,   // J16
        -5.3074592568e-9,   // J17
        5.4413968886e-8,    // J18
        -3.1074531821e-8,   // J19
        3.9908175909e-8,    // J20
    }};

    if (n < 0 || n > MAX_BUILTIN_DEGREE) {
      return 0.0;
    }
    return JN_TABLE[static_cast<size_t>(n)];
  }
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_ZONAL_GRAVITY_MODEL_HPP
