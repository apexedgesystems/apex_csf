#ifndef APEX_MATH_INTEGRATION_QUATERNION_HPP
#define APEX_MATH_INTEGRATION_QUATERNION_HPP
/**
 * @file Quaternion.hpp
 * @brief Quaternion type and integration for 6DOF attitude dynamics.
 *
 * Provides unit quaternion representation and specialized integrators
 * that maintain the unit constraint without gimbal lock.
 *
 * Use cases: Spacecraft attitude, drone orientation, IMU integration.
 * @note RT-safe: Zero allocation, fixed-size operations.
 */

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* ------------------------------- Quaternion ------------------------------- */

/**
 * @brief Unit quaternion for 3D rotations.
 *
 * Represents orientation as q = w + xi + yj + zk where |q| = 1.
 * Convention: scalar-first (w, x, y, z).
 *
 * Properties:
 *  - Singularity-free (no gimbal lock)
 *  - Smooth interpolation (SLERP)
 *  - Efficient composition (quaternion multiplication)
 *
 * @note RT-safe: Fixed-size, stack-allocated.
 */
struct Quaternion {
  double w{1.0}; ///< Scalar component (cos(theta/2)).
  double x{0.0}; ///< i component.
  double y{0.0}; ///< j component.
  double z{0.0}; ///< k component.

  /* -------------------------- Construction ------------------------------ */

  /** @brief Identity quaternion (no rotation). */
  constexpr Quaternion() noexcept = default;

  /** @brief Construct from components. */
  constexpr Quaternion(double w_, double x_, double y_, double z_) noexcept
      : w(w_), x(x_), y(y_), z(z_) {}

  /** @brief Construct from axis-angle (axis must be unit vector). */
  static Quaternion fromAxisAngle(double ax, double ay, double az, double angle) noexcept {
    const double HALF_ANGLE = 0.5 * angle;
    const double S = std::sin(HALF_ANGLE);
    const double C = std::cos(HALF_ANGLE);
    return Quaternion{C, ax * S, ay * S, az * S};
  }

  /** @brief Construct from angular velocity and small time step (first-order). */
  static Quaternion fromAngularVelocity(double wx, double wy, double wz, double dt) noexcept {
    const double ANGLE = std::sqrt(wx * wx + wy * wy + wz * wz) * dt;
    if (ANGLE < 1e-10) {
      // Small angle approximation
      const double HALF_DT = 0.5 * dt;
      return Quaternion{1.0, wx * HALF_DT, wy * HALF_DT, wz * HALF_DT};
    }
    const double HALF_ANGLE = 0.5 * ANGLE;
    const double S = std::sin(HALF_ANGLE) / (ANGLE / dt);
    const double C = std::cos(HALF_ANGLE);
    return Quaternion{C, wx * S, wy * S, wz * S};
  }

  /* -------------------------- Arithmetic -------------------------------- */

  /** @brief Quaternion multiplication (rotation composition). */
  constexpr Quaternion operator*(const Quaternion& rhs) const noexcept {
    return Quaternion{w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
                      w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
                      w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
                      w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w};
  }

  /** @brief Quaternion addition. */
  constexpr Quaternion operator+(const Quaternion& rhs) const noexcept {
    return Quaternion{w + rhs.w, x + rhs.x, y + rhs.y, z + rhs.z};
  }

  /** @brief Scalar multiplication. */
  constexpr Quaternion operator*(double scalar) const noexcept {
    return Quaternion{w * scalar, x * scalar, y * scalar, z * scalar};
  }

  /** @brief Conjugate (inverse for unit quaternions). */
  constexpr Quaternion conjugate() const noexcept { return Quaternion{w, -x, -y, -z}; }

  /** @brief Squared norm. */
  constexpr double normSq() const noexcept { return w * w + x * x + y * y + z * z; }

  /** @brief Norm. */
  double norm() const noexcept { return std::sqrt(normSq()); }

