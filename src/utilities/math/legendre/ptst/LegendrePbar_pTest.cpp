/**
 * @file LegendrePbar_pTest.cpp
 * @brief Performance tests for normalized Legendre Pbar triangles.
 *
 * Measures:
 *  - CPU triangle throughput for N in {50, 100, 180, 360}
 *  - CPU cached path (precomputed A/B recurrence coefficients) at N=180
 *  - CPU raw-buffer path (no vector allocation) at N=180
 *  - GPU kernel-only batch throughput at N=180 (no H2D/D2H in measurement)
 *  - GPU workspace end-to-end with pinned transfers at N=180
 *  - GPU optimized (workspace + cached coefficients) CPU-vs-GPU speedup
 *
 * Usage:
 *   ./LegendrePbar_PTEST --csv results.csv
 *   ./LegendrePbar_PTEST --quick
 *   ./LegendrePbar_PTEST --profile nsight --gtest_filter="*GpuOptimized*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangle.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangleCuda.cuh"
#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ub = vernier::bench;
using namespace apex::math::legendre;

namespace {

/* ----------------------------- Configuration ----------------------------- */

constexpr int BATCH_SIZE = 32; ///< Samples per batch (GPU efficiency sweet spot)

// Deterministic x samples covering domain endpoints and interior
constexpr double SEEDS[] = {-1.0, -0.75, -0.5, -0.3, -0.1, 0.0, 0.1, 0.3, 0.5, 0.75, 1.0};
constexpr int SEED_COUNT = sizeof(SEEDS) / sizeof(SEEDS[0]);

/* ----------------------------- Helpers ----------------------------- */

inline std::vector<double> generateXSamples(int count) {
  std::vector<double> xs;
  xs.reserve(count);
  for (int i = 0; i < count; ++i) {
    xs.push_back(SEEDS[i % SEED_COUNT]);
  }
  return xs;
}

inline std::size_t triIdx(int n, int m) noexcept {
  return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2 +
         static_cast<std::size_t>(m);
}

} // namespace

/* ----------------------------- CPU Baseline Tests ----------------------------- */

/**
 * @brief CPU baseline for N=50 (single triangle computation).
 */
PERF_TEST(LegendrePbarPerf, CpuN50) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 50;
  constexpr double X = 0.3;

  perf.setup([&] {
    const std::size_t TRI_SIZE = pbarTriangleSize(N);
    ASSERT_GT(TRI_SIZE, 0u);
  });

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto tri = computeNormalizedPbarTriangleVector(N, X);
      volatile double sink = tri[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto tri = computeNormalizedPbarTriangleVector(N, X);
        checksum = checksum + tri[0] + tri[triIdx(N, 0)];
      },
      "cpu-n50");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief CPU baseline for N=100.
 */
PERF_TEST(LegendrePbarPerf, CpuN100) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 100;
  constexpr double X = 0.3;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto tri = computeNormalizedPbarTriangleVector(N, X);
      volatile double sink = tri[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto tri = computeNormalizedPbarTriangleVector(N, X);
        checksum = checksum + tri[0] + tri[triIdx(N, 0)];
      },
      "cpu-n100");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief CPU baseline for N=180 (primary comparison point).
 */
PERF_TEST(LegendrePbarPerf, CpuN180) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 180;
  constexpr double X = 0.3;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto tri = computeNormalizedPbarTriangleVector(N, X);
      volatile double sink = tri[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto tri = computeNormalizedPbarTriangleVector(N, X);
        checksum = checksum + tri[0] + tri[triIdx(N, 0)];
      },
      "cpu-n180");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief CPU baseline for N=360 (high-degree stress test).
 */
PERF_TEST(LegendrePbarPerf, CpuN360) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 360;
  constexpr double X = 0.3;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      const auto tri = computeNormalizedPbarTriangleVector(N, X);
      volatile double sink = tri[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        const auto tri = computeNormalizedPbarTriangleVector(N, X);
        checksum = checksum + tri[0] + tri[triIdx(N, 0)];
      },
      "cpu-n360");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- CPU Cached (Precomputed A/B) Tests ----------------------------- */

