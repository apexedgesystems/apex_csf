/**
 * @file ArrayBase_uTest.cpp
 * @brief Unit tests for ArrayBase non-owning view.
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayBase.hpp"
#include "src/utilities/math/linalg/inc/ArrayOps.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using apex::math::linalg::Array;
using apex::math::linalg::ArrayBase;
using apex::math::linalg::Layout;
using apex::math::linalg::Status;

/* ------------------------- Default Construction -------------------------- */

/** @test Verify ArrayBase row-major construction and accessors. */
TEST(ArrayBaseTest, RowMajorConstruction) {
  std::array<double, 6> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
  ArrayBase<double> view(data.data(), 2, 3, Layout::RowMajor);

  EXPECT_EQ(view.rows(), 2u);
  EXPECT_EQ(view.cols(), 3u);
  EXPECT_EQ(view.size(), 6u);
  EXPECT_EQ(view.ld(), 3u);
  EXPECT_EQ(view.layout(), Layout::RowMajor);
  EXPECT_TRUE(view.isContiguous());
}

/** @test Verify ArrayBase column-major construction and accessors. */
TEST(ArrayBaseTest, ColMajorConstruction) {
  std::array<float, 6> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  ArrayBase<float> view(data.data(), 2, 3, Layout::ColMajor);

  EXPECT_EQ(view.rows(), 2u);
  EXPECT_EQ(view.cols(), 3u);
  EXPECT_EQ(view.ld(), 2u);
  EXPECT_EQ(view.layout(), Layout::ColMajor);
}

/* --------------------------- Element Access ------------------------------ */

/** @test Verify row-major element access. */
TEST(ArrayBaseTest, RowMajorElementAccess) {
  // Row-major 2x3: [[1,2,3],[4,5,6]]
  std::array<double, 6> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
  ArrayBase<double> view(data.data(), 2, 3, Layout::RowMajor);

  EXPECT_DOUBLE_EQ(view(0, 0), 1.0);
  EXPECT_DOUBLE_EQ(view(0, 1), 2.0);
  EXPECT_DOUBLE_EQ(view(0, 2), 3.0);
  EXPECT_DOUBLE_EQ(view(1, 0), 4.0);
  EXPECT_DOUBLE_EQ(view(1, 1), 5.0);
  EXPECT_DOUBLE_EQ(view(1, 2), 6.0);
}

/** @test Verify column-major element access. */
TEST(ArrayBaseTest, ColMajorElementAccess) {
  // Col-major 2x3: data stored column-by-column
  // [[1,3,5],[2,4,6]] when viewed logically
  std::array<double, 6> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
  ArrayBase<double> view(data.data(), 2, 3, Layout::ColMajor);

  EXPECT_DOUBLE_EQ(view(0, 0), 1.0);
  EXPECT_DOUBLE_EQ(view(1, 0), 2.0);
  EXPECT_DOUBLE_EQ(view(0, 1), 3.0);
  EXPECT_DOUBLE_EQ(view(1, 1), 4.0);
  EXPECT_DOUBLE_EQ(view(0, 2), 5.0);
  EXPECT_DOUBLE_EQ(view(1, 2), 6.0);
}

/* --------------------------- Checked Access ------------------------------ */

