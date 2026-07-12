#ifndef APEX_HIL_DEMO_POINT_MASS_DYNAMICS_HPP
#define APEX_HIL_DEMO_POINT_MASS_DYNAMICS_HPP
/**
 * @file PointMassDynamics.hpp
 * @brief 3DOF point-mass dynamics for the HIL plant model.
 *
 * Integrates F = ma with gravity, drag, and applied thrust.
 * Uses symplectic Euler for stability (upgradeable to RK4).
 *
 * @note NOT RT-safe: Uses double precision, intended for POSIX host only.
 */

#include <cstdint>

namespace appsim {
namespace plant {

/* ----------------------------- Vec3d ----------------------------- */

/**
 * @struct Vec3d
 * @brief 3D vector using double precision for plant-side computations.
 */
struct Vec3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};

  Vec3d operator+(const Vec3d& rhs) const noexcept;
  Vec3d operator-(const Vec3d& rhs) const noexcept;
  Vec3d operator*(double s) const noexcept;
  [[nodiscard]] double magnitude() const noexcept;
  [[nodiscard]] Vec3d normalized() const noexcept;
};

/* ----------------------------- PointMassDynamics ----------------------------- */

/**
 * @class PointMassDynamics
 * @brief 3DOF point-mass vehicle dynamics.
 *
 * State: position [m] and velocity [m/s] in NED frame.
 * Forces: gravity + drag + applied thrust.
 * Integration: symplectic Euler (velocity then position).
 *
 * @note NOT RT-safe: double precision, POSIX host only.
 */
class PointMassDynamics {
public:
  /**
   * @brief Initialize dynamics with vehicle mass.
   * @param mass Vehicle mass [kg].
   */
  void init(double mass) noexcept;

  /**
   * @brief Advance state by one timestep.
   * @param dt Timestep [s].
   * @param thrust Applied thrust vector [N] (NED frame).
   * @param gravity Gravity acceleration [m/s^2] (NED frame).
   * @param drag Drag force [N] (NED frame).
   */
  void step(double dt, const Vec3d& thrust, const Vec3d& gravity, const Vec3d& drag) noexcept;

  /**
   * @brief Reset state to initial conditions.
   * @param pos0 Initial position [m].
   * @param vel0 Initial velocity [m/s].
   */
  void reset(const Vec3d& pos0, const Vec3d& vel0) noexcept;

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] const Vec3d& position() const noexcept { return pos_; }
  [[nodiscard]] const Vec3d& velocity() const noexcept { return vel_; }
  [[nodiscard]] const Vec3d& acceleration() const noexcept { return accel_; }
  [[nodiscard]] double altitude() const noexcept { return -pos_.z; }
  [[nodiscard]] double mass() const noexcept { return mass_; }
  [[nodiscard]] uint32_t stepCount() const noexcept { return stepCount_; }

private:
  Vec3d pos_{};
  Vec3d vel_{};
  Vec3d accel_{};
  double mass_{1.0};
  uint32_t stepCount_{0};
};

} // namespace plant
} // namespace appsim

#endif // APEX_HIL_DEMO_POINT_MASS_DYNAMICS_HPP
