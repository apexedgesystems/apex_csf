/**
 * @file Quaternion_pTest.cpp
 * @brief Performance tests for quaternion operations.
 *
 * Measures:
 *  - CPU batch quaternion multiply throughput at 10k pairs
 *  - CPU batch vector rotate throughput at 10k pairs
 *  - CPU batch SLERP throughput at 10k pairs
 *  - CPU batch quaternion-to-rotation-matrix throughput at 10k pairs
 *  - GPU batch rotate / SLERP / multiply throughput at 10k pairs
 *  - CPU vs GPU speedup for rotate and SLERP
 *
 * Usage:
 *   ./Quaternion_PTEST --csv results.csv
 *   ./Quaternion_PTEST --quick
 *   ./Quaternion_PTEST --profile nsight --gtest_filter="*Gpu*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/math/quaternion/inc/Quaternion.hpp"
#include "src/utilities/math/quaternion/inc/QuaternionCuda.cuh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace ub = vernier::bench;
using namespace apex::math::quaternion;

namespace {

/* ----------------------------- Configuration ----------------------------- */

constexpr int BATCH_MEDIUM = 10000; ///< Medium batch for characterization

/* ----------------------------- Helpers ----------------------------- */

template <typename T>
inline std::vector<T> generateRandomQuaternions(int count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<T> dist(T(-1), T(1));

  std::vector<T> data(count * 4);
  for (int i = 0; i < count; ++i) {
    T w = dist(gen);
    T x = dist(gen);
    T y = dist(gen);
    T z = dist(gen);

    // Normalize
    T norm = std::sqrt(w * w + x * x + y * y + z * z);
    if (norm > T(1e-6)) {
      data[i * 4 + 0] = w / norm;
      data[i * 4 + 1] = x / norm;
      data[i * 4 + 2] = y / norm;
      data[i * 4 + 3] = z / norm;
    } else {
      data[i * 4 + 0] = T(1);
      data[i * 4 + 1] = T(0);
      data[i * 4 + 2] = T(0);
      data[i * 4 + 3] = T(0);
    }
  }
  return data;
}

template <typename T> inline std::vector<T> generateRandomVectors3(int count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<T> dist(T(-1), T(1));

  std::vector<T> data(count * 3);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

template <typename T> inline std::vector<T> generateRandomTs(int count, unsigned seed = 42) {
  std::mt19937 gen(seed);
  std::uniform_real_distribution<T> dist(T(0), T(1));

  std::vector<T> data(count);
  for (auto& v : data) {
    v = dist(gen);
  }
  return data;
}

} // namespace

/* ----------------------------- CPU Baseline: Multiply ----------------------------- */

/**
 * @brief CPU baseline for quaternion multiplication (10000 pairs).
 */
PERF_TEST(QuaternionPerf, CpuMultiply_Medium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS_A = generateRandomQuaternions<double>(BATCH);
  auto QS_B = generateRandomQuaternions<double>(BATCH, 123);
  std::vector<double> outs(BATCH * 4);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Quaternion<double> qa(&QS_A[b * 4]);
        Quaternion<double> qb(&QS_B[b * 4]);
        Quaternion<double> qout(&outs[b * 4]);
        qa.multiplyInto(qb, qout);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Quaternion<double> qa(&QS_A[b * 4]);
          Quaternion<double> qb(&QS_B[b * 4]);
          Quaternion<double> qout(&outs[b * 4]);
          qa.multiplyInto(qb, qout);
        }
        checksum = checksum + outs[0];
      },
      "cpu-multiply-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-quaternion: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- CPU Baseline: Rotate ----------------------------- */

/**
 * @brief CPU baseline for vector rotation by quaternion (10000 pairs).
 */
PERF_TEST(QuaternionPerf, CpuRotate_Medium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS = generateRandomQuaternions<double>(BATCH);
  const auto VS = generateRandomVectors3<double>(BATCH, 123);
  std::vector<double> outs(BATCH * 3);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Quaternion<double> q(&QS[b * 4]);
        q.rotateVectorInto(&VS[b * 3], &outs[b * 3]);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Quaternion<double> q(&QS[b * 4]);
          q.rotateVectorInto(&VS[b * 3], &outs[b * 3]);
        }
        checksum = checksum + outs[0];
      },
      "cpu-rotate-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-rotation: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- CPU Baseline: SLERP ----------------------------- */

/**
 * @brief CPU baseline for SLERP interpolation (10000 pairs).
 */
