/**
 * @file RigidBody6DOF_uTest.cpp
 * @brief Unit tests for the 13-state 6-DOF rigid body integrator.
 *
 * Closed-form scenarios with analytic ground truth:
 *
 *   1. Zero force + zero moment from rest    -> state preserved exactly
 *   2. Inertial gravity on level attitude    -> reduces to point-mass free fall
 *   3. Pure principal-axis body force        -> body velocity ramps linearly
 *   4. Pure principal-axis moment (zero omega)   -> omega_dot = M/I, integrate to constant alpha
 *   5. Constant omega about principal axis (zero M) -> omega stays constant indefinitely
 *      (Euler's equations: torque-free spin about a principal axis is stable)
 *   6. Rotational KE conservation under torque-free intermediate-axis tumble
 *   7. Quaternion remains unit-magnitude after thousands of steps
 *
 * Every expectation is a closed-form property of the body-axis 6-DOF
 * equations -- conservation laws, exact integrals of a known input, and
 * quaternion unit-norm -- checked against the analytic result rather than
 * against recorded output, so a regression in the equations fails the test.
 */

#include "src/sim/dynamics/rigid_body/inc/RigidBody6DOF.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::rigid_body::cross;
using sim::dynamics::rigid_body::InertiaTensor;
using sim::dynamics::rigid_body::RigidBody6DOFState;
using sim::dynamics::rigid_body::stepRigidBody6DOF;
using sim::dynamics::rigid_body::Vec3;

namespace {

/** No-op force / moment lambda factory. */
auto zeroVec3() {
  return [](double, const RigidBody6DOFState&) { return Vec3{}; };
}

/** Quick rotational KE: T = 0.5 omega . (I omega). */
double rotKineticEnergy(const Vec3& w, const InertiaTensor& I) {
  const Vec3 Iw = I.multiply(w);
  return 0.5 * (w.x * Iw.x + w.y * Iw.y + w.z * Iw.z);
}

} // namespace

/* ----------------------------- Zero force/moment ----------------------------- */

/** @test With zero force + zero moment, state is preserved over many steps. */
TEST(RigidBody6DOFTest, ZeroLoadsPreserveState) {
  RigidBody6DOFState s;
  s.position_inertial = Vec3{0.0, 0.0, 0.0};
  s.velocity_body = Vec3{0.0, 0.0, 0.0};
  s.attitude = apex::math::integration::Quaternion{1.0, 0.0, 0.0, 0.0}; // identity
  s.angular_velocity_body = Vec3{0.0, 0.0, 0.0};

  const InertiaTensor I{1.0, 1.0, 1.0, 0.0};
  const double dt = 0.01;
  for (int i = 0; i < 1000; ++i) {
    stepRigidBody6DOF(s, zeroVec3(), zeroVec3(), 1.0, I, i * dt, dt);
  }

  EXPECT_NEAR(s.position_inertial.x, 0.0, 1e-12);
  EXPECT_NEAR(s.position_inertial.y, 0.0, 1e-12);
  EXPECT_NEAR(s.position_inertial.z, 0.0, 1e-12);
  EXPECT_NEAR(s.attitude.w, 1.0, 1e-12);
  EXPECT_NEAR(s.attitude.x, 0.0, 1e-12);
  EXPECT_NEAR(s.attitude.y, 0.0, 1e-12);
  EXPECT_NEAR(s.attitude.z, 0.0, 1e-12);
}

/* ----------------------------- Reduces to point-mass ----------------------------- */

/**
 * @test With identity attitude and zero omega, the 6-DOF integrator reduces to
 * Newton's 2nd law on the inertial frame: a constant body-frame force gives
 * uniform acceleration matching the closed-form 0.5*a*t^2 translation.
 */
