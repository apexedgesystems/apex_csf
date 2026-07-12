/**
 * @file Transform_uTest.cpp
 * @brief Tests for the SE(3) Transform: the point/vector split, composition,
 *        inversion, and the flat-POD contract.
 */

#include "src/utilities/math/frames/inc/FramesStatus.hpp"
#include "src/utilities/math/frames/inc/Transform.hpp"

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

namespace fr = apex::math::frames;
using fr::Transform;

namespace {

constexpr double K_PI = 3.14159265358979323846;

template <typename T> constexpr T tol() {
  return std::is_same<T, double>::value ? T(1e-12) : T(1e-5);
}

} // namespace

template <typename T> class TransformTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(TransformTestT, ValueTypes);

/* ------------------------------ POD contract ------------------------------ */

/** @test Flat, trivially copyable, identity by default, streamable. */
TYPED_TEST(TransformTestT, PodContract) {
  using T = TypeParam;
  static_assert(std::is_trivially_copyable<Transform<T>>::value, "must stream");
  Transform<T> x;
  EXPECT_EQ(x.q[0], T(1));
  EXPECT_EQ(x.t[2], T(0));

  x.rotation().setFromAngleAxis(T(0.5), T(0), T(0), T(1));
  x.t[0] = T(2);
  Transform<T> y;
  std::memcpy(&y, &x, sizeof(x));
  EXPECT_EQ(y.q[3], x.q[3]);
  EXPECT_EQ(y.t[0], T(2));
}

/* --------------------------- Point/vector split --------------------------- */

/** @test A point picks up the lever arm; a direction does not. */
TYPED_TEST(TransformTestT, PointGetsLeverArmVectorDoesNot) {
  using T = TypeParam;
  Transform<T> x; // identity rotation
  x.t[0] = T(10);
  x.t[1] = T(-3);

  const T IN[3] = {T(1), T(2), T(3)};
  T p[3], v[3];
  ASSERT_EQ(fr::transformPointInto(x, IN, p), 0);
  ASSERT_EQ(fr::rotateVectorInto(x, IN, v), 0);

  EXPECT_NEAR(p[0], T(11), tol<T>());
  EXPECT_NEAR(p[1], T(-1), tol<T>());
  EXPECT_NEAR(p[2], T(3), tol<T>());
  EXPECT_NEAR(v[0], T(1), tol<T>());
  EXPECT_NEAR(v[1], T(2), tol<T>());
  EXPECT_NEAR(v[2], T(3), tol<T>());
}

/** @test Hand-derived case: +90 deg yaw with an offset (child +X -> parent +Y). */
TYPED_TEST(TransformTestT, QuarterTurnWithOffsetHandDerived) {
  using T = TypeParam;
  Transform<T> x;
  x.rotation().setFromAngleAxis(T(K_PI / 2), T(0), T(0), T(1));
  x.t[0] = T(5);

  const T PX[3] = {T(1), T(0), T(0)}; // child +X
  T out[3];
  ASSERT_EQ(fr::transformPointInto(x, PX, out), 0);
  EXPECT_NEAR(out[0], T(5), tol<T>() * T(4)); // rotates onto +Y, then +5 on X
  EXPECT_NEAR(out[1], T(1), tol<T>() * T(4));
  EXPECT_NEAR(out[2], T(0), tol<T>() * T(4));
}

/* ------------------------------- Compose ---------------------------------- */

/** @test compose(a, b) applied to p equals a(b(p)). */
TYPED_TEST(TransformTestT, ComposeEqualsSequentialApplication) {
  using T = TypeParam;
  Transform<T> a, b, ab;
  a.rotation().setFromEuler321(T(0.2), T(-0.3), T(0.9));
  a.t[0] = T(1);
  a.t[2] = T(-2);
  b.rotation().setFromAngleAxis(T(0.7), T(0), T(1), T(0));
  b.t[1] = T(4);

  ASSERT_EQ(fr::composeInto(a, b, ab), 0);

  const T P[3] = {T(0.5), T(-1.5), T(2.5)};
  T viaB[3], seq[3], direct[3];
  ASSERT_EQ(fr::transformPointInto(b, P, viaB), 0);
  ASSERT_EQ(fr::transformPointInto(a, viaB, seq), 0);
  ASSERT_EQ(fr::transformPointInto(ab, P, direct), 0);

  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-4);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(direct[i], seq[i], TOL);
  }
}

/** @test Composition is associative: (a b) c == a (b c). */
TYPED_TEST(TransformTestT, ComposeAssociative) {
  using T = TypeParam;
  Transform<T> a, b, c;
  a.rotation().setFromEuler321(T(0.1), T(0.2), T(0.3));
  a.t[0] = T(1);
  b.rotation().setFromEuler321(T(-0.4), T(0.5), T(-0.6));
  b.t[1] = T(2);
  c.rotation().setFromEuler321(T(0.7), T(-0.8), T(0.9));
  c.t[2] = T(3);

  Transform<T> ab, ab_c, bc, a_bc;
  ASSERT_EQ(fr::composeInto(a, b, ab), 0);
  ASSERT_EQ(fr::composeInto(ab, c, ab_c), 0);
  ASSERT_EQ(fr::composeInto(b, c, bc), 0);
  ASSERT_EQ(fr::composeInto(a, bc, a_bc), 0);

  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-4);
  for (int i = 0; i < 4; ++i) {
    EXPECT_NEAR(ab_c.q[i], a_bc.q[i], TOL);
  }
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(ab_c.t[i], a_bc.t[i], TOL);
  }
}

/* ------------------------------- Inverse ---------------------------------- */

/** @test x^-1 undoes x on points; compose(x, x^-1) is the identity. */
TYPED_TEST(TransformTestT, InverseRoundTrip) {
  using T = TypeParam;
  Transform<T> x, inv, id;
  x.rotation().setFromEuler321(T(0.3), T(-0.7), T(1.9));
  x.t[0] = T(3);
  x.t[1] = T(-1);
  x.t[2] = T(0.5);

  ASSERT_EQ(fr::inverseInto(x, inv), 0);

  const T P[3] = {T(2), T(4), T(-6)};
  T fwd[3], back[3];
  ASSERT_EQ(fr::transformPointInto(x, P, fwd), 0);
  ASSERT_EQ(fr::transformPointInto(inv, fwd, back), 0);
  const T TOL = std::is_same<T, double>::value ? T(1e-12) : T(1e-4);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(back[i], P[i], TOL);
  }

  ASSERT_EQ(fr::composeInto(x, inv, id), 0);
  EXPECT_NEAR(std::abs(static_cast<double>(id.q[0])), 1.0, 1e-6);
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(id.t[i], T(0), TOL);
  }
}

/** @test Status helpers behave; the vocabulary is wired. */
TYPED_TEST(TransformTestT, StatusVocabulary) {
  EXPECT_TRUE(fr::ok(fr::Status::SUCCESS));
  EXPECT_TRUE(fr::failed(fr::Status::ERROR_NO_PATH));
  EXPECT_FALSE(fr::ok(fr::Status::ERROR_CAPACITY));
}
