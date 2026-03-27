#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_MOON_GRAIL_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_MOON_GRAIL_MODEL_HPP
/**
 * @file GrailModel.hpp
 * @brief Lunar GRGM1200A spherical harmonic gravity model.
 *
 * This is the Moon-specific wrapper around SphericalHarmonicModel using
 * GRAIL mission coefficients and lunar reference frame constants.
 *
 * Notes:
 *  - Input is MCMF (Moon-Centered Moon-Fixed) (x,y,z) in meters.
 *  - Coefficients are GRGM1200A fully-normalized (barred).
 *  - For Earth gravity, see Egm2008Model in inc/earth/.
 *
 * Data source: NASA GRAIL mission (Lemoine et al., 2014)
 * https://pgda.gsfc.nasa.gov/products/50
 */

#include "src/sim/environment/gravity/inc/SphericalHarmonicModel.hpp"
#include "src/sim/environment/gravity/inc/moon/LunarConstants.hpp"

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- GrailParams ----------------------------- */

/**
 * @brief Configuration parameters for GrailModel (Moon).
 *
 * Defaults to GRGM1200A/lunar reference constants.
 */
struct GrailParams {
  double GM = lunar::GM;   ///< Moon gravitational parameter [m^3/s^2].
  double a = lunar::R_REF; ///< Lunar reference radius [m].
  int16_t N = 360;         ///< Max degree (default 360, max 1200).
};

/* ----------------------------- GrailModel ----------------------------- */

/**
 * @brief Lunar GRGM1200A spherical harmonics gravity model.
 *
 * Inherits from SphericalHarmonicModel, providing Moon-specific defaults.
 * Supports degree/order up to 1200 using GRAIL mission coefficients.
 *
 * @note RT-safe after init(): No allocation in hot path, O(N^2) operations.
 *
 * @example
 * @code
 * FullTableCoeffSource src;
 * src.open("grgm1200a_full.bin");
 *
 * GrailModel model;
 * GrailParams params;  // Uses lunar defaults
 * params.N = 360;
 * model.init(src, params);
 *
 * const double R[3] = {2000e3, 0.0, 0.0};  // MCMF position (selenocentric)
 * double V = 0.0, a[3] = {};
 * model.evaluate(R, V, a);
 * @endcode
 */
class GrailModel final : public SphericalHarmonicModel {
public:
  GrailModel() noexcept = default;
  ~GrailModel() override = default;

  // Inherit move semantics
  GrailModel(GrailModel&&) = default;
  GrailModel& operator=(GrailModel&&) = default;

  /**
   * @brief Initialize with Moon-specific parameters.
   * @param src GRAIL coefficient source (must outlive this model).
   * @param p Lunar model parameters (defaults to GRGM1200A).
   * @return false if invalid params or source.
   * @note NOT RT-safe: Allocates scratch buffers.
   */
  bool init(const CoeffSource& src, const GrailParams& p) noexcept {
    SphericalHarmonicParams base;
    base.GM = p.GM;
    base.a = p.a;
    base.N = p.N;
    return SphericalHarmonicModel::init(src, base);
  }

  /**
   * @brief Initialize with default lunar parameters.
   * @param src GRAIL coefficient source.
   * @param maxDegree Maximum degree to use.
   * @return false if invalid source.
   * @note NOT RT-safe: Allocates scratch buffers.
   */
  bool init(const CoeffSource& src, int16_t maxDegree = 360) noexcept {
    GrailParams p;
    p.N = maxDegree;
    return init(src, p);
  }
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_MOON_GRAIL_MODEL_HPP