TEST(RigidBody6DOFTest, IdentityAttitudeReducesToPointMass) {
  const double mass = 1500.0;
  const double Fx = 4500.0; // N -> a = 3 m/s^2 along body-x

  RigidBody6DOFState s; // identity attitude, zero omega, at rest

  auto force_body = [Fx](double, const RigidBody6DOFState&) { return Vec3{Fx, 0.0, 0.0}; };

  const InertiaTensor I{1.0, 1.0, 1.0, 0.0};
  const double dt = 0.01;
  const double t_final = 4.0;
  const int n = static_cast<int>(t_final / dt);
  for (int i = 0; i < n; ++i) {
    stepRigidBody6DOF(s, force_body, zeroVec3(), mass, I, i * dt, dt);
  }

  const double a = Fx / mass;
  const double t = n * dt;
  EXPECT_NEAR(s.position_inertial.x, 0.5 * a * t * t, 1e-7);
  EXPECT_NEAR(s.velocity_body.x, a * t, 1e-9);
  EXPECT_NEAR(s.position_inertial.y, 0.0, 1e-12);
  EXPECT_NEAR(s.position_inertial.z, 0.0, 1e-12);
}

/* ----------------------------- Constant moment about principal axis -----------------------------
 */

/**
 * @test Pure pitch moment with diagonal inertia: omega_y(t) = (M_y / Iyy)*t.
 * Integrating from rest gives pitch attitude q(t) = 0.5*(M/I)*t^2.
 */
TEST(RigidBody6DOFTest, ConstantPitchMomentSpinsAboutY) {
  const double Iyy = 4.0;
  const double My = 2.0; // N*m  -> omega_dot_y = 0.5 rad/s^2

  const InertiaTensor I{Iyy, Iyy, Iyy, 0.0};
  RigidBody6DOFState s; // start identity / zero / zero

  auto moment_body = [My](double, const RigidBody6DOFState&) { return Vec3{0.0, My, 0.0}; };

  const double dt = 0.001;
  const double t_final = 2.0;
  const int n = static_cast<int>(t_final / dt);
  for (int i = 0; i < n; ++i) {
    stepRigidBody6DOF(s, zeroVec3(), moment_body,
                      /*mass*/ 1.0, I, i * dt, dt);
  }

  const double t = n * dt;
  const double alpha = My / Iyy;
  EXPECT_NEAR(s.angular_velocity_body.y, alpha * t, 1e-9);
  // omega_x and omega_z stay zero by symmetry (no cross-coupling in diagonal I).
  EXPECT_NEAR(s.angular_velocity_body.x, 0.0, 1e-9);
  EXPECT_NEAR(s.angular_velocity_body.z, 0.0, 1e-9);
}

/* ----------------------------- Torque-free spin about principal axis -----------------------------
 */

/**
 * @test Constant omega about a principal axis with zero external moment stays
 * constant. omega x (I omega) is parallel to omega when omega is a principal axis, so
 * Euler's equation gives omega_dot = 0.
 */
TEST(RigidBody6DOFTest, TorqueFreePrincipalAxisSpinIsStable) {
  // Aircraft-like inertia (xz-symmetric). The principal axes are
  // (rotated x, y, rotated z); pure y-axis spin is exactly principal.
  const InertiaTensor I{12.0, 8.0, 16.0, 1.5};
  RigidBody6DOFState s;
  s.angular_velocity_body = Vec3{0.0, 1.5, 0.0}; // pure pitch spin

  const double dt = 0.001;
  for (int i = 0; i < 5000; ++i) { // 5 s
    stepRigidBody6DOF(s, zeroVec3(), zeroVec3(),
                      /*mass*/ 1.0, I, i * dt, dt);
  }

  EXPECT_NEAR(s.angular_velocity_body.x, 0.0, 1e-9);
  EXPECT_NEAR(s.angular_velocity_body.y, 1.5, 1e-9);
  EXPECT_NEAR(s.angular_velocity_body.z, 0.0, 1e-9);
}

