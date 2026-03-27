/**
 * @file Matrix3_uTest.cpp
 * @brief Unit tests for Matrix3 (3x3-only helpers).
 *
 * Coverage:
 *  - traceInto: correct diagonal sum
 *  - transposeView: shape/values, shared backing storage
 *  - gemmInto: naive 3x3 path success and numeric check
 *  - determinantInto: naive path; numeric check
 *  - inverseInPlace: naive path; numeric check
 *  - addInto / subInto / scaleInto: elementwise ops on 3x3
 */

#include "src/utilities/compatibility/inc/compat_blas.hpp"
#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Matrix3.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <gtest/gtest.h>
#include <vector>

using apex::math::linalg::Array;
using apex::math::linalg::Layout;
using apex::math::linalg::Matrix3;
using apex::math::linalg::Status;
using apex::math::linalg::test::expectNearMatrix;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::expectStatusOneOf;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/** @brief Fixture template to run tests for float and double. */
template <typename T> class Matrix3TestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(Matrix3TestT, ValueTypes);

/* ----------------------------- traceInto ----------------------------- */

/** @test traceInto returns a(0,0)+a(1,1)+a(2,2) for 3x3. */
TYPED_TEST(Matrix3TestT, TraceInto_3x3) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8), V(9)};
  auto aView = makeRowView(buf, 3, 3);
  Matrix3<V> a(aView);

  V t = V(0);
  expectStatus(a.traceInto(t), Status::SUCCESS);
  EXPECT_EQ(t, V(1 + 5 + 9));
}

/* --------------------------- transposeView --------------------------- */

/** @test transposeView produces a view-backed transpose with shared storage. */
TYPED_TEST(Matrix3TestT, TransposeView_3x3_SharedStorage) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8), V(9)};
  auto aView = makeRowView(buf, 3, 3);
  Matrix3<V> a(aView);

  auto at = a.transposeView();
  EXPECT_EQ(at.rows(), 3u);
  EXPECT_EQ(at.cols(), 3u);

  // Value mapping
  EXPECT_EQ(at.view()(0, 0), V(1));
  EXPECT_EQ(at.view()(1, 0), V(2));
  EXPECT_EQ(at.view()(2, 0), V(3));
  EXPECT_EQ(at.view()(0, 1), V(4));
  EXPECT_EQ(at.view()(1, 1), V(5));
  EXPECT_EQ(at.view()(2, 1), V(6));
  EXPECT_EQ(at.view()(0, 2), V(7));
  EXPECT_EQ(at.view()(1, 2), V(8));
  EXPECT_EQ(at.view()(2, 2), V(9));

  // Shared storage: mutate a and observe at
  a.view()(2, 1) = V(42);
  EXPECT_EQ(at.view()(1, 2), V(42));
}

/* ------------------------------ gemmInto ------------------------------ */

/** @test gemmInto: compute C = A*B using naive 3x3 multiplication. */
TYPED_TEST(Matrix3TestT, GemmInto_3x3) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8), V(9)};
  std::vector<V> bBuf{V(9), V(8), V(7), V(6), V(5), V(4), V(3), V(2), V(1)};
  std::vector<V> refBuf{V(30), V(24), V(18), V(84), V(69), V(54), V(138), V(114), V(90)};
  std::vector<V> cBuf(9, V(0));

  auto aView = makeRowView(aBuf, 3, 3);
  auto bView = makeRowView(bBuf, 3, 3);
  auto cView = makeRowView(cBuf, 3, 3);
  auto refMat = makeRowView(refBuf, 3, 3);

  Matrix3<V> a(aView);
  Matrix3<V> b(bView);
  Matrix3<V> c(cView);

  expectStatus(a.gemmInto(b, c), Status::SUCCESS);
  expectNearMatrix(c.view(), refMat, tol<V>());
}

/* --------------------------- determinantInto --------------------------- */

/** @test determinantInto: naive 3x3 path. Matrix chosen with known det = -306. */
TYPED_TEST(Matrix3TestT, DeterminantInto_3x3) {
  using V = TypeParam;

  std::vector<V> buf{V(6), V(1), V(1), V(4), V(-2), V(5), V(2), V(8), V(7)};
  auto aView = makeRowView(buf, 3, 3);
  Matrix3<V> a(aView);

  V det = V(0);
  auto st = a.determinantInto(det);
  expectStatus(st, Status::SUCCESS);
  EXPECT_NEAR(det, V(-306), tol<V>());
}

/* --------------------------- inverseInPlace --------------------------- */

/** @test inverseInPlace: naive 3x3 path. Verify a0 * inv(a) is approx I. */
TYPED_TEST(Matrix3TestT, InverseInPlace_3x3) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2), V(3), V(0), V(1), V(4), V(5), V(6), V(0)};
  auto aView = makeRowView(buf, 3, 3);
  Matrix3<V> a(aView);

  std::vector<V> a0Buf = buf;

  auto st = a.inverseInPlace();
  expectStatus(st, Status::SUCCESS);

  // Verify A * A^{-1} = I
  std::vector<V> eyeBuf(9, V(0));
  auto a0View = makeRowView(a0Buf, 3, 3);
  auto eyeView = makeRowView(eyeBuf, 3, 3);

  Matrix3<V> a0(a0View);
  Matrix3<V> eye(eyeView);

  expectStatus(a0.gemmInto(a, eye), Status::SUCCESS);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      EXPECT_NEAR(eye.view()(r, c), (r == c) ? V(1) : V(0), V(1e-3));
}

/* ------------------------ elementwise: add/sub/scale ------------------------ */

/** @test addInto / subInto / scaleInto operate elementwise on 3x3. */
TYPED_TEST(Matrix3TestT, Elementwise_Add_Sub_Scale_3x3) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8), V(9)};
  std::vector<V> bBuf{V(10), V(11), V(12), V(13), V(14), V(15), V(16), V(17), V(18)};
  std::vector<V> outBuf(9, V(0));

  auto aView = makeRowView(aBuf, 3, 3);
  auto bView = makeRowView(bBuf, 3, 3);
  auto oView = makeRowView(outBuf, 3, 3);

  Matrix3<V> a(aView);
  Matrix3<V> b(bView);
  Matrix3<V> out(oView);

  // add
  expectStatus(a.addInto(b, out), Status::SUCCESS);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      EXPECT_EQ(out.view()(r, c), a.view()(r, c) + b.view()(r, c));

  // sub
  expectStatus(a.subInto(b, out), Status::SUCCESS);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      EXPECT_EQ(out.view()(r, c), a.view()(r, c) - b.view()(r, c));

  // scale (out := 2*a)
  expectStatus(a.scaleInto(V(2), out), Status::SUCCESS);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      EXPECT_EQ(out.view()(r, c), V(2) * a.view()(r, c));
}
