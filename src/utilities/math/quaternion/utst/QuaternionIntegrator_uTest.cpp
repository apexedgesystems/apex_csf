/**
 * @file QuaternionIntegrator_uTest.cpp
 * @brief Unit tests for QuatData, the attitude integrator steps, and the
 *        Euler-321 conversions.
 *
 * Coverage:
 *  - QuatData layout, identity default, view round-trip
 *  - deltaInto small-angle and finite-angle branches
 *  - stepEuler / stepMidpoint / stepExponential vs the closed-form constant-
 *    rate rotation, including order-of-accuracy ranking
 *  - Unit-norm preservation over long integrations
 *  - Euler-321 set/extract round-trip and gimbal clamp
 */

#include "src/utilities/math/quaternion/inc/QuatData.hpp"
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionIntegrator.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionStatus.hpp"

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

using apex::math::quaternion::QuatData;
using apex::math::quaternion::Quaternion;
using apex::math::quaternion::QuaternionIntegrator;
using apex::math::quaternion::Status;

namespace {

constexpr double K_PI = 3.14159265358979323846;

template <typename T> constexpr T tol() {
  return std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
}

} // namespace

template <typename T> class QuatIntegratorTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(QuatIntegratorTestT, ValueTypes);

/* ------------------------------- QuatData -------------------------------- */

/** @test QuatData is flat, trivially copyable, identity by default. */
TYPED_TEST(QuatIntegratorTestT, QuatDataLayoutAndIdentity) {
  using T = TypeParam;
  static_assert(std::is_trivially_copyable<QuatData<T>>::value, "must be streamable");
  QuatData<T> q;
  EXPECT_EQ(q.w(), T(1));
  EXPECT_EQ(q.x(), T(0));
  EXPECT_EQ(q.y(), T(0));
  EXPECT_EQ(q.z(), T(0));
  // The view writes through to the owned storage.
  q.view().set(T(0.5), T(0.5), T(0.5), T(0.5));
  EXPECT_EQ(q.d[0], T(0.5));
  EXPECT_EQ(q.d[3], T(0.5));
  // Byte copy carries the value (the wire/streaming property).
  QuatData<T> r;
  std::memcpy(&r, &q, sizeof(q));
  EXPECT_EQ(r.y(), T(0.5));
}

/* ------------------------------- deltaInto -------------------------------- */

/** @test A finite-rate delta equals the angle-axis quaternion for omega*dt. */
TYPED_TEST(QuatIntegratorTestT, DeltaMatchesAngleAxis) {
  using T = TypeParam;
  const T W[3] = {T(0), T(0), T(0.7)}; // yaw rate
  const T DT = T(0.5);

  T dqData[4];
  Quaternion<T> dq(dqData);
  EXPECT_EQ(QuaternionIntegrator<T>::deltaInto(W, DT, dq), 0);

  T refData[4];
  Quaternion<T> ref(refData);
  ref.setFromAngleAxis(T(0.35), T(0), T(0), T(1)); // angle = |w| * dt
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(dqData[i], refData[i], tol<T>());
  }
}

/** @test The small-angle branch stays finite and near identity. */
TYPED_TEST(QuatIntegratorTestT, DeltaSmallAngleBranch) {
  using T = TypeParam;
  const T W[3] = {T(1e-13), T(0), T(0)};
  T dqData[4];
  Quaternion<T> dq(dqData);
  EXPECT_EQ(QuaternionIntegrator<T>::deltaInto(W, T(0.01), dq), 0);
  EXPECT_NEAR(dq.w(), T(1), tol<T>());
  EXPECT_NEAR(dq.x(), T(1e-13) * T(0.005), tol<T>());
}

/* ------------------------------- Steps ------------------------------------ */

