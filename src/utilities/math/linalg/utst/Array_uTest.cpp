/**
 * @file Array_uTest.cpp
 * @brief Unit tests for Array GEMM, Transpose, Inverse, Determinant, and Trace.
 */

#include "src/utilities/compatibility/inc/compat_blas.hpp"
#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

using apex::math::linalg::Array;
using apex::math::linalg::Layout;
using apex::math::linalg::Status;
using apex::math::linalg::test::expectNearMatrix;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::fillSequential;
using apex::math::linalg::test::makeColView;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/* ---------------------------------- GEMM ---------------------------------- */

/** @test GEMM (row-major, tight): float path basic multiply with alpha=1, beta=0. */
TEST(ArrayGemmTest, Gemm_RowMajor_Float) {
  using value_t = float;

  std::vector<value_t> aBuf(2 * 3), bBuf(3 * 2), cBuf(2 * 2, value_t(0)), refBuf(2 * 2, value_t(0));
  auto a = makeRowView(aBuf, 2, 3);
  auto b = makeRowView(bBuf, 3, 2);
  auto c = makeRowView(cBuf, 2, 2);
  auto ref = makeRowView(refBuf, 2, 2);

  fillSequential(a, value_t(1));
  fillSequential(b, value_t(1));

  for (std::size_t i = 0; i < 2; ++i) {
    for (std::size_t j = 0; j < 2; ++j) {
      value_t s = value_t(0);
      for (std::size_t k = 0; k < 3; ++k)
        s += a(i, k) * b(k, j);
      ref(i, j) = s;
    }
  }

  const uint8_t st = a.gemmInto(b, c, value_t(1), value_t(0));
  expectStatus(st, Status::SUCCESS);
  expectNearMatrix(c, ref, tol<value_t>());
}

/** @test GEMM (row-major, tight): double path with alpha/beta scaling. */
TEST(ArrayGemmTest, Gemm_RowMajor_Double_AlphaBeta) {
  using value_t = double;

  std::vector<value_t> aBuf(2 * 2), bBuf(2 * 2), cBuf(2 * 2), refBuf(2 * 2);
  auto a = makeRowView(aBuf, 2, 2);
  auto b = makeRowView(bBuf, 2, 2);
  auto c = makeRowView(cBuf, 2, 2);
  auto ref = makeRowView(refBuf, 2, 2);

  fillSequential(a, value_t(1));
  fillSequential(b, value_t(5));
  fillSequential(c, value_t(10));

  const value_t alpha = 2.0;
  const value_t beta = 0.5;
  for (std::size_t i = 0; i < 2; ++i) {
    for (std::size_t j = 0; j < 2; ++j) {
      value_t s = 0;
      for (std::size_t k = 0; k < 2; ++k)
        s += a(i, k) * b(k, j);
      ref(i, j) = alpha * s + beta * c(i, j);
    }
  }

  const uint8_t st = a.gemmInto(b, c, alpha, beta);
  expectStatus(st, Status::SUCCESS);
  expectNearMatrix(c, ref, tol<value_t>());
}

/** @test GEMM shape mismatch returns ERROR_SIZE_MISMATCH. */
TEST(ArrayGemmTest, Gemm_ShapeMismatch) {
  using value_t = double;

  std::vector<value_t> aBuf(2 * 3), bBuf(2 * 2), cBuf(2 * 2, value_t(0));
  auto a = makeRowView(aBuf, 2, 3);
  auto b = makeRowView(bBuf, 2, 2);
  auto c = makeRowView(cBuf, 2, 2);

  fillSequential(aBuf);
  fillSequential(bBuf);

  const uint8_t st = a.gemmInto(b, c);
  expectStatus(st, Status::ERROR_SIZE_MISMATCH);
}

/** @test GEMM layout mismatch across a/b/c returns ERROR_INVALID_LAYOUT. */
TEST(ArrayGemmTest, Gemm_LayoutMismatch) {
  using value_t = float;

  std::vector<value_t> aBuf(2 * 3), bBuf(3 * 2), cBuf(2 * 2, value_t(0));
  auto a = makeRowView(aBuf, 2, 3);
  auto b = makeColView(bBuf, 3, 2);
  auto c = makeRowView(cBuf, 2, 2);

  fillSequential(a);
  fillSequential(b);

  const uint8_t st = a.gemmInto(b, c);
  expectStatus(st, Status::ERROR_INVALID_LAYOUT);
}

