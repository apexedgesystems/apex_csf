/**
 * @file Matrix2_uTest.cpp
 * @brief Unit tests for Matrix2 (2x2-only helpers).
 *
 * Coverage:
 *  - traceInto: correct diagonal sum
 *  - transposeView: shape/values, shared backing storage
 *  - gemmInto: naive 2x2 path success and numeric check
 *  - determinantInto: ad - bc formula
 *  - inverseInPlace: Cramer's rule
 *  - solveInto: Cramer's rule for Ax=b
 *  - addInto / subInto / scaleInto: elementwise ops on 2x2
 *  - multiplyVecInto / transposeMultiplyVecInto: matrix-vector products
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Matrix2.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <gtest/gtest.h>
#include <vector>

using apex::math::linalg::Array;
using apex::math::linalg::Layout;
using apex::math::linalg::Matrix2;
using apex::math::linalg::Status;
using apex::math::linalg::Vector;
using apex::math::linalg::VectorOrient;
using apex::math::linalg::test::expectNearMatrix;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/** @brief Fixture template to run tests for float and double. */
template <typename T> class Matrix2TestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(Matrix2TestT, ValueTypes);

/* ----------------------------- traceInto ----------------------------- */

/** @test traceInto returns a(0,0)+a(1,1) for 2x2. */
TYPED_TEST(Matrix2TestT, TraceInto_2x2) {
  using V = TypeParam;

  std::vector<V> buf{V(3), V(5), V(7), V(11)};
  auto aView = makeRowView(buf, 2, 2);
  Matrix2<V> a(aView);

  V t = V(0);
  expectStatus(a.traceInto(t), Status::SUCCESS);
  EXPECT_EQ(t, V(3 + 11));
}

/* --------------------------- transposeView --------------------------- */

/** @test transposeView produces a view-backed transpose with shared storage. */
TYPED_TEST(Matrix2TestT, TransposeView_2x2_SharedStorage) {
  using V = TypeParam;

  std::vector<V> buf{V(1), V(2), V(3), V(4)};
  auto aView = makeRowView(buf, 2, 2);
  Matrix2<V> a(aView);

  auto at = a.transposeView();
  EXPECT_EQ(at.rows(), 2u);
  EXPECT_EQ(at.cols(), 2u);

  // Value mapping: A^T[i,j] = A[j,i]
  EXPECT_EQ(at.view()(0, 0), V(1));
  EXPECT_EQ(at.view()(1, 0), V(2));
  EXPECT_EQ(at.view()(0, 1), V(3));
  EXPECT_EQ(at.view()(1, 1), V(4));

  // Shared storage: mutate a and observe at
  a.view()(1, 0) = V(42);
  EXPECT_EQ(at.view()(0, 1), V(42));
}

/* ------------------------------ gemmInto ------------------------------ */

/** @test gemmInto: compute C = A*B using naive 2x2 multiplication. */
TYPED_TEST(Matrix2TestT, GemmInto_2x2) {
  using V = TypeParam;

  // A = [1 2; 3 4], B = [5 6; 7 8]
  // C = A*B = [1*5+2*7, 1*6+2*8; 3*5+4*7, 3*6+4*8] = [19, 22; 43, 50]
  std::vector<V> aBuf{V(1), V(2), V(3), V(4)};
  std::vector<V> bBuf{V(5), V(6), V(7), V(8)};
  std::vector<V> refBuf{V(19), V(22), V(43), V(50)};
  std::vector<V> cBuf(4, V(0));

  auto aView = makeRowView(aBuf, 2, 2);
  auto bView = makeRowView(bBuf, 2, 2);
  auto cView = makeRowView(cBuf, 2, 2);
  auto refMat = makeRowView(refBuf, 2, 2);

  Matrix2<V> a(aView);
  Matrix2<V> b(bView);
  Matrix2<V> c(cView);

  expectStatus(a.gemmInto(b, c), Status::SUCCESS);
  expectNearMatrix(c.view(), refMat, tol<V>());
}

