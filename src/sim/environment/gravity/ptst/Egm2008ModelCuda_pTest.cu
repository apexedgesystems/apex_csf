/**
 * @file Egm2008ModelCuda_pTest.cu
 * @brief Performance tests for the CUDA-accelerated EGM2008 gravity model.
 *
 * Measures:
 *  - GPU kernel timing at increasing truncation orders (N=180, 360, 720, 2190)
 *  - CPU vs GPU comparison at maximum fidelity (N=2190)
 *
 * Usage:
 *   ./SimEnvironmentGravity_PTEST --quick --gtest_filter="*CudaPerf*"
 *   ./SimEnvironmentGravity_PTEST --profile nsight --gtest_filter="*GpuKernel*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008ModelCuda.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;
using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008ModelCuda;
using sim::environment::gravity::Egm2008Params;

namespace {

/* ----------------------------- Configuration ----------------------------- */

constexpr double GM = 3.986004418e14;
constexpr double A = 6378137.0;
constexpr int BATCH = 100;

/* ----------------------------- Synthetic Coefficient Source ----------------------------- */

class DenseSynthSource final : public CoeffSource {
public:
  explicit DenseSynthSource(std::int16_t nMax) : nMax_(nMax) {}
  std::int16_t minDegree() const noexcept override { return 0; }
  std::int16_t maxDegree() const noexcept override { return nMax_; }
  bool get(std::int16_t n, std::int16_t m, double& c, double& s) const noexcept override {
    if (n < 0 || m < 0 || m > n || n > nMax_) {
      c = 0.0;
      s = 0.0;
      return false;
    }
    if (n == 0 && m == 0) {
      c = 1.0;
      s = 0.0;
      return true;
    }
    const double K = 1e-6;
    const double DENOM = static_cast<double>((n + 1) * (n + 1));
    c = K * std::sin(0.30 * n + 0.70 * m) / DENOM;
    s = K * std::cos(0.50 * n + 0.20 * m) / DENOM;
    return true;
  }

private:
  std::int16_t nMax_;
};

/* ----------------------------- Helpers ----------------------------- */

static bool cudaAvailable() { return apex::compat::cuda::runtimeAvailable(); }

std::vector<double> generatePositions(int count) {
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> altDist(200.0, 2000.0);
  std::uniform_real_distribution<double> latDist(-90.0, 90.0);
  std::uniform_real_distribution<double> lonDist(-180.0, 180.0);

  std::vector<double> pos(count * 3);
  for (int i = 0; i < count; ++i) {
    double alt = altDist(rng);
    double lat = latDist(rng) * M_PI / 180.0;
    double lon = lonDist(rng) * M_PI / 180.0;
    double r = A + alt * 1000.0;

    pos[i * 3 + 0] = r * std::cos(lat) * std::cos(lon);
    pos[i * 3 + 1] = r * std::cos(lat) * std::sin(lon);
    pos[i * 3 + 2] = r * std::sin(lat);
  }
  return pos;
}

} // namespace

/* ----------------------------- GPU Tests ----------------------------- */

/**
 * @brief GPU kernel timing for N=180.
 */
PERF_GPU_TEST(Egm2008CudaPerf, GpuKernelN180) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::int16_t N = 180;

  DenseSynthSource src(N);
  Egm2008ModelCuda model;
  Egm2008Params params{GM, A, N};
  ASSERT_TRUE(model.init(src, params, BATCH));

  auto positions = generatePositions(BATCH);
  std::vector<double> V(BATCH);
  std::vector<double> accel(BATCH * 3);

  perf.cudaWarmup([&](cudaStream_t) {
    model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
  });

  const dim3 GRID(BATCH);
  const dim3 BLOCK(std::min(N + 1, 256));

  auto result = perf.cudaKernel([&](cudaStream_t) {
                      model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  double msPerPos = result.totalTimeUs / 1000.0 / BATCH;
  std::printf("\n[N=%d, batch=%d] GPU: %.3f ms/pos (%.0f pos/s)\n", N, BATCH, msPerPos,
              1000.0 / msPerPos);
  std::printf("  Kernel time: %.3f ms\n", result.kernelTimeUs / 1000.0);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);
}

/**
 * @brief GPU kernel timing for N=360.
 */
PERF_GPU_TEST(Egm2008CudaPerf, GpuKernelN360) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::int16_t N = 360;

  DenseSynthSource src(N);
  Egm2008ModelCuda model;
  Egm2008Params params{GM, A, N};
  ASSERT_TRUE(model.init(src, params, BATCH));

  auto positions = generatePositions(BATCH);
  std::vector<double> V(BATCH);
  std::vector<double> accel(BATCH * 3);

  perf.cudaWarmup([&](cudaStream_t) {
    model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
  });

  const dim3 GRID(BATCH);
  const dim3 BLOCK(256);

  auto result = perf.cudaKernel([&](cudaStream_t) {
                      model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  double msPerPos = result.totalTimeUs / 1000.0 / BATCH;
  std::printf("\n[N=%d, batch=%d] GPU: %.3f ms/pos (%.0f pos/s)\n", N, BATCH, msPerPos,
              1000.0 / msPerPos);
  std::printf("  Kernel time: %.3f ms\n", result.kernelTimeUs / 1000.0);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);
}

