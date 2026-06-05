/**
 * @file Linalg_pTest.cpp
 * @brief Performance tests for linear algebra operations.
 *
 * Measures:
 *  - CPU 3x3 batch GEMM throughput at 1k and 10k matrices
 *  - CPU 3x3 batch inverse throughput at 10k matrices
 *  - GPU 3x3 batch GEMM / inverse / transpose throughput at 10k matrices
 *  - CPU vs GPU speedup for 3x3 batch GEMM and inverse
 *  - CPU GEMM throughput for 16x16 and 64x64 matrices (BLAS path)
 *
 * Usage:
 *   ./Linalg_PTEST --csv results.csv
 *   ./Linalg_PTEST --quick
 *   ./Linalg_PTEST --profile nsight --gtest_filter="*Gpu*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/math/linalg/inc/Array.hpp"
#include "src/utilities/math/linalg/inc/ArrayCuda.cuh"
#include "src/utilities/math/linalg/inc/Matrix3.hpp"
#include "src/utilities/math/linalg/inc/Vector.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace ub = vernier::bench;
using namespace apex::math::linalg;

namespace {

/* ----------------------------- Configuration ----------------------------- */

constexpr int BATCH_SMALL = 1000;   ///< Small batch for quick tests
constexpr int BATCH_MEDIUM = 10000; ///< Medium batch for characterization

/* ----------------------------- Helpers ----------------------------- */

template <typename T>
inline std::vector<T> generateRandomMatrices3x3(int count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<T> dist(T(-1), T(1));

  std::vector<T> data(count * 9);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

} // namespace

/* ----------------------------- CPU Baseline: GEMM 3x3 ----------------------------- */

/**
 * @brief CPU baseline for 3x3 batch GEMM (1000 matrices).
 */
PERF_TEST(LinalgPerf, CpuGemm3x3_Small) {
  UB_PERF_GUARD(perf);

  constexpr int BATCH = BATCH_SMALL;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);
  const auto BS = generateRandomMatrices3x3<double>(BATCH, 123);
  std::vector<double> cs(BATCH * 9);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Matrix3<double> A(const_cast<double*>(&AS[b * 9]), Layout::RowMajor);
        Matrix3<double> B(const_cast<double*>(&BS[b * 9]), Layout::RowMajor);
        Matrix3<double> C(&cs[b * 9], Layout::RowMajor);
        A.gemmInto(B, C);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Matrix3<double> A(const_cast<double*>(&AS[b * 9]), Layout::RowMajor);
          Matrix3<double> B(const_cast<double*>(&BS[b * 9]), Layout::RowMajor);
          Matrix3<double> C(&cs[b * 9], Layout::RowMajor);
          A.gemmInto(B, C);
        }
        checksum = checksum + cs[0];
      },
      "cpu-gemm3x3-1k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-matrix: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/**
 * @brief CPU baseline for 3x3 batch GEMM (10000 matrices).
 */
PERF_TEST(LinalgPerf, CpuGemm3x3_Medium) {
  UB_PERF_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);
  const auto BS = generateRandomMatrices3x3<double>(BATCH, 123);
  std::vector<double> cs(BATCH * 9);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Matrix3<double> A(const_cast<double*>(&AS[b * 9]), Layout::RowMajor);
        Matrix3<double> B(const_cast<double*>(&BS[b * 9]), Layout::RowMajor);
        Matrix3<double> C(&cs[b * 9], Layout::RowMajor);
        A.gemmInto(B, C);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Matrix3<double> A(const_cast<double*>(&AS[b * 9]), Layout::RowMajor);
          Matrix3<double> B(const_cast<double*>(&BS[b * 9]), Layout::RowMajor);
          Matrix3<double> C(&cs[b * 9], Layout::RowMajor);
          A.gemmInto(B, C);
        }
        checksum = checksum + cs[0];
      },
      "cpu-gemm3x3-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-matrix: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- CPU Baseline: Inverse ----------------------------- */

/**
 * @brief CPU baseline for 3x3 batch inverse (10000 matrices).
 */
PERF_TEST(LinalgPerf, CpuInverse3x3_Medium) {
  UB_PERF_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  auto AS = generateRandomMatrices3x3<double>(BATCH);
  std::vector<double> outs(BATCH * 9);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        // Copy to output, then invert in place
        std::memcpy(&outs[b * 9], &AS[b * 9], 9 * sizeof(double));
        Matrix3<double> M(&outs[b * 9], Layout::RowMajor);
        M.inverseInPlace();
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          std::memcpy(&outs[b * 9], &AS[b * 9], 9 * sizeof(double));
          Matrix3<double> M(&outs[b * 9], Layout::RowMajor);
          M.inverseInPlace();
        }
        checksum = checksum + outs[0];
      },
      "cpu-inverse3x3-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-matrix: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- GPU Batch Tests ----------------------------- */