/** @test gemmInto with alpha and beta: C = 2*A*B + 3*C. */
TYPED_TEST(Matrix2TestT, GemmInto_2x2_AlphaBeta) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3), V(4)};
  std::vector<V> bBuf{V(5), V(6), V(7), V(8)};
  std::vector<V> cBuf{V(1), V(1), V(1), V(1)};

  // C = 2*(A*B) + 3*C = 2*[19,22;43,50] + 3*[1,1;1,1] = [41,47;89,103]
  std::vector<V> refBuf{V(41), V(47), V(89), V(103)};

  auto aView = makeRowView(aBuf, 2, 2);
  auto bView = makeRowView(bBuf, 2, 2);
  auto cView = makeRowView(cBuf, 2, 2);
  auto refMat = makeRowView(refBuf, 2, 2);

  Matrix2<V> a(aView);
  Matrix2<V> b(bView);
  Matrix2<V> c(cView);

  expectStatus(a.gemmInto(b, c, V(2), V(3)), Status::SUCCESS);
  expectNearMatrix(c.view(), refMat, tol<V>());
}

/* --------------------------- determinantInto --------------------------- */

/** @test determinantInto: 2x2 det = ad - bc. */
TYPED_TEST(Matrix2TestT, DeterminantInto_2x2) {
  using V = TypeParam;

  // A = [3 8; 4 6], det = 3*6 - 8*4 = 18 - 32 = -14
  std::vector<V> buf{V(3), V(8), V(4), V(6)};
  auto aView = makeRowView(buf, 2, 2);
  Matrix2<V> a(aView);

  V det = V(0);
  expectStatus(a.determinantInto(det), Status::SUCCESS);
  EXPECT_NEAR(det, V(-14), tol<V>());
}

/* --------------------------- inverseInPlace --------------------------- */

/** @test inverseInPlace: naive 2x2 path. Verify A * inv(A) = I. */
TYPED_TEST(Matrix2TestT, InverseInPlace_2x2) {
  using V = TypeParam;

  // A = [4 7; 2 6], det = 24-14 = 10
  std::vector<V> buf{V(4), V(7), V(2), V(6)};
  auto aView = makeRowView(buf, 2, 2);
  Matrix2<V> a(aView);

  std::vector<V> a0Buf = buf;

  expectStatus(a.inverseInPlace(), Status::SUCCESS);

  // Verify A0 * A^{-1} = I
  std::vector<V> eyeBuf(4, V(0));
  auto a0View = makeRowView(a0Buf, 2, 2);
  auto eyeView = makeRowView(eyeBuf, 2, 2);

  Matrix2<V> a0(a0View);
  Matrix2<V> eye(eyeView);

  expectStatus(a0.gemmInto(a, eye), Status::SUCCESS);
  for (int r = 0; r < 2; ++r)
    for (int c = 0; c < 2; ++c)
      EXPECT_NEAR(eye.view()(r, c), (r == c) ? V(1) : V(0), V(1e-5));
}

/** @test inverseInPlace: singular matrix returns ERROR_SINGULAR. */
TYPED_TEST(Matrix2TestT, InverseInPlace_Singular) {
  using V = TypeParam;

  // A = [1 2; 2 4], det = 4 - 4 = 0
  std::vector<V> buf{V(1), V(2), V(2), V(4)};
  auto aView = makeRowView(buf, 2, 2);
  Matrix2<V> a(aView);

  expectStatus(a.inverseInPlace(), Status::ERROR_SINGULAR);
}

/* ------------------------------ solveInto ------------------------------ */

/** @test solveInto: solve Ax = b via Cramer's rule. */
TYPED_TEST(Matrix2TestT, SolveInto_2x2) {
  using V = TypeParam;

  // A = [2 1; 1 3], b = [5; 10]
  // det(A) = 6 - 1 = 5
  // x[0] = (5*3 - 10*1) / 5 = (15-10)/5 = 1
  // x[1] = (2*10 - 1*5) / 5 = (20-5)/5 = 3
  std::vector<V> aBuf{V(2), V(1), V(1), V(3)};
  std::vector<V> bBuf{V(5), V(10)};
  std::vector<V> xBuf{V(0), V(0)};

  auto aView = makeRowView(aBuf, 2, 2);
  Matrix2<V> a(aView);

  Array<V> bArr(bBuf.data(), 2, 1, Layout::RowMajor, 1);
  Array<V> xArr(xBuf.data(), 2, 1, Layout::RowMajor, 1);
  Vector<V> b(bArr, VectorOrient::Col);
  Vector<V> x(xArr, VectorOrient::Col);

  expectStatus(a.solveInto(b, x), Status::SUCCESS);
  EXPECT_NEAR(xBuf[0], V(1), tol<V>());
  EXPECT_NEAR(xBuf[1], V(3), tol<V>());
}

