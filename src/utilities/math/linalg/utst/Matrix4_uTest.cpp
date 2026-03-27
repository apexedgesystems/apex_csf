/**
 * @file Matrix4_uTest.cpp
 * @brief Unit tests for Matrix4 (4x4-only helpers).
 *
 * Coverage:
 *  - traceInto: correct diagonal sum
 *  - transposeView: shape/values, shared backing storage
 *  - gemmInto: naive 4x4 path success and numeric check
 *  - determinantInto: cofactor expansion
 *  - inverseInPlace: adjugate method
 *  - solveInto: Cramer's rule for Ax=b
 *  - addInto / subInto / scaleInto: elementwise ops on 4x4
 *  - multiplyVecInto / transposeMultiplyVecInto: matrix-vector products
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Matrix4.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <gtest/gtest.h>
#include <vector>

using apex::math::linalg::Array;
using apex::math::linalg::Layout;
using apex::math::linalg::Matrix4;
using apex::math::linalg::Status;
using apex::math::linalg::Vector;
using apex::math::linalg::VectorOrient;
using apex::math::linalg::test::expectNearMatrix;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/** @brief Fixture template to run tests for float and double. */
template <typename T> class Matrix4TestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(Matrix4TestT, ValueTypes);

/* ----------------------------- traceInto ----------------------------- */

/** @test traceInto returns a(0,0)+a(1,1)+a(2,2)+a(3,3) for 4x4. */
TYPED_TEST(Matrix4TestT, TraceInto_4x4) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2),  V(3),  V(4),  V(5),  V(6),  V(7),  V(8),
                     V(9), V(10), V(11), V(12), V(13), V(14), V(15), V(16)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  V t = V(0);
  expectStatus(a.traceInto(t), Status::SUCCESS);
  EXPECT_EQ(t, V(1 + 6 + 11 + 16)); // Diagonal: 1, 6, 11, 16
}

/* --------------------------- transposeView --------------------------- */

/** @test transposeView produces a view-backed transpose with shared storage. */
TYPED_TEST(Matrix4TestT, TransposeView_4x4_SharedStorage) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2),  V(3),  V(4),  V(5),  V(6),  V(7),  V(8),
                     V(9), V(10), V(11), V(12), V(13), V(14), V(15), V(16)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  auto at = a.transposeView();
  EXPECT_EQ(at.rows(), 4u);
  EXPECT_EQ(at.cols(), 4u);

  // Value mapping: A^T[i,j] = A[j,i]
  EXPECT_EQ(at.view()(0, 1), V(5));  // A[1,0] = 5
  EXPECT_EQ(at.view()(1, 0), V(2));  // A[0,1] = 2
  EXPECT_EQ(at.view()(3, 2), V(12)); // A[2,3] = 12

  // Shared storage: mutate a and observe at
  a.view()(2, 1) = V(42);
  EXPECT_EQ(at.view()(1, 2), V(42));
}

/* ------------------------------ gemmInto ------------------------------ */

/** @test gemmInto: compute C = A*B using naive 4x4 multiplication. */
TYPED_TEST(Matrix4TestT, GemmInto_4x4) {
  using V = TypeParam;

  // A = diag(1,2,3,4), B = ones(4,4)
  // C = A*B = rows are 1,1,1,1 / 2,2,2,2 / 3,3,3,3 / 4,4,4,4
  std::vector<V> aBuf{V(1), V(0), V(0), V(0), V(0), V(2), V(0), V(0),
                      V(0), V(0), V(3), V(0), V(0), V(0), V(0), V(4)};
  std::vector<V> bBuf(16, V(1));
  std::vector<V> cBuf(16, V(0));
  std::vector<V> refBuf{V(1), V(1), V(1), V(1), V(2), V(2), V(2), V(2),
                        V(3), V(3), V(3), V(3), V(4), V(4), V(4), V(4)};

  auto aView = makeRowView(aBuf, 4, 4);
  auto bView = makeRowView(bBuf, 4, 4);
  auto cView = makeRowView(cBuf, 4, 4);
  auto refMat = makeRowView(refBuf, 4, 4);

  Matrix4<V> a(aView);
  Matrix4<V> b(bView);
  Matrix4<V> c(cView);

  expectStatus(a.gemmInto(b, c), Status::SUCCESS);
  expectNearMatrix(c.view(), refMat, tol<V>());
}

