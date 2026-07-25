/**
 * @file Vecmat_uTest.cpp
 * @brief Tests for the fixed-size vector/matrix/rotation operations.
 *
 * Coverage:
 *  - Vec3: algebra, cross/dot identities, normalize + zero guard, aliasing
 *  - Mat3: multiply/transpose/det, inverse round-trip + singular guard, solve
 *  - Rotations: cross-implementation equivalence (vecmat DCM vs the
 *    quaternion conversion path), Euler round-trips + gimbal clamp,
 *    Rodrigues vs quaternion angle-axis, orthonormality, wind->body
 *    reductions and closed-form values
 */

#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/vecmat/inc/Mat3Ops.hpp"
#include "src/utilities/math/vecmat/inc/Rotations.hpp"
#include "src/utilities/math/vecmat/inc/Vec3Ops.hpp"
#include "src/utilities/math/vecmat/inc/VecmatStatus.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace vm = apex::math::vecmat;

namespace {

template <typename T> constexpr T tol() {
  return std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
}

} // namespace

template <typename T> class VecmatTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(VecmatTestT, ValueTypes);

/* --------------------------------- Vec3 ---------------------------------- */

/** @test Basic algebra: set/copy/add/sub/scale, incl. aliased operands. */
TYPED_TEST(VecmatTestT, Vec3Algebra) {
  using T = TypeParam;
  T a[3], b[3], out[3];
  vm::set(a, T(1), T(2), T(3));
  vm::set(b, T(4), T(-5), T(6));
  vm::add(a, b, out);
  EXPECT_EQ(out[1], T(-3));
  vm::sub(a, b, out);
  EXPECT_EQ(out[0], T(-3));
  vm::scale(a, T(2), out);
  EXPECT_EQ(out[2], T(6));
  vm::add(a, a, a); // aliasing: a = a + a
  EXPECT_EQ(a[0], T(2));
  vm::copy(b, out);
  EXPECT_EQ(out[2], T(6));
}

/** @test Cross/dot identities: x cross y = z; a.(a x b) = 0. */
TYPED_TEST(VecmatTestT, CrossDotIdentities) {
  using T = TypeParam;
  const T X[3] = {T(1), T(0), T(0)};
  const T Y[3] = {T(0), T(1), T(0)};
  T z[3];
  vm::cross(X, Y, z);
  EXPECT_EQ(z[0], T(0));
  EXPECT_EQ(z[1], T(0));
  EXPECT_EQ(z[2], T(1));

  const T A[3] = {T(1.3), T(-0.2), T(2.1)};
  const T B[3] = {T(-0.7), T(0.4), T(0.9)};
  T c[3];
  vm::cross(A, B, c);
  EXPECT_NEAR(vm::dot(A, c), T(0), tol<T>());
  EXPECT_NEAR(vm::dot(B, c), T(0), tol<T>());
}

/** @test Normalize yields a unit vector; the zero vector is refused. */
TYPED_TEST(VecmatTestT, NormalizeAndZeroGuard) {
  using T = TypeParam;
  const T V[3] = {T(3), T(0), T(4)};
  T u[3];
  ASSERT_EQ(vm::normalizeInto(V, u), 0);
  EXPECT_NEAR(vm::norm(u), T(1), tol<T>());
  EXPECT_NEAR(u[2], T(0.8), tol<T>());

  const T Z[3] = {T(0), T(0), T(0)};
  T o[3];
  const auto RC = static_cast<vm::Status>(vm::normalizeInto(Z, o));
  EXPECT_EQ(RC, vm::Status::ERROR_INVALID_VALUE);
  EXPECT_TRUE(vm::failed(RC));
  EXPECT_FALSE(vm::ok(RC));
}

/* --------------------------------- Mat3 ---------------------------------- */

