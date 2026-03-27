#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_SPHERICAL_HARMONIC_MODEL_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_SPHERICAL_HARMONIC_MODEL_HPP
/**
 * @file SphericalHarmonicModel.hpp
 * @brief Generic fully-normalized spherical harmonic gravity model.
 *
 * V(r,phi,lambda) = (GM/r) Sum_{n=0..N} (a/r)^n Sum_{m=0..n} Pbar_{nm}(sin phi)
 *                   [ Cbar_{nm} cos(m lambda) + Sbar_{nm} sin(m lambda) ]
 *
 * Notes:
 *  - Input is body-fixed Cartesian (x,y,z) in meters.
 *  - Coefficients are assumed fully-normalized (barred).
 *  - No dynamic allocation in the hot path; scratch buffers are pre-sized at init().
 *  - Works for any celestial body given appropriate GM, reference radius, and coefficients.
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/GravityModelBase.hpp"

#include <cstdint>
#include <vector>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- SphericalHarmonicParams ----------------------------- */

/**
 * @brief Configuration parameters for SphericalHarmonicModel.
 *
 * These parameters are body-agnostic. Use body-specific constants from:
 * - Earth: Wgs84Constants.hpp (wgs84::GM, wgs84::A)
 * - Moon: LunarConstants.hpp (lunar::GM, lunar::R_REF)
 */
struct SphericalHarmonicParams {
  double GM = 0.0; ///< Gravitational parameter [m^3/s^2]. Required.
  double a = 0.0;  ///< Reference radius [m]. Required.
  int16_t N = 0;   ///< Max degree to use (clamped to source max).
};

/* ----------------------------- SphericalHarmonicModel ----------------------------- */

/**
 * @brief Generic spherical harmonics gravity model.
 *
 * Supports degree/order up to 2190+. Two acceleration modes available:
 * - Numeric: Finite-difference gradient of potential (slower, for validation).
 * - Analytic: Closed-form derivatives of associated Legendre functions (~4.8x faster).
 *
 * This class is body-agnostic. For body-specific wrappers, see:
 * - Earth: Egm2008Model (inc/earth/)
 * - Moon: GrailModel (inc/moon/)
 *
 * @note RT-safe after init(): No allocation in hot path, O(N^2) operations.
 */
class SphericalHarmonicModel : public GravityModelBase {
public:
  SphericalHarmonicModel() noexcept = default;
  virtual ~SphericalHarmonicModel() = default;

  // Non-copyable, movable
  SphericalHarmonicModel(const SphericalHarmonicModel&) = delete;
  SphericalHarmonicModel& operator=(const SphericalHarmonicModel&) = delete;
  SphericalHarmonicModel(SphericalHarmonicModel&&) = default;
  SphericalHarmonicModel& operator=(SphericalHarmonicModel&&) = default;

  /**
   * @brief Initialize from a coefficient source.
   * @param src Coefficient source (must outlive this model).
   * @param p Model parameters (GM, reference radius, max degree).
   * @return false if invalid params or source.
   * @note NOT RT-safe: Allocates scratch buffers.
   */
  bool init(const CoeffSource& src, const SphericalHarmonicParams& p) noexcept;

  /**
   * @brief Compute gravitational potential V at body-fixed position.
   * @param r Position in body-fixed frame [m].
   * @param V Output potential [m^2/s^2].
   * @return true on success.
   * @note RT-safe after init(): No allocation, O(N^2).
   */
  bool potential(const double r[3], double& V) const noexcept override;

  /**
   * @brief Compute acceleration at body-fixed position.
   *
   * Uses numeric gradient (default) or analytic derivatives based on accelMode().
   *
   * @param r Position in body-fixed frame [m].
   * @param a Output acceleration [m/s^2].
   * @return true on success.
   * @note RT-safe after init(): No allocation, O(N^2).
   */
  bool acceleration(const double r[3], double a[3]) const noexcept override;

  /**
   * @brief Combined evaluation: compute V and a in a single pass (reuses setup).
   * @param r Position in body-fixed frame [m].
   * @param V Output potential [m^2/s^2].
   * @param a Output acceleration [m/s^2].
   * @return true on success.
   * @note RT-safe after init(): No allocation, O(N^2).
   */
  bool evaluate(const double r[3], double& V, double a[3]) const noexcept;

  /**
   * @brief Maximum degree used by this model.
   * @note RT-safe: O(1).
   */
  int16_t maxDegree() const noexcept override { return N_; }

  /// Acceleration computation mode.
  enum class AccelMode : uint8_t { Numeric, Analytic };

  /**
   * @brief Set acceleration computation mode.
   * @param m Mode (Numeric or Analytic).
   * @note RT-safe: O(1).
   */
  void setAccelMode(AccelMode m) noexcept { accelMode_ = m; }

  /**
   * @brief Get current acceleration mode.
   * @note RT-safe: O(1).
   */
  AccelMode accelMode() const noexcept { return accelMode_; }

protected:
  /// Triangular index: k = n(n+1)/2 + m
  static constexpr inline std::size_t idx(int n, int m) noexcept {
    return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2u +
           static_cast<std::size_t>(m);
  }

  /// Access to GM for derived classes
  double gm() const noexcept { return GM_; }

  /// Access to reference radius for derived classes
  double refRadius() const noexcept { return a_; }

private:
  void computePbar(double x) const noexcept;
  void ensureScratchCapacity(int16_t N) noexcept;
  bool loadCoefficients(const CoeffSource& src, int16_t N) noexcept;
  void buildBetaTable(int16_t N) noexcept;

private:
  const CoeffSource* src_ = nullptr;
  double GM_ = 0.0;
  double a_ = 1.0;
  int16_t N_ = 0;

  mutable std::vector<double> P_;
  mutable std::vector<double> dP_;
  mutable std::vector<double> cosml_;
  mutable std::vector<double> sinml_;

  // Interleaved coefficient storage: {C[0], S[0], C[1], S[1], ...}
  // Improves cache line utilization since C[k] and S[k] are always accessed together
  std::vector<double> coeffCS_;
  std::vector<double> beta_;
  std::vector<double> recurrA_;
  std::vector<double> recurrB_;

  AccelMode accelMode_ = AccelMode::Numeric;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_SPHERICAL_HARMONIC_MODEL_HPP
