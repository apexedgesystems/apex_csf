/**
 * @file Vector_uTest.cpp
 * @brief Unit tests for Vector (vector-specific APIs).
 *
 * Coverage:
 *  - Construction: row vs col orientation
 *  - Dot product: correct result, mismatch/orientation errors
 *  - Norm / Normalize: values, zero-norm handling
 *  - Cross product: correct 3D result, size/orientation errors
 */

#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using apex::math::linalg::Layout;
using apex::math::linalg::Status;
using apex::math::linalg::Vector;
using apex::math::linalg::VectorOrient;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::makeColView;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/** @brief Fixture template to run tests for float and double. */
template <typename T> class VectorTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(VectorTestT, ValueTypes);

/* ----------------------------- Construction ----------------------------- */

/** @test Construct column vector (Nx1) and row vector (1xN). */
TYPED_TEST(VectorTestT, Construct_RowVsCol) {
  using V = TypeParam;

  std::vector<V> colBuf{V(1), V(2), V(3)};
  auto colView = makeColView(colBuf, 3, 1);
  Vector<V> col(colView, VectorOrient::Col);
  EXPECT_EQ(col.size(), 3u);
  EXPECT_EQ(col.orient(), VectorOrient::Col);

  std::vector<V> rowBuf{V(4), V(5), V(6)};
  auto rowView = makeRowView(rowBuf, 1, 3);
  Vector<V> row(rowView, VectorOrient::Row);
  EXPECT_EQ(row.size(), 3u);
  EXPECT_EQ(row.orient(), VectorOrient::Row);
}

/* ----------------------------- Dot product ----------------------------- */

/** @test dotInto returns x.y for matching vectors; errors for size/orientation mismatch. */
TYPED_TEST(VectorTestT, DotInto_Success_And_Errors) {
  using V = TypeParam;

  std::vector<V> xBuf{V(1), V(2), V(3)};
  std::vector<V> yBuf{V(4), V(5), V(6)};
  auto xView = makeColView(xBuf, 3, 1);
  auto yView = makeColView(yBuf, 3, 1);

  Vector<V> x(xView, VectorOrient::Col);
  Vector<V> y(yView, VectorOrient::Col);

  V dot = V(0);
  expectStatus(x.dotInto(y, dot), Status::SUCCESS);
  EXPECT_EQ(dot, V(32));

  // Size mismatch
  std::vector<V> badBuf{V(7), V(8)};
  auto badView = makeColView(badBuf, 2, 1);
  Vector<V> bad(badView, VectorOrient::Col);
  expectStatus(x.dotInto(bad, dot), Status::ERROR_SIZE_MISMATCH);

  // Orientation mismatch
  auto yRowView = makeRowView(yBuf, 1, 3);
  Vector<V> yRow(yRowView, VectorOrient::Row);
  expectStatus(x.dotInto(yRow, dot), Status::ERROR_INVALID_LAYOUT);
}

/* ----------------------------- Norm / Normalize ----------------------------- */

/** @test norm2Into computes ||x||_2; normalizeInPlace scales to unit norm; zero-norm errors. */
TYPED_TEST(VectorTestT, Norm2_Normalize) {
  using V = TypeParam;
  std::vector<V> buf{V(3), V(4)};
  auto vView = makeColView(buf, 2, 1);
  Vector<V> v(vView, VectorOrient::Col);

  V nrm = V(0);
  expectStatus(v.norm2Into(nrm), Status::SUCCESS);
  EXPECT_NEAR(nrm, V(5), tol<V>());

  expectStatus(v.normalizeInPlace(), Status::SUCCESS);
  V nrm2 = V(0);
  expectStatus(v.norm2Into(nrm2), Status::SUCCESS);
  EXPECT_NEAR(nrm2, V(1), tol<V>());

  // Zero norm error
  std::vector<V> zBuf{V(0), V(0)};
  auto zView = makeColView(zBuf, 2, 1);
  Vector<V> z(zView, VectorOrient::Col);
  expectStatus(z.normalizeInPlace(), Status::ERROR_INVALID_VALUE);
}

