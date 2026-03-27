#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_MODEL_BASE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_MODEL_BASE_HPP
/**
 * @file GravityModelBase.hpp
 * @brief Minimal interface for gravity models in body-centered body-fixed coordinates.
 *
 * Body-agnostic: Works for any celestial body (Earth, Moon, etc.).
 * Position input is in the body's principal-axis fixed frame.
 */

#include <cstdint>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- GravityModelBase ----------------------------- */

/**
 * @brief Abstract base class for gravity field models.
 *
 * All implementations provide potential V(r) and acceleration a(r) in the
 * body-centered body-fixed frame (ECEF for Earth, MCMF for Moon, etc.).
 */
class GravityModelBase {
public:
  virtual ~GravityModelBase() = default;

  /**
   * @brief Gravitational potential V [m^2/s^2] at position (x,y,z).
   * @param r Position in body-fixed frame [m].
   * @param V Output potential [m^2/s^2].
   * @return true on success.
   * @note RT-safety depends on implementation.
   */
  virtual bool potential(const double r[3], double& V) const noexcept = 0;

  /**
   * @brief Gravitational acceleration [m/s^2] at position (x,y,z).
   * @param r Position in body-fixed frame [m].
   * @param a Output acceleration [m/s^2].
   * @return true on success.
   * @note RT-safety depends on implementation.
   */
  virtual bool acceleration(const double r[3], double a[3]) const noexcept = 0;

  /**
   * @brief Max spherical-harmonic degree used (0 for constant model, etc.).
   * @return Maximum degree N.
   * @note RT-safe: No allocation, O(1).
   */
  virtual int16_t maxDegree() const noexcept { return 0; }
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_MODEL_BASE_HPP