/** @test Identity multiply, transpose, and matrix-matrix product. */
TYPED_TEST(VecmatTestT, Mat3Basics) {
  using T = TypeParam;
  T i[9];
  vm::identity(i);
  const T V[3] = {T(1), T(-2), T(3)};
  T out[3];
  vm::multiplyVec(i, V, out);
  EXPECT_EQ(out[1], T(-2));

  const T M[9] = {T(1), T(2), T(3), T(4), T(5), T(6), T(7), T(8), T(10)};
  T mt[9], mtm[9];
  vm::transposeInto(M, mt);
  EXPECT_EQ(mt[1], T(4));
  EXPECT_EQ(mt[3], T(2));
  vm::multiplyMat(M, i, mtm);
  for (int k = 0; k < 9; ++k) {
    EXPECT_EQ(mtm[k], M[k]);
  }
}

/** @test Inverse round-trips (M * M^-1 = I); det matches; singular refused. */
TYPED_TEST(VecmatTestT, Mat3InverseRoundTripAndSingularGuard) {
  using T = TypeParam;
  const T M[9] = {T(1), T(2), T(3), T(4), T(5), T(6), T(7), T(8), T(10)};
  EXPECT_NEAR(vm::det(M), T(-3), tol<T>() * T(10));
  T inv[9], prod[9];
  ASSERT_EQ(vm::inverseInto(M, inv), 0);
  vm::multiplyMat(M, inv, prod);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_NEAR(prod[r * 3 + c], r == c ? T(1) : T(0), tol<T>() * T(100));
    }
  }

  const T S[9] = {T(1), T(2), T(3), T(2), T(4), T(6), T(0), T(1), T(1)}; // rank 2
  T o[9];
  EXPECT_EQ(vm::inverseInto(S, o), static_cast<uint8_t>(vm::Status::ERROR_SINGULAR));
}

/** @test solve() recovers x from an inertia-like symmetric system. */
TYPED_TEST(VecmatTestT, Mat3SolveInertiaLike) {
  using T = TypeParam;
  // Realistic small-aircraft inertia (kg m^2), symmetric with an Ixz product.
  const T I[9] = {T(1285), T(0), T(-80), T(0), T(1825), T(0), T(-80), T(0), T(2667)};
  const T X_TRUE[3] = {T(0.3), T(-0.1), T(0.2)};
  T b[3], x[3];
  vm::multiplyVec(I, X_TRUE, b);
  ASSERT_EQ(vm::solveInto(I, b, x), 0);
  const T TOL = std::is_same<T, double>::value ? T(1e-10) : T(1e-3);
  for (int k = 0; k < 3; ++k) {
    EXPECT_NEAR(x[k], X_TRUE[k], TOL);
  }
}

/* ------------------------------- Rotations -------------------------------- */

/** @test Cross-implementation equivalence: vecmat DCM == the quaternion path. */
TYPED_TEST(VecmatTestT, Euler321CrossImplementationEquivalence) {
  using T = TypeParam;
  const T CASES[][3] = {{T(0.3), T(-0.4), T(1.2)}, {T(-1.0), T(0.7), T(-2.5)}, {T(0), T(0), T(0)}};
  for (const auto& C : CASES) {
    // vecmat
    T dcmV[9];
    vm::dcmFromEuler321Into(C[0], C[1], C[2], dcmV);

    // quaternion: setFromEuler321 -> toRotationMatrixInto
    T qd[4], dcmQ[9];
    apex::math::quaternion::Quaternion<T> q(qd);
    ASSERT_EQ(q.setFromEuler321(C[0], C[1], C[2]), 0);
    ASSERT_EQ(q.toRotationMatrixInto(dcmQ), 0);

    const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
    for (int k = 0; k < 9; ++k) {
      EXPECT_NEAR(dcmV[k], dcmQ[k], TOL) << "vs quaternion, k=" << k;
    }
  }
}

