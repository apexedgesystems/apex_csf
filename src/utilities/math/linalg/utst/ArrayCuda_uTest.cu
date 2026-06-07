/**
 * @file ArrayCuda_uTest.cu
 * @brief Unit tests for CUDA batch linear algebra operations.
 *
 * Coverage:
 *  - gemm3x3Batch: GPU/CPU parity
 *  - transpose3x3Batch: correct transposition
 *  - inverse3x3Batch: inverse correctness
 *  - determinant3x3Batch: determinant values
 *  - matvec3x3Batch: matrix-vector multiply
 *  - cross3Batch: cross product
 *  - dot3Batch: dot product
 *  - normalize3Batch: unit vectors
 */

#include "src/utilities/math/linalg/inc/ArrayCuda.cuh"

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

using apex::math::linalg::cuda::cross3BatchCuda;
using apex::math::linalg::cuda::determinant3x3BatchCuda;
using apex::math::linalg::cuda::dot3BatchCuda;
using apex::math::linalg::cuda::gemm3x3BatchCuda;
using apex::math::linalg::cuda::gemm3x3BatchCudaF;
using apex::math::linalg::cuda::inverse3x3BatchCuda;
using apex::math::linalg::cuda::matvec3x3BatchCuda;
using apex::math::linalg::cuda::normalize3BatchCuda;
using apex::math::linalg::cuda::transpose3x3BatchCuda;

/* ----------------------------- Test Fixture ------------------------------- */

/**
 * @brief Fixture that skips tests when CUDA runtime is unavailable.
 */
class LinalgCudaFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Helper Functions --------------------------- */

namespace {

constexpr double K_TOL = 1e-10;
constexpr float K_TOL_F = 1e-5f;

template <typename T> void expectNear(T a, T b, T tol) { EXPECT_NEAR(a, b, tol); }

} // namespace

/* ----------------------------- gemm3x3Batch ------------------------------- */

/** @test gemm3x3Batch computes correct matrix products. */
TEST_F(LinalgCudaFixture, Gemm3x3Batch_Correctness) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  // A = [1 2 3; 4 5 6; 7 8 9], B = I
  std::vector<double> hAs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<double> hBs = {1, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0, 0, 0, 2, 0, 0, 0, 2};

  double *dAs = nullptr, *dBs = nullptr, *dCs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dBs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dCs, BATCH * 9 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dBs, hBs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(gemm3x3BatchCuda(dAs, dBs, BATCH, dCs, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hCs(BATCH * 9);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hCs.data(), dCs, BATCH * 9 * sizeof(double), cudaMemcpyDeviceToHost)));

  // First: A*I = A
  expectNear(hCs[0], 1.0, K_TOL);
  expectNear(hCs[1], 2.0, K_TOL);
  expectNear(hCs[2], 3.0, K_TOL);
  expectNear(hCs[4], 5.0, K_TOL);
  expectNear(hCs[8], 9.0, K_TOL);

  // Second: I * 2I = 2I
  expectNear(hCs[9], 2.0, K_TOL);
  expectNear(hCs[13], 2.0, K_TOL);
  expectNear(hCs[17], 2.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dCs);
#endif
}

/* ----------------------------- transpose3x3Batch -------------------------- */