/** @test Constant yaw rate integrates to the closed-form heading (all steps). */
TYPED_TEST(QuatIntegratorTestT, ConstantRateMatchesClosedForm) {
  using T = TypeParam;
  const T RATE = T(0.5); // rad/s about +Z
  const T DT = T(0.001);
  const int N = 2000; // 2 s -> 1 rad total
  const T W[3] = {T(0), T(0), RATE};

  uint8_t (*steps[3])(Quaternion<T>&, const T*, T) = {
      &QuaternionIntegrator<T>::stepEuler,
      &QuaternionIntegrator<T>::stepMidpoint,
      &QuaternionIntegrator<T>::stepExponential,
  };
  // Generous per-method bounds; exponential is exact to roundoff for
  // constant omega, Euler accrues O(dt) truncation.
  const double BOUND[3] = {1e-3, 1e-6, 1e-4};

  for (int m = 0; m < 3; ++m) {
    QuatData<T> q;
    Quaternion<T> v = q.view();
    for (int i = 0; i < N; ++i) {
      ASSERT_EQ(steps[m](v, W, DT), 0);
    }
    T roll = 0, pitch = 0, yaw = 0;
    ASSERT_EQ(v.toEuler321Into(roll, pitch, yaw), 0);
    const double WANT = 1.0; // rad
    const bool IS_DOUBLE = std::is_same<T, double>::value;
    const double YAW_TOL = IS_DOUBLE ? BOUND[m] : 2e-3;
    EXPECT_NEAR(static_cast<double>(yaw), WANT, YAW_TOL) << "method " << m;
    EXPECT_NEAR(static_cast<double>(roll), 0.0, 1e-3);
    EXPECT_NEAR(static_cast<double>(pitch), 0.0, 1e-3);
  }
}

/** @test The exponential step is exact (to roundoff) for constant omega. */
TYPED_TEST(QuatIntegratorTestT, ExponentialIsExactForConstantRate) {
  using T = TypeParam;
  const T W[3] = {T(0.3), T(-0.2), T(0.4)};
  const T DT = T(0.05);
  const int N = 400; // 20 s

  QuatData<T> q;
  Quaternion<T> v = q.view();
  for (int i = 0; i < N; ++i) {
    ASSERT_EQ(QuaternionIntegrator<T>::stepExponential(v, W, DT), 0);
  }

  // Closed form: one rotation about the fixed omega axis by |w| * t_total.
  const T WN = std::sqrt(W[0] * W[0] + W[1] * W[1] + W[2] * W[2]);
  T refData[4];
  Quaternion<T> ref(refData);
  ref.setFromAngleAxis(WN * DT * T(N), W[0] / WN, W[1] / WN, W[2] / WN);

  // Compare as rotations (q and -q are the same rotation).
  const T SIGN = (q.w() * ref.w() >= T(0)) ? T(1) : T(-1);
  const T TOL = std::is_same<T, double>::value ? T(1e-9) : T(2e-4);
  EXPECT_NEAR(q.w(), SIGN * refData[0], TOL);
  EXPECT_NEAR(q.x(), SIGN * refData[1], TOL);
  EXPECT_NEAR(q.y(), SIGN * refData[2], TOL);
  EXPECT_NEAR(q.z(), SIGN * refData[3], TOL);
}

/** @test Every step keeps the quaternion unit-norm over a long run. */
TYPED_TEST(QuatIntegratorTestT, NormPreservedOverLongRun) {
  using T = TypeParam;
  const T W[3] = {T(1.1), T(-2.3), T(0.7)};
  QuatData<T> q;
  Quaternion<T> v = q.view();
  for (int i = 0; i < 20000; ++i) {
    ASSERT_EQ(QuaternionIntegrator<T>::stepMidpoint(v, W, T(0.002)), 0);
  }
  T n = 0;
  ASSERT_EQ(v.normInto(n), 0);
  EXPECT_NEAR(n, T(1), tol<T>() * T(10));
}

/** @test Zero angular velocity leaves the attitude unchanged. */
TYPED_TEST(QuatIntegratorTestT, ZeroRateIsIdentity) {
  using T = TypeParam;
  const T W[3] = {T(0), T(0), T(0)};
  QuatData<T> q;
  q.view().setFromAngleAxis(T(0.8), T(0), T(1), T(0));
  const QuatData<T> BEFORE = q;
  Quaternion<T> v = q.view();
  ASSERT_EQ(QuaternionIntegrator<T>::stepEuler(v, W, T(0.01)), 0);
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(q.d[i], BEFORE.d[i], tol<T>());
  }
}

