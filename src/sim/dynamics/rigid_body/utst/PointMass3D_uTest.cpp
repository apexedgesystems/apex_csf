/**
 * @file PointMass3D_uTest.cpp
 * @brief Unit tests for PointMass3D dynamics.
 *
 * Test physics scenarios with closed-form solutions so we can verify
 * the integrator + EOM math against analytic ground truth:
 *
 *   1. Free fall under constant g  -> kinematics: z(t) = z0 + v0*t - 0.5*g*t^2
 *   2. Constant horizontal force   -> uniform acceleration
 *   3. Newton's first law          -> zero force preserves velocity
 *   4. Energy conservation under gravity (free fall)
 */

#include "src/sim/dynamics/rigid_body/inc/PointMass3D.hpp"

#include <cmath>

#include <gtest/gtest.h>

using sim::dynamics::rigid_body::PointMass3DState;
using sim::dynamics::rigid_body::stepPointMass3D;
using sim::dynamics::rigid_body::Vec3;

namespace {

constexpr double kG = 9.80665; // standard gravity (m/s^2)

} // namespace

/* ----------------------------- Vec3 + derivative primitives ----------------------------- */

/** @test Vec3 element-wise add / subtract / scale behave as expected. */
TEST(PointMass3DTest, Vec3ArithmeticBasics) {
  const Vec3 a{1.0, 2.0, 3.0};
  const Vec3 b{0.5, -1.0, 4.0};

  const Vec3 sum = a + b;
  EXPECT_DOUBLE_EQ(sum.x, 1.5);
  EXPECT_DOUBLE_EQ(sum.y, 1.0);
  EXPECT_DOUBLE_EQ(sum.z, 7.0);

  const Vec3 diff = a - b; // exercises Vec3::operator-
  EXPECT_DOUBLE_EQ(diff.x, 0.5);
  EXPECT_DOUBLE_EQ(diff.y, 3.0);
  EXPECT_DOUBLE_EQ(diff.z, -1.0);

  const Vec3 scaled = a * 2.0;
  EXPECT_DOUBLE_EQ(scaled.x, 2.0);
  EXPECT_DOUBLE_EQ(scaled.z, 6.0);
}

/** @test pointMass3DDerivative returns (v, F/m) directly. */
TEST(PointMass3DTest, DerivativeIsVelocityAndForceOverMass) {
  PointMass3DState s;
  s.velocity = Vec3{10.0, 0.0, -2.0};
  const Vec3 force{600.0, 0.0, 0.0};
  const double mass = 200.0;

  const auto d = sim::dynamics::rigid_body::pointMass3DDerivative(s, force, mass);
  // dp/dt = v
  EXPECT_DOUBLE_EQ(d.position.x, 10.0);
  EXPECT_DOUBLE_EQ(d.position.z, -2.0);
  // dv/dt = F/m = 3 m/s^2 along x
  EXPECT_DOUBLE_EQ(d.velocity.x, 3.0);
  EXPECT_DOUBLE_EQ(d.velocity.y, 0.0);
}

/* ----------------------------- Newton's first law ----------------------------- */

/** @test With zero applied force, velocity is preserved over many steps. */
TEST(PointMass3DTest, ZeroForcePreservesVelocity) {
  PointMass3DState s;
  s.velocity = Vec3{10.0, -5.0, 2.0};

  auto zero_force = [](double, const PointMass3DState&) { return Vec3{}; };

  const double dt = 0.01;
  for (int i = 0; i < 1000; ++i) { // 10 s of integration
    stepPointMass3D(s, zero_force, /*mass*/ 1500.0, /*t*/ i * dt, dt);
  }
  EXPECT_NEAR(s.velocity.x, 10.0, 1e-9);
  EXPECT_NEAR(s.velocity.y, -5.0, 1e-9);
  EXPECT_NEAR(s.velocity.z, 2.0, 1e-9);
}

/* ----------------------------- Free fall under gravity ----------------------------- */

/** @test Free fall from rest matches z = -0.5*g*t^2 + z0 over 5 s.
 *
 *  Kinematic (closed form): with v0 = 0,
 *    z(t)  = z0 - 0.5 * g * t^2
 *    vz(t) = -g * t
 */
TEST(PointMass3DTest, FreeFallFromRestMatchesKinematics) {
  const double mass = 100.0;                // kg (any positive)
  const Vec3 g_force{0.0, 0.0, -mass * kG}; // F = m*g pointing -Z

  PointMass3DState s;
  s.position = Vec3{0.0, 0.0, 1000.0};

  auto force = [&](double, const PointMass3DState&) { return g_force; };

  const double dt = 0.01;
  const double t_final = 5.0;
  const int n_steps = static_cast<int>(t_final / dt);
  for (int i = 0; i < n_steps; ++i) {
    stepPointMass3D(s, force, mass, i * dt, dt);
  }

  const double t = n_steps * dt;
  const double z_expected = 1000.0 - 0.5 * kG * t * t;
  const double vz_expected = -kG * t;

  // RK4 with constant acceleration is exact to machine precision.
  EXPECT_NEAR(s.position.z, z_expected, 1e-7);
  EXPECT_NEAR(s.velocity.z, vz_expected, 1e-9);
}

/* ----------------------------- Constant horizontal force ----------------------------- */

/** @test Constant horizontal force from rest produces uniform acceleration. */
TEST(PointMass3DTest, ConstantForceProducesUniformAcceleration) {
  const double mass = 1500.0;
  const double Fx = 4500.0; // N -> a = 3 m/s^2

  PointMass3DState s; // start at rest, origin

  auto force = [&](double, const PointMass3DState&) { return Vec3{Fx, 0.0, 0.0}; };

  const double dt = 0.01;
  const double t_final = 4.0;
  const int n_steps = static_cast<int>(t_final / dt);
  for (int i = 0; i < n_steps; ++i) {
    stepPointMass3D(s, force, mass, i * dt, dt);
  }

  // a = F/m = 3 m/s^2; over 4 s: x = 0.5*a*t^2 = 24 m, vx = a*t = 12 m/s.
  const double a = Fx / mass;
  const double t = n_steps * dt;
  EXPECT_NEAR(s.position.x, 0.5 * a * t * t, 1e-7);
  EXPECT_NEAR(s.velocity.x, a * t, 1e-9);
  EXPECT_NEAR(s.position.y, 0.0, 1e-12);
  EXPECT_NEAR(s.position.z, 0.0, 1e-12);
}

/* ----------------------------- Energy conservation ----------------------------- */

/** @test KE + PE conserved during free fall (no integrator drift). */
TEST(PointMass3DTest, FreeFallConservesTotalEnergy) {
  const double mass = 1.0;
  const Vec3 g_force{0.0, 0.0, -mass * kG};

  PointMass3DState s;
  s.position = Vec3{0.0, 0.0, 100.0};
  s.velocity = Vec3{0.0, 0.0, 0.0};

  const double E_initial = 0.5 * mass * 0.0 + mass * kG * s.position.z;

  auto force = [&](double, const PointMass3DState&) { return g_force; };

  const double dt = 0.001;
  for (int i = 0; i < 4000; ++i) { // 4 s of fall (still above z=0)
    stepPointMass3D(s, force, mass, i * dt, dt);
  }

  const double v_sq =
      s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y + s.velocity.z * s.velocity.z;
  const double E_final = 0.5 * mass * v_sq + mass * kG * s.position.z;

  // RK4 with constant force is exact; energy should be machine-clean.
  EXPECT_NEAR(E_final, E_initial, 1e-6);
}