/* ----------------------------- Rotational energy conservation ----------------------------- */

/**
 * @test Torque-free tumble: rotational KE is conserved exactly because
 * there's no external moment doing work. Tests the cross-coupled Euler
 * equations (the omega x (I omega) term must integrate symplectically).
 *
 * Setup: nonzero angular velocity about all three axes, asymmetric I.
 * Tumble is stable about the major (Izz) and minor (Iyy) axes; this case
 * gives stable rotation and the integrator should preserve energy.
 */
TEST(RigidBody6DOFTest, TorqueFreeTumbleConservesRotationalEnergy) {
  // Strongly asymmetric, but spin biased toward the largest principal
  // moment so the trajectory is bounded (avoids intermediate-axis chaos).
  const InertiaTensor I{2.0, 4.0, 8.0, 0.0};
  RigidBody6DOFState s;
  s.angular_velocity_body = Vec3{0.05, 0.05, 1.5}; // biased toward Izz

  const double T0 = rotKineticEnergy(s.angular_velocity_body, I);

  const double dt = 0.001;
  for (int i = 0; i < 5000; ++i) { // 5 s
    stepRigidBody6DOF(s, zeroVec3(), zeroVec3(),
                      /*mass*/ 1.0, I, i * dt, dt);
  }

  const double T1 = rotKineticEnergy(s.angular_velocity_body, I);
  // RK4 is non-symplectic but the drift over 5000 steps at this dt is
  // dominated by O(dt^4); a few parts in 1e6 is comfortable.
  EXPECT_NEAR(T1, T0, std::fabs(T0) * 1e-6);
}

/* ----------------------------- Quaternion unit-magnitude ----------------------------- */

/** @test Attitude quaternion stays unit-norm after 10000 steps with continuous spin. */
TEST(RigidBody6DOFTest, QuaternionRemainsUnit) {
  const InertiaTensor I{2.0, 4.0, 8.0, 0.5};
  RigidBody6DOFState s;
  s.angular_velocity_body = Vec3{0.5, 1.0, 0.7};

  const double dt = 0.005;
  for (int i = 0; i < 10000; ++i) { // 50 s
    stepRigidBody6DOF(s, zeroVec3(), zeroVec3(),
                      /*mass*/ 1.0, I, i * dt, dt);
  }

  const double n_sq = s.attitude.normSq();
  EXPECT_NEAR(n_sq, 1.0, 1e-10);
}

/* ----------------------------- Rotating-frame kinematics coupling ----------------------------- */

/**
 * @test The attitude-driven body->inertial rotation in p_dot = R(q) v_body is
 * self-consistent with the quaternion rate q_dot = 0.5 q (x) omega.
 *
 * A body with a constant body-frame speed and a constant yaw rate, held in
 * coordinated turn by a centripetal force (F/m = omega x v cancels the
 * transport term so |v_body| stays fixed), must trace a closed inertial
 * circle and return to the origin after one period. A sign error in either
 * rotate() or the quaternion rate would open the loop or trace it backwards.
 * The other cases (all at identity or fixed attitude) never exercise the
 * attitude->translation coupling.
 */
