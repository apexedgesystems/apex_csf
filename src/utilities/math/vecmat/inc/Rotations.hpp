#ifndef APEX_MATH_VECMAT_ROTATIONS_HPP
#define APEX_MATH_VECMAT_ROTATIONS_HPP
/**
 * @file Rotations.hpp
 * @brief DCM constructors and extractors over raw row-major T[9] arrays.
 *
 * Conventions (one statement, referenced by consumers instead of restated):
 *  - Euler 3-2-1: rotate about Z (yaw), then Y (pitch), then X (roll).
 *  - DCM maps BODY to INERTIAL: v_i = R * v_b, R = Rz(yaw) Ry(pitch) Rx(roll).
 *  - Row-major storage, matching Quaternion<T>::toRotationMatrixInto.
 *  - Wind axes: x_w along the velocity vector, z_w in the lift plane
 *    (positive lift = -z_w), per the aero force convention F_w = (-D, Y, -L).
 *
 * Quaternion <-> DCM conversion lives with the quaternion library
 * (toRotationMatrixInto); this header owns the angle-parameterized forms.
 *
 * @note RT-SAFE: All functions noexcept, no allocation.
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/math/vecmat/inc/VecmatStatus.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace vecmat {

/** @brief dcm = Rz(yaw) * Ry(pitch) * Rx(roll)  (body -> inertial). */
template <typename T>
inline void dcmFromEuler321Into(T rollRad, T pitchRad, T yawRad, T* dcm) noexcept {
  const T CR = apex::compat::cos(rollRad);
  const T SR = apex::compat::sin(rollRad);
  const T CP = apex::compat::cos(pitchRad);
  const T SP = apex::compat::sin(pitchRad);
  const T CY = apex::compat::cos(yawRad);
  const T SY = apex::compat::sin(yawRad);

  dcm[0] = CP * CY;
  dcm[1] = SR * SP * CY - CR * SY;
  dcm[2] = CR * SP * CY + SR * SY;
  dcm[3] = CP * SY;
  dcm[4] = SR * SP * SY + CR * CY;
  dcm[5] = CR * SP * SY - SR * CY;
  dcm[6] = -SP;
  dcm[7] = SR * CP;
  dcm[8] = CR * CP;
}

/**
 * @brief Extract 3-2-1 Euler angles from a body->inertial DCM.
 *
 * At the gimbal singularity (|dcm[6]| >= 1) pitch clamps to +/-pi/2, roll is
 * reported 0, and yaw absorbs the in-plane rotation.
 */
template <typename T>
inline void euler321FromDcmInto(const T* dcm, T& rollRad, T& pitchRad, T& yawRad) noexcept {
  const T SP = -dcm[6];
  if (apex::compat::fabs(SP) >= T(1)) {
    pitchRad = apex::compat::copysign(T(1.5707963267948966), SP);
    rollRad = T(0);
    yawRad = apex::compat::atan2(-dcm[1], dcm[4]);
    return;
  }
  pitchRad = apex::compat::asin(SP);
  rollRad = apex::compat::atan2(dcm[7], dcm[8]);
  yawRad = apex::compat::atan2(dcm[3], dcm[0]);
}

/**
 * @brief Rodrigues: dcm rotating by angleRad about the unit axis (ax, ay, az).
 */
template <typename T>
inline void dcmFromAxisAngleInto(T ax, T ay, T az, T angleRad, T* dcm) noexcept {
  const T C = apex::compat::cos(angleRad);
  const T S = apex::compat::sin(angleRad);
  const T V = T(1) - C; // versine

  dcm[0] = C + ax * ax * V;
  dcm[1] = ax * ay * V - az * S;
  dcm[2] = ax * az * V + ay * S;
  dcm[3] = ay * ax * V + az * S;
  dcm[4] = C + ay * ay * V;
  dcm[5] = ay * az * V - ax * S;
  dcm[6] = az * ax * V - ay * S;
  dcm[7] = az * ay * V + ax * S;
  dcm[8] = C + az * az * V;
}

/**
 * @brief Wind -> body DCM from angle of attack and sideslip.
 *
 * R_bw such that F_body = R_bw * F_wind with F_wind = (-D, Y, -L):
 *
 *   [ ca*cb  -ca*sb  -sa ]
 *   [ sb      cb      0  ]
 *   [ sa*cb  -sa*sb   ca ]
 */
template <typename T> inline void dcmWindToBodyInto(T alphaRad, T betaRad, T* dcm) noexcept {
  const T CA = apex::compat::cos(alphaRad);
  const T SA = apex::compat::sin(alphaRad);
  const T CB = apex::compat::cos(betaRad);
  const T SB = apex::compat::sin(betaRad);

  dcm[0] = CA * CB;
  dcm[1] = -CA * SB;
  dcm[2] = -SA;
  dcm[3] = SB;
  dcm[4] = CB;
  dcm[5] = T(0);
  dcm[6] = SA * CB;
  dcm[7] = -SA * SB;
  dcm[8] = CA;
}

} // namespace vecmat
} // namespace math
} // namespace apex

#endif // APEX_MATH_VECMAT_ROTATIONS_HPP
