/**
 * @file Quaternion_uTest.cpp
 * @brief Unit tests for apex::math::integration::Quaternion and integrators.
 *
 * Notes:
 *  - Tests verify quaternion arithmetic and normalization.
 *  - Tests verify rotation operations and Euler conversion.
 *  - Tests verify quaternion integration methods.
 */

#include "src/utilities/math/integration/inc/Quaternion.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::Attitude6DOF;
using apex::math::integration::Quaternion;
using apex::math::integration::QuaternionIntegrator;

/* --------------------------- Quaternion Basics ----------------------------- */

/** @test Default quaternion is identity. */
TEST(QuaternionTest, DefaultIsIdentity) {
  Quaternion q;

  EXPECT_DOUBLE_EQ(q.w, 1.0);
  EXPECT_DOUBLE_EQ(q.x, 0.0);
  EXPECT_DOUBLE_EQ(q.y, 0.0);
  EXPECT_DOUBLE_EQ(q.z, 0.0);
}

/** @test Quaternion from axis-angle with zero angle gives identity. */
TEST(QuaternionTest, ZeroAngleGivesIdentity) {
  Quaternion q = Quaternion::fromAxisAngle(1.0, 0.0, 0.0, 0.0);

  EXPECT_NEAR(q.w, 1.0, 1e-10);
  EXPECT_NEAR(q.x, 0.0, 1e-10);
  EXPECT_NEAR(q.y, 0.0, 1e-10);
  EXPECT_NEAR(q.z, 0.0, 1e-10);
}

/** @test Quaternion from 180 degree rotation about Z. */
TEST(QuaternionTest, Rotate180AboutZ) {
  Quaternion q = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, M_PI);

  // cos(pi/2) = 0, sin(pi/2) = 1
  EXPECT_NEAR(q.w, 0.0, 1e-10);
  EXPECT_NEAR(q.x, 0.0, 1e-10);
  EXPECT_NEAR(q.y, 0.0, 1e-10);
  EXPECT_NEAR(q.z, 1.0, 1e-10);
}

/** @test Quaternion norm and normalization. */
TEST(QuaternionTest, NormAndNormalize) {
  Quaternion q{2.0, 0.0, 0.0, 0.0};

  EXPECT_DOUBLE_EQ(q.norm(), 2.0);
  EXPECT_DOUBLE_EQ(q.normSq(), 4.0);

  Quaternion qn = q.normalized();
  EXPECT_NEAR(qn.norm(), 1.0, 1e-10);
  EXPECT_DOUBLE_EQ(qn.w, 1.0);
}

/** @test Quaternion multiplication (rotation composition). */
TEST(QuaternionTest, Multiplication) {
  // 90 degree rotation about Z
  Quaternion q1 = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, M_PI / 2.0);
  // Another 90 degree rotation about Z
  Quaternion q2 = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, M_PI / 2.0);

  // Combined should be 180 degrees about Z
  Quaternion q3 = q1 * q2;
  q3 = q3.normalized();

  EXPECT_NEAR(q3.w, 0.0, 1e-10);
  EXPECT_NEAR(q3.x, 0.0, 1e-10);
  EXPECT_NEAR(q3.y, 0.0, 1e-10);
  EXPECT_NEAR(q3.z, 1.0, 1e-10);
}

/** @test Quaternion conjugate. */
TEST(QuaternionTest, Conjugate) {
  Quaternion q{0.5, 0.5, 0.5, 0.5};
  Quaternion qc = q.conjugate();

  EXPECT_DOUBLE_EQ(qc.w, 0.5);
  EXPECT_DOUBLE_EQ(qc.x, -0.5);
  EXPECT_DOUBLE_EQ(qc.y, -0.5);
  EXPECT_DOUBLE_EQ(qc.z, -0.5);
}

/* -------------------------- Rotation Operations ---------------------------- */

/** @test Identity quaternion does not rotate vector. */
TEST(QuaternionTest, IdentityDoesNotRotate) {
  Quaternion q; // Identity
  auto result = q.rotate(1.0, 0.0, 0.0);

  EXPECT_NEAR(result[0], 1.0, 1e-10);
  EXPECT_NEAR(result[1], 0.0, 1e-10);
  EXPECT_NEAR(result[2], 0.0, 1e-10);
}

/** @test 90 degree rotation about Z axis. */
TEST(QuaternionTest, Rotate90AboutZ) {
  Quaternion q = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, M_PI / 2.0);
  auto result = q.rotate(1.0, 0.0, 0.0);

  // X axis rotates to Y axis
  EXPECT_NEAR(result[0], 0.0, 1e-10);
  EXPECT_NEAR(result[1], 1.0, 1e-10);
  EXPECT_NEAR(result[2], 0.0, 1e-10);
}

