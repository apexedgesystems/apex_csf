#ifndef APEX_SIM_DYNAMICS_RIGID_BODY_RIGID_BODY_6DOF_HPP
#define APEX_SIM_DYNAMICS_RIGID_BODY_RIGID_BODY_6DOF_HPP
/**
 * @file RigidBody6DOF.hpp
 * @brief 13-state 6-DOF rigid body dynamics with quaternion attitude.
 *
 * State (13 elements):
 *   - position_inertial   (m, NED or NEU per caller's choice)
 *   - velocity_body       (m/s, body frame: u forward, v right, w down)
 *   - attitude            (unit quaternion, body-to-inertial, scalar-first)
 *   - angular_velocity_body (rad/s, body frame: p roll, q pitch, r yaw)
 *
 * Equations of motion (body-axis 6-DOF, velocity resolved in the body frame):
 *   p_dot_inertial = R(q) v_body                         (kinematic)
 *   v_dot_body     = F_body / m  -  omega_body x v_body       (Newton in rotating frame)
 *   q_dot          = 0.5 q * (0, p, q, r)                  (quaternion rate)
 *   omega_dot_body = I^-1 ( M_body - omega x (I omega) )           (Euler's equations)
 *
 * The state struct supports `+` and `*scalar` element-wise so it plugs
 * directly into `stepRK4`. The quaternion is integrated additively across
 * the 4 RK stages and re-normalized once at the end of each step (standard
 * flight-sim trick: the additive error per step is O(dt^2), the
 * normalization recovers unit-magnitude).
 *
 * Inertia tensor is the full body-frame symmetric tensor
 * (Ixx, Iyy, Izz, Ixz, Ixy, Iyz). It reduces to the canonical aircraft
 * xz-symmetry case when Ixy = Iyz = 0, and to diagonal when all products
 * of inertia are 0.
 */

#include "src/sim/dynamics/integrators/inc/RK4.hpp"
#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"
#include "src/utilities/math/integration/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionIntegrator.hpp"
#include "src/utilities/math/vecmat/inc/Mat3Ops.hpp"

#include <cmath>

namespace sim::dynamics::rigid_body {

/* ----------------------------- InertiaTensor ----------------------------- */

/**
 * Full body-frame symmetric inertia tensor.
 *
 * Products of inertia use the positive convention (stored positive,
 * entering the matrix negated):
 *
 *      [ Ixx  -Ixy  -Ixz ]
 *  I = [-Ixy   Iyy  -Iyz ]
 *      [-Ixz  -Iyz   Izz ]
 *
 * Member order keeps Ixz fourth so existing xz-symmetric aggregate
 * inits like `InertiaTensor{Ixx, Iyy, Izz, Ixz}` still mean Ixz with
 * Ixy = Iyz = 0. The full form reduces exactly to the old xz-symmetric
 * behavior when Ixy = Iyz = 0, and to diagonal when all products are 0.
 *
 * Storing the full tensor makes this parallel-axis-friendly: a
 * parallel-axis shift introduces Ixy / Iyz cross terms that the old
 * 4-term form could not represent. See `mass_properties::aggregate`.
 */
struct InertiaTensor {
  double Ixx = 1.0;
  double Iyy = 1.0;
  double Izz = 1.0;
  double Ixz = 0.0;
  double Ixy = 0.0;
  double Iyz = 0.0;

  /** Row-major 3x3 form (products negated per the sign convention). */
  void matrixInto(double* m) const {
    m[0] = Ixx;
    m[1] = -Ixy;
    m[2] = -Ixz;
    m[3] = -Ixy;
    m[4] = Iyy;
    m[5] = -Iyz;
    m[6] = -Ixz;
    m[7] = -Iyz;
    m[8] = Izz;
  }

  /** Compute I * omega (matrix-vector product) for the full tensor. */
  [[nodiscard]] Vec3 multiply(const Vec3& w) const {
    double m[9], wv[3] = {w.x, w.y, w.z}, o[3];
    matrixInto(m);
    apex::math::vecmat::multiplyVec(m, wv, o);
    return {o[0], o[1], o[2]};
  }

  /**
   * Solve I * omega_dot = b for omega_dot (adjugate inverse; det > 0 for
   * any physical, positive-definite inertia tensor).
   */
  [[nodiscard]] Vec3 solve(const Vec3& b) const {
    double m[9], bv[3] = {b.x, b.y, b.z}, o[3];
    matrixInto(m);
    (void)apex::math::vecmat::solveInto(m, bv, o);
    return {o[0], o[1], o[2]};
  }
};

/* --------------------------- RigidBody6DOFState --------------------------- */

/**
 * 13-state rigid-body state: position (3) + body velocity (3) +
 * attitude quaternion (4) + body angular velocity (3).
 *
 * Operator+ / operator* are required by the integrator templates; both
 * are element-wise across all components. The driver re-normalizes the
 * attitude quaternion once per step.
 */
struct RigidBody6DOFState {
  Vec3 position_inertial{};
  Vec3 velocity_body{};
  apex::math::integration::Quaternion attitude{}; // identity by default
  Vec3 angular_velocity_body{};

