/**
 * @file 00_Egm2008CudaTiming_dTest.cu
 * @brief Integration test: CUDA-accelerated EGM2008 model timing comparison vs CPU.
 *
 * Goals:
 *  - Compare CPU vs GPU evaluation timing at N=2190
 *  - Measure GPU batch evaluation throughput
 *  - Verify numerical parity between CPU and GPU
 *
 * Prereqs:
 *  - egm2008_full_n2190.bin in data/earth/ (86MB, 36-byte records)
 *  - CUDA runtime available
 *
 * Notes:
 *  - Not included in standard test runs
 */

#include "src/sim/environment/gravity/inc/FullTableCoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008ModelCuda.cuh"
#include "src/sim/environment/gravity/inc/earth/Wgs84Constants.hpp"

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008ModelCuda;
using sim::environment::gravity::Egm2008Params;
using sim::environment::gravity::FullTableCoeffSource;

namespace {

/* ----------------------------- Test Configuration ----------------------------- */

constexpr const char* K_FULL_TABLE_PATH =
    "src/sim/environment/gravity/data/earth/egm2008_full_n2190.bin";

// WGS84 constants
constexpr double GM = 3.986004418e14; // m^3/s^2
constexpr double A = 6378137.0;       // m (semi-major axis)

// Test point: 400km altitude over equator
constexpr double TEST_ALT_KM = 400.0;
constexpr double TEST_R = A + TEST_ALT_KM * 1000.0;

/* ----------------------------- Helper Functions ----------------------------- */

static bool cudaAvailable() { return apex::compat::cuda::runtimeAvailable(); }

/// Generate random positions in a shell around Earth
static void generatePositions(std::vector<double>& pos, int count, double minAlt, double maxAlt) {
  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<double> altDist(minAlt, maxAlt);
  std::uniform_real_distribution<double> latDist(-90.0, 90.0);
  std::uniform_real_distribution<double> lonDist(-180.0, 180.0);

  pos.resize(count * 3);
  for (int i = 0; i < count; ++i) {
    double alt = altDist(rng);
    double lat = latDist(rng) * M_PI / 180.0;
    double lon = lonDist(rng) * M_PI / 180.0;
    double r = A + alt * 1000.0;

    pos[i * 3 + 0] = r * std::cos(lat) * std::cos(lon);
    pos[i * 3 + 1] = r * std::cos(lat) * std::sin(lon);
    pos[i * 3 + 2] = r * std::sin(lat);
  }
}

} // namespace

/* ----------------------------- CPU vs GPU Comparison ----------------------------- */

/**
 * @test CpuVsGpuSinglePointN2190
 * @brief Compare CPU vs GPU single-point evaluation at maximum fidelity.
 */