/* ----------------------------- Cross product ----------------------------- */

/** @test crossInto computes x cross y for 3D column vectors; errors otherwise. */
TYPED_TEST(VectorTestT, CrossInto_3D) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(0), V(0)};
  std::vector<V> bBuf{V(0), V(1), V(0)};
  std::vector<V> cBuf(3, V(0));

  auto aView = makeColView(aBuf, 3, 1);
  auto bView = makeColView(bBuf, 3, 1);
  auto cView = makeColView(cBuf, 3, 1);

  Vector<V> a(aView, VectorOrient::Col);
  Vector<V> b(bView, VectorOrient::Col);
  Vector<V> c(cView, VectorOrient::Col);

  expectStatus(a.crossInto(b, c), Status::SUCCESS);
  EXPECT_EQ(c.data()[0], V(0));
  EXPECT_EQ(c.data()[1], V(0));
  EXPECT_EQ(c.data()[2], V(1));

  // Wrong size
  std::vector<V> badBuf{V(1), V(2)};
  auto badView = makeColView(badBuf, 2, 1);
  Vector<V> bad(badView, VectorOrient::Col);
  expectStatus(a.crossInto(bad, c), Status::ERROR_UNSUPPORTED_OP);
}

/* ----------------------------- Add / Sub / Scale ----------------------------- */

