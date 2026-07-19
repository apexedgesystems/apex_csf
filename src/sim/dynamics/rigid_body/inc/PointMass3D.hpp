#ifndef APEX_SIM_DYNAMICS_RIGID_BODY_POINT_MASS_3D_HPP
#define APEX_SIM_DYNAMICS_RIGID_BODY_POINT_MASS_3D_HPP
/**
 * @file PointMass3D.hpp
 * @brief 6-state 3D translational point-mass dynamics.
 *
 * State:
 *   - position  (m, inertial frame, X/Y/Z)
 *   - velocity  (m/s, inertial frame, X/Y/Z)
 *
 * Equations of motion (Newton's second law):
 *   p_dot = v
 *   v_dot = F / m
 *
 * Heading / flight-path angle / bank are NOT integrated state -- the
 * caller computes them from velocity (or maintains them as auxiliary
 * inputs to the force model). For full quaternion attitude, use
 * RigidBody6DOF instead.
 *
 * The State struct supports `+` and `*scalar` so it plugs directly into
 * `sim::dynamics::integrators::stepRK4` / `stepForwardEuler`.
 */

#include "src/sim/dynamics/integrators/inc/RK4.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"

#include <array>

namespace sim::dynamics::rigid_body {

/** 3D vector helper used inside State for clarity at the call site. */
struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double k) const { return {x * k, y * k, z * k}; }
};

/* ------------------------------ Vec3 helpers ------------------------------ */
// Thin Vec3 adapters over the tier-S vecmat implementations: the math has
// exactly one home; these keep the struct ergonomics at the call sites.

/** Cross product a x b (right-handed). */
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  const double A[3] = {a.x, a.y, a.z};
  const double B[3] = {b.x, b.y, b.z};
  double o[3];
  apex::math::vecmat::cross(A, B, o);
  return {o[0], o[1], o[2]};
}

/** Dot product a . b. */
inline double dot(const Vec3& a, const Vec3& b) {
  const double A[3] = {a.x, a.y, a.z};
  const double B[3] = {b.x, b.y, b.z};
  return apex::math::vecmat::dot(A, B);
}

/** Euclidean norm |a|. */
inline double norm(const Vec3& a) {
  const double A[3] = {a.x, a.y, a.z};
  return apex::math::vecmat::norm(A);
}

/**
 * 6-state point-mass.
 *
 * Position + velocity in an inertial (or quasi-inertial) frame -- the
 * caller chooses the frame. For an aircraft demo over a small patch the
 * frame is local-tangent (X = north, Y = east, Z = up).
 */
struct PointMass3DState {
  Vec3 position{};
  Vec3 velocity{};

  // Operators required by the integrator templates.
  PointMass3DState operator+(const PointMass3DState& o) const {
    return {position + o.position, velocity + o.velocity};
  }
  PointMass3DState operator*(double k) const { return {position * k, velocity * k}; }
};

/**
 * Computes the state derivative dx/dt for Newton's second law.
 *
 *   dp/dt = v
 *   dv/dt = F_applied / mass
 *
 * @param  state          current (position, velocity)
 * @param  applied_force  net external force in the same frame (N)
 * @param  mass           mass (kg)
 * @return state derivative
 */
inline PointMass3DState pointMass3DDerivative(const PointMass3DState& state,
                                              const Vec3& applied_force, double mass) {
  return PointMass3DState{state.velocity, applied_force * (1.0 / mass)};
}

/**
 * One-shot RK4 step. Caller supplies a force-evaluation lambda
 * `force(t, state) -> Vec3` so the integrator can sample at the four
 * stage points. `mass` is held constant across the step (constant-mass
 * approximation; for time-varying mass, recompute it between steps using
 * the mass-properties model, e.g. FuelBurnMassProperties).
 *
 * @param  state    state (mutated in place)
 * @param  force    function (t, state) -> applied_force_N
 * @param  mass_kg  constant mass for the duration of the step
 * @param  t        current time (s)
 * @param  dt       step (s)
 */
template <typename ForceFn>
inline void stepPointMass3D(PointMass3DState& state, ForceFn&& force, double mass_kg, double t,
                            double dt) {
  auto deriv = [&force, mass_kg](double tau, const PointMass3DState& s) {
    return pointMass3DDerivative(s, force(tau, s), mass_kg);
  };
  sim::dynamics::integrators::stepRK4(state, deriv, t, dt);
}

} // namespace sim::dynamics::rigid_body

#endif // APEX_SIM_DYNAMICS_RIGID_BODY_POINT_MASS_3D_HPP
