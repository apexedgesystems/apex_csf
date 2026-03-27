#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_SOURCE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_SOURCE_HPP
/**
 * @file CoeffSource.hpp
 * @brief Read-only source of fully-normalized spherical harmonic coefficients.
 *
 * Contract:
 *  - Degrees/orders satisfy 0 <= m <= n.
 *  - get(n,m, C, S) returns fully-normalized (barred) coefficients: Cbar_nm, Sbar_nm.
 *  - minDegree()/maxDegree() bound the available (n,m).
 */

#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- CoeffSource ----------------------------- */

/**
 * @brief Abstract interface for spherical harmonic coefficient sources.
 *
 * Implementations may read from binary files, memory tables, or other sources.
 */
struct CoeffSource {
  virtual ~CoeffSource() = default;

  /**
   * @brief Minimum degree available from this source.
   * @return Minimum degree n.
   * @note RT-safe: O(1).
   */
  virtual int16_t minDegree() const noexcept = 0;

  /**
   * @brief Maximum degree available from this source.
   * @return Maximum degree n.
   * @note RT-safe: O(1).
   */
  virtual int16_t maxDegree() const noexcept = 0;

  /**
   * @brief Retrieve fully-normalized coefficients Cbar_nm and Sbar_nm.
   * @param n Degree (0 <= n <= maxDegree).
   * @param m Order (0 <= m <= n).
   * @param C Output Cbar_nm.
   * @param S Output Sbar_nm.
   * @return true if coefficients found.
   * @note RT-safety depends on implementation.
   */
  virtual bool get(int16_t n, int16_t m, double& C, double& S) const noexcept = 0;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_SOURCE_HPP