TEST(Egm2008CudaTiming, CpuVsGpuSinglePointN2190) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  std::cout << "\n=== CPU vs GPU Single-Point Comparison (N=2190) ===\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const int N = 2190;
  Egm2008Params params{GM, A, static_cast<int16_t>(N)};

  // Initialize both models
  Egm2008Model cpuModel;
  Egm2008ModelCuda gpuModel;

  std::cout << "  Initializing CPU model (N=" << N << ")...\n";
  auto cpuInitStart = std::chrono::high_resolution_clock::now();
  ASSERT_TRUE(cpuModel.init(src, params));
  auto cpuInitEnd = std::chrono::high_resolution_clock::now();
  auto cpuInitMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(cpuInitEnd - cpuInitStart).count();
  std::cout << "    CPU init: " << cpuInitMs << " ms\n";
  cpuModel.setAccelMode(Egm2008Model::AccelMode::Analytic);

  std::cout << "  Initializing GPU model (N=" << N << ")...\n";
  auto gpuInitStart = std::chrono::high_resolution_clock::now();
  ASSERT_TRUE(gpuModel.init(src, params, 100)); // batch=100
  auto gpuInitEnd = std::chrono::high_resolution_clock::now();
  auto gpuInitMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(gpuInitEnd - gpuInitStart).count();
  std::cout << "    GPU init: " << gpuInitMs << " ms (includes device upload)\n";

  // Test point
  const double r_ecef[3] = {TEST_R, 0.0, 0.0};

  // Warmup
  double vCpu = 0, vGpu = 0;
  double aCpu[3] = {}, aGpu[3] = {};
  cpuModel.evaluate(r_ecef, vCpu, aCpu);
  gpuModel.evaluateECEF(r_ecef, vGpu, aGpu);

  // CPU timing
  constexpr int CPU_ITERS = 5;
  auto cpuStart = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < CPU_ITERS; ++i) {
    cpuModel.evaluate(r_ecef, vCpu, aCpu);
  }
  auto cpuEnd = std::chrono::high_resolution_clock::now();
  auto cpuUs = std::chrono::duration_cast<std::chrono::microseconds>(cpuEnd - cpuStart).count();
  double cpuMsPerCall = static_cast<double>(cpuUs) / CPU_ITERS / 1000.0;

  // GPU timing (single point - has transfer overhead)
  constexpr int GPU_ITERS = 10;
  auto gpuStart = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < GPU_ITERS; ++i) {
    gpuModel.evaluateECEF(r_ecef, vGpu, aGpu);
  }
  auto gpuEnd = std::chrono::high_resolution_clock::now();
  auto gpuUs = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd - gpuStart).count();
  double gpuMsPerCall = static_cast<double>(gpuUs) / GPU_ITERS / 1000.0;

  std::cout << "\n  Single-point evaluation timing:\n";
  std::cout << "    CPU: " << std::fixed << std::setprecision(2) << cpuMsPerCall << " ms/call\n";
  std::cout << "    GPU: " << std::fixed << std::setprecision(2) << gpuMsPerCall
            << " ms/call (includes transfer overhead)\n";
  std::cout << "    Speedup: " << std::setprecision(1) << cpuMsPerCall / gpuMsPerCall << "x\n";

  // Verify numerical parity
  std::cout << "\n  Numerical comparison:\n";
  std::cout << "    CPU V: " << std::scientific << std::setprecision(10) << vCpu << "\n";
  std::cout << "    GPU V: " << std::scientific << std::setprecision(10) << vGpu << "\n";
  double vErr = std::abs(vGpu - vCpu) / std::abs(vCpu);
  std::cout << "    V relative error: " << std::scientific << vErr << "\n";

  double aCpuMag = std::sqrt(aCpu[0] * aCpu[0] + aCpu[1] * aCpu[1] + aCpu[2] * aCpu[2]);
  double aGpuMag = std::sqrt(aGpu[0] * aGpu[0] + aGpu[1] * aGpu[1] + aGpu[2] * aGpu[2]);
  double aErr = std::abs(aGpuMag - aCpuMag) / aCpuMag;
  std::cout << "    |a| relative error: " << std::scientific << aErr << "\n";

  EXPECT_LT(vErr, 1e-6) << "Potential relative error too large";
  EXPECT_LT(aErr, 1e-3) << "Acceleration relative error too large";
}

/**
 * @test GpuBatchThroughputN2190
 * @brief Measure GPU batch evaluation throughput at maximum fidelity.
 */