/**
 * @brief CPU with precomputed recurrence coefficients for N=180.
 *
 * Eliminates sqrt() calls from the hot loop.
 */
PERF_TEST(LegendrePbarPerf, CpuCachedN180) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 180;
  constexpr double X = 0.3;

  // One-time precomputation of A/B coefficients
  const auto coeffs = computeRecurrenceCoefficientsVector(N);
  const std::size_t TRI_SIZE = pbarTriangleSize(N);
  std::vector<double> out(TRI_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      computeNormalizedPbarTriangleCached(N, X, coeffs.A.data(), coeffs.B.data(), out.data(),
                                          TRI_SIZE);
      volatile double sink = out[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        computeNormalizedPbarTriangleCached(N, X, coeffs.A.data(), coeffs.B.data(), out.data(),
                                            TRI_SIZE);
        checksum = checksum + out[0] + out[triIdx(N, 0)];
      },
      "cpu-cached-n180");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief CPU raw buffer API (no vector allocation) for N=180.
 *
 * Tests overhead of vector allocation vs raw buffer.
 */
PERF_TEST(LegendrePbarPerf, CpuRawBufferN180) {
  UB_PERF_GUARD(perf);
  ub::attachProfilerHooks(perf, ub::detail::getPerfConfig());

  constexpr int N = 180;
  constexpr double X = 0.3;

  const std::size_t TRI_SIZE = pbarTriangleSize(N);
  std::vector<double> out(TRI_SIZE);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      computeNormalizedPbarTriangle(N, X, out.data(), TRI_SIZE);
      volatile double sink = out[0];
      (void)sink;
    }
  });

  volatile double checksum = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        computeNormalizedPbarTriangle(N, X, out.data(), TRI_SIZE);
        checksum = checksum + out[0] + out[triIdx(N, 0)];
      },
      "cpu-rawbuf-n180");

  std::printf("  Throughput: %.0f calls/s, Median: %.3f us/call\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- GPU Kernel-Only Tests ----------------------------- */

/**
 * @brief GPU kernel-only timing for N=180 (pure compute capability).
 */
PERF_GPU_TEST(LegendrePbarPerf, GpuKernelN180) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int N = 180;
  const auto XS = generateXSamples(BATCH_SIZE);
  const std::size_t TRI_SIZE = pbarTriangleSize(N);
  const std::size_t OUT_LEN = TRI_SIZE * BATCH_SIZE;

  // Allocate device buffers (not measured)
  double* dXs = nullptr;
  double* dOut = nullptr;
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dXs, BATCH_SIZE * sizeof(double))));
  ASSERT_TRUE(apex::compat::cuda::isSuccess(cudaMalloc(&dOut, OUT_LEN * sizeof(double))));

  // Copy input data once (not measured)
  ASSERT_TRUE(apex::compat::cuda::isSuccess(
      cudaMemcpy(dXs, XS.data(), BATCH_SIZE * sizeof(double), cudaMemcpyHostToDevice)));

  perf.cudaWarmup([&](cudaStream_t s) {
    ASSERT_TRUE(computeNormalizedPbarTriangleBatchCuda(N, dXs, BATCH_SIZE, dOut, OUT_LEN, s));
  });

  const dim3 GRID(BATCH_SIZE);
  const dim3 BLOCK(std::min(N + 1, 256));

  auto result = perf.cudaKernel([&](cudaStream_t s) {
                      ASSERT_TRUE(computeNormalizedPbarTriangleBatchCuda(N, dXs, BATCH_SIZE, dOut,
                                                                         OUT_LEN, s));
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  std::printf("  GPU kernel: %.3f us per triangle\n",
              result.totalTimeUs); // Framework already computes per-call
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);

  cudaFree(dXs);
  cudaFree(dOut);
}