PERF_TEST(QuaternionPerf, CpuSlerp_Medium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS_A = generateRandomQuaternions<double>(BATCH);
  auto QS_B = generateRandomQuaternions<double>(BATCH, 123);
  const auto TS = generateRandomTs<double>(BATCH, 456);
  std::vector<double> outs(BATCH * 4);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Quaternion<double> qa(&QS_A[b * 4]);
        Quaternion<double> qb(&QS_B[b * 4]);
        Quaternion<double> qout(&outs[b * 4]);
        qa.slerpInto(qb, TS[b], qout);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Quaternion<double> qa(&QS_A[b * 4]);
          Quaternion<double> qb(&QS_B[b * 4]);
          Quaternion<double> qout(&outs[b * 4]);
          qa.slerpInto(qb, TS[b], qout);
        }
        checksum = checksum + outs[0];
      },
      "cpu-slerp-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-slerp: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- CPU Baseline: ToRotationMatrix ----------------------------- */

/**
 * @brief CPU baseline for quaternion to rotation matrix (10000 quaternions).
 */
PERF_TEST(QuaternionPerf, CpuToRotMat_Medium) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS = generateRandomQuaternions<double>(BATCH);
  std::vector<double> outs(BATCH * 9);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      for (int b = 0; b < BATCH; ++b) {
        Quaternion<double> q(&QS[b * 4]);
        q.toRotationMatrixInto(&outs[b * 9]);
      }
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        for (int b = 0; b < BATCH; ++b) {
          Quaternion<double> q(&QS[b * 4]);
          q.toRotationMatrixInto(&outs[b * 9]);
        }
        checksum = checksum + outs[0];
      },
      "cpu-torotmat-10k");

  std::printf("  Throughput: %.0f batches/s, Median: %.3f us/batch\n", result.callsPerSecond,
              result.stats.median);
  std::printf("  Per-conversion: %.3f ns\n", result.stats.median * 1000.0 / BATCH);
}

/* ----------------------------- GPU Batch Tests ----------------------------- */

/**
 * @brief GPU batch vector rotation by quaternion (10000 pairs).
 */
