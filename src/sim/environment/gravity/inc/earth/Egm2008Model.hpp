#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_HPP
/**
 * @file Egm2008Model.hpp
 * @brief Earth EGM2008 spherical harmonic gravity model.
 *
 * This is the Earth-specific wrapper around SphericalHarmonicModel using
 * EGM2008 coefficients and WGS84 reference frame constants.
 *
 * Notes:
 *  - Input is ECEF (x,y,z) in meters.
 *  - Coefficients are EGM2008 fully-normalized (barred).
 *  - For lunar gravity, see GrailModel in inc/moon/.
 */

#include "src/sim/environment/gravity/inc/SphericalHarmonicModel.hpp"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Egm2008Params ----------------------------- */

/**
 * @brief Configuration parameters for Egm2008Model (Earth).
 *
 * Defaults to WGS84/EGM2008 constants.
 */
struct Egm2008Params {
  double GM = wgs84::GM; ///< Earth gravitational parameter [m^3/s^2].
  double a = wgs84::A;   ///< WGS84 semi-major axis [m].
  int16_t N = 180;       ///< Max degree (default 180, max 2190).
};

/* ----------------------------- Egm2008Model ----------------------------- */

/**
 * @brief Earth EGM2008 spherical harmonics gravity model.
 *
 * Inherits from SphericalHarmonicModel, providing Earth-specific defaults.
 * Supports degree/order up to 2190 using EGM2008 coefficients.
 *
 * @note RT-safe after init(): No allocation in hot path, O(N^2) operations.
 *
 * @example
 * @code
 * FullTableCoeffSource src;
 * src.open("egm2008_full.bin");
 *
 * Egm2008Model model;
 * Egm2008Params params;  // Uses WGS84 defaults
 * params.N = 360;
 * model.init(src, params);
 *
 * const double R[3] = {7000e3, 0.0, 0.0};  // ECEF position
 * double V = 0.0, a[3] = {};
 * model.evaluate(R, V, a);
 * @endcode
 */
class Egm2008Model final : public SphericalHarmonicModel {
public:
  Egm2008Model() noexcept = default;
  ~Egm2008Model() override = default;

  // Inherit move semantics
  Egm2008Model(Egm2008Model&&) = default;
  Egm2008Model& operator=(Egm2008Model&&) = default;

  /**
   * @brief Initialize with Earth-specific parameters.
   * @param src EGM2008 coefficient source (must outlive this model).
   * @param p Earth model parameters (defaults to WGS84).
   * @return false if invalid params or source.
   * @note NOT RT-safe: Allocates scratch buffers.
   */
  bool init(const CoeffSource& src, const Egm2008Params& p) noexcept {
    SphericalHarmonicParams base;
    base.GM = p.GM;
    base.a = p.a;
    base.N = p.N;
    return SphericalHarmonicModel::init(src, base);
  }

  /**
   * @brief Initialize with default WGS84 parameters.
   * @param src EGM2008 coefficient source.
   * @param maxDegree Maximum degree to use.
   * @return false if invalid source.
   * @note NOT RT-safe: Allocates scratch buffers.
   */
  bool init(const CoeffSource& src, int16_t maxDegree = 180) noexcept {
    Egm2008Params p;
    p.N = maxDegree;
    return init(src, p);
  }
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_HPP
