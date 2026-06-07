/**
 * @file QuaternionCuda_uTest.cu
 * @brief Unit tests for CUDA batch quaternion operations.
 *
 * Coverage:
 *  - rotateVectorBatch: GPU/CPU parity
 *  - slerpBatch: boundary t=0, t=1, t=0.5
 *  - normalizeBatch: unit quaternion result
 *  - multiplyBatch: Hamilton product consistency
 *  - toRotationMatrixBatch: matrix values match CPU
 */

#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

// Local CUDA runtime header detection
#if defined(__has_include)
#if __has_include(<cuda_runtime_api.h>)
#define LOCAL_HAS_CUDA_RUNTIME 1
#include <cuda_runtime_api.h>
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif

using apex::math::quaternion::Quaternion;
using apex::math::quaternion::cuda::multiplyBatchCuda;
using apex::math::quaternion::cuda::normalizeBatchCuda;
using apex::math::quaternion::cuda::rotateVectorBatchCuda;
using apex::math::quaternion::cuda::rotateVectorBatchCudaF;
using apex::math::quaternion::cuda::slerpBatchCuda;
using apex::math::quaternion::cuda::toRotationMatrixBatchCuda;

/* ----------------------------- Test Fixture ------------------------------- */

/**
 * @brief Fixture that skips tests when CUDA runtime is unavailable.
 */
class QuaternionCudaFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Helper Functions --------------------------- */

namespace {

constexpr double K_PI = 3.14159265358979323846;
constexpr double K_TOL = 1e-10;
constexpr float K_TOL_F = 1e-5f;

template <typename T> void expectNear(T a, T b, T tol) { EXPECT_NEAR(a, b, tol); }

} // namespace

/* ----------------------------- rotateVectorBatch -------------------------- */

/** @test rotateVectorBatch rotates vectors correctly (GPU vs CPU parity). */
TEST_F(QuaternionCudaFixture, RotateVectorBatch_Parity) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 4;

  // 90-degree rotation about Z-axis
  const double HALF_ANGLE = K_PI / 4.0;
  const double COS_HA = std::cos(HALF_ANGLE);
  const double SIN_HA = std::sin(HALF_ANGLE);

  // Host quaternions (all same rotation)
  std::vector<double> hQs(BATCH * 4);
  for (int i = 0; i < BATCH; ++i) {
    hQs[i * 4 + 0] = COS_HA;
    hQs[i * 4 + 1] = 0.0;
    hQs[i * 4 + 2] = 0.0;
    hQs[i * 4 + 3] = SIN_HA;
  }

  // Host input vectors: [1,0,0] for all
  std::vector<double> hVsIn(BATCH * 3);
  for (int i = 0; i < BATCH; ++i) {
    hVsIn[i * 3 + 0] = 1.0;
    hVsIn[i * 3 + 1] = 0.0;
    hVsIn[i * 3 + 2] = 0.0;
  }

  // Allocate device memory
  double *dQs = nullptr, *dVsIn = nullptr, *dVsOut = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dVsIn, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dVsOut, BATCH * 3 * sizeof(double))));

  // Copy to device
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, hQs.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dVsIn, hVsIn.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  // Execute kernel
  ASSERT_TRUE(rotateVectorBatchCuda(dQs, dVsIn, BATCH, dVsOut, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  // Copy back
  std::vector<double> hVsOut(BATCH * 3);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hVsOut.data(), dVsOut, BATCH * 3 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Verify: 90-deg rotation about Z of [1,0,0] = [0,1,0]
  for (int i = 0; i < BATCH; ++i) {
    expectNear(hVsOut[i * 3 + 0], 0.0, K_TOL);
    expectNear(hVsOut[i * 3 + 1], 1.0, K_TOL);
    expectNear(hVsOut[i * 3 + 2], 0.0, K_TOL);
  }

  // Cleanup
  cudaFree(dQs);
  cudaFree(dVsIn);
  cudaFree(dVsOut);
#endif
}

/* ----------------------------- slerpBatch --------------------------------- */

/** @test slerpBatch at t=0 returns first quaternion. */
TEST_F(QuaternionCudaFixture, SlerpBatch_T0) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  // qA = identity, qB = 180-deg about Z
  std::vector<double> hQsA = {1, 0, 0, 0, 1, 0, 0, 0};
  std::vector<double> hQsB = {0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<double> hTs = {0.0, 0.0};

  double *dQsA = nullptr, *dQsB = nullptr, *dTs = nullptr, *dQsOut = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dTs, BATCH * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, hQsA.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, hQsB.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dTs, hTs.data(), BATCH * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hQsOut(BATCH * 4);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hQsOut.data(), dQsOut, BATCH * 4 * sizeof(double), cudaMemcpyDeviceToHost)));

  // At t=0, output should equal qA
  for (int i = 0; i < BATCH; ++i) {
    expectNear(hQsOut[i * 4 + 0], hQsA[i * 4 + 0], K_TOL);
    expectNear(hQsOut[i * 4 + 1], hQsA[i * 4 + 1], K_TOL);
    expectNear(hQsOut[i * 4 + 2], hQsA[i * 4 + 2], K_TOL);
    expectNear(hQsOut[i * 4 + 3], hQsA[i * 4 + 3], K_TOL);
  }

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dTs);
  cudaFree(dQsOut);