PERF_GPU_TEST(QuaternionPerf, GpuRotate_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto QS = generateRandomQuaternions<double>(BATCH);
  const auto VS = generateRandomVectors3<double>(BATCH, 123);

  // Allocate device buffers
  double* dQs = nullptr;
  double* dVsIn = nullptr;
  double* dVsOut = nullptr;

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dVsIn, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dVsOut, BATCH * 3 * sizeof(double))));

  // Copy input data
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, QS.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dVsIn, VS.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup(
      [&](cudaStream_t s) { cuda::rotateVectorBatchCuda(dQs, dVsIn, BATCH, dVsOut, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result = perf.cudaKernel([&](cudaStream_t s) {
                      cuda::rotateVectorBatchCuda(dQs, dVsIn, BATCH, dVsOut, s);
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  std::printf("  GPU kernel: %.3f us for %d rotations\n", result.totalTimeUs, BATCH);
  std::printf("  Per-rotation: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dQs);
  cudaFree(dVsIn);
  cudaFree(dVsOut);
}

/**
 * @brief GPU batch SLERP interpolation (10000 pairs).
 */
PERF_GPU_TEST(QuaternionPerf, GpuSlerp_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto QS_A = generateRandomQuaternions<double>(BATCH);
  const auto QS_B = generateRandomQuaternions<double>(BATCH, 123);
  const auto TS = generateRandomTs<double>(BATCH, 456);

  // Allocate device buffers
  double* dQsA = nullptr;
  double* dQsB = nullptr;
  double* dTs = nullptr;
  double* dQsOut = nullptr;

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dTs, BATCH * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  // Copy input data
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, QS_A.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, QS_B.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dTs, TS.data(), BATCH * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result = perf.cudaKernel([&](cudaStream_t s) {
                      cuda::slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, s);
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  std::printf("  GPU kernel: %.3f us for %d slerps\n", result.totalTimeUs, BATCH);
  std::printf("  Per-slerp: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dTs);
  cudaFree(dQsOut);
}

/**
 * @brief GPU batch quaternion multiplication (10000 pairs).
 */
PERF_GPU_TEST(QuaternionPerf, GpuMultiply_Medium) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  const auto QS_A = generateRandomQuaternions<double>(BATCH);
  const auto QS_B = generateRandomQuaternions<double>(BATCH, 123);

  // Allocate device buffers
  double* dQsA = nullptr;
  double* dQsB = nullptr;
  double* dQsOut = nullptr;

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  // Copy input data
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, QS_A.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, QS_B.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::multiplyBatchCuda(dQsA, dQsB, BATCH, dQsOut, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto result = perf.cudaKernel([&](cudaStream_t s) {
                      cuda::multiplyBatchCuda(dQsA, dQsB, BATCH, dQsOut, s);
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  std::printf("  GPU kernel: %.3f us for %d multiplies\n", result.totalTimeUs, BATCH);
  std::printf("  Per-multiply: %.3f ns\n", result.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dQsOut);
}

/* ----------------------------- CPU vs GPU Comparison ----------------------------- */

/**
 * @brief CPU vs GPU comparison for vector rotation.
 */
PERF_GPU_COMPARISON(QuaternionPerf, CpuVsGpu_Rotate) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS = generateRandomQuaternions<double>(BATCH);
  const auto VS = generateRandomVectors3<double>(BATCH, 123);
  std::vector<double> cpuOut(BATCH * 3);

  // CPU baseline
  auto cpuResult = perf.cpuBaseline([&] {
    for (int b = 0; b < BATCH; ++b) {
      Quaternion<double> q(&QS[b * 4]);
      q.rotateVectorInto(&VS[b * 3], &cpuOut[b * 3]);
    }
  });

  // GPU path
  double* dQs = nullptr;
  double* dVsIn = nullptr;
  double* dVsOut = nullptr;

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQs, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dVsIn, BATCH * 3 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dVsOut, BATCH * 3 * sizeof(double))));

  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQs, QS.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dVsIn, VS.data(), BATCH * 3 * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup(
      [&](cudaStream_t s) { cuda::rotateVectorBatchCuda(dQs, dVsIn, BATCH, dVsOut, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto gpuResult = perf.cudaKernel([&](cudaStream_t s) {
                         cuda::rotateVectorBatchCuda(dQs, dVsIn, BATCH, dVsOut, s);
                       })
                       .withLaunchConfig(GRID, BLOCK)
                       .measure();

  std::printf("Speedup: %.2fx\n", gpuResult.speedupVsCpu);
  std::printf("  CPU: %.3f us for %d rotations (%.3f ns/rotation)\n", cpuResult.stats.median, BATCH,
              cpuResult.stats.median * 1000.0 / BATCH);
  std::printf("  GPU: %.3f us for %d rotations (%.3f ns/rotation)\n", gpuResult.totalTimeUs, BATCH,
              gpuResult.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dQs);
  cudaFree(dVsIn);
  cudaFree(dVsOut);
}

/**
 * @brief CPU vs GPU comparison for SLERP interpolation.
 */
PERF_GPU_COMPARISON(QuaternionPerf, CpuVsGpu_Slerp) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int BATCH = BATCH_MEDIUM;
  auto QS_A = generateRandomQuaternions<double>(BATCH);
  auto QS_B = generateRandomQuaternions<double>(BATCH, 123);
  const auto TS = generateRandomTs<double>(BATCH, 456);
  std::vector<double> cpuOut(BATCH * 4);

  // CPU baseline
  auto cpuResult = perf.cpuBaseline([&] {
    for (int b = 0; b < BATCH; ++b) {
      Quaternion<double> qa(&QS_A[b * 4]);
      Quaternion<double> qb(&QS_B[b * 4]);
      Quaternion<double> qout(&cpuOut[b * 4]);
      qa.slerpInto(qb, TS[b], qout);
    }
  });

  // GPU path
  double* dQsA = nullptr;
  double* dQsB = nullptr;
  double* dTs = nullptr;
  double* dQsOut = nullptr;

  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsA, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsB, BATCH * 4 * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dTs, BATCH * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dQsOut, BATCH * 4 * sizeof(double))));

  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsA, QS_A.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dQsB, QS_B.data(), BATCH * 4 * sizeof(double), cudaMemcpyHostToDevice)));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dTs, TS.data(), BATCH * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) { cuda::slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, s); });

  constexpr int THREADS = 256;
  const dim3 GRID((BATCH + THREADS - 1) / THREADS);
  const dim3 BLOCK(THREADS);

  auto gpuResult = perf.cudaKernel([&](cudaStream_t s) {
                         cuda::slerpBatchCuda(dQsA, dQsB, dTs, BATCH, dQsOut, s);
                       })
                       .withLaunchConfig(GRID, BLOCK)
                       .measure();

  std::printf("Speedup: %.2fx\n", gpuResult.speedupVsCpu);
  std::printf("  CPU: %.3f us for %d slerps (%.3f ns/slerp)\n", cpuResult.stats.median, BATCH,
              cpuResult.stats.median * 1000.0 / BATCH);
  std::printf("  GPU: %.3f us for %d slerps (%.3f ns/slerp)\n", gpuResult.totalTimeUs, BATCH,
              gpuResult.totalTimeUs * 1000.0 / BATCH);
  std::printf("  Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dQsA);
  cudaFree(dQsB);
  cudaFree(dTs);
  cudaFree(dQsOut);
}

PERF_MAIN()
