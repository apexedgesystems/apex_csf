/**
 * @file ConvFilterKernel_uTest.cu
 * @brief Unit tests for ConvFilterKernel CUDA kernels.
 */

#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cmath>

#include <numeric>
#include <vector>

#include <gtest/gtest.h>

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

using sim::gpu_compute::cuda::conv2dCuda;
using sim::gpu_compute::cuda::convSetKernel;
using sim::gpu_compute::cuda::generateBoxKernel;
using sim::gpu_compute::cuda::generateGaussianKernel;

static constexpr float K_TOL = 1e-4f;

/* ----------------------------- Test Fixture ----------------------------- */

class ConvFilterKernelFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Null Parameter Tests ----------------------------- */

/** @test conv2dCuda returns false with null input. */
TEST_F(ConvFilterKernelFixture, NullInputReturnsFalse) {
  EXPECT_FALSE(conv2dCuda(nullptr, 64, 64, 1, reinterpret_cast<float*>(1)));
}

/** @test conv2dCuda returns false with null output. */
TEST_F(ConvFilterKernelFixture, NullOutputReturnsFalse) {
  EXPECT_FALSE(conv2dCuda(reinterpret_cast<const float*>(1), 64, 64, 1, nullptr));
}

/** @test conv2dCuda returns false with zero dimensions. */
TEST_F(ConvFilterKernelFixture, ZeroDimensionsReturnsFalse) {
  EXPECT_FALSE(
      conv2dCuda(reinterpret_cast<const float*>(1), 0, 64, 1, reinterpret_cast<float*>(1)));
}

/** @test conv2dCuda returns false with excessive radius. */
TEST_F(ConvFilterKernelFixture, ExcessiveRadiusReturnsFalse) {
  EXPECT_FALSE(
      conv2dCuda(reinterpret_cast<const float*>(1), 64, 64, 16, reinterpret_cast<float*>(1)));
}

/* ----------------------------- Kernel Generation Tests ----------------------------- */

/** @test Gaussian kernel sums to 1.0. */
TEST_F(ConvFilterKernelFixture, GaussianKernelNormalized) {
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;
  std::vector<float> kernel(DIAM * DIAM);
  generateGaussianKernel(kernel.data(), R, 1.5f);

  float sum = 0.0f;
  for (const auto& v : kernel) {
    sum += v;
  }
  EXPECT_NEAR(sum, 1.0f, 1e-5f);
}

/** @test Gaussian kernel is symmetric. */
TEST_F(ConvFilterKernelFixture, GaussianKernelSymmetric) {
  constexpr std::uint32_t R = 2;
  constexpr std::uint32_t DIAM = 2 * R + 1;
  std::vector<float> kernel(DIAM * DIAM);
  generateGaussianKernel(kernel.data(), R, 1.0f);

  // Check horizontal symmetry
  for (std::uint32_t y = 0; y < DIAM; ++y) {
    for (std::uint32_t x = 0; x < R; ++x) {
      EXPECT_NEAR(kernel[y * DIAM + x], kernel[y * DIAM + (DIAM - 1 - x)], 1e-6f);
    }
  }

  // Check vertical symmetry
  for (std::uint32_t y = 0; y < R; ++y) {
    for (std::uint32_t x = 0; x < DIAM; ++x) {
      EXPECT_NEAR(kernel[y * DIAM + x], kernel[(DIAM - 1 - y) * DIAM + x], 1e-6f);
    }
  }
}

/** @test Box kernel has uniform weights summing to 1.0. */
TEST_F(ConvFilterKernelFixture, BoxKernelUniform) {
  constexpr std::uint32_t R = 2;
  constexpr std::uint32_t DIAM = 2 * R + 1;
  std::vector<float> kernel(DIAM * DIAM);
  generateBoxKernel(kernel.data(), R);

  const float EXPECTED = 1.0f / static_cast<float>(DIAM * DIAM);
  for (const auto& v : kernel) {
    EXPECT_NEAR(v, EXPECTED, 1e-6f);
  }
}

/* ----------------------------- Identity Convolution Tests ----------------------------- */