/**
 * @brief GPU kernel timing for N=720.
 */
PERF_GPU_TEST(Egm2008CudaPerf, GpuKernelN720) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::int16_t N = 720;

  DenseSynthSource src(N);
  Egm2008ModelCuda model;
  Egm2008Params params{GM, A, N};
  ASSERT_TRUE(model.init(src, params, BATCH));

  auto positions = generatePositions(BATCH);
  std::vector<double> V(BATCH);
  std::vector<double> accel(BATCH * 3);

  perf.cudaWarmup([&](cudaStream_t) {
    model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
  });

  const dim3 GRID(BATCH);
  const dim3 BLOCK(256);

  auto result = perf.cudaKernel([&](cudaStream_t) {
                      model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  double msPerPos = result.totalTimeUs / 1000.0 / BATCH;
  std::printf("\n[N=%d, batch=%d] GPU: %.3f ms/pos (%.0f pos/s)\n", N, BATCH, msPerPos,
              1000.0 / msPerPos);
  std::printf("  Kernel time: %.3f ms\n", result.kernelTimeUs / 1000.0);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);
}

/**
 * @brief GPU kernel timing for N=2190 (maximum fidelity).
 */
PERF_GPU_TEST(Egm2008CudaPerf, GpuKernelN2190) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::int16_t N = 2190;

  DenseSynthSource src(N);
  Egm2008ModelCuda model;
  Egm2008Params params{GM, A, N};
  ASSERT_TRUE(model.init(src, params, BATCH));

  auto positions = generatePositions(BATCH);
  std::vector<double> V(BATCH);
  std::vector<double> accel(BATCH * 3);

  perf.cudaWarmup([&](cudaStream_t) {
    model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
  });

  const dim3 GRID(BATCH);
  const dim3 BLOCK(256);

  auto result = perf.cudaKernel([&](cudaStream_t) {
                      model.evaluateBatchECEF(positions.data(), BATCH, V.data(), accel.data());
                    })
                    .withLaunchConfig(GRID, BLOCK)
                    .measure();

  double msPerPos = result.totalTimeUs / 1000.0 / BATCH;
  std::printf("\n[N=%d, batch=%d] GPU: %.3f ms/pos (%.0f pos/s)\n", N, BATCH, msPerPos,
              1000.0 / msPerPos);
  std::printf("  Kernel time: %.3f ms\n", result.kernelTimeUs / 1000.0);
  std::printf("  Occupancy: %.1f%%\n", result.stats.occupancy.achievedOccupancy * 100);
}

/* ----------------------------- CPU vs GPU Comparison ----------------------------- */

/**
 * @brief CPU vs GPU speedup at N=2190 (maximum fidelity).
 */
PERF_GPU_COMPARISON(Egm2008CudaPerf, CpuVsGpuN2190) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::int16_t N = 2190;

  DenseSynthSource src(N);
  auto positions = generatePositions(BATCH);

  Egm2008Model cpuModel;
  Egm2008Params cpuParams{GM, A, N};
  ASSERT_TRUE(cpuModel.init(src, cpuParams));
  cpuModel.setAccelMode(Egm2008Model::AccelMode::Analytic);

  std::vector<double> cpuV(BATCH);
  std::vector<double> cpuAccel(BATCH * 3);

  auto cpuResult = perf.cpuBaseline([&] {
    for (int i = 0; i < BATCH; ++i) {
      cpuModel.evaluate(&positions[i * 3], cpuV[i], &cpuAccel[i * 3]);
    }
  });

  Egm2008ModelCuda gpuModel;
  Egm2008Params gpuParams{GM, A, N};
  ASSERT_TRUE(gpuModel.init(src, gpuParams, BATCH));

  std::vector<double> gpuV(BATCH);
  std::vector<double> gpuAccel(BATCH * 3);

  perf.cudaWarmup([&](cudaStream_t) {
    gpuModel.evaluateBatchECEF(positions.data(), BATCH, gpuV.data(), gpuAccel.data());
  });

  const dim3 GRID(BATCH);
  const dim3 BLOCK(256);

  auto gpuResult =
      perf.cudaKernel([&](cudaStream_t) {
            gpuModel.evaluateBatchECEF(positions.data(), BATCH, gpuV.data(), gpuAccel.data());
          })
          .withLaunchConfig(GRID, BLOCK)
          .measure();

  std::printf("\n=== CPU vs GPU (N=%d, batch=%d) ===\n", N, BATCH);
  std::printf("CPU: %.2f ms (%d positions)\n", cpuResult.stats.median / 1000.0, BATCH);
  std::printf("GPU: %.2f ms (%d positions)\n", gpuResult.totalTimeUs / 1000.0, BATCH);
  std::printf("CPU per-pos: %.2f ms\n", cpuResult.stats.median / 1000.0 / BATCH);
  std::printf("GPU per-pos: %.3f ms\n", gpuResult.totalTimeUs / 1000.0 / BATCH);
  std::printf("Speedup: %.1fx\n", gpuResult.speedupVsCpu);
  std::printf("Occupancy: %.1f%%\n", gpuResult.stats.occupancy.achievedOccupancy * 100);
}