  RigidBody6DOFState operator+(const RigidBody6DOFState& o) const {
    return {position_inertial + o.position_inertial, velocity_body + o.velocity_body,
            attitude + o.attitude, angular_velocity_body + o.angular_velocity_body};
  }

  RigidBody6DOFState operator*(double k) const {
    return {position_inertial * k, velocity_body * k, attitude * k, angular_velocity_body * k};
  }
};

/* ----------------------------- Derivative -------------------------------- */

/**
 * Compute dx/dt for the 13-state 6-DOF rigid body.
 *
 * The caller supplies forces and moments already resolved into the body
 * frame (gravity must be rotated from inertial, aero forces are naturally
 * body-frame in stability-derivative aero, thrust is body-frame).
 *
 * @param  s            current 13-state
 * @param  force_body   net applied force in body frame (N)
 * @param  moment_body  net applied moment about CG in body frame (N*m)
 * @param  mass_kg      mass (kg, constant for this step)
 * @param  I            inertia tensor about CG, body frame
 * @return state derivative
 */
inline RigidBody6DOFState rigidBody6DOFDerivative(const RigidBody6DOFState& s,
                                                  const Vec3& force_body, const Vec3& moment_body,
                                                  double mass_kg, const InertiaTensor& I) {
  // p_dot_inertial = R(q) * v_body
  const auto v_i = s.attitude.rotate(s.velocity_body.x, s.velocity_body.y, s.velocity_body.z);
  const Vec3 p_dot{v_i[0], v_i[1], v_i[2]};

  // v_dot_body = F/m - omega x v_body
  const Vec3 omega_cross_v = cross(s.angular_velocity_body, s.velocity_body);
  const Vec3 v_dot{force_body.x / mass_kg - omega_cross_v.x,
                   force_body.y / mass_kg - omega_cross_v.y,
                   force_body.z / mass_kg - omega_cross_v.z};

  // q_dot = 0.5 q * (0, omega): the shared attitude kinematics rate.
  double att[4] = {s.attitude.w, s.attitude.x, s.attitude.y, s.attitude.z};
  const double W[3] = {s.angular_velocity_body.x, s.angular_velocity_body.y,
                       s.angular_velocity_body.z};
  double dq[4];
  apex::math::quaternion::QuaternionIntegrator<double>::rateInto(
      apex::math::quaternion::Quaternion<double>(att), W, dq);
  const apex::math::integration::Quaternion q_dot{dq[0], dq[1], dq[2], dq[3]};

  // omega_dot = I^-1 (M - omega x (I omega))
  const Vec3 I_omega = I.multiply(s.angular_velocity_body);
  const Vec3 omega_cross_I_omega = cross(s.angular_velocity_body, I_omega);
  const Vec3 net_moment{moment_body.x - omega_cross_I_omega.x,
                        moment_body.y - omega_cross_I_omega.y,
                        moment_body.z - omega_cross_I_omega.z};
  const Vec3 omega_dot = I.solve(net_moment);

  return RigidBody6DOFState{p_dot, v_dot, q_dot, omega_dot};
}

/* -------------------------------- Driver --------------------------------- */

/**
 * One RK4 step of 13-state 6-DOF dynamics.
 *
 * @param  state      mutated in place
 * @param  force_fn   callable (t, state) -> Vec3 (body frame, N)
 * @param  moment_fn  callable (t, state) -> Vec3 (body frame, N*m)
 * @param  mass_kg    constant mass for this step
 * @param  I          body-frame inertia tensor (constant for this step)
 * @param  t          current time (s)
 * @param  dt         step (s)
 *
 * The attitude quaternion is re-normalized after the step to recover from
 * the additive drift the RK stages introduce. Drift per step is O(dt^2);
 * for dt = 0.02 s (50 Hz aircraft tick) this is ~4e-4 before correction.
 */
template <typename ForceFn, typename MomentFn>
inline void stepRigidBody6DOF(RigidBody6DOFState& state, ForceFn&& force_fn, MomentFn&& moment_fn,
                              double mass_kg, const InertiaTensor& I, double t, double dt) {
  auto deriv = [&force_fn, &moment_fn, mass_kg, &I](double tau, const RigidBody6DOFState& s) {
    return rigidBody6DOFDerivative(s, force_fn(tau, s), moment_fn(tau, s), mass_kg, I);
  };
  sim::dynamics::integrators::stepRK4(state, deriv, t, dt);
  state.attitude.normalize();
}

} // namespace sim::dynamics::rigid_body

#endif // APEX_SIM_DYNAMICS_RIGID_BODY_RIGID_BODY_6DOF_HPP