/* ------------------------------- Transpose -------------------------------- */

/** @brief Make a col-major view with explicit non-tight ld. */
template <typename T>
static Array<T> makeColViewWithLd(std::vector<T>& buf, std::size_t rows, std::size_t cols,
                                  std::size_t ld) {
  return Array<T>(buf.data(), rows, cols, Layout::ColMajor, ld);
}

/** @brief Fixture template for float and double. */
template <typename T> class ArrayTransposeTestT : public ::testing::Test {};
using TransposeTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(ArrayTransposeTestT, TransposeTypes);

/** @test Row-major to row-major, tight source/destination. */
TYPED_TEST(ArrayTransposeTestT, RowMajor_Tight) {
  using ValueT = TypeParam;

  std::vector<ValueT> aBuf(2 * 3), atBuf(3 * 2), refBuf(3 * 2, ValueT(0));
  auto a = makeRowView(aBuf, 2, 3);
  auto at = makeRowView(atBuf, 3, 2);
  auto ref = makeRowView(refBuf, 3, 2);

  fillSequential(a);

  for (std::size_t r = 0; r < a.rows(); ++r)
    for (std::size_t c = 0; c < a.cols(); ++c)
      ref(c, r) = a(r, c);

  const uint8_t st = a.transposeInto(at);
  expectStatus(st, Status::SUCCESS);
  expectNearMatrix(at, ref, tol<ValueT>());
}

/** @test Mismatched destination shape is rejected. */
TYPED_TEST(ArrayTransposeTestT, MismatchShape) {
  using ValueT = TypeParam;

  std::vector<ValueT> aBuf(2 * 3), atBadBuf(3 * 3);
  auto a = makeRowView(aBuf, 2, 3);
  auto atBad = makeRowView(atBadBuf, 3, 3);

  fillSequential(a);

  const uint8_t st = a.transposeInto(atBad);
  expectStatus(st, Status::ERROR_SIZE_MISMATCH);
}

/** @test Non-contiguous source is rejected. */
TYPED_TEST(ArrayTransposeTestT, NonContiguous_Source) {
  using ValueT = TypeParam;

  const std::size_t rows = 2, cols = 3, ldSrc = cols + 2;
  std::vector<ValueT> aBuf((rows - 1) * ldSrc + cols), atBuf(cols * rows);

  Array<ValueT> a(aBuf.data(), rows, cols, Layout::RowMajor, ldSrc);
  auto at = makeRowView(atBuf, cols, rows);

  fillSequential(a);

  const uint8_t st = a.transposeInto(at);
  expectStatus(st, Status::ERROR_NON_CONTIGUOUS);
}

/* ------------------------------- Inverse ---------------------------------- */

template <typename T> class ArrayInverseTestT : public ::testing::Test {};
using InverseTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(ArrayInverseTestT, InverseTypes);

/** @test inverseInPlace succeeds for row-major tight 2x2 with known inverse. */
TYPED_TEST(ArrayInverseTestT, InverseInPlace_RowMajor_Tight_2x2) {
  using V = TypeParam;

  std::vector<V> aBuf{V(4), V(7), V(2), V(6)};
  auto a = makeRowView(aBuf, 2, 2);

  std::vector<V> refBuf{V(0.6), V(-0.7), V(-0.2), V(0.4)};
  auto ref = makeRowView(refBuf, 2, 2);

  const uint8_t st = a.inverseInPlace();
  expectStatus(st, Status::SUCCESS);

  const auto ABS_TOL = std::is_same<V, float>::value ? V(5e-5f) : tol<V>();
  expectNearMatrix(a, ref, ABS_TOL);
}

/** @test inverseInPlace returns ERROR_SINGULAR for singular matrices. */
TYPED_TEST(ArrayInverseTestT, InverseInPlace_Singular) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(1), V(2)};
  auto a = makeRowView(aBuf, 2, 2);

  expectStatus(a.inverseInPlace(), Status::ERROR_SINGULAR);
}

