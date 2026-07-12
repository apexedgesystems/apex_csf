/**
 * @file DragModel.cpp
 * @brief Exponential atmosphere drag model implementation.
 */

#include "DragModel.hpp"

#include <cmath>

namespace appsim {
namespace plant {

/* ----------------------------- DragModel Methods ----------------------------- */

void DragModel::init(double cd, double refArea, double rho0, double scaleHeight) noexcept {
  cd_ = cd;
  refArea_ = refArea;
  rho0_ = rho0;
  scaleHeight_ = scaleHeight;
}

Vec3d DragModel::computeDrag(double altitude, const Vec3d& velocity) const noexcept {
  const double SPEED = velocity.magnitude();
  if (SPEED < 1.0e-12) {
    return {0.0, 0.0, 0.0};
  }

  const double RHO = density(altitude);
  const double DRAG_MAG = 0.5 * RHO * cd_ * refArea_ * SPEED * SPEED;

  // Drag opposes velocity
  const Vec3d V_HAT = velocity.normalized();
  return V_HAT * (-DRAG_MAG);
}

double DragModel::density(double altitude) const noexcept {
  if (altitude < 0.0) {
    return rho0_;
  }
  return rho0_ * std::exp(-altitude / scaleHeight_);
}

} // namespace plant
} // namespace appsim