/** @test transpose3x3Batch produces correct transposition. */
TEST_F(LinalgCudaFixture, Transpose3x3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  std::vector<double> hAs = {1, 2, 3, 4, 5, 6, 7, 8, 9};

  double *dAs = nullptr, *dOuts = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, BATCH * 9 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(transpose3x3BatchCuda(dAs, BATCH, dOuts, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hOuts(BATCH * 9);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hOuts.data(), dOuts, BATCH * 9 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Transposed: [1 4 7; 2 5 8; 3 6 9]
  expectNear(hOuts[0], 1.0, K_TOL);
  expectNear(hOuts[1], 4.0, K_TOL);
  expectNear(hOuts[2], 7.0, K_TOL);
  expectNear(hOuts[3], 2.0, K_TOL);
  expectNear(hOuts[4], 5.0, K_TOL);
  expectNear(hOuts[5], 8.0, K_TOL);
  expectNear(hOuts[6], 3.0, K_TOL);
  expectNear(hOuts[7], 6.0, K_TOL);
  expectNear(hOuts[8], 9.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dOuts);
#endif
}

/* ----------------------------- inverse3x3Batch ---------------------------- */

/** @test inverse3x3Batch computes correct inverses. */
TEST_F(LinalgCudaFixture, Inverse3x3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // A = [1 2 3; 0 1 4; 5 6 0], det = 1
  std::vector<double> hAs = {1, 2, 3, 0, 1, 4, 5, 6, 0};

  double *dAs = nullptr, *dOuts = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, BATCH * 9 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(inverse3x3BatchCuda(dAs, BATCH, dOuts, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hInv(BATCH * 9);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hInv.data(), dOuts, BATCH * 9 * sizeof(double), cudaMemcpyDeviceToHost)));

  // Known inverse: [-24 18 5; 20 -15 -4; -5 4 1]
  expectNear(hInv[0], -24.0, K_TOL);
  expectNear(hInv[1], 18.0, K_TOL);
  expectNear(hInv[2], 5.0, K_TOL);
  expectNear(hInv[3], 20.0, K_TOL);
  expectNear(hInv[4], -15.0, K_TOL);
  expectNear(hInv[5], -4.0, K_TOL);
  expectNear(hInv[6], -5.0, K_TOL);
  expectNear(hInv[7], 4.0, K_TOL);
  expectNear(hInv[8], 1.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dOuts);
#endif
}

/* ----------------------------- determinant3x3Batch ------------------------ */

/** @test determinant3x3Batch computes correct determinants. */
TEST_F(LinalgCudaFixture, Determinant3x3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  // A1: det = 1, A2: det = -306
  std::vector<double> hAs = {1, 2, 3, 0, 1, 4, 5, 6, 0, 6, 1, 1, 4, -2, 5, 2, 8, 7};

  double *dAs = nullptr, *dDets = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dDets, BATCH * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(determinant3x3BatchCuda(dAs, BATCH, dDets, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hDets(BATCH);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hDets.data(), dDets, BATCH * sizeof(double), cudaMemcpyDeviceToHost)));

  expectNear(hDets[0], 1.0, K_TOL);
  expectNear(hDets[1], -306.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dDets);
#endif
}

/* ----------------------------- matvec3x3Batch ----------------------------- */

/** @test matvec3x3Batch computes correct matrix-vector products. */
TEST_F(LinalgCudaFixture, Matvec3x3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // A = I, x = [1,2,3] -> y = [1,2,3]
  std::vector<double> hAs = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<double> hXs = {1, 2, 3};

  double *dAs = nullptr, *dXs = nullptr, *dYs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dXs, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dYs, BATCH * 3 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dXs, hXs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(matvec3x3BatchCuda(dAs, dXs, BATCH, dYs, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hYs(BATCH * 3);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hYs.data(), dYs, BATCH * 3 * sizeof(double), cudaMemcpyDeviceToHost)));

  expectNear(hYs[0], 1.0, K_TOL);
  expectNear(hYs[1], 2.0, K_TOL);
  expectNear(hYs[2], 3.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dXs);
  cudaFree(dYs);
#endif
}

/* ----------------------------- cross3Batch -------------------------------- */

/** @test cross3Batch computes correct cross products. */
TEST_F(LinalgCudaFixture, Cross3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  // [1,0,0] x [0,1,0] = [0,0,1]
  std::vector<double> hAs = {1, 0, 0};
  std::vector<double> hBs = {0, 1, 0};

  double *dAs = nullptr, *dBs = nullptr, *dCs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dBs, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dCs, BATCH * 3 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dBs, hBs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(cross3BatchCuda(dAs, dBs, BATCH, dCs, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hCs(BATCH * 3);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hCs.data(), dCs, BATCH * 3 * sizeof(double), cudaMemcpyDeviceToHost)));

  expectNear(hCs[0], 0.0, K_TOL);
  expectNear(hCs[1], 0.0, K_TOL);
  expectNear(hCs[2], 1.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dCs);
