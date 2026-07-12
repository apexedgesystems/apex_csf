#ifndef APEX_HIL_DEMO_DRAG_MODEL_HPP
#define APEX_HIL_DEMO_DRAG_MODEL_HPP
/**
 * @file DragModel.hpp
 * @brief Simple exponential atmosphere drag model for the HIL plant.
 *
 * Computes aerodynamic drag force using an exponential atmosphere model
 * and a constant drag coefficient / reference area.
 *
 * @note NOT RT-safe: Uses double precision, intended for POSIX host only.
 */

#include "PointMassDynamics.hpp"

namespace appsim {
namespace plant {

/* ----------------------------- DragModel ----------------------------- */

/**
 * @class DragModel
 * @brief Exponential atmosphere + aerodynamic drag.
 *
 * Atmosphere: rho(h) = rho0 * exp(-h / scaleHeight)
 * Drag force: F = -0.5 * rho * Cd * A * |v|^2 * v_hat
 *
 * @note NOT RT-safe: double precision, POSIX host only.
 */
class DragModel {
public:
  /**
   * @brief Initialize drag model parameters.
   * @param cd Drag coefficient (dimensionless).
   * @param refArea Reference area [m^2].
   * @param rho0 Sea-level air density [kg/m^3].
   * @param scaleHeight Atmospheric scale height [m].
   */
  void init(double cd, double refArea, double rho0 = 1.225, double scaleHeight = 8500.0) noexcept;

  /**
   * @brief Compute drag force given altitude and velocity.
   * @param altitude Altitude above reference [m].
   * @param velocity Velocity vector [m/s].
   * @return Drag force vector [N] (opposes velocity).
   */
  [[nodiscard]] Vec3d computeDrag(double altitude, const Vec3d& velocity) const noexcept;

  /**
   * @brief Compute air density at a given altitude.
   * @param altitude Altitude above reference [m].
   * @return Air density [kg/m^3].
   */
  [[nodiscard]] double density(double altitude) const noexcept;

private:
  double cd_{0.5};
  double refArea_{1.0};
  double rho0_{1.225};
  double scaleHeight_{8500.0};
};

} // namespace plant
} // namespace appsim

#endif // APEX_HIL_DEMO_DRAG_MODEL_HPP