  /** @brief Normalize to unit quaternion. */
  Quaternion normalized() const noexcept {
    const double N = norm();
    if (N < 1e-12)
      return Quaternion{1.0, 0.0, 0.0, 0.0};
    const double INV_N = 1.0 / N;
    return Quaternion{w * INV_N, x * INV_N, y * INV_N, z * INV_N};
  }

  /** @brief Normalize in place. */
  void normalize() noexcept {
    const double N = norm();
    if (N < 1e-12) {
      w = 1.0;
      x = y = z = 0.0;
      return;
    }
    const double INV_N = 1.0 / N;
    w *= INV_N;
    x *= INV_N;
    y *= INV_N;
    z *= INV_N;
  }

  /* -------------------------- Rotation Operations ----------------------- */

  /**
   * @brief Rotate a vector by this quaternion.
   * @param vx, vy, vz Vector components.
   * @return Rotated vector as array {x, y, z}.
   */
  std::array<double, 3> rotate(double vx, double vy, double vz) const noexcept {
    // q * v * q^-1 optimized
    const double T2 = w * x;
    const double T3 = w * y;
    const double T4 = w * z;
    const double T5 = -x * x;
    const double T6 = x * y;
    const double T7 = x * z;
    const double T8 = -y * y;
    const double T9 = y * z;
    const double T10 = -z * z;

    return {{2.0 * ((T8 + T10) * vx + (T6 - T4) * vy + (T3 + T7) * vz) + vx,
             2.0 * ((T4 + T6) * vx + (T5 + T10) * vy + (T9 - T2) * vz) + vy,
             2.0 * ((T7 - T3) * vx + (T2 + T9) * vy + (T5 + T8) * vz) + vz}};
  }

  /**
   * @brief Convert to Euler angles (ZYX convention, radians).
   * @return {roll, pitch, yaw}.
   */
  std::array<double, 3> toEuler() const noexcept {
    // Roll (x-axis rotation)
    const double SINR_COSP = 2.0 * (w * x + y * z);
    const double COSR_COSP = 1.0 - 2.0 * (x * x + y * y);
    const double ROLL = std::atan2(SINR_COSP, COSR_COSP);

    // Pitch (y-axis rotation)
    const double SINP = 2.0 * (w * y - z * x);
    double pitch;
    if (std::abs(SINP) >= 1.0) {
      pitch = std::copysign(M_PI / 2.0, SINP); // Use 90 degrees if out of range
    } else {
      pitch = std::asin(SINP);
    }

    // Yaw (z-axis rotation)
    const double SINY_COSP = 2.0 * (w * z + x * y);
    const double COSY_COSP = 1.0 - 2.0 * (y * y + z * z);
    const double YAW = std::atan2(SINY_COSP, COSY_COSP);

    return {{ROLL, pitch, YAW}};
  }
};

/* ----------------------- QuaternionIntegrator ---------------------------- */

/**
 * @brief First-order quaternion integrator from angular velocity.
 *
 * Integrates: dq/dt = 0.5 * q * omega_quat
 * where omega_quat = (0, wx, wy, wz).
 *
 * Maintains unit constraint via normalization.
 *
 * @note RT-safe: Zero allocation, O(1) operations.
 */
class QuaternionIntegrator {
public:
  struct Stats {
    std::size_t steps{0};
    std::size_t normalizations{0};
  };

  /**
   * @brief Integrate quaternion with angular velocity (first-order).
   *
   * @param q Quaternion to update (modified in place).
   * @param wx, wy, wz Angular velocity in body frame (rad/s).
   * @param dt Time step (seconds).
   * @note RT-safe: O(1), one normalization.
   */
  void stepEuler(Quaternion& q, double wx, double wy, double wz, double dt) noexcept {
    // dq/dt = 0.5 * q * (0, wx, wy, wz)
    const double HALF_DT = 0.5 * dt;
    const double DW = -HALF_DT * (q.x * wx + q.y * wy + q.z * wz);
    const double DX = HALF_DT * (q.w * wx + q.y * wz - q.z * wy);
    const double DY = HALF_DT * (q.w * wy + q.z * wx - q.x * wz);
    const double DZ = HALF_DT * (q.w * wz + q.x * wy - q.y * wx);

    q.w += DW;
    q.x += DX;
    q.y += DY;
    q.z += DZ;

    q.normalize();
    ++stats_.steps;
    ++stats_.normalizations;
  }

