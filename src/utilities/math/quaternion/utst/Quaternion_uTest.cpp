/**
 * @file Quaternion_uTest.cpp
 * @brief Unit tests for Quaternion operations.
 *
 * Coverage:
 *  - Construction and accessors
 *  - Normalization, conjugate, inverse
 *  - Quaternion multiplication
 *  - Vector rotation
 *  - Rotation matrix conversion
 *  - Angle-axis conversion
 *  - SLERP interpolation
 */

#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionStatus.hpp"

#include <cmath>
#include <gtest/gtest.h>

using apex::math::quaternion::Quaternion;
using apex::math::quaternion::Status;

namespace {

constexpr double K_PI = 3.14159265358979323846;

template <typename T> constexpr T tol() {
  return std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
}

inline void expectStatus(uint8_t st, Status want) { EXPECT_EQ(st, static_cast<uint8_t>(want)); }

} // namespace

/** @brief Fixture template to run tests for float and double. */
template <typename T> class QuaternionTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(QuaternionTestT, ValueTypes);

/* --------------------------- Construction -------------------------------- */

/** @test Construct quaternion and verify accessors. */
TYPED_TEST(QuaternionTestT, Construction) {
  using V = TypeParam;

  V data[4] = {V(1), V(0), V(0), V(0)};
  Quaternion<V> q(data);

  EXPECT_EQ(q.w(), V(1));
  EXPECT_EQ(q.x(), V(0));
  EXPECT_EQ(q.y(), V(0));
  EXPECT_EQ(q.z(), V(0));
}

/* --------------------------- Set Operations ------------------------------ */

/** @test setIdentity produces [1, 0, 0, 0]. */
TYPED_TEST(QuaternionTestT, SetIdentity) {
  using V = TypeParam;

  V data[4] = {V(0), V(1), V(2), V(3)};
  Quaternion<V> q(data);

  expectStatus(q.setIdentity(), Status::SUCCESS);
  EXPECT_EQ(q.w(), V(1));
  EXPECT_EQ(q.x(), V(0));
  EXPECT_EQ(q.y(), V(0));
  EXPECT_EQ(q.z(), V(0));
}

/** @test set() assigns components. */
TYPED_TEST(QuaternionTestT, SetComponents) {
  using V = TypeParam;

  V data[4] = {};
  Quaternion<V> q(data);

  expectStatus(q.set(V(0.5), V(0.5), V(0.5), V(0.5)), Status::SUCCESS);
  EXPECT_EQ(q.w(), V(0.5));
  EXPECT_EQ(q.x(), V(0.5));
  EXPECT_EQ(q.y(), V(0.5));
  EXPECT_EQ(q.z(), V(0.5));
}

/** @test setFromAngleAxis for 90-degree rotation about Z-axis. */
TYPED_TEST(QuaternionTestT, SetFromAngleAxis) {
  using V = TypeParam;

  V data[4] = {};
  Quaternion<V> q(data);

  const V ANGLE = static_cast<V>(K_PI / 2); // 90 degrees
  expectStatus(q.setFromAngleAxis(ANGLE, V(0), V(0), V(1)), Status::SUCCESS);

  // Expected: [cos(45deg), 0, 0, sin(45deg)]
  const V EXPECTED_W = std::cos(ANGLE / V(2));
  const V EXPECTED_Z = std::sin(ANGLE / V(2));

  EXPECT_NEAR(q.w(), EXPECTED_W, tol<V>());
  EXPECT_NEAR(q.x(), V(0), tol<V>());
  EXPECT_NEAR(q.y(), V(0), tol<V>());
  EXPECT_NEAR(q.z(), EXPECTED_Z, tol<V>());
}

/* --------------------------- Normalization ------------------------------- */

/** @test normInto computes Euclidean norm. */
TYPED_TEST(QuaternionTestT, NormInto) {
  using V = TypeParam;

  V data[4] = {V(1), V(2), V(3), V(4)};
  Quaternion<V> q(data);

  V nrm = V(0);
  expectStatus(q.normInto(nrm), Status::SUCCESS);
  EXPECT_NEAR(nrm, std::sqrt(V(30)), tol<V>());
}

/** @test normalizeInPlace produces unit quaternion. */
TYPED_TEST(QuaternionTestT, NormalizeInPlace) {
  using V = TypeParam;

  V data[4] = {V(1), V(2), V(3), V(4)};
  Quaternion<V> q(data);

  expectStatus(q.normalizeInPlace(), Status::SUCCESS);

  V nrm = V(0);
  (void)q.normInto(nrm);
  EXPECT_NEAR(nrm, V(1), tol<V>());
}