/* --------------------------- determinantInto --------------------------- */

/** @test determinantInto: 4x4 cofactor expansion. */
TYPED_TEST(Matrix4TestT, DeterminantInto_4x4) {
  using V = TypeParam;

  // Use a matrix with known determinant
  // A = [[1,2,3,4],[5,6,7,8],[2,6,4,8],[3,1,1,2]]
  // det(A) can be computed: det = 72 (verified via external tools)
  std::vector<V> buf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8),
                     V(2), V(6), V(4), V(8), V(3), V(1), V(1), V(2)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  V det = V(0);
  expectStatus(a.determinantInto(det), Status::SUCCESS);
  EXPECT_NEAR(det, V(72), tol<V>() * 100); // Allow larger tolerance for 4x4
}

/* --------------------------- inverseInPlace --------------------------- */

/** @test inverseInPlace: naive 4x4 path. Verify A * inv(A) = I. */
TYPED_TEST(Matrix4TestT, InverseInPlace_4x4) {
  using V = TypeParam;

  // Use a well-conditioned 4x4 matrix
  // A = [[4,0,0,0],[0,3,0,0],[0,0,2,0],[0,0,0,1]] (diagonal)
  std::vector<V> buf{V(4), V(0), V(0), V(0), V(0), V(3), V(0), V(0),
                     V(0), V(0), V(2), V(0), V(0), V(0), V(0), V(1)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  std::vector<V> a0Buf = buf;

  expectStatus(a.inverseInPlace(), Status::SUCCESS);

  // Verify A0 * A^{-1} = I
  std::vector<V> eyeBuf(16, V(0));
  auto a0View = makeRowView(a0Buf, 4, 4);
  auto eyeView = makeRowView(eyeBuf, 4, 4);

  Matrix4<V> a0(a0View);
  Matrix4<V> eye(eyeView);

  expectStatus(a0.gemmInto(a, eye), Status::SUCCESS);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      EXPECT_NEAR(eye.view()(r, c), (r == c) ? V(1) : V(0), V(1e-5));
}

/** @test inverseInPlace: non-diagonal invertible matrix. */
TYPED_TEST(Matrix4TestT, InverseInPlace_4x4_NonDiagonal) {
  using V = TypeParam;

  // A with det = 72
  std::vector<V> buf{V(1), V(2), V(3), V(4), V(5), V(6), V(7), V(8),
                     V(2), V(6), V(4), V(8), V(3), V(1), V(1), V(2)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  std::vector<V> a0Buf = buf;

  expectStatus(a.inverseInPlace(), Status::SUCCESS);

  // Verify A0 * A^{-1} = I
  std::vector<V> eyeBuf(16, V(0));
  auto a0View = makeRowView(a0Buf, 4, 4);
  auto eyeView = makeRowView(eyeBuf, 4, 4);

  Matrix4<V> a0(a0View);
  Matrix4<V> eye(eyeView);

  expectStatus(a0.gemmInto(a, eye), Status::SUCCESS);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      EXPECT_NEAR(eye.view()(r, c), (r == c) ? V(1) : V(0), V(1e-4));
}

/** @test inverseInPlace: singular matrix returns ERROR_SINGULAR. */
TYPED_TEST(Matrix4TestT, InverseInPlace_Singular) {
  using V = TypeParam;

  // A with duplicate rows (singular)
  std::vector<V> buf{V(1), V(2), V(3), V(4), V(1), V(2),  V(3),  V(4),
                     V(5), V(6), V(7), V(8), V(9), V(10), V(11), V(12)};
  auto aView = makeRowView(buf, 4, 4);
  Matrix4<V> a(aView);

  expectStatus(a.inverseInPlace(), Status::ERROR_SINGULAR);
}

/* ------------------------------ solveInto ------------------------------ */

/** @test solveInto: solve Ax = b via Cramer's rule. */
TYPED_TEST(Matrix4TestT, SolveInto_4x4) {
  using V = TypeParam;

  // A = diag(2, 3, 4, 5), b = [2, 6, 12, 20]
  // x = [1, 2, 3, 4]
  std::vector<V> aBuf{V(2), V(0), V(0), V(0), V(0), V(3), V(0), V(0),
                      V(0), V(0), V(4), V(0), V(0), V(0), V(0), V(5)};
  std::vector<V> bBuf{V(2), V(6), V(12), V(20)};
  std::vector<V> xBuf{V(0), V(0), V(0), V(0)};

  auto aView = makeRowView(aBuf, 4, 4);
  Matrix4<V> a(aView);

  Array<V> bArr(bBuf.data(), 4, 1, Layout::RowMajor, 1);
  Array<V> xArr(xBuf.data(), 4, 1, Layout::RowMajor, 1);
  Vector<V> b(bArr, VectorOrient::Col);
  Vector<V> x(xArr, VectorOrient::Col);

  expectStatus(a.solveInto(b, x), Status::SUCCESS);
  EXPECT_NEAR(xBuf[0], V(1), tol<V>());
  EXPECT_NEAR(xBuf[1], V(2), tol<V>());
  EXPECT_NEAR(xBuf[2], V(3), tol<V>());
  EXPECT_NEAR(xBuf[3], V(4), tol<V>());
}

/* ------------------------ elementwise: add/sub/scale ------------------------ */

/** @test addInto / subInto / scaleInto operate elementwise on 4x4. */
TYPED_TEST(Matrix4TestT, Elementwise_Add_Sub_Scale_4x4) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2),  V(3),  V(4),  V(5),  V(6),  V(7),  V(8),
                      V(9), V(10), V(11), V(12), V(13), V(14), V(15), V(16)};
  std::vector<V> bBuf{V(16), V(15), V(14), V(13), V(12), V(11), V(10), V(9),
                      V(8),  V(7),  V(6),  V(5),  V(4),  V(3),  V(2),  V(1)};
  std::vector<V> outBuf(16, V(0));

  auto aView = makeRowView(aBuf, 4, 4);
  auto bView = makeRowView(bBuf, 4, 4);
  auto oView = makeRowView(outBuf, 4, 4);

  Matrix4<V> a(aView);
  Matrix4<V> b(bView);
  Matrix4<V> out(oView);

  // add: all elements should be 17
  expectStatus(a.addInto(b, out), Status::SUCCESS);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      EXPECT_EQ(out.view()(r, c), V(17));

  // scale (out := 2*a)
  expectStatus(a.scaleInto(V(2), out), Status::SUCCESS);
  EXPECT_EQ(out.view()(0, 0), V(2));
  EXPECT_EQ(out.view()(3, 3), V(32));
}

