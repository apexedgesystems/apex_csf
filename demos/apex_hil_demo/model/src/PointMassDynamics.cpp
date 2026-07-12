/**
 * @file PointMassDynamics.cpp
 * @brief 3DOF point-mass dynamics implementation.
 */

#include "PointMassDynamics.hpp"

#include <cmath>

namespace appsim {
namespace plant {

/* ----------------------------- Vec3d Methods ----------------------------- */

Vec3d Vec3d::operator+(const Vec3d& rhs) const noexcept {
  return {x + rhs.x, y + rhs.y, z + rhs.z};
}

Vec3d Vec3d::operator-(const Vec3d& rhs) const noexcept {
  return {x - rhs.x, y - rhs.y, z - rhs.z};
}

Vec3d Vec3d::operator*(double s) const noexcept { return {x * s, y * s, z * s}; }

double Vec3d::magnitude() const noexcept { return std::sqrt(x * x + y * y + z * z); }

Vec3d Vec3d::normalized() const noexcept {
  const double MAG = magnitude();
  if (MAG < 1.0e-12) {
    return {0.0, 0.0, 0.0};
  }
  return {x / MAG, y / MAG, z / MAG};
}

/* ----------------------------- PointMassDynamics Methods ----------------------------- */

void PointMassDynamics::init(double mass) noexcept {
  mass_ = mass;
  pos_ = {};
  vel_ = {};
  accel_ = {};
  stepCount_ = 0;
}

void PointMassDynamics::step(double dt, const Vec3d& thrust, const Vec3d& gravity,
                             const Vec3d& drag) noexcept {
  // Total acceleration: a = gravity + (thrust + drag) / mass
  accel_ = gravity + (thrust + drag) * (1.0 / mass_);

  // Symplectic Euler: update velocity first, then position
  vel_ = vel_ + accel_ * dt;
  pos_ = pos_ + vel_ * dt;

  ++stepCount_;
}

void PointMassDynamics::reset(const Vec3d& pos0, const Vec3d& vel0) noexcept {
  pos_ = pos0;
  vel_ = vel0;
  accel_ = {};
  stepCount_ = 0;
}

} // namespace plant
} // namespace appsim