/**
 * @brief GPU batch 3x3 GEMM (10000 matrices).
 */
PERF_GPU_TEST(LinalgPerf, GpuGemm3x3_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);
  const auto BS = generateRandomMatrices3x3<double>(BATCH, 123);

  // Allocate device buffers
  double* dAs = nullptr;
  double* dBs = nullptr;
  double* dCs = nullptr;
  const std::size_t SIZE = BATCH * 9 * sizeof(double);

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dAs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dBs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dCs, SIZE)));

  // Copy input data
  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dAs, AS.data(), SIZE, cudaMemcpyHostToDevice)));
  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dBs, BS.data(), SIZE, cudaMemcpyHostToDevice)));

  // Warmup (no ASSERT in lambda)
  perf.cudaWarmup([&](cudaStream_t s) { cuda::gemm3x3BatchCuda(dAs, dBs, BATCH, dCs, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result =
      perf.cudaKernel([&](cudaStream_t s) { cuda::gemm3x3BatchCuda(dAs, dBs, BATCH, dCs, s); })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("  GPU kernel: %.3f us for %d matrices\n", result.totalTimeUs, BATCH);
  std::printf("  Per-matrix: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dCs);
}

/**
 * @brief GPU batch 3x3 inverse (10000 matrices).
 */
PERF_GPU_TEST(LinalgPerf, GpuInverse3x3_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);

  // Allocate device buffers
  double* dAs = nullptr;
  double* dOuts = nullptr;
  const std::size_t SIZE = BATCH * 9 * sizeof(double);

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dAs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, SIZE)));

  // Copy input data
  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dAs, AS.data(), SIZE, cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::inverse3x3BatchCuda(dAs, BATCH, dOuts, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result =
      perf.cudaKernel([&](cudaStream_t s) { cuda::inverse3x3BatchCuda(dAs, BATCH, dOuts, s); })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("  GPU kernel: %.3f us for %d matrices\n", result.totalTimeUs, BATCH);
  std::printf("  Per-matrix: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dAs);
  cudaFree(dOuts);
}

/**
 * @brief GPU batch 3x3 transpose (10000 matrices).
 */
PERF_GPU_TEST(LinalgPerf, GpuTranspose3x3_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);

  double* dAs = nullptr;
  double* dOuts = nullptr;
  const std::size_t SIZE = BATCH * 9 * sizeof(double);

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dAs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, SIZE)));
  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dAs, AS.data(), SIZE, cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::transpose3x3BatchCuda(dAs, BATCH, dOuts, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result =
      perf.cudaKernel([&](cudaStream_t s) { cuda::transpose3x3BatchCuda(dAs, BATCH, dOuts, s); })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("  GPU kernel: %.3f us for %d matrices\n", result.totalTimeUs, BATCH);
  std::printf("  Per-matrix: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dAs);
  cudaFree(dOuts);
}

/* ----------------------------- CPU vs GPU Comparison ----------------------------- */

/**
 * @brief CPU vs GPU comparison for 3x3 batch GEMM.
 */
PERF_GPU_COMPARISON(LinalgPerf, CpuVsGpu_Gemm3x3) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto AS = generateRandomMatrices3x3<double>(BATCH);
  const auto BS = generateRandomMatrices3x3<double>(BATCH, 123);
  std::vector<double> cpuOut(BATCH * 9);

  // CPU baseline
  auto cpuResult = perf.cpuBaseline([&] {
    for (int b = 0; b < BATCH; ++b) {
      Matrix3<double> A(const_cast<double*>(&AS[b * 9]), Layout::RowMajor);
      Matrix3<double> B(const_cast<double*>(&BS[b * 9]), Layout::RowMajor);
      Matrix3<double> C(&cpuOut[b * 9], Layout::RowMajor);
      A.gemmInto(B, C);
    }
  });

  // GPU path
  double* dAs = nullptr;
  double* dBs = nullptr;
  double* dCs = nullptr;
  const std::size_t SIZE = BATCH * 9 * sizeof(double);

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dAs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dBs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dCs, SIZE)));

  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dAs, AS.data(), SIZE, cudaMemcpyHostToDevice)));
  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dBs, BS.data(), SIZE, cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::gemm3x3BatchCuda(dAs, dBs, BATCH, dCs, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto gpuResult =
      perf.cudaKernel([&](cudaStream_t s) { cuda::gemm3x3BatchCuda(dAs, dBs, BATCH, dCs, s); })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("Speedup: %.2fx\n", gpuResult.speedupVsCpu);
  std::printf("  CPU: %.3f us for %d matrices (%.3f ns/matrix)\n", cpuResult.stats.median, BATCH,
              cpuResult.stats.median * 1000.0 / BATCH);
  std::printf("  GPU: %.3f us for %d matrices (%.3f ns/matrix)\n", gpuResult.totalTimeUs, BATCH,
              gpuResult.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dAs);
  cudaFree(dBs);
  cudaFree(dCs);
}