/** @test Identity kernel (radius=0) preserves input. */
TEST_F(ConvFilterKernelFixture, IdentityKernelPreservesInput) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 64;
  constexpr std::uint32_t H = 64;
  constexpr std::uint32_t N = W * H;

  // Identity kernel: single 1.0 at center
  float identityKernel = 1.0f;
  ASSERT_TRUE(convSetKernel(&identityKernel, 0));

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = static_cast<float>(i) * 0.01f;
  }

  float *dIn = nullptr, *dOut = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, 0, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<float> hOutput(N);
  ASSERT_EQ(cudaMemcpy(hOutput.data(), dOut, N * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);

  for (std::uint32_t i = 0; i < N; ++i) {
    EXPECT_NEAR(hOutput[i], hInput[i], K_TOL) << "pixel " << i;
  }

  cudaFree(dIn);
  cudaFree(dOut);
#endif
}

/* ----------------------------- Box Blur Tests ----------------------------- */

/** @test Box blur of uniform image produces same uniform value. */
TEST_F(ConvFilterKernelFixture, BoxBlurUniformImage) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 128;
  constexpr std::uint32_t H = 128;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 2;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  // Box kernel
  std::vector<float> hKernel(DIAM * DIAM);
  generateBoxKernel(hKernel.data(), R);
  ASSERT_TRUE(convSetKernel(hKernel.data(), R));

  // Uniform input (value = 7.0)
  std::vector<float> hInput(N, 7.0f);

  float *dIn = nullptr, *dOut = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, R, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<float> hOutput(N);
  ASSERT_EQ(cudaMemcpy(hOutput.data(), dOut, N * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // All pixels should be ~7.0 (uniform in = uniform out)
  for (std::uint32_t i = 0; i < N; ++i) {
    EXPECT_NEAR(hOutput[i], 7.0f, K_TOL) << "pixel " << i;
  }

  cudaFree(dIn);
  cudaFree(dOut);
#endif
}

/* ----------------------------- Edge Detection Tests ----------------------------- */

/** @test Box blur smooths a step edge (center column should decrease). */
TEST_F(ConvFilterKernelFixture, BoxBlurSmoothsStepEdge) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 64;
  constexpr std::uint32_t H = 64;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  generateBoxKernel(hKernel.data(), R);
  ASSERT_TRUE(convSetKernel(hKernel.data(), R));

  // Step edge: left half = 0, right half = 1
  std::vector<float> hInput(N);
  for (std::uint32_t y = 0; y < H; ++y) {
    for (std::uint32_t x = 0; x < W; ++x) {
      hInput[y * W + x] = (x >= W / 2) ? 1.0f : 0.0f;
    }
  }

  float *dIn = nullptr, *dOut = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, R, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<float> hOutput(N);
  ASSERT_EQ(cudaMemcpy(hOutput.data(), dOut, N * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // Middle row: value at edge should be ~0.5 (blurred step)
  const std::uint32_t MID_Y = H / 2;
  const float EDGE_VAL = hOutput[MID_Y * W + W / 2];
  EXPECT_GT(EDGE_VAL, 0.2f);
  EXPECT_LT(EDGE_VAL, 0.8f);

  // Far left should be close to 0, far right close to 1
  const float FAR_LEFT = hOutput[MID_Y * W + 0];
  const float FAR_RIGHT = hOutput[MID_Y * W + W - 1];
  EXPECT_LT(FAR_LEFT, 0.1f);
  EXPECT_GT(FAR_RIGHT, 0.9f);

  cudaFree(dIn);
  cudaFree(dOut);
#endif
}

/* ----------------------------- Large Image Tests ----------------------------- */

/** @test Large image (2048x2048) with Gaussian blur completes without error. */
TEST_F(ConvFilterKernelFixture, LargeImageCompletes) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 2048;
  constexpr std::uint32_t H = 2048;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  generateGaussianKernel(hKernel.data(), R, 1.5f);
  ASSERT_TRUE(convSetKernel(hKernel.data(), R));

  std::vector<float> hInput(N, 1.0f);

  float *dIn = nullptr, *dOut = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, R, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Spot check center pixel (uniform input -> should be ~1.0)
  float centerVal = 0.0f;
  ASSERT_EQ(
      cudaMemcpy(&centerVal, dOut + (H / 2) * W + W / 2, sizeof(float), cudaMemcpyDeviceToHost),
      cudaSuccess);
  EXPECT_NEAR(centerVal, 1.0f, K_TOL);

  cudaFree(dIn);
  cudaFree(dOut);
#endif
}

/** @test Large kernel radius (R=7, 15x15) works correctly. */
TEST_F(ConvFilterKernelFixture, LargeKernelRadius) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 256;
  constexpr std::uint32_t H = 256;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 7;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  generateGaussianKernel(hKernel.data(), R, 3.0f);
  ASSERT_TRUE(convSetKernel(hKernel.data(), R));

  // Uniform input
  std::vector<float> hInput(N, 5.0f);

  float *dIn = nullptr, *dOut = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, R, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  float centerVal = 0.0f;
  ASSERT_EQ(
      cudaMemcpy(&centerVal, dOut + (H / 2) * W + W / 2, sizeof(float), cudaMemcpyDeviceToHost),
      cudaSuccess);
  EXPECT_NEAR(centerVal, 5.0f, K_TOL);

  cudaFree(dIn);
  cudaFree(dOut);
#endif
}