/** @test Verify bounds-checked get/set. */
TEST(ArrayBaseTest, CheckedAccess) {
  std::array<double, 4> data = {1.0, 2.0, 3.0, 4.0};
  ArrayBase<double> view(data.data(), 2, 2, Layout::RowMajor);

  double val = 0.0;
  EXPECT_EQ(view.get(0, 0, val), static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(val, 1.0);

  EXPECT_EQ(view.get(1, 1, val), static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(val, 4.0);

  // Out of bounds
  EXPECT_EQ(view.get(2, 0, val), static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS));
  EXPECT_EQ(view.get(0, 2, val), static_cast<uint8_t>(Status::ERROR_OUT_OF_BOUNDS));
}

/* ---------------------------- Transpose View ----------------------------- */

/** @test Verify transpose view flips dimensions. */
TEST(ArrayBaseTest, TransposeView) {
  std::array<double, 6> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
  ArrayBase<double> view(data.data(), 2, 3, Layout::RowMajor);

  auto t = view.transposeView();
  EXPECT_EQ(t.rows(), 3u);
  EXPECT_EQ(t.cols(), 2u);
  EXPECT_EQ(t.layout(), Layout::ColMajor);

  // Original (0,1) = 2.0 should be at (1,0) in transpose
  EXPECT_DOUBLE_EQ(t(1, 0), 2.0);
}

/* ------------------------------ ArrayOps --------------------------------- */

/** @test Verify setIdentity. */
TEST(ArrayOpsTest, SetIdentity) {
  std::array<double, 9> data = {};
  Array<double> mat(data.data(), 3, 3, Layout::RowMajor);

  auto st = apex::math::linalg::setIdentity(mat);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  EXPECT_DOUBLE_EQ(mat(0, 0), 1.0);
  EXPECT_DOUBLE_EQ(mat(1, 1), 1.0);
  EXPECT_DOUBLE_EQ(mat(2, 2), 1.0);
  EXPECT_DOUBLE_EQ(mat(0, 1), 0.0);
  EXPECT_DOUBLE_EQ(mat(1, 0), 0.0);
}

/** @test Verify addInto. */
TEST(ArrayOpsTest, AddInto) {
  std::array<double, 4> a_data = {1.0, 2.0, 3.0, 4.0};
  std::array<double, 4> b_data = {5.0, 6.0, 7.0, 8.0};
  std::array<double, 4> c_data = {};

  Array<double> a(a_data.data(), 2, 2, Layout::RowMajor);
  Array<double> b(b_data.data(), 2, 2, Layout::RowMajor);
  Array<double> c(c_data.data(), 2, 2, Layout::RowMajor);

  auto st = apex::math::linalg::addInto(a, b, c);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  EXPECT_DOUBLE_EQ(c(0, 0), 6.0);
  EXPECT_DOUBLE_EQ(c(0, 1), 8.0);
  EXPECT_DOUBLE_EQ(c(1, 0), 10.0);
  EXPECT_DOUBLE_EQ(c(1, 1), 12.0);
}

/** @test Verify trace. */
TEST(ArrayOpsTest, Trace) {
  std::array<double, 9> data = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
  Array<double> mat(data.data(), 3, 3, Layout::RowMajor);

  double tr = 0.0;
  auto st = apex::math::linalg::traceInto(mat, tr);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(tr, 15.0); // 1 + 5 + 9
}

/* --------------------------- Skew-Symmetric ----------------------------- */

/** @test skew3Into creates correct skew-symmetric matrix from column vector. */
TEST(ArrayOpsTest, Skew3Into_ColVector) {
  // v = [1, 2, 3]^T
  std::array<double, 3> v_data = {1.0, 2.0, 3.0};
  Array<double> v(v_data.data(), 3, 1, Layout::RowMajor);

  std::array<double, 9> out_data = {};
  Array<double> out(out_data.data(), 3, 3, Layout::RowMajor);

  auto st = apex::math::linalg::skew3Into(v, out);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  // Expected: [0, -3, 2; 3, 0, -1; -2, 1, 0]
  EXPECT_DOUBLE_EQ(out(0, 0), 0.0);
  EXPECT_DOUBLE_EQ(out(0, 1), -3.0);
  EXPECT_DOUBLE_EQ(out(0, 2), 2.0);
  EXPECT_DOUBLE_EQ(out(1, 0), 3.0);
  EXPECT_DOUBLE_EQ(out(1, 1), 0.0);
  EXPECT_DOUBLE_EQ(out(1, 2), -1.0);
  EXPECT_DOUBLE_EQ(out(2, 0), -2.0);
  EXPECT_DOUBLE_EQ(out(2, 1), 1.0);
  EXPECT_DOUBLE_EQ(out(2, 2), 0.0);
}

/** @test skew3Into rejects non-3-element vector. */
TEST(ArrayOpsTest, Skew3Into_SizeMismatch) {
  std::array<double, 4> v_data = {1.0, 2.0, 3.0, 4.0};
  Array<double> v(v_data.data(), 4, 1, Layout::RowMajor);

  std::array<double, 9> out_data = {};
  Array<double> out(out_data.data(), 3, 3, Layout::RowMajor);

  auto st = apex::math::linalg::skew3Into(v, out);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH));
}

