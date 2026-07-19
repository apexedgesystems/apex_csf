#ifndef APEX_MATH_QUATERNION_INTEGRATOR_HPP
#define APEX_MATH_QUATERNION_INTEGRATOR_HPP
/**
 * @file QuaternionIntegrator.hpp
 * @brief Attitude integration steps for dq/dt = 0.5 * q * (0, omega).
 *
 * Three fidelity rungs over the same kinematics, all renormalizing:
 *  - stepEuler       first-order; the cheapest per-tick update.
 *  - stepMidpoint    second-order (midpoint derivative); better for larger
 *                    dt * |omega| products.
 *  - stepExponential exact for omega constant over the step (exponential
 *                    map: q <- q * exp(0.5 * omega * dt)).
 *
 * All steps take the body-frame angular velocity as a 3-array [wx, wy, wz]
 * (rad/s) and operate on a Quaternion<T> view, so the same code drives an
 * owned QuatData<T>, a state-block field, or a bus-frame slot.
 *
 * @note RT-SAFE: All operations noexcept, no allocations, O(1).
 */

#include "src/utilities/compatibility/inc/compat_math.hpp"
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionStatus.hpp"

#include <stdint.h>

namespace apex {
namespace math {
namespace quaternion {

/* -------------------------- QuaternionIntegrator -------------------------- */

/**
 * @brief Stateless attitude-integration steps (templated, view-based).
 *
 * @tparam T Element type (float or double).
 */
template <typename T> class QuaternionIntegrator {
public:
  /**
   * @brief Incremental rotation from a constant angular velocity over dt.
   *
   * out = exp(0.5 * omega * dt): the exponential-map primitive, with the
   * small-angle series below ~1e-10 rad to avoid 0/0.
   *
   * @param omegaBody [wx, wy, wz] body rates (rad/s).
   * @param dt        Step (s).
   * @param out       Resulting unit delta rotation.
   * @note RT-SAFE: No allocation.
   */
  static uint8_t deltaInto(const T* omegaBody, T dt, Quaternion<T>& out) noexcept {
    const T WX = omegaBody[0], WY = omegaBody[1], WZ = omegaBody[2];
    const T ANGLE = apex::compat::sqrt(WX * WX + WY * WY + WZ * WZ) * dt;
    if (ANGLE < T(1e-10)) {
      const T HALF_DT = T(0.5) * dt;
      return out.set(T(1), WX * HALF_DT, WY * HALF_DT, WZ * HALF_DT);
    }
    const T HALF_ANGLE = T(0.5) * ANGLE;
    const T S = apex::compat::sin(HALF_ANGLE) / (ANGLE / dt);
    return out.set(apex::compat::cos(HALF_ANGLE), WX * S, WY * S, WZ * S);
  }

  /**
   * @brief First-order step: q += 0.5 * q * (0, omega) * dt, then normalize.
   * @note RT-SAFE: No allocation.
   */
  static uint8_t stepEuler(Quaternion<T>& q, const T* omegaBody, T dt) noexcept {
    T dq[4];
    rate(q, omegaBody, dq);
    q.w() += dq[0] * dt;
    q.x() += dq[1] * dt;
    q.y() += dq[2] * dt;
    q.z() += dq[3] * dt;
    return q.normalizeInPlace();
  }

  /**
   * @brief Second-order (midpoint) step, then normalize.
   * @note RT-SAFE: No allocation.
   */
  static uint8_t stepMidpoint(Quaternion<T>& q, const T* omegaBody, T dt) noexcept {
    const T HALF_DT = T(0.5) * dt;

    T dq[4];
    rate(q, omegaBody, dq);

    T mid[4] = {q.w() + dq[0] * HALF_DT, q.x() + dq[1] * HALF_DT, q.y() + dq[2] * HALF_DT,
                q.z() + dq[3] * HALF_DT};
    Quaternion<T> qMid(mid);
    T dqMid[4];
    rate(qMid, omegaBody, dqMid);

    q.w() += dqMid[0] * dt;
    q.x() += dqMid[1] * dt;
    q.y() += dqMid[2] * dt;
    q.z() += dqMid[3] * dt;
    return q.normalizeInPlace();
  }

  /**
   * @brief Exponential-map step: q <- q * delta(omega, dt), then normalize.
   *
   * Exact when omega is constant over the step.
   * @note RT-SAFE: No allocation.
   */
  static uint8_t stepExponential(Quaternion<T>& q, const T* omegaBody, T dt) noexcept {
    T dqData[4];
    Quaternion<T> dq(dqData);
    uint8_t rc = deltaInto(omegaBody, dt, dq);
    if (rc != static_cast<uint8_t>(Status::SUCCESS)) {
      return rc;
    }
    T outData[4];
    Quaternion<T> out(outData);
    rc = q.multiplyInto(dq, out);
    if (rc != static_cast<uint8_t>(Status::SUCCESS)) {
      return rc;
    }
    (void)q.set(out.w(), out.x(), out.y(), out.z());
    return q.normalizeInPlace();
  }

  /**
   * @brief dq/dt = 0.5 * q * (0, omega), written to dq[4] [w,x,y,z].
   *
   * The attitude kinematics rate itself, for consumers that integrate
   * additively through their own scheme (an RK4 state template) and
   * normalize on their own terms; the step functions above wrap it.
   * @note RT-SAFE: No allocation.
   */
  static void rateInto(const Quaternion<T>& q, const T* omegaBody, T* dq) noexcept {
    rate(q, omegaBody, dq);
  }

private:
  /** @brief dq/dt = 0.5 * q * (0, omega), written to dq[4]. */
  static void rate(const Quaternion<T>& q, const T* w, T* dq) noexcept {
    dq[0] = T(-0.5) * (q.x() * w[0] + q.y() * w[1] + q.z() * w[2]);
    dq[1] = T(0.5) * (q.w() * w[0] + q.y() * w[2] - q.z() * w[1]);
    dq[2] = T(0.5) * (q.w() * w[1] + q.z() * w[0] - q.x() * w[2]);
    dq[3] = T(0.5) * (q.w() * w[2] + q.x() * w[1] - q.y() * w[0]);
  }
};

} // namespace quaternion
} // namespace math
} // namespace apex

#endif // APEX_MATH_QUATERNION_INTEGRATOR_HPP
