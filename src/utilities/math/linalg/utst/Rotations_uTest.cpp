/**
 * @file Rotations_uTest.cpp
 * @brief Unit tests for Rotations (rotation utilities for aerospace applications).
 *
 * Coverage:
 *  - dcmFromEuler321Into: Euler 3-2-1 to DCM conversion
 *  - eulerFromDcm321Into: DCM to Euler 3-2-1 extraction
 *  - dcmFromAxisAngleInto: Rodrigues formula
 *  - axisAngleFromDcmInto: DCM to axis-angle extraction
 *  - dcmFromSmallAnglesInto: first-order approximation
 *  - Round-trip tests: Euler -> DCM -> Euler, axis-angle -> DCM -> axis-angle
 */

#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayStatus.hpp"
#include "src/utilities/math/linalg/inc/Matrix3.hpp"
#include "src/utilities/math/linalg/inc/Rotations.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"
#include "src/utilities/math/linalg/utst/ArrayTestHelpers.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

using apex::math::linalg::Array;
using apex::math::linalg::axisAngleFromDcmInto;
using apex::math::linalg::dcmFromAxisAngleInto;
using apex::math::linalg::dcmFromEuler321Into;
using apex::math::linalg::dcmFromSmallAnglesInto;
using apex::math::linalg::eulerFromDcm321Into;
using apex::math::linalg::Layout;
using apex::math::linalg::Matrix3;
using apex::math::linalg::Status;
using apex::math::linalg::Vector;
using apex::math::linalg::VectorOrient;
using apex::math::linalg::test::expectStatus;
using apex::math::linalg::test::makeRowView;
using apex::math::linalg::test::tol;

/** @brief Fixture template to run tests for float and double. */
template <typename T> class RotationsTestT : public ::testing::Test {};
using ValueTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(RotationsTestT, ValueTypes);

/* ------------------------- dcmFromEuler321Into ------------------------- */

/** @test dcmFromEuler321Into: identity rotation (zero angles). */
TYPED_TEST(RotationsTestT, DcmFromEuler321_Identity) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromEuler321Into(V(0), V(0), V(0), dcm), Status::SUCCESS);

  // Identity DCM
  EXPECT_NEAR(dcm.view()(0, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 1), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 2), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 0), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 2), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 0), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 1), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(1), tol<V>());
}

/** @test dcmFromEuler321Into: 90-degree yaw rotation. */
TYPED_TEST(RotationsTestT, DcmFromEuler321_Yaw90) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  const V PI_2 = V(M_PI) / V(2);
  expectStatus(dcmFromEuler321Into(V(0), V(0), PI_2, dcm), Status::SUCCESS);

  // Rz(90): [[0,-1,0],[1,0,0],[0,0,1]]
  EXPECT_NEAR(dcm.view()(0, 0), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 1), V(-1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(1), tol<V>());
}

/** @test dcmFromEuler321Into: 90-degree pitch rotation. */
TYPED_TEST(RotationsTestT, DcmFromEuler321_Pitch90) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  const V PI_2 = V(M_PI) / V(2);
  expectStatus(dcmFromEuler321Into(V(0), PI_2, V(0), dcm), Status::SUCCESS);

  // Ry(90): [[0,0,1],[0,1,0],[-1,0,0]]
  EXPECT_NEAR(dcm.view()(0, 0), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 2), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 0), V(-1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(0), tol<V>());
}

/** @test dcmFromEuler321Into: 90-degree roll rotation. */
TYPED_TEST(RotationsTestT, DcmFromEuler321_Roll90) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  const V PI_2 = V(M_PI) / V(2);
  expectStatus(dcmFromEuler321Into(PI_2, V(0), V(0), dcm), Status::SUCCESS);

  // Rx(90): [[1,0,0],[0,0,-1],[0,1,0]]
  EXPECT_NEAR(dcm.view()(0, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 2), V(-1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 1), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(0), tol<V>());
}

/* ------------------------- eulerFromDcm321Into ------------------------- */

/** @test eulerFromDcm321Into: round-trip Euler -> DCM -> Euler. */
TYPED_TEST(RotationsTestT, EulerRoundTrip) {
  using V = TypeParam;

  const V ROLL_IN = V(0.3);
  const V PITCH_IN = V(0.2);
  const V YAW_IN = V(0.5);

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromEuler321Into(ROLL_IN, PITCH_IN, YAW_IN, dcm), Status::SUCCESS);

  V rollOut = V(0);
  V pitchOut = V(0);
  V yawOut = V(0);
  expectStatus(eulerFromDcm321Into(dcm, rollOut, pitchOut, yawOut), Status::SUCCESS);

  EXPECT_NEAR(rollOut, ROLL_IN, tol<V>() * 10);
  EXPECT_NEAR(pitchOut, PITCH_IN, tol<V>() * 10);
  EXPECT_NEAR(yawOut, YAW_IN, tol<V>() * 10);
}