/** @test Euler angle conversion for identity. */
TEST(QuaternionTest, EulerIdentity) {
  Quaternion q;
  auto euler = q.toEuler();

  EXPECT_NEAR(euler[0], 0.0, 1e-10); // Roll
  EXPECT_NEAR(euler[1], 0.0, 1e-10); // Pitch
  EXPECT_NEAR(euler[2], 0.0, 1e-10); // Yaw
}

/* ------------------------ Quaternion Integration --------------------------- */

/** @test Euler integration with constant angular velocity. */
TEST(QuaternionIntegratorTest, EulerStepConstantOmega) {
  QuaternionIntegrator integrator;
  Quaternion q;

  // Rotate about Z at 1 rad/s
  const double DT = 0.01;
  const double OMEGA_Z = 1.0;

  // Integrate for 1 second
  for (int i = 0; i < 100; ++i) {
    integrator.stepEuler(q, 0.0, 0.0, OMEGA_Z, DT);
  }

  // Should have rotated about 1 radian about Z
  auto euler = q.toEuler();
  EXPECT_NEAR(euler[2], 1.0, 0.05); // Yaw ~ 1 radian
}

/** @test Midpoint integration is more accurate than Euler. */
TEST(QuaternionIntegratorTest, MidpointMoreAccurate) {
  const double DT = 0.1;
  const double OMEGA = 1.0;
  const int STEPS = 100;

  // Euler integration
  QuaternionIntegrator eulerInt;
  Quaternion qEuler;
  for (int i = 0; i < STEPS; ++i) {
    eulerInt.stepEuler(qEuler, 0.0, 0.0, OMEGA, DT);
  }

  // Midpoint integration
  QuaternionIntegrator midInt;
  Quaternion qMid;
  for (int i = 0; i < STEPS; ++i) {
    midInt.stepMidpoint(qMid, 0.0, 0.0, OMEGA, DT);
  }

  // Expected: rotation of OMEGA * STEPS * DT = 10 radians about Z
  Quaternion qExact = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, OMEGA * STEPS * DT);

  // Midpoint error should be smaller
  double errEuler =
      (qEuler.w - qExact.w) * (qEuler.w - qExact.w) + (qEuler.z - qExact.z) * (qEuler.z - qExact.z);
  double errMid =
      (qMid.w - qExact.w) * (qMid.w - qExact.w) + (qMid.z - qExact.z) * (qMid.z - qExact.z);

  EXPECT_LT(errMid, errEuler);
}

/** @test Exponential integration is exact for constant omega. */
TEST(QuaternionIntegratorTest, ExponentialExactForConstantOmega) {
  QuaternionIntegrator integrator;
  Quaternion q;

  const double DT = 0.5; // Large step
  const double OMEGA = 2.0;

  integrator.stepExponential(q, 0.0, 0.0, OMEGA, DT);

  // Should match exact rotation
  Quaternion qExact = Quaternion::fromAxisAngle(0.0, 0.0, 1.0, OMEGA * DT);

  EXPECT_NEAR(q.w, qExact.w, 1e-10);
  EXPECT_NEAR(q.x, qExact.x, 1e-10);
  EXPECT_NEAR(q.y, qExact.y, 1e-10);
  EXPECT_NEAR(q.z, qExact.z, 1e-10);
}

/** @test Quaternion remains normalized after many steps. */
TEST(QuaternionIntegratorTest, RemainsNormalized) {
  QuaternionIntegrator integrator;
  Quaternion q;

  // Random-ish angular velocity
  for (int i = 0; i < 10000; ++i) {
    integrator.stepEuler(q, 0.1, 0.2, 0.3, 0.001);
  }

  EXPECT_NEAR(q.norm(), 1.0, 1e-10);
}

/* ----------------------------- Attitude6DOF -------------------------------- */

/** @test Attitude6DOF default construction. */
TEST(Attitude6DOFTest, DefaultConstruction) {
  Attitude6DOF state;

  EXPECT_DOUBLE_EQ(state.px, 0.0);
  EXPECT_DOUBLE_EQ(state.py, 0.0);
  EXPECT_DOUBLE_EQ(state.pz, 0.0);
  EXPECT_DOUBLE_EQ(state.vx, 0.0);
  EXPECT_DOUBLE_EQ(state.vy, 0.0);
  EXPECT_DOUBLE_EQ(state.vz, 0.0);
  EXPECT_DOUBLE_EQ(state.q.w, 1.0);
  EXPECT_DOUBLE_EQ(state.wx, 0.0);
  EXPECT_DOUBLE_EQ(state.wy, 0.0);
  EXPECT_DOUBLE_EQ(state.wz, 0.0);
}