#endif
}

/* ----------------------------- dot3Batch ---------------------------------- */

/** @test dot3Batch computes correct dot products. */
TEST_F(LinalgCudaFixture, Dot3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  std::vector<double> hAs = {1, 2, 3, 1, 0, 0};
  std::vector<double> hBs = {4, 5, 6, 0, 1, 0};

  double *dAs = nullptr, *dBs = nullptr, *dOuts = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dBs, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, BATCH * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dBs, hBs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(dot3BatchCuda(dAs, dBs, BATCH, dOuts, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hOuts(BATCH);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hOuts.data(), dOuts, BATCH * sizeof(double), cudaMemcpyDeviceToHost)));

  // [1,2,3].[4,5,6] = 32, [1,0,0].[0,1,0] = 0
  expectNear(hOuts[0], 32.0, K_TOL);
  expectNear(hOuts[1], 0.0, K_TOL);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dOuts);
#endif
}

/* ----------------------------- normalize3Batch ---------------------------- */

/** @test normalize3Batch produces unit vectors. */
TEST_F(LinalgCudaFixture, Normalize3Batch) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 2;

  std::vector<double> hVs = {3, 4, 0, 0, 0, 5};

  double* dVs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dVs, BATCH * 3 * sizeof(double))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dVs, hVs.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(normalize3BatchCuda(dVs, BATCH, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<double> hOut(BATCH * 3);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hOut.data(), dVs, BATCH * 3 * sizeof(double), cudaMemcpyDeviceToHost)));

  // [3,4,0] -> [0.6, 0.8, 0]
  expectNear(hOut[0], 0.6, K_TOL);
  expectNear(hOut[1], 0.8, K_TOL);
  expectNear(hOut[2], 0.0, K_TOL);

  // [0,0,5] -> [0, 0, 1]
  expectNear(hOut[3], 0.0, K_TOL);
  expectNear(hOut[4], 0.0, K_TOL);
  expectNear(hOut[5], 1.0, K_TOL);

  cudaFree(dVs);
#endif
}

/* ----------------------------- Float Variants ----------------------------- */

/** @test gemm3x3BatchCudaF works for float precision. */
TEST_F(LinalgCudaFixture, Gemm3x3BatchFloat) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr int BATCH = 1;

  std::vector<float> hAs = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<float> hBs = {2, 0, 0, 0, 2, 0, 0, 0, 2};

  float *dAs = nullptr, *dBs = nullptr, *dCs = nullptr;
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dAs, BATCH * 9 * sizeof(float))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dBs, BATCH * 9 * sizeof(float))));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaMalloc(&dCs, BATCH * 9 * sizeof(float))));

  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dAs, hAs.data(), BATCH * 9 * sizeof(float), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(dBs, hBs.data(), BATCH * 9 * sizeof(float), cudaMemcpyHostToDevice)));

  ASSERT_TRUE(gemm3x3BatchCudaF(dAs, dBs, BATCH, dCs, nullptr));
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(cudaDeviceSynchronize()));

  std::vector<float> hCs(BATCH * 9);
  ASSERT_TRUE(::apex::compat::cuda::isSuccess(
      cudaMemcpy(hCs.data(), dCs, BATCH * 9 * sizeof(float), cudaMemcpyDeviceToHost)));

  // I * 2I = 2I
  expectNear(hCs[0], 2.0f, K_TOL_F);
  expectNear(hCs[4], 2.0f, K_TOL_F);
  expectNear(hCs[8], 2.0f, K_TOL_F);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dCs);
#endif
}