/** @test eulerFromDcm321Into: gimbal lock case (pitch = 90 degrees). */
TYPED_TEST(RotationsTestT, EulerGimbalLock) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  const V PI_2 = V(M_PI) / V(2);
  expectStatus(dcmFromEuler321Into(V(0.1), PI_2, V(0.2), dcm), Status::SUCCESS);

  V rollOut = V(0);
  V pitchOut = V(0);
  V yawOut = V(0);
  expectStatus(eulerFromDcm321Into(dcm, rollOut, pitchOut, yawOut), Status::SUCCESS);

  // At gimbal lock, pitch should be ~90 degrees
  EXPECT_NEAR(pitchOut, PI_2, tol<V>() * 100);
  // Roll is assumed 0 in gimbal lock
  EXPECT_NEAR(rollOut, V(0), tol<V>());
}

/* ------------------------- dcmFromAxisAngleInto ------------------------- */

/** @test dcmFromAxisAngleInto: identity rotation (zero angle). */
TYPED_TEST(RotationsTestT, DcmFromAxisAngle_Identity) {
  using V = TypeParam;

  std::vector<V> axisBuf{V(1), V(0), V(0)};
  Array<V> axisArr(axisBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axis(axisArr, VectorOrient::Col);

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromAxisAngleInto(axis, V(0), dcm), Status::SUCCESS);

  // Identity DCM
  EXPECT_NEAR(dcm.view()(0, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 1), V(0), tol<V>());
}

/** @test dcmFromAxisAngleInto: 90-degree rotation about Z-axis. */
TYPED_TEST(RotationsTestT, DcmFromAxisAngle_Z90) {
  using V = TypeParam;

  std::vector<V> axisBuf{V(0), V(0), V(1)};
  Array<V> axisArr(axisBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axis(axisArr, VectorOrient::Col);

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  const V PI_2 = V(M_PI) / V(2);
  expectStatus(dcmFromAxisAngleInto(axis, PI_2, dcm), Status::SUCCESS);

  // Rz(90): [[0,-1,0],[1,0,0],[0,0,1]]
  EXPECT_NEAR(dcm.view()(0, 0), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 1), V(-1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(0), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(1), tol<V>());
}

/** @test dcmFromAxisAngleInto: 180-degree rotation about X-axis. */
TYPED_TEST(RotationsTestT, DcmFromAxisAngle_X180) {
  using V = TypeParam;

  std::vector<V> axisBuf{V(1), V(0), V(0)};
  Array<V> axisArr(axisBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axis(axisArr, VectorOrient::Col);

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromAxisAngleInto(axis, V(M_PI), dcm), Status::SUCCESS);

  // Rx(180): [[1,0,0],[0,-1,0],[0,0,-1]]
  EXPECT_NEAR(dcm.view()(0, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(-1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(-1), tol<V>());
}

/* ------------------------- axisAngleFromDcmInto ------------------------- */

/** @test axisAngleFromDcmInto: round-trip axis-angle -> DCM -> axis-angle. */
TYPED_TEST(RotationsTestT, AxisAngleRoundTrip) {
  using V = TypeParam;

  // Create rotation: 45 degrees about normalized axis (1,1,1)/sqrt(3)
  const V SQRT3 = std::sqrt(V(3));
  std::vector<V> axisInBuf{V(1) / SQRT3, V(1) / SQRT3, V(1) / SQRT3};
  Array<V> axisInArr(axisInBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axisIn(axisInArr, VectorOrient::Col);

  const V ANGLE_IN = V(M_PI) / V(4); // 45 degrees

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromAxisAngleInto(axisIn, ANGLE_IN, dcm), Status::SUCCESS);

  std::vector<V> axisOutBuf{V(0), V(0), V(0)};
  Array<V> axisOutArr(axisOutBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axisOut(axisOutArr, VectorOrient::Col);
  V angleOut = V(0);

  expectStatus(axisAngleFromDcmInto(dcm, axisOut, angleOut), Status::SUCCESS);

  EXPECT_NEAR(angleOut, ANGLE_IN, tol<V>() * 10);
  EXPECT_NEAR(axisOutBuf[0], axisInBuf[0], tol<V>() * 100);
  EXPECT_NEAR(axisOutBuf[1], axisInBuf[1], tol<V>() * 100);
  EXPECT_NEAR(axisOutBuf[2], axisInBuf[2], tol<V>() * 100);
}

/** @test axisAngleFromDcmInto: identity matrix returns zero angle. */
TYPED_TEST(RotationsTestT, AxisAngleFromDcm_Identity) {
  using V = TypeParam;

  // Identity DCM
  std::vector<V> dcmBuf{V(1), V(0), V(0), V(0), V(1), V(0), V(0), V(0), V(1)};
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  std::vector<V> axisBuf{V(0), V(0), V(0)};
  Array<V> axisArr(axisBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axis(axisArr, VectorOrient::Col);
  V angle = V(0);

  expectStatus(axisAngleFromDcmInto(dcm, axis, angle), Status::SUCCESS);

  EXPECT_NEAR(angle, V(0), tol<V>());
  // Axis defaults to [1,0,0] for zero angle
  EXPECT_NEAR(axisBuf[0], V(1), tol<V>());
  EXPECT_NEAR(axisBuf[1], V(0), tol<V>());
  EXPECT_NEAR(axisBuf[2], V(0), tol<V>());
}

/* ------------------------ dcmFromSmallAnglesInto ------------------------ */

/** @test dcmFromSmallAnglesInto: zero angles yield identity. */
TYPED_TEST(RotationsTestT, SmallAngles_Identity) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromSmallAnglesInto(V(0), V(0), V(0), dcm), Status::SUCCESS);

  EXPECT_NEAR(dcm.view()(0, 0), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(1, 1), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(2, 2), V(1), tol<V>());
  EXPECT_NEAR(dcm.view()(0, 1), V(0), tol<V>());
}

/** @test dcmFromSmallAnglesInto: small angles approximate exact DCM. */
TYPED_TEST(RotationsTestT, SmallAngles_Approximation) {
  using V = TypeParam;

  const V ROLL = V(0.01);
  const V PITCH = V(0.02);
  const V YAW = V(0.015);

  std::vector<V> dcmExactBuf(9, V(0));
  std::vector<V> dcmApproxBuf(9, V(0));
  auto dcmExactView = makeRowView(dcmExactBuf, 3, 3);
  auto dcmApproxView = makeRowView(dcmApproxBuf, 3, 3);
  Matrix3<V> dcmExact(dcmExactView);
  Matrix3<V> dcmApprox(dcmApproxView);

  expectStatus(dcmFromEuler321Into(ROLL, PITCH, YAW, dcmExact), Status::SUCCESS);
  expectStatus(dcmFromSmallAnglesInto(ROLL, PITCH, YAW, dcmApprox), Status::SUCCESS);

  // Small angle approximation should be close to exact for small angles
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_NEAR(dcmApprox.view()(r, c), dcmExact.view()(r, c), V(0.001));
    }
  }
}

/* ----------------------- Orthogonality Verification ----------------------- */

/** @test DCM from Euler angles has determinant = 1 (proper rotation). */
TYPED_TEST(RotationsTestT, DcmOrthogonality_Euler) {
  using V = TypeParam;

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromEuler321Into(V(0.5), V(0.3), V(0.7), dcm), Status::SUCCESS);

  // Proper rotation: det(R) = 1
  V det = V(0);
  expectStatus(dcm.determinantInto(det), Status::SUCCESS);
  EXPECT_NEAR(det, V(1), tol<V>() * 100);

  // Row vectors should be unit length
  for (int r = 0; r < 3; ++r) {
    V rowNorm2 = V(0);
    for (int c = 0; c < 3; ++c) {
      rowNorm2 += dcm.view()(r, c) * dcm.view()(r, c);
    }
    EXPECT_NEAR(std::sqrt(rowNorm2), V(1), tol<V>() * 10);
  }

  // Column vectors should be unit length
  for (int c = 0; c < 3; ++c) {
    V colNorm2 = V(0);
    for (int r = 0; r < 3; ++r) {
      colNorm2 += dcm.view()(r, c) * dcm.view()(r, c);
    }
    EXPECT_NEAR(std::sqrt(colNorm2), V(1), tol<V>() * 10);
  }
}