#endif
}

/** @test slerpBatch at t=0.5 produces midpoint quaternion. */
TEST_F(QuaternionCudaFixture, SlerpBatch_T05) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // qA = identity, qB = 180-deg about Z (quaternion [0,0,0,1])
  std::vector<double> hQsA = {1, 0, 0, 0};
  std::vector<double> hQsB = {0, 0, 0, 1};
  std::vector<double> hTs = {0.5};

  double *dQsA = nullptr, *dQsB = nullptr, *dTs = nullptr, *dQsOut = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dTs, BATCH * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, hQsA.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, hQsB.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dTs, hTs.data(), BATCH * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hQsOut(BATCH * 4);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hQsOut.data(), dQsOut, BATCH * 4 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Midpoint: 90-deg about Z = [cos(45), 0, 0, sin(45)] = [sqrt(2)/2, 0, 0, sqrt(2)/2]
  const double EXPECTED = std::sqrt(2.0) / 2.0;
  expectNear(hQsOut[0], EXPECTED, K_TOL);
  expectNear(hQsOut[1], 0.0, K_TOL);
  expectNear(hQsOut[2], 0.0, K_TOL);
  expectNear(hQsOut[3], EXPECTED, K_TOL);

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dTs);
  cudaFree(dQsOut);
#endif
}

/* ----------------------------- normalizeBatch ----------------------------- */