/** @test inverseInPlace returns ERROR_NOT_SQUARE for non-square inputs. */
TYPED_TEST(ArrayInverseTestT, InverseInPlace_NonSquare) {
  using V = TypeParam;

  std::vector<V> aBuf(2 * 3, V(0));
  auto a = makeRowView(aBuf, 2, 3);

  expectStatus(a.inverseInPlace(), Status::ERROR_NOT_SQUARE);
}

/** @test inverseInto succeeds and writes into dst. */
TYPED_TEST(ArrayInverseTestT, InverseInto_RowToRow_3x3) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3), V(0), V(1), V(4), V(5), V(6), V(0)};
  auto a = makeRowView(aBuf, 3, 3);

  std::vector<V> dstBuf(3 * 3, V(0));
  auto dst = makeRowView(dstBuf, 3, 3);

  std::vector<V> refBuf{V(-24), V(18), V(5), V(20), V(-15), V(-4), V(-5), V(4), V(1)};
  auto ref = makeRowView(refBuf, 3, 3);

  const uint8_t st = a.inverseInto(dst);
  expectStatus(st, Status::SUCCESS);

  const auto ABS_TOL = std::is_same<V, float>::value ? V(5e-5f) : tol<V>();
  expectNearMatrix(dst, ref, ABS_TOL);
}

/* ----------------------------- Determinant -------------------------------- */

template <typename T> class ArrayDetTraceTestT : public ::testing::Test {};
using DetTraceTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(ArrayDetTraceTestT, DetTraceTypes);

/** @test determinant on 2x2 (row-major, tight). */
TYPED_TEST(ArrayDetTraceTestT, Determinant_RowMajor_2x2) {
  using V = TypeParam;
  std::vector<V> aBuf{V(4), V(7), V(2), V(6)};
  auto a = makeRowView(aBuf, 2, 2);

  V detVal = V(0);
  const uint8_t st = a.determinant(detVal);
  expectStatus(st, Status::SUCCESS);
  EXPECT_NEAR(detVal, V(10), tol<V>());
}

/** @test determinant on 3x3 (row-major, tight). */
TYPED_TEST(ArrayDetTraceTestT, Determinant_RowMajor_3x3) {
  using V = TypeParam;
  std::vector<V> aBuf{V(1), V(2), V(3), V(0), V(1), V(4), V(5), V(6), V(0)};
  auto a = makeRowView(aBuf, 3, 3);

  V detVal = V(0);
  const uint8_t st = a.determinant(detVal);
  expectStatus(st, Status::SUCCESS);
  EXPECT_NEAR(detVal, V(1), tol<V>());
}

/** @test determinant rejects non-square matrices. */
TYPED_TEST(ArrayDetTraceTestT, Determinant_NonSquare) {
  using V = TypeParam;
  std::vector<V> aBuf(2 * 3, V(0));
  auto a = makeRowView(aBuf, 2, 3);

  V detVal = V(0);
  expectStatus(a.determinant(detVal), Status::ERROR_NOT_SQUARE);
}

/* --------------------------------- Trace ---------------------------------- */

/** @test trace is the same across row/col layouts for the same logical matrix. */
TYPED_TEST(ArrayDetTraceTestT, Trace_RowVsCol_Match) {
  using V = TypeParam;

  std::vector<V> rowBuf{V(1), V(2), V(3), V(0), V(1), V(4), V(5), V(6), V(0)};
  auto aRow = makeRowView(rowBuf, 3, 3);

  std::vector<V> colBuf{V(1), V(0), V(5), V(2), V(1), V(6), V(3), V(4), V(0)};
  auto aCol = makeColView(colBuf, 3, 3);

  V tRow = V(0), tCol = V(0);
  expectStatus(aRow.trace(tRow), Status::SUCCESS);
  expectStatus(aCol.trace(tCol), Status::SUCCESS);

  EXPECT_NEAR(tRow, V(2), tol<V>());
  EXPECT_NEAR(tCol, V(2), tol<V>());
}

/** @test trace rejects non-square matrices. */
TYPED_TEST(ArrayDetTraceTestT, Trace_NonSquare) {
  using V = TypeParam;

  std::vector<V> aBuf(2 * 3, V(0));
  auto a = makeRowView(aBuf, 2, 3);

  V tr = V(0);
  expectStatus(a.trace(tr), Status::ERROR_NOT_SQUARE);
}