/** @test addInto computes elementwise addition; subInto subtraction; scaleInto scaling. */
TYPED_TEST(VectorTestT, Add_Sub_Scale) {
  using V = TypeParam;

  std::vector<V> aBuf{V(1), V(2), V(3)};
  std::vector<V> bBuf{V(4), V(5), V(6)};
  std::vector<V> oBuf(3, V(0));

  auto aView = makeColView(aBuf, 3, 1);
  auto bView = makeColView(bBuf, 3, 1);
  auto oView = makeColView(oBuf, 3, 1);

  Vector<V> a(aView, VectorOrient::Col);
  Vector<V> b(bView, VectorOrient::Col);
  Vector<V> o(oView, VectorOrient::Col);

  expectStatus(a.addInto(b, o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(5));
  EXPECT_EQ(o.data()[1], V(7));
  EXPECT_EQ(o.data()[2], V(9));

  expectStatus(a.subInto(b, o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(-3));
  EXPECT_EQ(o.data()[1], V(-3));
  EXPECT_EQ(o.data()[2], V(-3));

  expectStatus(a.scaleInto(V(2), o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(2));
  EXPECT_EQ(o.data()[1], V(4));
  EXPECT_EQ(o.data()[2], V(6));
}

/* ----------------------------- Angle between ----------------------------- */

/** @test angleBetweenInto computes angle(x,y) in radians; errors on zero-norm. */
TYPED_TEST(VectorTestT, AngleBetween) {
  using V = TypeParam;

  std::vector<V> xBuf{V(1), V(0)};
  std::vector<V> yBuf{V(0), V(1)};
  auto xView = makeColView(xBuf, 2, 1);
  auto yView = makeColView(yBuf, 2, 1);
  Vector<V> x(xView, VectorOrient::Col);
  Vector<V> y(yView, VectorOrient::Col);

  V ang = V(0);
  expectStatus(x.angleBetweenInto(y, ang), Status::SUCCESS);
  EXPECT_NEAR(ang, V(M_PI / 2), tol<V>());

  // Parallel vectors: angle 0
  std::vector<V> pBuf{V(2), V(0)};
  auto pView = makeColView(pBuf, 2, 1);
  Vector<V> p(pView, VectorOrient::Col);
  expectStatus(x.angleBetweenInto(p, ang), Status::SUCCESS);
  EXPECT_NEAR(ang, V(0), tol<V>());

  // Zero-norm vector gives error
  std::vector<V> zBuf{V(0), V(0)};
  auto zView = makeColView(zBuf, 2, 1);
  Vector<V> z(zView, VectorOrient::Col);
  expectStatus(x.angleBetweenInto(z, ang), Status::ERROR_INVALID_VALUE);
}

/* ----------------------------- absInto ----------------------------- */

/** @test absInto computes elementwise absolute value. */
TYPED_TEST(VectorTestT, AbsInto) {
  using V = TypeParam;

  std::vector<V> xBuf{V(-1), V(2), V(-3)};
  std::vector<V> oBuf(3, V(0));
  auto xView = makeColView(xBuf, 3, 1);
  auto oView = makeColView(oBuf, 3, 1);
  Vector<V> x(xView, VectorOrient::Col);
  Vector<V> o(oView, VectorOrient::Col);

  expectStatus(x.absInto(o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(1));
  EXPECT_EQ(o.data()[1], V(2));
  EXPECT_EQ(o.data()[2], V(3));
}

/* ----------------------------- minInto ----------------------------- */

/** @test minInto computes elementwise minimum. */
TYPED_TEST(VectorTestT, MinInto) {
  using V = TypeParam;

  std::vector<V> xBuf{V(1), V(5), V(3)};
  std::vector<V> yBuf{V(4), V(2), V(6)};
  std::vector<V> oBuf(3, V(0));
  auto xView = makeColView(xBuf, 3, 1);
  auto yView = makeColView(yBuf, 3, 1);
  auto oView = makeColView(oBuf, 3, 1);
  Vector<V> x(xView, VectorOrient::Col);
  Vector<V> y(yView, VectorOrient::Col);
  Vector<V> o(oView, VectorOrient::Col);

  expectStatus(x.minInto(y, o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(1)); // min(1, 4) = 1
  EXPECT_EQ(o.data()[1], V(2)); // min(5, 2) = 2
  EXPECT_EQ(o.data()[2], V(3)); // min(3, 6) = 3
}

/* ----------------------------- maxInto ----------------------------- */

/** @test maxInto computes elementwise maximum. */
TYPED_TEST(VectorTestT, MaxInto) {
  using V = TypeParam;

  std::vector<V> xBuf{V(1), V(5), V(3)};
  std::vector<V> yBuf{V(4), V(2), V(6)};
  std::vector<V> oBuf(3, V(0));
  auto xView = makeColView(xBuf, 3, 1);
  auto yView = makeColView(yBuf, 3, 1);
  auto oView = makeColView(oBuf, 3, 1);
  Vector<V> x(xView, VectorOrient::Col);
  Vector<V> y(yView, VectorOrient::Col);
  Vector<V> o(oView, VectorOrient::Col);

  expectStatus(x.maxInto(y, o), Status::SUCCESS);
  EXPECT_EQ(o.data()[0], V(4)); // max(1, 4) = 4
  EXPECT_EQ(o.data()[1], V(5)); // max(5, 2) = 5
  EXPECT_EQ(o.data()[2], V(6)); // max(3, 6) = 6
}

/* ----------------------------- sumInto ----------------------------- */

/** @test sumInto computes sum of all elements. */
TYPED_TEST(VectorTestT, SumInto) {
  using V = TypeParam;

  std::vector<V> xBuf{V(1), V(2), V(3), V(4)};
  auto xView = makeColView(xBuf, 4, 1);
  Vector<V> x(xView, VectorOrient::Col);

  V sum = V(0);
  expectStatus(x.sumInto(sum), Status::SUCCESS);
  EXPECT_EQ(sum, V(10)); // 1 + 2 + 3 + 4 = 10
}

/* ----------------------------- meanInto ----------------------------- */

/** @test meanInto computes average of all elements. */
TYPED_TEST(VectorTestT, MeanInto) {
  using V = TypeParam;

  std::vector<V> xBuf{V(2), V(4), V(6), V(8)};
  auto xView = makeColView(xBuf, 4, 1);
  Vector<V> x(xView, VectorOrient::Col);

  V mean = V(0);
  expectStatus(x.meanInto(mean), Status::SUCCESS);
  EXPECT_NEAR(mean, V(5), tol<V>()); // (2 + 4 + 6 + 8) / 4 = 5
}