/* ----------------------------- GPU Workspace Tests ----------------------------- */

/**
 * @brief GPU workspace timing for N=180 (realistic end-to-end).
 */
PERF_GPU_TEST(LegendrePbarPerf, GpuWorkspaceN180) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int N = 180;
  const auto XS = generateXSamples(BATCH_SIZE);

  PbarWorkspace ws{};
  ASSERT_TRUE(createPbarWorkspace(ws, N, BATCH_SIZE, true, nullptr));

  perf.cudaWarmup([&](cudaStream_t) {
    std::memcpy(ws.hXs, XS.data(), BATCH_SIZE * sizeof(double));
    ASSERT_TRUE(enqueueCompute(ws, nullptr, nullptr, true));
    ASSERT_TRUE(synchronize(ws));
  });

  const dim3 GRID(BATCH_SIZE);
  const dim3 BLOCK(std::min(N + 1, 256));

  auto result = perf.cudaKernel([&](cudaStream_t) {
                      std::memcpy(ws.hXs, XS.data(), BATCH_SIZE * sizeof(double));
                      ASSERT_TRUE(enqueueCompute(ws, nullptr, nullptr, true));
                      ASSERT_TRUE(synchronize(ws));
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  std::printf("  GPU workspace: %.3f us per triangle (with H2D/D2H)\n", result.totalTimeUs);
  std::printf("  Transfer overhead: %.1f%%\n",
              result.stats.transfers.transferOverheadPct(result.kernelTimeUs));

  destroyPbarWorkspace(ws);
}

/* ----------------------------- GPU Optimized Tests ----------------------------- */

/**
 * @brief GPU optimized for N=180 (workspace + precomputed coefficients).
 */
PERF_GPU_COMPARISON(LegendrePbarPerf, GpuOptimizedN180) {
  UB_PERF_GPU_GUARD(perf);

  constexpr int N = 180;
  const auto XS = generateXSamples(BATCH_SIZE);

  // CPU baseline (batched for fair comparison)
  auto cpuResult = perf.cpuBaseline([&] {
    for (const auto& x : XS) {
      const auto tri = computeNormalizedPbarTriangleVector(N, x);
      volatile double sink = tri[0];
      (void)sink;
    }
  });

  // GPU optimized path
  PbarWorkspace ws{};
  ASSERT_TRUE(createPbarWorkspace(ws, N, BATCH_SIZE, true, nullptr));
  ASSERT_TRUE(ensurePbarCoefficients(ws));

  perf.cudaWarmup([&](cudaStream_t) {
    std::memcpy(ws.hXs, XS.data(), BATCH_SIZE * sizeof(double));
    ASSERT_TRUE(enqueueCompute(ws, nullptr, nullptr, true));
    ASSERT_TRUE(synchronize(ws));
  });

  const dim3 GRID(BATCH_SIZE);
  const dim3 BLOCK(std::min(N + 1, 256));

  auto gpuResult = perf.cudaKernel([&](cudaStream_t) {
                         std::memcpy(ws.hXs, XS.data(), BATCH_SIZE * sizeof(double));
                         ASSERT_TRUE(enqueueCompute(ws, nullptr, nullptr, true));
                         ASSERT_TRUE(synchronize(ws));
                       })
                       .withLaunchConfig(GRID, BLOCK)
                       .measure();

  std::printf("Speedup: %.2fx (batch)\n", gpuResult.speedupVsCpu);
  std::printf("  CPU: %.3f us per triangle (sequential)\n", cpuResult.stats.median / BATCH_SIZE);
  std::printf("  GPU: %.3f us per triangle (amortized over batch)\n",
              gpuResult.totalTimeUs / BATCH_SIZE);
  std::printf("  GPU batch throughput: %.3f us for %d triangles\n", gpuResult.totalTimeUs,
              BATCH_SIZE);
  std::printf("  Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);

  destroyPbarWorkspace(ws);
}

PERF_MAIN()