TEST(Egm2008CudaTiming, GpuBatchThroughputN2190) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  std::cout << "\n=== GPU Batch Evaluation Throughput (N=2190) ===\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const int N = 2190;
  Egm2008Params params{GM, A, static_cast<int16_t>(N)};

  const int BATCH_SIZES[] = {1, 10, 50, 100, 200, 300};

  for (int batchSize : BATCH_SIZES) {
    Egm2008ModelCuda model;
    if (!model.init(src, params, batchSize)) {
      std::cout << "  Batch=" << std::setw(3) << batchSize << ": INIT FAILED (likely OOM)\n";
      continue;
    }

    // Generate random positions
    std::vector<double> positions;
    generatePositions(positions, batchSize, 200.0, 2000.0); // LEO altitudes

    std::vector<double> V(batchSize);
    std::vector<double> accel(batchSize * 3);

    // Warmup
    model.evaluateBatchECEF(positions.data(), batchSize, V.data(), accel.data());

    // Timed iterations
    constexpr int ITERS = 20;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITERS; ++i) {
      model.evaluateBatchECEF(positions.data(), batchSize, V.data(), accel.data());
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double msPerBatch = static_cast<double>(us) / ITERS / 1000.0;
    double msPerPos = msPerBatch / batchSize;
    double posPerSec = 1000.0 / msPerPos;

    std::cout << "  Batch=" << std::setw(3) << batchSize << ": " << std::fixed
              << std::setprecision(2) << std::setw(8) << msPerBatch << " ms/batch, "
              << std::setprecision(3) << std::setw(8) << msPerPos << " ms/pos, "
              << std::setprecision(0) << std::setw(6) << posPerSec << " pos/s\n";
  }
}

/**
 * @test CpuVsGpuBatchComparison
 * @brief Compare CPU sequential vs GPU batch for same workload.
 */
TEST(Egm2008CudaTiming, CpuVsGpuBatchComparison) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  std::cout << "\n=== CPU Sequential vs GPU Batch Comparison (N=2190) ===\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const int N = 2190;
  Egm2008Params params{GM, A, static_cast<int16_t>(N)};

  constexpr int BATCH = 100;

  // Initialize models
  Egm2008Model cpuModel;
  Egm2008ModelCuda gpuModel;

  ASSERT_TRUE(cpuModel.init(src, params));
  cpuModel.setAccelMode(Egm2008Model::AccelMode::Analytic);
  ASSERT_TRUE(gpuModel.init(src, params, BATCH));

  // Generate random positions
  std::vector<double> positions;
  generatePositions(positions, BATCH, 200.0, 2000.0);

  std::vector<double> cpuV(BATCH);
  std::vector<double> cpuAccel(BATCH * 3);
  std::vector<double> gpuV(BATCH);
  std::vector<double> gpuAccel(BATCH * 3);

  // CPU sequential timing
  std::cout << "  Processing " << BATCH << " positions...\n";
  auto cpuStart = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < BATCH; ++i) {
    cpuModel.evaluate(&positions[i * 3], cpuV[i], &cpuAccel[i * 3]);
  }
  auto cpuEnd = std::chrono::high_resolution_clock::now();
  auto cpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(cpuEnd - cpuStart).count();

  // GPU batch timing
  auto gpuStart = std::chrono::high_resolution_clock::now();
  gpuModel.evaluateBatchECEF(positions.data(), BATCH, gpuV.data(), gpuAccel.data());
  auto gpuEnd = std::chrono::high_resolution_clock::now();
  auto gpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(gpuEnd - gpuStart).count();

  double speedup = static_cast<double>(cpuMs) / std::max(1L, gpuMs);

  std::cout << "\n  Results:\n";
  std::cout << "    CPU sequential (" << BATCH << " positions): " << cpuMs << " ms\n";
  std::cout << "    GPU batch     (" << BATCH << " positions): " << gpuMs << " ms\n";
  std::cout << "    Speedup: " << std::fixed << std::setprecision(1) << speedup << "x\n";
  std::cout << "    CPU per position: " << std::setprecision(2)
            << static_cast<double>(cpuMs) / BATCH << " ms\n";
  std::cout << "    GPU per position: " << std::setprecision(3)
            << static_cast<double>(gpuMs) / BATCH << " ms\n";

  // Verify numerical parity
  double maxVErr = 0;
  double maxAErr = 0;
  for (int i = 0; i < BATCH; ++i) {
    double vErr = std::abs(gpuV[i] - cpuV[i]) / std::abs(cpuV[i]);
    maxVErr = std::max(maxVErr, vErr);

    double cpuMag = std::sqrt(cpuAccel[i * 3 + 0] * cpuAccel[i * 3 + 0] +
                              cpuAccel[i * 3 + 1] * cpuAccel[i * 3 + 1] +
                              cpuAccel[i * 3 + 2] * cpuAccel[i * 3 + 2]);
    double gpuMag = std::sqrt(gpuAccel[i * 3 + 0] * gpuAccel[i * 3 + 0] +
                              gpuAccel[i * 3 + 1] * gpuAccel[i * 3 + 1] +
                              gpuAccel[i * 3 + 2] * gpuAccel[i * 3 + 2]);
    double aErr = std::abs(gpuMag - cpuMag) / cpuMag;
    maxAErr = std::max(maxAErr, aErr);
  }

  std::cout << "\n  Numerical verification:\n";
  std::cout << "    Max V relative error: " << std::scientific << maxVErr << "\n";
  std::cout << "    Max |a| relative error: " << maxAErr << "\n";

  EXPECT_LT(maxVErr, 1e-6) << "Potential error too large";
  EXPECT_LT(maxAErr, 1e-2) << "Acceleration error too large";
}

