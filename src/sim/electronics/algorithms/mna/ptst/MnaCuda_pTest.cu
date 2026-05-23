/**
 * @file MnaCuda_pTest.cu
 * @brief GPU performance tests for MNA solvers (MnaBatchCuda + MnaSystemCuda).
 *
 * Uses Vernier's PERF_GPU_TEST harness so the CSV automatically records
 * `kernelTimeMedianUs`, `transfers.h2dTimeUs`, `transfers.d2hTimeUs`,
 * `occupancy.achievedOccupancy`, etc.
 *
 * Two workload families:
 *   - Batch small-matrix solves (MnaBatchCuda): dims 8/16/32/64, batches
 *     up to 1024. End-to-end and kernel-only regimes.
 *   - Large single-system solve (MnaSystemCuda): dim 100/500/1000/2000.
 *     Probes the cuSOLVER vs CPU LAPACK crossover.
 *
 * CPU reference: per-system LAPACKE_dgesv on the same matrices, run
 * separately via PERF_TEST so Vernier's CV/median pipeline still
 * records it for cross-comparison.
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"

#include <gtest/gtest.h>

extern "C" {
#include <lapacke.h>
}

#include <cstddef>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <random>
#include <vector>

namespace ub = vernier::bench;
namespace mna_cuda = sim::electronics::algorithms::mna::cuda;

/* ----------------------------- Matrix Builders ----------------------------- */

static void buildBatch(std::size_t dim, std::size_t batch, std::vector<double>& As,
                       std::vector<double>& bs) {
  As.assign(batch * dim * dim, 0.0);
  bs.resize(batch * dim);
  std::mt19937 rng(0xC0DE);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  for (std::size_t k = 0; k < batch; ++k) {
    double* A = As.data() + k * dim * dim;
    for (std::size_t i = 0; i < dim; ++i) {
      A[i * dim + i] = 4.0 + 0.01 * dist(rng);
      if (i + 1 < dim) {
        A[i * dim + (i + 1)] = -1.0;
        A[(i + 1) * dim + i] = -1.0;
      }
    }
    for (std::size_t i = 0; i < dim; ++i) {
      bs[k * dim + i] = dist(rng);
    }
  }
}

static void buildSingle(std::size_t dim, std::vector<double>& A, std::vector<double>& b) {
  A.assign(dim * dim, 0.0);
  b.resize(dim);
  std::mt19937 rng(0xFEED);
  std::uniform_real_distribution<double> dist(-1.0, 1.0);
  for (std::size_t i = 0; i < dim; ++i) {
    A[i * dim + i] = 4.0 + 0.01 * dist(rng);
    if (i + 1 < dim) {
      A[i * dim + (i + 1)] = -1.0;
      A[(i + 1) * dim + i] = -1.0;
    }
    b[i] = dist(rng);
  }
}

/* ----------------------------- Batch Tests ----------------------------- */

static void runBatchCase(std::size_t dim, std::size_t batch, const char* label) {
  if (!mna_cuda::batchAvailable() || !mna_cuda::isSupportedDim(dim)) {
    GTEST_SKIP() << "CUDA batch solver unavailable or dim unsupported.";
  }
  UB_PERF_GPU_GUARD(perf);

  std::vector<double> As, bsTemplate;
  buildBatch(dim, batch, As, bsTemplate);

  mna_cuda::MnaBatchWorkspace ws;
  if (!ws.prepare(dim, batch)) {
    GTEST_SKIP() << "GPU workspace allocation failed.";
  }
  std::vector<double> bsGpu(bsTemplate.size());

  // Vernier wants a launch config to compute occupancy.
  const auto CFG = mna_cuda::getLaunchConfig(dim, batch);
  const dim3 grid(static_cast<unsigned>(CFG.gridX), static_cast<unsigned>(CFG.gridY),
                  static_cast<unsigned>(CFG.gridZ));
  const dim3 block(static_cast<unsigned>(CFG.blockX), static_cast<unsigned>(CFG.blockY),
                   static_cast<unsigned>(CFG.blockZ));

  std::printf("\n=== MnaBatchCuda %s (dim=%zu, batch=%zu) ===\n", label, dim, batch);

  // The library entry point bundles H2D + kernel + D2H in one call. Wrap
  // the whole call as the "kernel" lambda for Vernier; transfers are
  // then implicit. End-to-end wall time is captured by Vernier's stats.
  auto runFn = [&](cudaStream_t s) {
    mna_cuda::solveBatchCustom(ws, As.data(), bsGpu.data(), dim, batch, s);
  };

  perf.cudaWarmup(runFn);
  auto result = perf.cudaKernel(runFn, "gpu_batch_solve").withLaunchConfig(grid, block).measure();

  std::printf("  GPU end-to-end: %.2f us  occupancy=%.0f%%\n", result.stats.kernelTimeMedianUs,
              result.stats.occupancy.achievedOccupancy * 100.0);
}

