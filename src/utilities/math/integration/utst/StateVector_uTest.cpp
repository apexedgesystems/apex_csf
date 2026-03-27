/**
 * @file StateVector_uTest.cpp
 * @brief Unit tests for apex::math::integration::StateVector.
 *
 * Notes:
 *  - Tests verify arithmetic, norms, and integrator compatibility.
 *  - All operations are RT-safe (stack-allocated, no heap).
 */

#include "src/utilities/math/integration/inc/StateVector.hpp"

#include <gtest/gtest.h>

#include <cmath>

using apex::math::integration::State3;
using apex::math::integration::State6;
using apex::math::integration::StateVector;

/* -------------------------- Default Construction -------------------------- */

/** @test StateVector default constructs to zeros. */
TEST(StateVector, DefaultConstruction) {
  StateVector<4> v;
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_DOUBLE_EQ(v[i], 0.0);
  }
}

/** @test StateVector constructs from initializer list. */
TEST(StateVector, InitializerListConstruction) {
  State3 v{1.0, 2.0, 3.0};
  EXPECT_DOUBLE_EQ(v[0], 1.0);
  EXPECT_DOUBLE_EQ(v[1], 2.0);
  EXPECT_DOUBLE_EQ(v[2], 3.0);
}

/** @test StateVector constructs from std::array. */
TEST(StateVector, ArrayConstruction) {
  std::array<double, 3> arr = {4.0, 5.0, 6.0};
  State3 v(arr);
  EXPECT_DOUBLE_EQ(v[0], 4.0);
  EXPECT_DOUBLE_EQ(v[1], 5.0);
  EXPECT_DOUBLE_EQ(v[2], 6.0);
}

/* -------------------------- Element Access -------------------------------- */

/** @test Size returns correct value. */
TEST(StateVector, Size) {
  EXPECT_EQ(State3::size(), 3u);
  EXPECT_EQ(State6::size(), 6u);
  EXPECT_EQ(StateVector<12>::size(), 12u);
}

/** @test Data pointer access works. */
TEST(StateVector, DataPointer) {
  State3 v{1.0, 2.0, 3.0};
  const double* ptr = v.data();
  EXPECT_DOUBLE_EQ(ptr[0], 1.0);
  EXPECT_DOUBLE_EQ(ptr[1], 2.0);
  EXPECT_DOUBLE_EQ(ptr[2], 3.0);
}

/** @test Mutable element access works. */
TEST(StateVector, MutableAccess) {
  State3 v{1.0, 2.0, 3.0};
  v[1] = 10.0;
  EXPECT_DOUBLE_EQ(v[1], 10.0);
}

/* -------------------------- Arithmetic ------------------------------------ */

/** @test Vector addition. */
TEST(StateVector, Addition) {
  State3 a{1.0, 2.0, 3.0};
  State3 b{4.0, 5.0, 6.0};
  State3 c = a + b;
  EXPECT_DOUBLE_EQ(c[0], 5.0);
  EXPECT_DOUBLE_EQ(c[1], 7.0);
  EXPECT_DOUBLE_EQ(c[2], 9.0);
}

/** @test Vector subtraction. */
TEST(StateVector, Subtraction) {
  State3 a{4.0, 5.0, 6.0};
  State3 b{1.0, 2.0, 3.0};
  State3 c = a - b;
  EXPECT_DOUBLE_EQ(c[0], 3.0);
  EXPECT_DOUBLE_EQ(c[1], 3.0);
  EXPECT_DOUBLE_EQ(c[2], 3.0);
}

/** @test Scalar multiplication (right). */
TEST(StateVector, ScalarMultiplicationRight) {
  State3 a{1.0, 2.0, 3.0};
  State3 b = a * 2.0;
  EXPECT_DOUBLE_EQ(b[0], 2.0);
  EXPECT_DOUBLE_EQ(b[1], 4.0);
  EXPECT_DOUBLE_EQ(b[2], 6.0);
}

/** @test Scalar multiplication (left). */
TEST(StateVector, ScalarMultiplicationLeft) {
  State3 a{1.0, 2.0, 3.0};
  State3 b = 3.0 * a;
  EXPECT_DOUBLE_EQ(b[0], 3.0);
  EXPECT_DOUBLE_EQ(b[1], 6.0);
  EXPECT_DOUBLE_EQ(b[2], 9.0);
}

/** @test In-place addition. */
TEST(StateVector, InPlaceAddition) {
  State3 a{1.0, 2.0, 3.0};
  State3 b{4.0, 5.0, 6.0};
  a += b;
  EXPECT_DOUBLE_EQ(a[0], 5.0);
  EXPECT_DOUBLE_EQ(a[1], 7.0);
  EXPECT_DOUBLE_EQ(a[2], 9.0);
}

/** @test In-place subtraction. */
TEST(StateVector, InPlaceSubtraction) {
  State3 a{4.0, 5.0, 6.0};
  State3 b{1.0, 2.0, 3.0};
  a -= b;
  EXPECT_DOUBLE_EQ(a[0], 3.0);
  EXPECT_DOUBLE_EQ(a[1], 3.0);
  EXPECT_DOUBLE_EQ(a[2], 3.0);
}

/** @test In-place scalar multiplication. */
TEST(StateVector, InPlaceScalarMultiplication) {
  State3 a{1.0, 2.0, 3.0};
  a *= 2.0;
  EXPECT_DOUBLE_EQ(a[0], 2.0);
  EXPECT_DOUBLE_EQ(a[1], 4.0);
  EXPECT_DOUBLE_EQ(a[2], 6.0);
}

/* -------------------------- Norms ----------------------------------------- */

/** @test Euclidean norm (L2). */
TEST(StateVector, EuclideanNorm) {
  State3 v{3.0, 4.0, 0.0};
  EXPECT_DOUBLE_EQ(v.norm(), 5.0);
}

/** @test Infinity norm. */
TEST(StateVector, InfinityNorm) {
  State3 v{1.0, -5.0, 3.0};
  EXPECT_DOUBLE_EQ(v.normInf(), 5.0);
}

/** @test Dot product. */
TEST(StateVector, DotProduct) {
  State3 a{1.0, 2.0, 3.0};
  State3 b{4.0, 5.0, 6.0};
  double dot = a.dot(b);
  EXPECT_DOUBLE_EQ(dot, 32.0); // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
}

/* -------------------------- Type Aliases ---------------------------------- */

/** @test State6 has correct size. */
TEST(StateVector, State6Size) {
  EXPECT_EQ(State6::size(), 6u);
  State6 v{1, 2, 3, 4, 5, 6};
  EXPECT_DOUBLE_EQ(v[5], 6.0);
}

/** @test StateVector works with integrator operations. */
TEST(StateVector, IntegratorCompatibility) {
  State3 y{1.0, 0.0, 0.0};
  State3 k1{0.0, 1.0, 0.0};
  double dt = 0.1;

  // Euler step: y_new = y + k1 * dt
  State3 yNew = y + k1 * dt;
  EXPECT_DOUBLE_EQ(yNew[0], 1.0);
  EXPECT_DOUBLE_EQ(yNew[1], 0.1);
  EXPECT_DOUBLE_EQ(yNew[2], 0.0);
}