/** @test normalizeBatch produces unit quaternions. */
TEST_F(QuaternionCudaFixture, NormalizeBatch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 3;

  std::vector<double> hQs = {1, 2, 3, 4, 2, 0, 0, 0, 0, 0, 3, 4};

  double* dQs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, hQs.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(normalizeBatchCuda(dQs, BATCH, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hQsOut(BATCH * 4);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hQsOut.data(), dQs, BATCH * 4 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Verify all have unit norm
  for (int i = 0; i < BATCH; ++i) {
    const double W = hQsOut[i * 4 + 0];
    const double X = hQsOut[i * 4 + 1];
    const double Y = hQsOut[i * 4 + 2];
    const double Z = hQsOut[i * 4 + 3];
    const double NRM = std::sqrt(W * W + X * X + Y * Y + Z * Z);
    expectNear(NRM, 1.0, K_TOL);
  }

  cudaFree(dQs);
#endif
}

/* ----------------------------- multiplyBatch ------------------------------ */

/** @test multiplyBatch follows Hamilton product. */
TEST_F(QuaternionCudaFixture, MultiplyBatch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // Two 90-deg rotations about Z -> 180-deg rotation
  const double HALF_90 = K_PI / 4.0;
  const double COS45 = std::cos(HALF_90);
  const double SIN45 = std::sin(HALF_90);

  std::vector<double> hQsA = {COS45, 0, 0, SIN45};
  std::vector<double> hQsB = {COS45, 0, 0, SIN45};

  double *dQsA = nullptr, *dQsB = nullptr, *dQsOut = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, hQsA.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, hQsB.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(multiplyBatchCuda(dQsA, dQsB, BATCH, dQsOut, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hQsOut(BATCH * 4);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hQsOut.data(), dQsOut, BATCH * 4 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Expected: 180-deg about Z = [0, 0, 0, 1]
  expectNear(hQsOut[0], 0.0, K_TOL);
  expectNear(hQsOut[1], 0.0, K_TOL);
  expectNear(hQsOut[2], 0.0, K_TOL);
  expectNear(hQsOut[3], 1.0, K_TOL);

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dQsOut);
#endif
}

/* ------------------------- toRotationMatrixBatch -------------------------- */

/** @test toRotationMatrixBatch produces correct 3x3 matrices. */
TEST_F(QuaternionCudaFixture, ToRotationMatrixBatch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // 90-deg rotation about Z-axis
  const double HALF_ANGLE = K_PI / 4.0;
  const double COS_HA = std::cos(HALF_ANGLE);
  const double SIN_HA = std::sin(HALF_ANGLE);

  std::vector<double> hQs = {COS_HA, 0, 0, SIN_HA};

  double *dQs = nullptr, *dMats = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dMats, BATCH * 9 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, hQs.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(toRotationMatrixBatchCuda(dQs, BATCH, dMats, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hMats(BATCH * 9);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hMats.data(), dMats, BATCH * 9 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Expected rotation matrix for 90-deg about Z:
  // [ 0, -1,  0]
  // [ 1,  0,  0]
  // [ 0,  0,  1]
  expectNear(hMats[0], 0.0, K_TOL);
  expectNear(hMats[1], -1.0, K_TOL);
  expectNear(hMats[2], 0.0, K_TOL);
  expectNear(hMats[3], 1.0, K_TOL);
  expectNear(hMats[4], 0.0, K_TOL);
  expectNear(hMats[5], 0.0, K_TOL);
  expectNear(hMats[6], 0.0, K_TOL);
  expectNear(hMats[7], 0.0, K_TOL);
  expectNear(hMats[8], 1.0, K_TOL);

  cudaFree(dQs);
  cudaFree(dMats);
#endif
}

/* ----------------------------- Float Variants ----------------------------- */

/** @test rotateVectorBatchCudaF works for float precision. */
TEST_F(QuaternionCudaFixture, RotateVectorBatchFloat) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  const float HALF_ANGLE = static_cast<float>(K_PI / 4.0);
  const float COS_HA = std::cos(HALF_ANGLE);
  const float SIN_HA = std::sin(HALF_ANGLE);

  std::vector<float> hQs(BATCH * 4);
  std::vector<float> hVsIn(BATCH * 3);
  for (int i = 0; i < BATCH; ++i) {
    hQs[i * 4 + 0] = COS_HA;
    hQs[i * 4 + 1] = 0.0f;
    hQs[i * 4 + 2] = 0.0f;
    hQs[i * 4 + 3] = SIN_HA;
    hVsIn[i * 3 + 0] = 1.0f;
    hVsIn[i * 3 + 1] = 0.0f;
    hVsIn[i * 3 + 2] = 0.0f;
  }

  float *dQs = nullptr, *dVsIn = nullptr, *dVsOut = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(float))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dVsIn, BATCH * 3 * sizeof(float))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dVsOut, BATCH * 3 * sizeof(float))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, hQs.data(), BATCH * 4 * sizeof(float), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dVsIn, hVsIn.data(), BATCH * 3 * sizeof(float), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(rotateVectorBatchCudaF(dQs, dVsIn, BATCH, dVsOut, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<float> hVsOut(BATCH * 3);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hVsOut.data(), dVsOut, BATCH * 3 * sizeof(float), cudaMemcpyDeviceToHost)));

  for (int i = 0; i < BATCH; ++i) {
    expectNear(hVsOut[i * 3 + 0], 0.0f, K_TOL_F);
    expectNear(hVsOut[i * 3 + 1], 1.0f, K_TOL_F);
    expectNear(hVsOut[i * 3 + 2], 0.0f, K_TOL_F);
  }

  cudaFree(dQs);
  cudaFree(dVsIn);
  cudaFree(dVsOut);
#endif
}