  /**
   * @brief Integrate quaternion with angular velocity (second-order).
   *
   * Uses midpoint method for better accuracy.
   *
   * @param q Quaternion to update.
   * @param wx, wy, wz Angular velocity (constant over step).
   * @param dt Time step.
   */
  void stepMidpoint(Quaternion& q, double wx, double wy, double wz, double dt) noexcept {
    // Compute derivative at current state
    const double HALF_DT = 0.5 * dt;

    Quaternion qDot;
    qDot.w = -0.5 * (q.x * wx + q.y * wy + q.z * wz);
    qDot.x = 0.5 * (q.w * wx + q.y * wz - q.z * wy);
    qDot.y = 0.5 * (q.w * wy + q.z * wx - q.x * wz);
    qDot.z = 0.5 * (q.w * wz + q.x * wy - q.y * wx);

    // Midpoint state
    Quaternion qMid;
    qMid.w = q.w + qDot.w * HALF_DT;
    qMid.x = q.x + qDot.x * HALF_DT;
    qMid.y = q.y + qDot.y * HALF_DT;
    qMid.z = q.z + qDot.z * HALF_DT;

    // Derivative at midpoint
    Quaternion qDotMid;
    qDotMid.w = -0.5 * (qMid.x * wx + qMid.y * wy + qMid.z * wz);
    qDotMid.x = 0.5 * (qMid.w * wx + qMid.y * wz - qMid.z * wy);
    qDotMid.y = 0.5 * (qMid.w * wy + qMid.z * wx - qMid.x * wz);
    qDotMid.z = 0.5 * (qMid.w * wz + qMid.x * wy - qMid.y * wx);

    // Full step using midpoint derivative
    q.w += qDotMid.w * dt;
    q.x += qDotMid.x * dt;
    q.y += qDotMid.y * dt;
    q.z += qDotMid.z * dt;

    q.normalize();
    ++stats_.steps;
    ++stats_.normalizations;
  }

  /**
   * @brief Integrate using exponential map (exact for constant omega).
   *
   * For constant angular velocity, this is the exact solution.
   *
   * @param q Quaternion to update.
   * @param wx, wy, wz Angular velocity.
   * @param dt Time step.
   */
  void stepExponential(Quaternion& q, double wx, double wy, double wz, double dt) noexcept {
    // Rotation quaternion from omega * dt
    Quaternion dq = Quaternion::fromAngularVelocity(wx, wy, wz, dt);

    // Apply rotation: q_new = q * dq
    q = q * dq;
    q.normalize();
    ++stats_.steps;
    ++stats_.normalizations;
  }

  void reset() noexcept { stats_ = Stats{}; }
  const Stats& stats() const noexcept { return stats_; }

private:
  Stats stats_{};
};

/* ------------------------- Attitude6DOF ---------------------------------- */

/**
 * @brief Combined position/velocity and quaternion/angular velocity state.
 *
 * Full 6DOF state for rigid body dynamics.
 */
struct Attitude6DOF {
  // Translational state
  double px{0}, py{0}, pz{0}; ///< Position (m).
  double vx{0}, vy{0}, vz{0}; ///< Velocity (m/s).

  // Rotational state
  Quaternion q;               ///< Orientation quaternion.
  double wx{0}, wy{0}, wz{0}; ///< Angular velocity in body frame (rad/s).
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_QUATERNION_HPP