/** @test solveInto: singular matrix returns ERROR_SINGULAR. */
TYPED_TEST(Matrix2TestT, SolveInto_Singular) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(2), V(4)};
  std::vector<V> bBuf{V(1), V(2)};
  std::vector<V> xBuf{V(0), V(0)};

  auto aView = makeRowView(aBuf, 2, 2);
  Matrix2<V> a(aView);

  Array<V> bArr(bBuf.data(), 2, 1, Layout::RowMajor, 1);
  Array<V> xArr(xBuf.data(), 2, 1, Layout::RowMajor, 1);
  Vector<V> b(bArr, VectorOrient::Col);
  Vector<V> x(xArr, VectorOrient::Col);

  expectStatus(a.solveInto(b, x), Status::ERROR_SINGULAR);
}

/* ------------------------ elementwise: add/sub/scale ------------------------ */

/** @test addInto / subInto / scaleInto operate elementwise on 2x2. */
TYPED_TEST(Matrix2TestT, Elementwise_Add_Sub_Scale_2x2) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3), V(4)};
  std::vector<V> bBuf{V(10), V(20), V(30), V(40)};
  std::vector<V> outBuf(4, V(0));

  auto aView = makeRowView(aBuf, 2, 2);
  auto bView = makeRowView(bBuf, 2, 2);
  auto oView = makeRowView(outBuf, 2, 2);

  Matrix2<V> a(aView);
  Matrix2<V> b(bView);
  Matrix2<V> out(oView);

  // add
  expectStatus(a.addInto(b, out), Status::SUCCESS);
  for (int r = 0; r < 2; ++r)
    for (int c = 0; c < 2; ++c)
      EXPECT_EQ(out.view()(r, c), a.view()(r, c) + b.view()(r, c));

  // sub
  expectStatus(a.subInto(b, out), Status::SUCCESS);
  for (int r = 0; r < 2; ++r)
    for (int c = 0; c < 2; ++c)
      EXPECT_EQ(out.view()(r, c), a.view()(r, c) - b.view()(r, c));

  // scale (out := 2*a)
  expectStatus(a.scaleInto(V(2), out), Status::SUCCESS);
  for (int r = 0; r < 2; ++r)
    for (int c = 0; c < 2; ++c)
      EXPECT_EQ(out.view()(r, c), V(2) * a.view()(r, c));
}

/* ------------------------ multiplyVecInto ------------------------ */

/** @test multiplyVecInto: y = A*x for 2x2. */
TYPED_TEST(Matrix2TestT, MultiplyVecInto_2x2) {
  using V = TypeParam;

  // A = [1 2; 3 4], x = [5; 6]
  // y = A*x = [1*5+2*6; 3*5+4*6] = [17; 39]
  std::vector<V> aBuf{V(1), V(2), V(3), V(4)};
  std::vector<V> xBuf{V(5), V(6)};
  std::vector<V> yBuf{V(0), V(0)};

  auto aView = makeRowView(aBuf, 2, 2);
  Matrix2<V> a(aView);

  Array<V> xArr(xBuf.data(), 2, 1, Layout::RowMajor, 1);
  Array<V> yArr(yBuf.data(), 2, 1, Layout::RowMajor, 1);
  Vector<V> x(xArr, VectorOrient::Col);
  Vector<V> y(yArr, VectorOrient::Col);

  expectStatus(a.multiplyVecInto(x, y), Status::SUCCESS);
  EXPECT_NEAR(yBuf[0], V(17), tol<V>());
  EXPECT_NEAR(yBuf[1], V(39), tol<V>());
}

/** @test transposeMultiplyVecInto: y = A^T * x for 2x2. */
TYPED_TEST(Matrix2TestT, TransposeMultiplyVecInto_2x2) {
  using V = TypeParam;

  // A = [1 2; 3 4], x = [5; 6]
  // y = A^T*x = [1*5+3*6; 2*5+4*6] = [23; 34]
  std::vector<V> aBuf{V(1), V(2), V(3), V(4)};
  std::vector<V> xBuf{V(5), V(6)};
  std::vector<V> yBuf{V(0), V(0)};

  auto aView = makeRowView(aBuf, 2, 2);
  Matrix2<V> a(aView);

  Array<V> xArr(xBuf.data(), 2, 1, Layout::RowMajor, 1);
  Array<V> yArr(yBuf.data(), 2, 1, Layout::RowMajor, 1);
  Vector<V> x(xArr, VectorOrient::Col);
  Vector<V> y(yArr, VectorOrient::Col);

  expectStatus(a.transposeMultiplyVecInto(x, y), Status::SUCCESS);
  EXPECT_NEAR(yBuf[0], V(23), tol<V>());
  EXPECT_NEAR(yBuf[1], V(34), tol<V>());
}