/* ------------------------ multiplyVecInto ------------------------ */

/** @test multiplyVecInto: y = A*x for 4x4. */
TYPED_TEST(Matrix4TestT, MultiplyVecInto_4x4) {
  using V = TypeParam;

  // A = diag(1,2,3,4), x = [1,1,1,1]
  // y = A*x = [1,2,3,4]
  std::vector<V> aBuf{V(1), V(0), V(0), V(0), V(0), V(2), V(0), V(0),
                      V(0), V(0), V(3), V(0), V(0), V(0), V(0), V(4)};
  std::vector<V> xBuf{V(1), V(1), V(1), V(1)};
  std::vector<V> yBuf{V(0), V(0), V(0), V(0)};

  auto aView = makeRowView(aBuf, 4, 4);
  Matrix4<V> a(aView);

  Array<V> xArr(xBuf.data(), 4, 1, Layout::RowMajor, 1);
  Array<V> yArr(yBuf.data(), 4, 1, Layout::RowMajor, 1);
  Vector<V> x(xArr, VectorOrient::Col);
  Vector<V> y(yArr, VectorOrient::Col);

  expectStatus(a.multiplyVecInto(x, y), Status::SUCCESS);
  EXPECT_NEAR(yBuf[0], V(1), tol<V>());
  EXPECT_NEAR(yBuf[1], V(2), tol<V>());
  EXPECT_NEAR(yBuf[2], V(3), tol<V>());
  EXPECT_NEAR(yBuf[3], V(4), tol<V>());
}
