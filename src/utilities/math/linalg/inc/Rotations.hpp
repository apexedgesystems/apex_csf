#ifndef APEX_MATH_LINALG_ROTATIONS_HPP
#define APEX_MATH_LINALG_ROTATIONS_HPP
/**
 * @file Rotations.hpp
 * @brief Header-only rotation utilities for aerospace applications.
 *
 * Design:
 *  - Provides conversion between rotation representations.
 *  - Euler angles use aerospace 3-2-1 (yaw-pitch-roll) convention.
 *  - All functions are inline, noexcept, return status codes.
 *
 * Conventions:
 *  - Euler 3-2-1: Rotate about Z (yaw), then Y (pitch), then X (roll).
 *  - DCM: Direction Cosine Matrix (rotation matrix), maps body to inertial.
 *  - Axis-angle: Uses Rodrigues formula.
 *
 * @note RT-SAFE: All functions are inline, no allocations.
 */

#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Matrix3.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <cmath>
#include <cstdint>

namespace apex {
namespace math {
namespace linalg {

/* ----------------------------- Euler to DCM ----------------------------- */

/**
 * @brief Construct DCM from Euler angles (3-2-1 / yaw-pitch-roll sequence).
 *
 * The resulting DCM transforms from body to inertial frame.
 *
 * @param roll Rotation about X-axis in radians.
 * @param pitch Rotation about Y-axis in radians.
 * @param yaw Rotation about Z-axis in radians.
 * @param dcm Output 3x3 rotation matrix.
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 */
template <typename T>
inline uint8_t dcmFromEuler321Into(T roll, T pitch, T yaw, Matrix3<T>& dcm) noexcept {
  if (dcm.rows() != 3 || dcm.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  const T CR = std::cos(roll);
  const T SR = std::sin(roll);
  const T CP = std::cos(pitch);
  const T SP = std::sin(pitch);
  const T CY = std::cos(yaw);
  const T SY = std::sin(yaw);

  // DCM = Rz(yaw) * Ry(pitch) * Rx(roll)
  // Row-major: dcm(i,j) = element at row i, column j
  dcm.view()(0, 0) = CY * CP;
  dcm.view()(0, 1) = CY * SP * SR - SY * CR;
  dcm.view()(0, 2) = CY * SP * CR + SY * SR;

  dcm.view()(1, 0) = SY * CP;
  dcm.view()(1, 1) = SY * SP * SR + CY * CR;
  dcm.view()(1, 2) = SY * SP * CR - CY * SR;

  dcm.view()(2, 0) = -SP;
  dcm.view()(2, 1) = CP * SR;
  dcm.view()(2, 2) = CP * CR;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ----------------------------- DCM to Euler ----------------------------- */

/**
 * @brief Extract Euler angles (3-2-1 / yaw-pitch-roll) from DCM.
 *
 * Warning: Has a singularity at pitch = +/- 90 degrees (gimbal lock).
 *
 * @param dcm Input 3x3 rotation matrix.
 * @param roll Output rotation about X-axis in radians.
 * @param pitch Output rotation about Y-axis in radians.
 * @param yaw Output rotation about Z-axis in radians.
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 */
template <typename T>
inline uint8_t eulerFromDcm321Into(const Matrix3<T>& dcm, T& roll, T& pitch, T& yaw) noexcept {
  if (dcm.rows() != 3 || dcm.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  const T R20 = dcm.view()(2, 0);

  // Pitch from element (2,0) = -sin(pitch)
  // Clamp to avoid domain errors in asin
  T sp = -R20;
  if (sp > T(1)) {
    sp = T(1);
  }
  if (sp < T(-1)) {
    sp = T(-1);
  }
  pitch = std::asin(sp);

  // Check for gimbal lock (pitch near +/- 90 degrees)
  const T CP = std::cos(pitch);
  const T EPSILON = std::numeric_limits<T>::epsilon() * T(100);

  if (std::abs(CP) > EPSILON) {
    // Normal case
    roll = std::atan2(dcm.view()(2, 1), dcm.view()(2, 2));
    yaw = std::atan2(dcm.view()(1, 0), dcm.view()(0, 0));
  } else {
    // Gimbal lock: assume roll = 0 and solve for yaw
    roll = T(0);
    yaw = std::atan2(-dcm.view()(0, 1), dcm.view()(1, 1));
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* --------------------------- Axis-Angle to DCM -------------------------- */

/**
 * @brief Construct DCM from axis-angle representation (Rodrigues formula).
 *
 * The axis vector should be normalized. If not normalized, behavior is undefined.
 *
 * @param axis 3-element unit vector representing rotation axis.
 * @param angle Rotation angle in radians.
 * @param dcm Output 3x3 rotation matrix.
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 */
template <typename T>
inline uint8_t dcmFromAxisAngleInto(const Vector<T>& axis, T angle, Matrix3<T>& dcm) noexcept {
  if (axis.size() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  if (dcm.rows() != 3 || dcm.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // Rodrigues formula: R = I + sin(theta)*K + (1-cos(theta))*K^2
  // where K is the skew-symmetric matrix of the axis
  const T* ax = axis.data();
  const T X = ax[0];
  const T Y = ax[1];
  const T Z = ax[2];

  const T C = std::cos(angle);
  const T S = std::sin(angle);
  const T T1 = T(1) - C;

  // Direct Rodrigues formula expansion
  dcm.view()(0, 0) = C + X * X * T1;
  dcm.view()(0, 1) = X * Y * T1 - Z * S;
  dcm.view()(0, 2) = X * Z * T1 + Y * S;

  dcm.view()(1, 0) = Y * X * T1 + Z * S;
  dcm.view()(1, 1) = C + Y * Y * T1;
  dcm.view()(1, 2) = Y * Z * T1 - X * S;

  dcm.view()(2, 0) = Z * X * T1 - Y * S;
  dcm.view()(2, 1) = Z * Y * T1 + X * S;
  dcm.view()(2, 2) = C + Z * Z * T1;

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* --------------------------- DCM to Axis-Angle -------------------------- */

/**
 * @brief Extract axis-angle representation from DCM.
 *
 * @param dcm Input 3x3 rotation matrix.
 * @param axis Output 3-element unit vector representing rotation axis.
 * @param angle Output rotation angle in radians [0, pi].
 * @return Status::SUCCESS on success, ERROR_SIZE_MISMATCH otherwise.
 * @note RT-SAFE: No allocation.
 * @note When angle is near 0 or pi, the axis is poorly defined.
 */
template <typename T>
inline uint8_t axisAngleFromDcmInto(const Matrix3<T>& dcm, Vector<T>& axis, T& angle) noexcept {
  if (dcm.rows() != 3 || dcm.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }
  if (axis.size() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // trace(R) = 1 + 2*cos(theta)
  T tr = T(0);
  (void)dcm.traceInto(tr);

  T cosTheta = (tr - T(1)) / T(2);
  if (cosTheta > T(1)) {
    cosTheta = T(1);
  }
  if (cosTheta < T(-1)) {
    cosTheta = T(-1);
  }

  angle = std::acos(cosTheta);

  const T EPSILON = std::numeric_limits<T>::epsilon() * T(100);

  if (std::abs(angle) < EPSILON) {
    // Near identity: axis is arbitrary, use default
    axis.data()[0] = T(1);
    axis.data()[1] = T(0);
    axis.data()[2] = T(0);
  } else if (std::abs(angle - T(M_PI)) < EPSILON) {
    // Near 180 degrees: extract axis from R + I (diagonal elements)
    T x2 = (dcm.view()(0, 0) + T(1)) / T(2);
    T y2 = (dcm.view()(1, 1) + T(1)) / T(2);
    T z2 = (dcm.view()(2, 2) + T(1)) / T(2);

    if (x2 < T(0))
      x2 = T(0);
    if (y2 < T(0))
      y2 = T(0);
    if (z2 < T(0))
      z2 = T(0);

    T x = std::sqrt(x2);
    T y = std::sqrt(y2);
    T z = std::sqrt(z2);

    // Determine signs from off-diagonal elements
    if (dcm.view()(0, 1) < T(0)) {
      y = -y;
    }
    if (dcm.view()(0, 2) < T(0)) {
      z = -z;
    }

    axis.data()[0] = x;
    axis.data()[1] = y;
    axis.data()[2] = z;
  } else {
    // General case: axis from skew-symmetric part
    const T DENOM = T(2) * std::sin(angle);
    axis.data()[0] = (dcm.view()(2, 1) - dcm.view()(1, 2)) / DENOM;
    axis.data()[1] = (dcm.view()(0, 2) - dcm.view()(2, 0)) / DENOM;
    axis.data()[2] = (dcm.view()(1, 0) - dcm.view()(0, 1)) / DENOM;
  }

  return static_cast<uint8_t>(Status::SUCCESS);
}

/* ------------------------- Small Angle Rotation ------------------------- */

/**
 * @brief Construct DCM for small rotation angles (first-order approximation).
 *
 * For angles << 1, sin(x) ~ x and cos(x) ~ 1 - x^2/2.
 * This approximation is useful for perturbation analysis.
 *
 * @param roll Small rotation about X-axis in radians.
 * @param pitch Small rotation about Y-axis in radians.
 * @param yaw Small rotation about Z-axis in radians.
 * @param dcm Output 3x3 rotation matrix.
 * @return Status::SUCCESS on success.
 * @note RT-SAFE: No allocation.
 */
template <typename T>
inline uint8_t dcmFromSmallAnglesInto(T roll, T pitch, T yaw, Matrix3<T>& dcm) noexcept {
  if (dcm.rows() != 3 || dcm.cols() != 3) {
    return static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH);
  }

  // First-order approximation: sin(x) ~ x, cos(x) ~ 1
  dcm.view()(0, 0) = T(1);
  dcm.view()(0, 1) = -yaw;
  dcm.view()(0, 2) = pitch;

  dcm.view()(1, 0) = yaw;
  dcm.view()(1, 1) = T(1);
  dcm.view()(1, 2) = -roll;

  dcm.view()(2, 0) = -pitch;
  dcm.view()(2, 1) = roll;
  dcm.view()(2, 2) = T(1);

  return static_cast<uint8_t>(Status::SUCCESS);
}

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ROTATIONS_HPP