/** @test Euler extraction round-trips; gimbal clamps pitch and stays finite. */
TYPED_TEST(VecmatTestT, Euler321RoundTripAndGimbal) {
  using T = TypeParam;
  T dcm[9];
  vm::dcmFromEuler321Into(T(0.3), T(-0.4), T(1.2), dcm);
  T r = 0, p = 0, y = 0;
  vm::euler321FromDcmInto(dcm, r, p, y);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
  EXPECT_NEAR(r, T(0.3), TOL);
  EXPECT_NEAR(p, T(-0.4), TOL);
  EXPECT_NEAR(y, T(1.2), TOL);

  vm::dcmFromEuler321Into(T(0), T(1.5707963267948966), T(0), dcm);
  vm::euler321FromDcmInto(dcm, r, p, y);
  const T PTOL = std::is_same<T, double>::value ? T(1e-6) : T(2e-3);
  EXPECT_NEAR(p, T(1.5707963267948966), PTOL);
  EXPECT_TRUE(std::isfinite(static_cast<double>(r)));
  EXPECT_TRUE(std::isfinite(static_cast<double>(y)));
}

/** @test Rodrigues matches the quaternion angle-axis rotation matrix. */
TYPED_TEST(VecmatTestT, RodriguesMatchesQuaternion) {
  using T = TypeParam;
  const T AXIS[3] = {T(0.36), T(0.48), T(0.8)}; // unit
  const T ANGLE = T(0.9);
  T dcmR[9];
  vm::dcmFromAxisAngleInto(AXIS[0], AXIS[1], AXIS[2], ANGLE, dcmR);

  T qd[4], dcmQ[9];
  apex::math::quaternion::Quaternion<T> q(qd);
  ASSERT_EQ(q.setFromAngleAxis(ANGLE, AXIS[0], AXIS[1], AXIS[2]), 0);
  ASSERT_EQ(q.toRotationMatrixInto(dcmQ), 0);

  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
  for (int k = 0; k < 9; ++k) {
    EXPECT_NEAR(dcmR[k], dcmQ[k], TOL);
  }
}

/** @test Every constructed DCM is orthonormal with det +1. */
TYPED_TEST(VecmatTestT, DcmOrthonormality) {
  using T = TypeParam;
  T dcm[9], dcmT[9], prod[9];
  vm::dcmFromEuler321Into(T(0.5), T(-1.1), T(2.7), dcm);
  vm::transposeInto(dcm, dcmT);
  vm::multiplyMat(dcm, dcmT, prod);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_NEAR(prod[r * 3 + c], r == c ? T(1) : T(0), TOL);
    }
  }
  EXPECT_NEAR(vm::det(dcm), T(1), TOL);
}

/** @test Wind->body matches the aero closed form and its axis reductions. */
TYPED_TEST(VecmatTestT, WindToBodyClosedForm) {
  using T = TypeParam;
  const T ALPHA = T(0.1), BETA = T(-0.05);
  T dcm[9];
  vm::dcmWindToBodyInto(ALPHA, BETA, dcm);

  // F_wind = (-D, Y, -L); expected F_body per the aero convention.
  const T D = T(120), Y = T(15), L = T(900);
  const T FW[3] = {-D, Y, -L};
  T fb[3];
  vm::multiplyVec(dcm, FW, fb);
  const T CA = std::cos(ALPHA), SA = std::sin(ALPHA);
  const T CB = std::cos(BETA), SB = std::sin(BETA);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-3);
  EXPECT_NEAR(fb[0], CA * CB * (-D) - CA * SB * Y - SA * (-L), TOL);
  EXPECT_NEAR(fb[1], SB * (-D) + CB * Y, TOL);
  EXPECT_NEAR(fb[2], SA * CB * (-D) - SA * SB * Y + CA * (-L), TOL);

  // alpha = beta = 0: wind and body coincide.
  vm::dcmWindToBodyInto(T(0), T(0), dcm);
  T i[9];
  vm::identity(i);
  for (int k = 0; k < 9; ++k) {
    EXPECT_NEAR(dcm[k], i[k], tol<T>());
  }
}