/* ----------------------------- Separable Correctness Tests ----------------------------- */

/** @test Separable Gaussian matches 2D Gaussian within float tolerance. */
TEST_F(ConvFilterKernelFixture, SeparableMatchesTwoD) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 128;
  constexpr std::uint32_t H = 128;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;
  constexpr float SIGMA = 1.5f;

  std::vector<float> hKernel2D(DIAM * DIAM);
  generateGaussianKernel(hKernel2D.data(), R, SIGMA);
  ASSERT_TRUE(convSetKernel(hKernel2D.data(), R));

  std::vector<float> hKernel1D(DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel1D(hKernel1D.data(), R, SIGMA);
  ASSERT_TRUE(sim::gpu_compute::cuda::convSetKernel1D(hKernel1D.data(), R));

  std::vector<float> hInput(N);
  for (std::uint32_t y = 0; y < H; ++y) {
    for (std::uint32_t x = 0; x < W; ++x) {
      hInput[y * W + x] = sinf(static_cast<float>(x) * 0.1f) * cosf(static_cast<float>(y) * 0.15f);
    }
  }

  float *dIn = nullptr, *dOut2D = nullptr, *dOutSep = nullptr, *dTemp = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut2D, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOutSep, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dTemp, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(conv2dCuda(dIn, W, H, R, dOut2D));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  ASSERT_TRUE(sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOutSep));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<float> h2D(N);
  std::vector<float> hSep(N);
  ASSERT_EQ(cudaMemcpy(h2D.data(), dOut2D, N * sizeof(float), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(hSep.data(), dOutSep, N * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);

  std::uint32_t matchCount = 0;
  std::uint32_t totalCount = 0;
  float maxDiff = 0.0f;
  for (std::uint32_t y = R; y < H - R; ++y) {
    for (std::uint32_t x = R; x < W - R; ++x) {
      const float DIFF = std::fabs(h2D[y * W + x] - hSep[y * W + x]);
      if (DIFF > maxDiff) {
        maxDiff = DIFF;
      }
      if (DIFF < 1e-3f) {
        ++matchCount;
      }
      ++totalCount;
    }
  }

  const float MATCH_RATE = static_cast<float>(matchCount) / static_cast<float>(totalCount);
  EXPECT_GT(MATCH_RATE, 0.99f) << "maxDiff=" << maxDiff;
  EXPECT_LT(maxDiff, 0.01f);

  cudaFree(dIn);
  cudaFree(dOut2D);
  cudaFree(dOutSep);
  cudaFree(dTemp);
#endif
}

/** @test Separable box blur of uniform image returns same value. */
TEST_F(ConvFilterKernelFixture, SeparableBoxUniform) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t W = 128;
  constexpr std::uint32_t H = 128;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 2;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel1D(DIAM, 1.0f / static_cast<float>(DIAM));
  ASSERT_TRUE(sim::gpu_compute::cuda::convSetKernel1D(hKernel1D.data(), R));

  std::vector<float> hInput(N, 3.0f);

  float *dIn = nullptr, *dOut = nullptr, *dTemp = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dTemp, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<float> hOutput(N);
  ASSERT_EQ(cudaMemcpy(hOutput.data(), dOut, N * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);

  for (std::uint32_t i = 0; i < N; ++i) {
    EXPECT_NEAR(hOutput[i], 3.0f, 1e-3f) << "pixel " << i;
  }

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dTemp);
#endif
}