/**
 * @brief CPU vs GPU comparison for 3x3 batch inverse.
 */
PERF_GPU_COMPARISON(LinalgPerf, CpuVsGpu_Inverse3x3) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  auto AS = generateRandomMatrices3x3<double>(BATCH);
  std::vector<double> cpuOut(BATCH * 9);

  // CPU baseline
  auto cpuResult = perf.cpuBaseline([&] {
    for (int b = 0; b < BATCH; ++b) {
      std::memcpy(&cpuOut[b * 9], &AS[b * 9], 9 * sizeof(double));
      Matrix3<double> M(&cpuOut[b * 9], Layout::RowMajor);
      M.inverseInPlace();
    }
  });

  // GPU path
  double* dAs = nullptr;
  double* dOuts = nullptr;
  const std::size_t SIZE = BATCH * 9 * sizeof(double);

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dAs, SIZE)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dOuts, SIZE)));

  ASSERT_TRUE(
      apex::compat::cuda::isSuccess(cudaMemcpy(dAs, AS.data(), SIZE, cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::inverse3x3BatchCuda(dAs, BATCH, dOuts, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto gpuResult =
      perf.cudaKernel([&](cudaStream_t s) { cuda::inverse3x3BatchCuda(dAs, BATCH, dOuts, s); })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("Speedup: %.2fx\n", gpuResult.speedupVsCpu);
  std::printf("  CPU: %.3f us for %d matrices (%.3f ns/matrix)\n", cpuResult.stats.median, BATCH,
              cpuResult.stats.median * 1000.0 / BATCH);
  std::printf("  GPU: %.3f us for %d matrices (%.3f ns/matrix)\n", gpuResult.totalTimeUs, BATCH,
              gpuResult.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dAs);
  cudaFree(dOuts);
}

/* ----------------------------- Large Matrix Tests ----------------------------- */

/**
 * @brief CPU GEMM for 16x16 matrices (uses BLAS if available).
 */
PERF_TEST(LinalgPerf, CpuGemm_16x16) {
  UB_PERF_GUARD(perf);

  constexpr int N = 16;
  constexpr int COUNT = 1000;

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);

  // Allocate storage for matrices
  std::vector<double> dataA(N * N);
  std::vector<double> dataB(N * N);
  std::vector<double> dataC(N * N);

  for (std::size_t i = 0; i < N * N; ++i) {
    dataA[i] = dist(gen);
    dataB[i] = dist(gen);
  }

  // Create non-owning views
  Array<double> A(dataA.data(), N, N, Layout::RowMajor);
  Array<double> B(dataB.data(), N, N, Layout::RowMajor);
  Array<double> C(dataC.data(), N, N, Layout::RowMajor);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      A.gemmInto(B, C);
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < COUNT; ++i) {
          A.gemmInto(B, C);
        }
        checksum = checksum + C.data()[0];
      },
      "cpu-gemm-16x16");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-GEMM: %.3f us\n", result.stats.median / COUNT);
}

/**
 * @brief CPU GEMM for 64x64 matrices (uses BLAS).
 */
PERF_TEST(LinalgPerf, CpuGemm_64x64) {
  UB_PERF_GUARD(perf);

  constexpr int N = 64;
  constexpr int COUNT = 100;

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);

  // Allocate storage for matrices
  std::vector<double> dataA(N * N);
  std::vector<double> dataB(N * N);
  std::vector<double> dataC(N * N);

  for (std::size_t i = 0; i < N * N; ++i) {
    dataA[i] = dist(gen);
    dataB[i] = dist(gen);
  }

  // Create non-owning views
  Array<double> A(dataA.data(), N, N, Layout::RowMajor);
  Array<double> B(dataB.data(), N, N, Layout::RowMajor);
  Array<double> C(dataC.data(), N, N, Layout::RowMajor);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      A.gemmInto(B, C);
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < COUNT; ++i) {
          A.gemmInto(B, C);
        }
        checksum = checksum + C.data()[0];
      },
      "cpu-gemm-64x64");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-GEMM: %.3f us\n", result.stats.median / COUNT);
}

PERF_MAIN()