/** @test DCM from axis-angle has determinant = 1 (proper rotation). */
TYPED_TEST(RotationsTestT, DcmOrthogonality_AxisAngle) {
  using V = TypeParam;

  std::vector<V> axisBuf{V(0.5), V(0.5), V(0.7071)};
  // Normalize
  V norm = std::sqrt(axisBuf[0] * axisBuf[0] + axisBuf[1] * axisBuf[1] + axisBuf[2] * axisBuf[2]);
  axisBuf[0] /= norm;
  axisBuf[1] /= norm;
  axisBuf[2] /= norm;

  Array<V> axisArr(axisBuf.data(), 3, 1, Layout::RowMajor, 1);
  Vector<V> axis(axisArr, VectorOrient::Col);

  std::vector<V> dcmBuf(9, V(0));
  auto dcmView = makeRowView(dcmBuf, 3, 3);
  Matrix3<V> dcm(dcmView);

  expectStatus(dcmFromAxisAngleInto(axis, V(1.2), dcm), Status::SUCCESS);

  // Proper rotation: det(R) = 1
  V det = V(0);
  expectStatus(dcm.determinantInto(det), Status::SUCCESS);
  EXPECT_NEAR(det, V(1), tol<V>() * 100);

  // Row vectors should be unit length
  for (int r = 0; r < 3; ++r) {
    V rowNorm2 = V(0);
    for (int c = 0; c < 3; ++c) {
      rowNorm2 += dcm.view()(r, c) * dcm.view()(r, c);
    }
    EXPECT_NEAR(std::sqrt(rowNorm2), V(1), tol<V>() * 10);
  }

  // Column vectors should be unit length
  for (int c = 0; c < 3; ++c) {
    V colNorm2 = V(0);
    for (int r = 0; r < 3; ++r) {
      colNorm2 += dcm.view()(r, c) * dcm.view()(r, c);
    }
    EXPECT_NEAR(std::sqrt(colNorm2), V(1), tol<V>() * 10);
  }
}