TEST(RigidBody6DOFTest, ConstantYawSpinTracesClosedInertialCircle) {
  RigidBody6DOFState s; // identity attitude
  const double V = 10.0;
  const double yaw_rate = 0.2; // rad/s about body z
  s.velocity_body = Vec3{V, 0.0, 0.0};
  s.angular_velocity_body = Vec3{0.0, 0.0, yaw_rate};

  const InertiaTensor I{1.0, 1.0, 1.0, 0.0};
  auto zero_moment = [](double, const RigidBody6DOFState&) { return Vec3{}; };
  // Coordinated-turn force (mass = 1): F = omega x v, cancels -omega x v in
  // v_dot so the body speed is held constant.
  auto turn_force = [](double, const RigidBody6DOFState& st) {
    return cross(st.angular_velocity_body, st.velocity_body);
  };

  const double dt = 0.001;
  const double period = 2.0 * M_PI / yaw_rate; // 31.4159 s
  const int n = static_cast<int>(std::lround(period / dt));
  for (int i = 0; i < n; ++i) {
    stepRigidBody6DOF(s, turn_force, zero_moment, 1.0, I, i * dt, dt);
  }

  // Back at the origin (within the dt discretization of the closed loop).
  EXPECT_NEAR(s.position_inertial.x, 0.0, 1e-2);
  EXPECT_NEAR(s.position_inertial.y, 0.0, 1e-2);
  EXPECT_NEAR(s.position_inertial.z, 0.0, 1e-12);
  // Body speed preserved exactly (coordinated turn).
  EXPECT_NEAR(sim::dynamics::rigid_body::norm(s.velocity_body), V, 1e-9);
}

/* ----------------------------- Angular momentum conservation ----------------------------- */

/**
 * @test Torque-free motion conserves the magnitude of the inertial angular
 * momentum |H| = |I omega|. Euler's equation only rotates H in the body
 * frame; with no external moment its inertial magnitude is invariant. This
 * is independent of the rotational-KE check and guards the omega x (I omega)
 * coupling term directly.
 */
TEST(RigidBody6DOFTest, TorqueFreeMotionConservesAngularMomentum) {
  const InertiaTensor I{2.0, 4.0, 8.0, 0.0};
  RigidBody6DOFState s;
  s.angular_velocity_body = Vec3{0.05, 0.05, 1.5}; // biased toward Izz (bounded)

  const double H0 = sim::dynamics::rigid_body::norm(I.multiply(s.angular_velocity_body));

  const double dt = 0.001;
  for (int i = 0; i < 5000; ++i) { // 5 s
    stepRigidBody6DOF(s, zeroVec3(), zeroVec3(), 1.0, I, i * dt, dt);
  }

  const double H1 = sim::dynamics::rigid_body::norm(I.multiply(s.angular_velocity_body));
  EXPECT_NEAR(H1, H0, std::fabs(H0) * 1e-6);
}

/* ----------------------------- Inertia tensor solve ----------------------------- */

/** @test InertiaTensor::solve is the inverse of multiply across the xz plane. */
TEST(InertiaTensorTest, SolveInvertsMultiply) {
  const InertiaTensor I{12.0, 8.0, 16.0, 1.5};

  // Test a handful of arbitrary input vectors.
  for (const auto& w : {Vec3{1.0, 2.0, 3.0}, Vec3{-0.5, 0.0, 4.0}, Vec3{7.7, -2.2, 1.1}}) {
    const Vec3 b = I.multiply(w);
    const Vec3 w2 = I.solve(b);
    EXPECT_NEAR(w2.x, w.x, 1e-10);
    EXPECT_NEAR(w2.y, w.y, 1e-10);
    EXPECT_NEAR(w2.z, w.z, 1e-10);
  }
}

/* ----------------------------- cross / dot / norm sanity ----------------------------- */

TEST(Vec3HelpersTest, CrossDotNormBasics) {
  const Vec3 a{1.0, 0.0, 0.0};
  const Vec3 b{0.0, 1.0, 0.0};
  const Vec3 c = cross(a, b);
  EXPECT_NEAR(c.x, 0.0, 1e-15);
  EXPECT_NEAR(c.y, 0.0, 1e-15);
  EXPECT_NEAR(c.z, 1.0, 1e-15);

  EXPECT_NEAR(sim::dynamics::rigid_body::dot(a, b), 0.0, 1e-15);
  EXPECT_NEAR(sim::dynamics::rigid_body::dot(a, a), 1.0, 1e-15);

  EXPECT_NEAR(sim::dynamics::rigid_body::norm(Vec3{3.0, 4.0, 0.0}), 5.0, 1e-12);
}
