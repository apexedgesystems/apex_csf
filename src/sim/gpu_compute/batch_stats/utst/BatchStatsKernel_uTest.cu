/**
 * @file BatchStatsKernel_uTest.cu
 * @brief Unit tests for BatchStatsKernel CUDA kernels.
 */

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsKernel.cuh"
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

using sim::gpu_compute::GroupStats;
using sim::gpu_compute::cuda::batchHistogramCuda;
using sim::gpu_compute::cuda::batchStatsCuda;

static constexpr float K_TOL = 1e-3f;

/* ----------------------------- Test Fixture ----------------------------- */

class BatchStatsKernelFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Null Parameter Tests ----------------------------- */

/** @test batchStatsCuda returns false with null input. */
TEST_F(BatchStatsKernelFixture, NullInputReturnsFalse) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  GroupStats* dOut = nullptr;
  cudaMalloc(&dOut, sizeof(GroupStats));
  EXPECT_FALSE(batchStatsCuda(nullptr, 1024, 1024, dOut));
  cudaFree(dOut);
#endif
}

/** @test batchStatsCuda returns false with null output. */
TEST_F(BatchStatsKernelFixture, NullOutputReturnsFalse) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  float* dIn = nullptr;
  cudaMalloc(&dIn, 1024 * sizeof(float));
  EXPECT_FALSE(batchStatsCuda(dIn, 1024, 1024, nullptr));
  cudaFree(dIn);
#endif
}

/** @test batchStatsCuda returns false with zero elements. */
TEST_F(BatchStatsKernelFixture, ZeroElementsReturnsFalse) {
  EXPECT_FALSE(
      batchStatsCuda(reinterpret_cast<const float*>(1), 0, 1024, reinterpret_cast<GroupStats*>(1)));
}

/* ----------------------------- Single Group Tests ----------------------------- */

/** @test Single-group reduction produces correct min/max/sum. */
TEST_F(BatchStatsKernelFixture, SingleGroupCorrectness) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1024;
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = static_cast<float>(i) - 500.0f; // Range: [-500, 523]
  }

  float* dInput = nullptr;
  GroupStats* dOutput = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOutput, sizeof(GroupStats)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(batchStatsCuda(dInput, N, N, dOutput));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  GroupStats result{};
  ASSERT_EQ(cudaMemcpy(&result, dOutput, sizeof(GroupStats), cudaMemcpyDeviceToHost), cudaSuccess);

  EXPECT_NEAR(result.minVal, -500.0f, K_TOL);
  EXPECT_NEAR(result.maxVal, 523.0f, K_TOL);
  EXPECT_EQ(result.count, N);

  // Verify sum
  float expectedSum = 0.0f;
  for (const auto& v : hInput) {
    expectedSum += v;
  }
  EXPECT_NEAR(result.sum, expectedSum, std::abs(expectedSum) * 1e-4f);

  cudaFree(dInput);
  cudaFree(dOutput);
#endif
}

/* ----------------------------- Multi-Group Tests ----------------------------- */

/** @test Multi-group reduction produces correct per-group results. */
TEST_F(BatchStatsKernelFixture, MultiGroupCorrectness) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 2048;
  constexpr std::uint32_t GROUP_SIZE = 512;
  constexpr std::uint32_t GROUP_COUNT = N / GROUP_SIZE;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    // Each group gets a different offset so results are distinguishable
    const float GROUP_OFFSET = static_cast<float>(i / GROUP_SIZE) * 100.0f;
    hInput[i] = GROUP_OFFSET + static_cast<float>(i % GROUP_SIZE) * 0.1f;
  }

  float* dInput = nullptr;
  GroupStats* dOutput = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOutput, GROUP_COUNT * sizeof(GroupStats)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(batchStatsCuda(dInput, N, GROUP_SIZE, dOutput));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<GroupStats> results(GROUP_COUNT);
  ASSERT_EQ(
      cudaMemcpy(results.data(), dOutput, GROUP_COUNT * sizeof(GroupStats), cudaMemcpyDeviceToHost),
      cudaSuccess);

  for (std::uint32_t g = 0; g < GROUP_COUNT; ++g) {
    const float GROUP_OFFSET = static_cast<float>(g) * 100.0f;
    EXPECT_NEAR(results[g].minVal, GROUP_OFFSET, K_TOL) << "group " << g;
    EXPECT_NEAR(results[g].maxVal, GROUP_OFFSET + 51.1f, 0.1f) << "group " << g;
    EXPECT_EQ(results[g].count, GROUP_SIZE) << "group " << g;
  }

  cudaFree(dInput);
  cudaFree(dOutput);
#endif
}

/* ----------------------------- Histogram Tests ----------------------------- */

/** @test Histogram bins values correctly for uniform input. */
TEST_F(BatchStatsKernelFixture, HistogramUniformInput) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1000;
  constexpr std::uint32_t BINS = 10;

  // Uniform distribution [0, 10)
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = static_cast<float>(i) / 100.0f; // 0.00 to 9.99
  }

  float* dInput = nullptr;
  std::uint32_t* dHist = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dHist, BINS * sizeof(std::uint32_t)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(batchHistogramCuda(dInput, N, N, BINS, 0.0f, 10.0f, dHist));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<std::uint32_t> result(BINS);
  ASSERT_EQ(cudaMemcpy(result.data(), dHist, BINS * sizeof(std::uint32_t), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // Each bin should have ~100 elements (uniform distribution)
  std::uint32_t total = 0;
  for (std::uint32_t b = 0; b < BINS; ++b) {
    EXPECT_EQ(result[b], 100u) << "bin " << b;
    total += result[b];
  }
  EXPECT_EQ(total, N);

  cudaFree(dInput);
  cudaFree(dHist);
#endif
}

/** @test Histogram null parameters return false. */
TEST_F(BatchStatsKernelFixture, HistogramNullReturnsFalse) {
  EXPECT_FALSE(
      batchHistogramCuda(nullptr, 100, 100, 10, 0.0f, 1.0f, reinterpret_cast<std::uint32_t*>(1)));
  EXPECT_FALSE(
      batchHistogramCuda(reinterpret_cast<const float*>(1), 100, 100, 10, 0.0f, 1.0f, nullptr));
}

/* ----------------------------- Large Scale Tests ----------------------------- */

/** @test Large array (1M elements) completes without error. */
TEST_F(BatchStatsKernelFixture, LargeArrayCompletes) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 1u << 20; // 1M
  constexpr std::uint32_t GROUP_SIZE = 4096;
  constexpr std::uint32_t GROUP_COUNT = N / GROUP_SIZE;

  std::vector<float> hInput(N, 1.0f);

  float* dInput = nullptr;
  GroupStats* dOutput = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dOutput, GROUP_COUNT * sizeof(GroupStats)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(batchStatsCuda(dInput, N, GROUP_SIZE, dOutput));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<GroupStats> results(GROUP_COUNT);
  ASSERT_EQ(
      cudaMemcpy(results.data(), dOutput, GROUP_COUNT * sizeof(GroupStats), cudaMemcpyDeviceToHost),
      cudaSuccess);

  for (const auto& g : results) {
    EXPECT_NEAR(g.minVal, 1.0f, K_TOL);
    EXPECT_NEAR(g.maxVal, 1.0f, K_TOL);
    EXPECT_EQ(g.count, GROUP_SIZE);
  }

  cudaFree(dInput);
  cudaFree(dOutput);
#endif
}