/** @test normalizeInPlace on zero quaternion returns error. */
TYPED_TEST(QuaternionTestT, NormalizeInPlace_Zero) {
  using V = TypeParam;

  V data[4] = {V(0), V(0), V(0), V(0)};
  Quaternion<V> q(data);

  expectStatus(q.normalizeInPlace(), Status::ERROR_INVALID_VALUE);
}

/* --------------------------- Conjugate / Inverse ------------------------- */

/** @test conjugateInto flips imaginary parts. */
TYPED_TEST(QuaternionTestT, ConjugateInto) {
  using V = TypeParam;

  V data[4] = {V(1), V(2), V(3), V(4)};
  V outData[4] = {};
  Quaternion<V> q(data);
  Quaternion<V> out(outData);

  expectStatus(q.conjugateInto(out), Status::SUCCESS);
  EXPECT_EQ(out.w(), V(1));
  EXPECT_EQ(out.x(), V(-2));
  EXPECT_EQ(out.y(), V(-3));
  EXPECT_EQ(out.z(), V(-4));
}

/** @test inverseInto computes q^{-1}. */
TYPED_TEST(QuaternionTestT, InverseInto) {
  using V = TypeParam;

  // Unit quaternion: inverse = conjugate
  const V S = std::sqrt(V(2)) / V(2);
  V data[4] = {S, S, V(0), V(0)};
  V outData[4] = {};
  Quaternion<V> q(data);
  Quaternion<V> inv(outData);

  expectStatus(q.inverseInto(inv), Status::SUCCESS);
  EXPECT_NEAR(inv.w(), S, tol<V>());
  EXPECT_NEAR(inv.x(), -S, tol<V>());
  EXPECT_NEAR(inv.y(), V(0), tol<V>());
  EXPECT_NEAR(inv.z(), V(0), tol<V>());
}

/* ---------------------- Quaternion Multiplication ------------------------ */

/** @test multiplyInto follows Hamilton product. */
TYPED_TEST(QuaternionTestT, MultiplyInto) {
  using V = TypeParam;

  // Two 90-degree rotations about Z should give 180-degree rotation
  const V HALF_90 = static_cast<V>(K_PI / 4);
  const V COS45 = std::cos(HALF_90);
  const V SIN45 = std::sin(HALF_90);

  V data1[4] = {COS45, V(0), V(0), SIN45};
  V data2[4] = {COS45, V(0), V(0), SIN45};
  V outData[4] = {};

  Quaternion<V> q1(data1);
  Quaternion<V> q2(data2);
  Quaternion<V> out(outData);

  expectStatus(q1.multiplyInto(q2, out), Status::SUCCESS);

  // Expected: 180-degree rotation about Z = [0, 0, 0, 1]
  EXPECT_NEAR(out.w(), V(0), tol<V>());
  EXPECT_NEAR(out.x(), V(0), tol<V>());
  EXPECT_NEAR(out.y(), V(0), tol<V>());
  EXPECT_NEAR(out.z(), V(1), tol<V>());
}

/* ---------------------- Vector Rotation ---------------------------------- */

/** @test rotateVectorInto rotates correctly. */
TYPED_TEST(QuaternionTestT, RotateVectorInto) {
  using V = TypeParam;

  // 90-degree rotation about Z-axis
  V qData[4] = {};
  Quaternion<V> q(qData);
  q.setFromAngleAxis(static_cast<V>(K_PI / 2), V(0), V(0), V(1));

  // Rotate [1, 0, 0] -> expected [0, 1, 0]
  V vIn[3] = {V(1), V(0), V(0)};
  V vOut[3] = {};

  expectStatus(q.rotateVectorInto(vIn, vOut), Status::SUCCESS);
  EXPECT_NEAR(vOut[0], V(0), tol<V>());
  EXPECT_NEAR(vOut[1], V(1), tol<V>());
  EXPECT_NEAR(vOut[2], V(0), tol<V>());
}

/** @test rotateVectorInto 180-degree rotation. */
TYPED_TEST(QuaternionTestT, RotateVectorInto_180Deg) {
  using V = TypeParam;

  // 180-degree rotation about Z-axis
  V qData[4] = {};
  Quaternion<V> q(qData);
  q.setFromAngleAxis(static_cast<V>(K_PI), V(0), V(0), V(1));

  // Rotate [1, 0, 0] -> expected [-1, 0, 0]
  V vIn[3] = {V(1), V(0), V(0)};
  V vOut[3] = {};

  expectStatus(q.rotateVectorInto(vIn, vOut), Status::SUCCESS);
  EXPECT_NEAR(vOut[0], V(-1), tol<V>());
  EXPECT_NEAR(vOut[1], V(0), tol<V>());
  EXPECT_NEAR(vOut[2], V(0), tol<V>());
}

/* ---------------------- Conversion Operations ---------------------------- */