/**
 * @test TruncationLevelComparison
 * @brief Compare CPU vs GPU at various truncation levels.
 */
TEST(Egm2008CudaTiming, TruncationLevelComparison) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  std::cout << "\n=== CPU vs GPU at Various Truncation Levels ===\n";
  std::cout << "  (100 positions batch)\n\n";

  FullTableCoeffSource src;
  if (!src.open(K_FULL_TABLE_PATH)) {
    GTEST_SKIP() << "EGM2008 data file not available: " << K_FULL_TABLE_PATH;
  }

  const int ORDERS[] = {60, 180, 360, 720, 1080, 2190};
  constexpr int BATCH = 100;

  // Generate positions once
  std::vector<double> positions;
  generatePositions(positions, BATCH, 200.0, 2000.0);

  std::vector<double> cpuV(BATCH);
  std::vector<double> cpuAccel(BATCH * 3);
  std::vector<double> gpuV(BATCH);
  std::vector<double> gpuAccel(BATCH * 3);

  std::cout << "  N       CPU(ms)    GPU(ms)    Speedup\n";
  std::cout << "  ----    -------    -------    -------\n";

  for (int n : ORDERS) {
    Egm2008Params params{GM, A, static_cast<int16_t>(n)};

    Egm2008Model cpuModel;
    Egm2008ModelCuda gpuModel;

    ASSERT_TRUE(cpuModel.init(src, params));
    cpuModel.setAccelMode(Egm2008Model::AccelMode::Analytic);
    ASSERT_TRUE(gpuModel.init(src, params, BATCH));

    // CPU timing
    auto cpuStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < BATCH; ++i) {
      cpuModel.evaluate(&positions[i * 3], cpuV[i], &cpuAccel[i * 3]);
    }
    auto cpuEnd = std::chrono::high_resolution_clock::now();
    auto cpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(cpuEnd - cpuStart).count();

    // GPU timing
    auto gpuStart = std::chrono::high_resolution_clock::now();
    gpuModel.evaluateBatchECEF(positions.data(), BATCH, gpuV.data(), gpuAccel.data());
    auto gpuEnd = std::chrono::high_resolution_clock::now();
    auto gpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(gpuEnd - gpuStart).count();

    double speedup = static_cast<double>(cpuMs) / std::max(1L, gpuMs);

    std::cout << "  " << std::setw(4) << n << "    " << std::setw(7) << cpuMs << "    "
              << std::setw(7) << gpuMs << "    " << std::fixed << std::setprecision(1)
              << std::setw(6) << speedup << "x\n";
  }
}