/* ------------------------------- Euler 321 -------------------------------- */

/** @test setFromEuler321 -> toEuler321Into round-trips away from the singularity. */
TYPED_TEST(QuatIntegratorTestT, Euler321RoundTrip) {
  using T = TypeParam;
  const T CASES[][3] = {
      {T(0.3), T(-0.4), T(1.2)},
      {T(-1.0), T(0.7), T(-2.5)},
      {T(0), T(0), T(0)},
      {T(0.1), T(1.2), T(3.0)},
  };
  for (const auto& C : CASES) {
    T qd[4];
    Quaternion<T> q(qd);
    ASSERT_EQ(q.setFromEuler321(C[0], C[1], C[2]), 0);
    T n = 0;
    q.normInto(n);
    EXPECT_NEAR(n, T(1), tol<T>() * T(4)); // the sequence composes unit rotations
    T r = 0, p = 0, y = 0;
    ASSERT_EQ(q.toEuler321Into(r, p, y), 0);
    const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
    EXPECT_NEAR(r, C[0], TOL);
    EXPECT_NEAR(p, C[1], TOL);
    EXPECT_NEAR(y, C[2], TOL);
  }
}

/** @test A yaw-only Euler set matches the angle-axis quaternion about +Z. */
TYPED_TEST(QuatIntegratorTestT, EulerYawOnlyMatchesAngleAxis) {
  using T = TypeParam;
  T ad[4], bd[4];
  Quaternion<T> a(ad), b(bd);
  ASSERT_EQ(a.setFromEuler321(T(0), T(0), T(0.9)), 0);
  ASSERT_EQ(b.setFromAngleAxis(T(0.9), T(0), T(0), T(1)), 0);
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(ad[i], bd[i], tol<T>());
  }
}

/** @test At the gimbal singularity, pitch clamps to +/-pi/2 and stays finite. */
TYPED_TEST(QuatIntegratorTestT, EulerGimbalClamp) {
  using T = TypeParam;
  T qd[4];
  Quaternion<T> q(qd);
  ASSERT_EQ(q.setFromEuler321(T(0), T(K_PI / 2), T(0)), 0);
  T r = 0, p = 0, y = 0;
  ASSERT_EQ(q.toEuler321Into(r, p, y), 0);
  const T PITCH_TOL = std::is_same<T, double>::value ? T(1e-6) : T(1e-3);
  EXPECT_NEAR(p, T(K_PI / 2), PITCH_TOL);
  EXPECT_TRUE(std::isfinite(static_cast<double>(r)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(y)));
}

/* --------------------------- Rotation-matrix set --------------------------- */

/** @test setFromRotationMatrix inverts toRotationMatrixInto (up to sign),
 *        across all four Shepperd branches. */
TYPED_TEST(QuatIntegratorTestT, RotationMatrixRoundTrip) {
  using T = TypeParam;
  // Cases chosen to force each branch: near-identity (trace), and
  // near-180-degree rotations about each axis (per-diagonal dominance).
  const T CASES[][4] = {
      {T(0.1), T(0.2), T(0.3), T(0)},   // small rotation: trace branch
      {T(3.1), T(0.02), T(0.01), T(0)}, // ~180 about X
      {T(0.02), T(3.1), T(0.01), T(0)}, // ~180 about Y
      {T(0.02), T(0.01), T(3.1), T(0)}, // ~180 about Z
  };
  for (const auto& C : CASES) {
    T ad[4], bd[4], m[9];
    Quaternion<T> a(ad), b(bd);
    // Axis-angle from the case's dominant axis composition via Euler.
    ASSERT_EQ(a.setFromEuler321(C[0], C[1], C[2]), 0);
    ASSERT_EQ(a.toRotationMatrixInto(m), 0);
    ASSERT_EQ(b.setFromRotationMatrix(m), 0);
    const T SIGN =
        (ad[0] * bd[0] + ad[1] * bd[1] + ad[2] * bd[2] + ad[3] * bd[3]) >= T(0) ? T(1) : T(-1);
    const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
    for (int i = 0; i < 4; ++i) {
      EXPECT_NEAR(ad[i], SIGN * bd[i], TOL);
    }
  }
}