/** @test toRotationMatrixInto produces correct 3x3 matrix. */
TYPED_TEST(QuaternionTestT, ToRotationMatrixInto) {
  using V = TypeParam;

  // 90-degree rotation about Z-axis
  V qData[4] = {};
  Quaternion<V> q(qData);
  q.setFromAngleAxis(static_cast<V>(K_PI / 2), V(0), V(0), V(1));

  V mat[9] = {};
  expectStatus(q.toRotationMatrixInto(mat), Status::SUCCESS);

  // Expected rotation matrix for 90-deg about Z:
  // [ 0, -1,  0]
  // [ 1,  0,  0]
  // [ 0,  0,  1]
  EXPECT_NEAR(mat[0], V(0), tol<V>());
  EXPECT_NEAR(mat[1], V(-1), tol<V>());
  EXPECT_NEAR(mat[2], V(0), tol<V>());
  EXPECT_NEAR(mat[3], V(1), tol<V>());
  EXPECT_NEAR(mat[4], V(0), tol<V>());
  EXPECT_NEAR(mat[5], V(0), tol<V>());
  EXPECT_NEAR(mat[6], V(0), tol<V>());
  EXPECT_NEAR(mat[7], V(0), tol<V>());
  EXPECT_NEAR(mat[8], V(1), tol<V>());
}

/** @test toAngleAxisInto extracts angle and axis correctly. */
TYPED_TEST(QuaternionTestT, ToAngleAxisInto) {
  using V = TypeParam;

  // 90-degree rotation about Z-axis
  V qData[4] = {};
  Quaternion<V> q(qData);
  const V ANGLE_IN = static_cast<V>(K_PI / 2);
  q.setFromAngleAxis(ANGLE_IN, V(0), V(0), V(1));

  V angle = V(0), axisX = V(0), axisY = V(0), axisZ = V(0);
  expectStatus(q.toAngleAxisInto(angle, axisX, axisY, axisZ), Status::SUCCESS);

  EXPECT_NEAR(angle, ANGLE_IN, tol<V>());
  EXPECT_NEAR(axisX, V(0), tol<V>());
  EXPECT_NEAR(axisY, V(0), tol<V>());
  EXPECT_NEAR(axisZ, V(1), tol<V>());
}

/* ---------------------- SLERP Interpolation ------------------------------ */

/** @test slerpInto at t=0 returns first quaternion. */
TYPED_TEST(QuaternionTestT, SlerpInto_T0) {
  using V = TypeParam;

  V data1[4] = {V(1), V(0), V(0), V(0)};
  V data2[4] = {V(0), V(0), V(0), V(1)};
  V outData[4] = {};

  Quaternion<V> q1(data1);
  Quaternion<V> q2(data2);
  Quaternion<V> out(outData);

  expectStatus(q1.slerpInto(q2, V(0), out), Status::SUCCESS);
  EXPECT_NEAR(out.w(), V(1), tol<V>());
  EXPECT_NEAR(out.x(), V(0), tol<V>());
  EXPECT_NEAR(out.y(), V(0), tol<V>());
  EXPECT_NEAR(out.z(), V(0), tol<V>());
}

/** @test slerpInto at t=1 returns second quaternion. */
TYPED_TEST(QuaternionTestT, SlerpInto_T1) {
  using V = TypeParam;

  V data1[4] = {V(1), V(0), V(0), V(0)};
  V data2[4] = {V(0), V(0), V(0), V(1)};
  V outData[4] = {};

  Quaternion<V> q1(data1);
  Quaternion<V> q2(data2);
  Quaternion<V> out(outData);

  expectStatus(q1.slerpInto(q2, V(1), out), Status::SUCCESS);
  EXPECT_NEAR(out.w(), V(0), tol<V>());
  EXPECT_NEAR(out.x(), V(0), tol<V>());
  EXPECT_NEAR(out.y(), V(0), tol<V>());
  EXPECT_NEAR(out.z(), V(1), tol<V>());
}

/** @test slerpInto at t=0.5 produces midpoint rotation. */
TYPED_TEST(QuaternionTestT, SlerpInto_T05) {
  using V = TypeParam;

  // Identity to 180-deg about Z
  V data1[4] = {V(1), V(0), V(0), V(0)};
  V data2[4] = {V(0), V(0), V(0), V(1)};
  V outData[4] = {};

  Quaternion<V> q1(data1);
  Quaternion<V> q2(data2);
  Quaternion<V> out(outData);

  expectStatus(q1.slerpInto(q2, V(0.5), out), Status::SUCCESS);

  // Midpoint: 90-deg about Z
  const V EXPECTED = std::sqrt(V(2)) / V(2);
  EXPECT_NEAR(out.w(), EXPECTED, tol<V>());
  EXPECT_NEAR(out.x(), V(0), tol<V>());
  EXPECT_NEAR(out.y(), V(0), tol<V>());
  EXPECT_NEAR(out.z(), EXPECTED, tol<V>());
}