PERF_GPU_TEST(MnaCudaBatch, Dim8_Batch64) { runBatchCase(8, 64, "Dim8_Batch64"); }
PERF_GPU_TEST(MnaCudaBatch, Dim8_Batch256) { runBatchCase(8, 256, "Dim8_Batch256"); }
PERF_GPU_TEST(MnaCudaBatch, Dim8_Batch1024) { runBatchCase(8, 1024, "Dim8_Batch1024"); }
PERF_GPU_TEST(MnaCudaBatch, Dim16_Batch64) { runBatchCase(16, 64, "Dim16_Batch64"); }
PERF_GPU_TEST(MnaCudaBatch, Dim16_Batch256) { runBatchCase(16, 256, "Dim16_Batch256"); }
PERF_GPU_TEST(MnaCudaBatch, Dim16_Batch1024) { runBatchCase(16, 1024, "Dim16_Batch1024"); }
PERF_GPU_TEST(MnaCudaBatch, Dim32_Batch64) { runBatchCase(32, 64, "Dim32_Batch64"); }
PERF_GPU_TEST(MnaCudaBatch, Dim32_Batch256) { runBatchCase(32, 256, "Dim32_Batch256"); }
PERF_GPU_TEST(MnaCudaBatch, Dim32_Batch1024) { runBatchCase(32, 1024, "Dim32_Batch1024"); }
PERF_GPU_TEST(MnaCudaBatch, Dim64_Batch64) { runBatchCase(64, 64, "Dim64_Batch64"); }
PERF_GPU_TEST(MnaCudaBatch, Dim64_Batch256) { runBatchCase(64, 256, "Dim64_Batch256"); }
PERF_GPU_TEST(MnaCudaBatch, Dim64_Batch1024) { runBatchCase(64, 1024, "Dim64_Batch1024"); }

/* ----------------------------- Single-Solve Tests ----------------------------- */

static void runSingleCase(std::size_t dim, const char* label) {
  if (!mna_cuda::available()) {
    GTEST_SKIP() << "cuSOLVER unavailable.";
  }
  UB_PERF_GPU_GUARD(perf);

  std::vector<double> A, bTemplate;
  buildSingle(dim, A, bTemplate);

  mna_cuda::MnaCudaWorkspace ws;
  if (!ws.prepare(dim)) {
    GTEST_SKIP() << "cuSOLVER workspace allocation failed.";
  }
  std::vector<double> bGpu(dim);

  std::printf("\n=== MnaSystemCuda %s (dim=%zu) ===\n", label, dim);

  auto runFn = [&](cudaStream_t s) {
    (void)s;
    mna_cuda::solveCuda(ws, A.data(), bGpu.data(), dim);
  };

  perf.cudaWarmup(runFn);
  auto result = perf.cudaKernel(runFn, "gpu_single_solve").measure();

  std::printf("  GPU end-to-end: %.2f us\n", result.stats.kernelTimeMedianUs);
}

PERF_GPU_TEST(MnaCudaSolve, Dim100) { runSingleCase(100, "Dim100"); }
PERF_GPU_TEST(MnaCudaSolve, Dim500) { runSingleCase(500, "Dim500"); }
PERF_GPU_TEST(MnaCudaSolve, Dim1000) { runSingleCase(1000, "Dim1000"); }
// Dim1121 = Intel 4004 NR matrix size (1121 nets). Phase 4 inner-loop target.
PERF_GPU_TEST(MnaCudaSolve, Dim1121) { runSingleCase(1121, "Dim1121_Intel4004"); }
PERF_GPU_TEST(MnaCudaSolve, Dim2000) { runSingleCase(2000, "Dim2000"); }

/* ----------------------------- CPU Reference (PERF_TEST) ----------------------------- */

static void runBatchCpu(std::size_t dim, std::size_t batch) {
  UB_PERF_GUARD(perf);
  std::vector<double> As, bsTemplate;
  buildBatch(dim, batch, As, bsTemplate);
  std::vector<double> bs(bsTemplate.size());
  std::vector<double> workA(dim * dim);
  std::vector<int> ipiv(dim);
  const int N = static_cast<int>(dim);
  auto runOnce = [&] {
    bs = bsTemplate;
    for (std::size_t k = 0; k < batch; ++k) {
      for (std::size_t i = 0; i < dim * dim; ++i)
        workA[i] = As[k * dim * dim + i];
      LAPACKE_dgesv(LAPACK_ROW_MAJOR, N, 1, workA.data(), N, ipiv.data(), bs.data() + k * dim, 1);
    }
  };
  perf.warmup(runOnce);
  perf.measured(runOnce, "cpu_batch_solve");
}

PERF_TEST(MnaCpuBatch, Dim8_Batch64) { runBatchCpu(8, 64); }
PERF_TEST(MnaCpuBatch, Dim8_Batch1024) { runBatchCpu(8, 1024); }
PERF_TEST(MnaCpuBatch, Dim16_Batch1024) { runBatchCpu(16, 1024); }
PERF_TEST(MnaCpuBatch, Dim32_Batch1024) { runBatchCpu(32, 1024); }
PERF_TEST(MnaCpuBatch, Dim64_Batch1024) { runBatchCpu(64, 1024); }

static void runSingleCpu(std::size_t dim) {
  UB_PERF_GUARD(perf);
  std::vector<double> A, bTemplate;
  buildSingle(dim, A, bTemplate);
  std::vector<double> b(dim);
  std::vector<int> ipiv(dim);
  std::vector<double> workA = A;
  auto runOnce = [&] {
    workA = A;
    b = bTemplate;
    LAPACKE_dgesv(LAPACK_ROW_MAJOR, static_cast<int>(dim), 1, workA.data(), static_cast<int>(dim),
                  ipiv.data(), b.data(), 1);
  };
  perf.warmup(runOnce);
  perf.measured(runOnce, "cpu_single_solve");
}

PERF_TEST(MnaCpuSolve, Dim100) { runSingleCpu(100); }
PERF_TEST(MnaCpuSolve, Dim500) { runSingleCpu(500); }
PERF_TEST(MnaCpuSolve, Dim1000) { runSingleCpu(1000); }
PERF_TEST(MnaCpuSolve, Dim2000) { runSingleCpu(2000); }

PERF_GPU_MAIN()