/* ----------------------------- Outer Product ----------------------------- */

/** @test outerInto creates correct outer product from column vectors. */
TEST(ArrayOpsTest, OuterInto_ColVectors) {
  // a = [1, 2, 3]^T, b = [4, 5]^T
  // C = a * b^T = 3x2 matrix
  std::array<double, 3> a_data = {1.0, 2.0, 3.0};
  std::array<double, 2> b_data = {4.0, 5.0};
  std::array<double, 6> c_data = {};

  Array<double> a(a_data.data(), 3, 1, Layout::RowMajor);
  Array<double> b(b_data.data(), 2, 1, Layout::RowMajor);
  Array<double> c(c_data.data(), 3, 2, Layout::RowMajor);

  auto st = apex::math::linalg::outerInto(a, b, c);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));

  // C[i,j] = a[i] * b[j]
  EXPECT_DOUBLE_EQ(c(0, 0), 4.0);  // 1*4
  EXPECT_DOUBLE_EQ(c(0, 1), 5.0);  // 1*5
  EXPECT_DOUBLE_EQ(c(1, 0), 8.0);  // 2*4
  EXPECT_DOUBLE_EQ(c(1, 1), 10.0); // 2*5
  EXPECT_DOUBLE_EQ(c(2, 0), 12.0); // 3*4
  EXPECT_DOUBLE_EQ(c(2, 1), 15.0); // 3*5
}

/** @test outerInto rejects mismatched output dimensions. */
TEST(ArrayOpsTest, OuterInto_SizeMismatch) {
  std::array<double, 3> a_data = {1.0, 2.0, 3.0};
  std::array<double, 2> b_data = {4.0, 5.0};
  std::array<double, 4> c_data = {};

  Array<double> a(a_data.data(), 3, 1, Layout::RowMajor);
  Array<double> b(b_data.data(), 2, 1, Layout::RowMajor);
  Array<double> c(c_data.data(), 2, 2, Layout::RowMajor); // Wrong size

  auto st = apex::math::linalg::outerInto(a, b, c);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::ERROR_SIZE_MISMATCH));
}

/* ------------------------------ Matrix Norms ------------------------------ */

/** @test frobeniusNormInto computes correct Frobenius norm. */
TEST(ArrayOpsTest, FrobeniusNormInto) {
  // A = [1, 2; 3, 4]
  // ||A||_F = sqrt(1 + 4 + 9 + 16) = sqrt(30)
  std::array<double, 4> data = {1.0, 2.0, 3.0, 4.0};
  Array<double> mat(data.data(), 2, 2, Layout::RowMajor);

  double norm = 0.0;
  auto st = apex::math::linalg::frobeniusNormInto(mat, norm);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_NEAR(norm, std::sqrt(30.0), 1e-10);
}

/** @test infNormInto computes correct infinity norm (max row sum). */
TEST(ArrayOpsTest, InfNormInto) {
  // A = [1, -2; -3, 4]
  // Row sums: |1| + |-2| = 3, |-3| + |4| = 7
  // ||A||_inf = 7
  std::array<double, 4> data = {1.0, -2.0, -3.0, 4.0};
  Array<double> mat(data.data(), 2, 2, Layout::RowMajor);

  double norm = 0.0;
  auto st = apex::math::linalg::infNormInto(mat, norm);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(norm, 7.0);
}

/** @test oneNormInto computes correct one norm (max column sum). */
TEST(ArrayOpsTest, OneNormInto) {
  // A = [1, -2; -3, 4]
  // Col sums: |1| + |-3| = 4, |-2| + |4| = 6
  // ||A||_1 = 6
  std::array<double, 4> data = {1.0, -2.0, -3.0, 4.0};
  Array<double> mat(data.data(), 2, 2, Layout::RowMajor);

  double norm = 0.0;
  auto st = apex::math::linalg::oneNormInto(mat, norm);
  EXPECT_EQ(st, static_cast<uint8_t>(Status::SUCCESS));
  EXPECT_DOUBLE_EQ(norm, 6.0);
}
