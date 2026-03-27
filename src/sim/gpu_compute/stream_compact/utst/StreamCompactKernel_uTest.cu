/**
 * @file StreamCompactKernel_uTest.cu
 * @brief Unit tests for StreamCompactKernel CUDA kernels.
 */

#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cmath>

#include <algorithm>
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

using sim::gpu_compute::cuda::classifyHistogramCuda;
using sim::gpu_compute::cuda::streamCompactCuda;

/* ----------------------------- Test Fixture ----------------------------- */

class StreamCompactKernelFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Null Parameter Tests ----------------------------- */

/** @test streamCompactCuda returns false with null input. */
TEST_F(StreamCompactKernelFixture, NullInputReturnsFalse) {
  EXPECT_FALSE(streamCompactCuda(nullptr, 100, 0.5f, reinterpret_cast<float*>(1),
                                 reinterpret_cast<std::uint32_t*>(1)));
}

/** @test streamCompactCuda returns false with zero elements. */
TEST_F(StreamCompactKernelFixture, ZeroElementsReturnsFalse) {
  EXPECT_FALSE(streamCompactCuda(reinterpret_cast<const float*>(1), 0, 0.5f,
                                 reinterpret_cast<float*>(1), reinterpret_cast<std::uint32_t*>(1)));
}

/** @test classifyHistogramCuda returns false with null input. */
TEST_F(StreamCompactKernelFixture, ClassifyNullReturnsFalse) {
  EXPECT_FALSE(
      classifyHistogramCuda(nullptr, 100, 8, 0.0f, 1.0f, reinterpret_cast<std::uint32_t*>(1)));
}

/* ----------------------------- Basic Compaction Tests ----------------------------- */

/** @test All elements above threshold: all selected. */
TEST_F(StreamCompactKernelFixture, AllAboveThreshold) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1024;
  std::vector<float> hInput(N, 1.0f); // All above 0.5

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dCount, sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(streamCompactCuda(dIn, N, 0.5f, dOut, dCount));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::uint32_t count = 0;
  ASSERT_EQ(cudaMemcpy(&count, dCount, sizeof(std::uint32_t), cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(count, N);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
#endif
}

/** @test No elements above threshold: none selected. */
TEST_F(StreamCompactKernelFixture, NoneAboveThreshold) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1024;
  std::vector<float> hInput(N, 0.0f); // All below 0.5

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dCount, sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(streamCompactCuda(dIn, N, 0.5f, dOut, dCount));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::uint32_t count = 0;
  ASSERT_EQ(cudaMemcpy(&count, dCount, sizeof(std::uint32_t), cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(count, 0u);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
#endif
}

/** @test Half above threshold: correct count and values preserved. */
TEST_F(StreamCompactKernelFixture, HalfAboveThreshold) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1024;
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = (i % 2 == 0) ? 1.0f : 0.0f; // Alternating above/below
  }

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dCount, sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(streamCompactCuda(dIn, N, 0.5f, dOut, dCount));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::uint32_t count = 0;
  ASSERT_EQ(cudaMemcpy(&count, dCount, sizeof(std::uint32_t), cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(count, N / 2);

  // All output values should be 1.0
  std::vector<float> hOut(count);
  ASSERT_EQ(cudaMemcpy(hOut.data(), dOut, count * sizeof(float), cudaMemcpyDeviceToHost),
            cudaSuccess);
  for (std::uint32_t i = 0; i < count; ++i) {
    EXPECT_FLOAT_EQ(hOut[i], 1.0f) << "element " << i;
  }

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
#endif
}

/* ----------------------------- Classification Tests ----------------------------- */

/** @test Classify uniform values into correct bins. */
TEST_F(StreamCompactKernelFixture, ClassifyUniform) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 800;
  constexpr std::uint32_t BINS = 8;

  // 100 elements per bin: [0, 0.125), [0.125, 0.25), ..., [0.875, 1.0)
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = static_cast<float>(i) / static_cast<float>(N);
  }

  float* dIn = nullptr;
  std::uint32_t* dHist = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dHist, BINS * sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(classifyHistogramCuda(dIn, N, BINS, 0.0f, 1.0f, dHist));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<std::uint32_t> result(BINS);
  ASSERT_EQ(cudaMemcpy(result.data(), dHist, BINS * sizeof(std::uint32_t), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // Each bin should have ~100 elements
  std::uint32_t total = 0;
  for (std::uint32_t b = 0; b < BINS; ++b) {
    EXPECT_EQ(result[b], 100u) << "bin " << b;
    total += result[b];
  }
  EXPECT_EQ(total, N);

  cudaFree(dIn);
  cudaFree(dHist);
#endif
}

/* ----------------------------- Large Scale Tests ----------------------------- */

/** @test Large array (4M elements) compact + classify completes without error. */
TEST_F(StreamCompactKernelFixture, LargeArrayCompletes) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 4u << 20; // 4M
  constexpr std::uint32_t BINS = 16;

  // ~50% above threshold
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = (i % 2 == 0) ? 0.8f : 0.2f;
  }

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  std::uint32_t* dHist = nullptr;
  ASSERT_EQ(cudaMalloc(&dIn, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOut, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dCount, sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dHist, BINS * sizeof(std::uint32_t)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice), cudaSuccess);

  ASSERT_TRUE(streamCompactCuda(dIn, N, 0.5f, dOut, dCount));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::uint32_t count = 0;
  ASSERT_EQ(cudaMemcpy(&count, dCount, sizeof(std::uint32_t), cudaMemcpyDeviceToHost), cudaSuccess);
  EXPECT_EQ(count, N / 2);

  // Classify compacted elements
  ASSERT_TRUE(classifyHistogramCuda(dOut, count, BINS, 0.0f, 1.0f, dHist));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<std::uint32_t> hist(BINS);
  ASSERT_EQ(cudaMemcpy(hist.data(), dHist, BINS * sizeof(std::uint32_t), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // All compacted values are 0.8, should be in bin 12 (0.8 / (1.0/16) = 12.8 -> bin 12)
  std::uint32_t total = 0;
  for (std::uint32_t b = 0; b < BINS; ++b) {
    total += hist[b];
  }
  EXPECT_EQ(total, count);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
  cudaFree(dHist);
#endif
}
