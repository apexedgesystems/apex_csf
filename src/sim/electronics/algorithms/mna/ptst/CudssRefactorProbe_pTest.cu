/**
 * @file CudssRefactorProbe_pTest.cu
 * @brief cuDSS warm refactor-and-solve probe at Dim1121 (Intel 4004 MNA).
 *
 * The Phase 4 dense-cuSOLVER per-iter wall is 9.6 ms at Dim1121 -- 18x slower
 * than CPU sparse KLU (~543 us per stamp+factor+solve at the same scale). A
 * sparse direct solver only wins if the expensive symbolic analysis is done
 * once and each Newton iteration pays only a numeric *re-factorization* on the
 * unchanged sparsity pattern. This probe answers: at Dim1121 with the Intel
 * 4004 sparsity pattern, what is the per-iter cost of
 * `cudssMatrixSetValues + REFACTORIZATION + SOLVE`?
 *
 * cuDSS (NVIDIA Direct Sparse Solver) is the supported successor to the
 * deprecated cuSolverRf API: ANALYSIS performs the reordering + symbolic
 * factorization once, then REFACTORIZATION re-uses that structure for new
 * values. A residual check ||b - A x||_inf confirms both the initial factor and
 * the per-iter refactor solve correctly.
 *
 * Compares against:
 *   - 543 us  -- CPU sparse KLU at full 4004 (GridStampAll ptest).
 *   - 9.6 ms  -- GPU dense LU at Dim1121 (Phase 4 wall measurement).
 */

#include "src/bench/inc/Perf.hpp"

#include <cuda_runtime_api.h>
#include <cudss.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#define CHECK_CUDA(call)                                                                           \
  do {                                                                                             \
    cudaError_t status_ = (call);                                                                  \
    if (status_ != cudaSuccess) {                                                                  \
      GTEST_FAIL() << #call << " -> " << cudaGetErrorString(status_);                              \
    }                                                                                              \
  } while (0)

#define CHECK_CUDSS(call)                                                                          \
  do {                                                                                             \
    cudssStatus_t status_ = (call);                                                                \
    if (status_ != CUDSS_STATUS_SUCCESS) {                                                         \
      GTEST_FAIL() << #call << " -> cudssStatus=" << static_cast<int>(status_);                    \
    }                                                                                              \
  } while (0)

constexpr int N = 1121;
constexpr int N_TRANSISTORS = 2242;
constexpr double GMIN = 1e-3;

// Dense Intel-4004-like MNA matrix: 4 nz per synthetic transistor (gds / -gds
// at the d/s slots) plus a GMIN-to-ground diagonal and the ground-row anchor.
void buildSparseMna(std::vector<double>& dense, std::vector<double>& rhs) {
  dense.assign(N * N, 0.0);
  rhs.assign(N, 0.0);
  std::mt19937 rng(0xBEEF1121u);
  std::uniform_int_distribution<int> netDist(1, N - 1);
  std::uniform_real_distribution<double> gDist(1e-4, 5e-3);
  auto add = [&](int r, int c, double v) {
    if (r <= 0 || c <= 0 || r >= N || c >= N)
      return;
    dense[r * N + c] += v;
  };
  for (int i = 0; i < N_TRANSISTORS; ++i) {
    const int D = netDist(rng);
    const int S = netDist(rng);
    if (D == S)
      continue;
    const double G = gDist(rng);
    add(D, D, G);
    add(S, S, G);
    add(D, S, -G);
    add(S, D, -G);
  }
  for (int i = 1; i < N; ++i) {
    dense[i * N + i] += GMIN;
    rhs[i] = 0.01 * std::sin(0.37 * i);
  }
  dense[0] = 1.0; // ground anchor.
}

void denseToCsr(const std::vector<double>& dense, std::vector<int>& rowPtr,
                std::vector<int>& colIdx, std::vector<double>& values) {
  rowPtr.assign(N + 1, 0);
  colIdx.clear();
  values.clear();
  for (int r = 0; r < N; ++r) {
    rowPtr[r] = static_cast<int>(colIdx.size());
    for (int c = 0; c < N; ++c) {
      const double V = dense[r * N + c];
      if (V != 0.0) {
        colIdx.push_back(c);
        values.push_back(V);
      }
    }
  }
  rowPtr[N] = static_cast<int>(colIdx.size());
}

double residualInf(const std::vector<int>& rowPtr, const std::vector<int>& colIdx,
                   const std::vector<double>& values, const std::vector<double>& rhs,
                   const std::vector<double>& x) {
  double maxR = 0.0;
  for (int r = 0; r < N; ++r) {
    double ax = 0.0;
    for (int k = rowPtr[r]; k < rowPtr[r + 1]; ++k)
      ax += values[k] * x[colIdx[k]];
    maxR = std::max(maxR, std::abs(rhs[r] - ax));
  }
  return maxR;
}

namespace ub = vernier::bench;

PERF_TEST(CudssRefactor, RefactorAndSolve_Dim1121) {
  UB_PERF_GUARD(perf);

  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseMna(dense, rhs);

  std::vector<int> rowPtr;
  std::vector<int> colIdx;
  std::vector<double> values;
  denseToCsr(dense, rowPtr, colIdx, values);
  const int NNZ = static_cast<int>(colIdx.size());

  std::printf("\n=== cuDSS Refactor Probe at Dim%d ===\n", N);
  std::printf("  NNZ = %d  (%.2f%% density)\n", NNZ, 100.0 * NNZ / static_cast<double>(N * N));

  int* dRowPtr = nullptr;
  int* dColIdx = nullptr;
  double* dVal = nullptr;
  double* dX = nullptr;
  double* dB = nullptr;
  CHECK_CUDA(cudaMalloc(&dRowPtr, (N + 1) * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dColIdx, NNZ * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dVal, NNZ * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dX, N * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dB, N * sizeof(double)));
  CHECK_CUDA(cudaMemcpy(dRowPtr, rowPtr.data(), (N + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dColIdx, colIdx.data(), NNZ * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dVal, values.data(), NNZ * sizeof(double), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dB, rhs.data(), N * sizeof(double), cudaMemcpyHostToDevice));

  cudssHandle_t handle = nullptr;
  CHECK_CUDSS(cudssCreate(&handle));
  cudssConfig_t config = nullptr;
  CHECK_CUDSS(cudssConfigCreate(&config));
  cudssData_t data = nullptr;
  CHECK_CUDSS(cudssDataCreate(handle, &data));

  cudssMatrix_t A = nullptr;
  cudssMatrix_t x = nullptr;
  cudssMatrix_t b = nullptr;
  CHECK_CUDSS(cudssMatrixCreateCsr(&A, N, N, NNZ, dRowPtr, nullptr, dColIdx, dVal, CUDSS_R_32I,
                                   CUDSS_R_32I, CUDSS_R_64F, CUDSS_MTYPE_GENERAL, CUDSS_MVIEW_FULL,
                                   CUDSS_BASE_ZERO));
  CHECK_CUDSS(cudssMatrixCreateDn(&x, N, 1, N, dX, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));
  CHECK_CUDSS(cudssMatrixCreateDn(&b, N, 1, N, dB, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));

  // One-time analysis + initial factorization (the cost amortized across the
  // Newton loop), then verify the initial solve.
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_ANALYSIS, config, data, A, x, b));
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_FACTORIZATION, config, data, A, x, b));
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b));
  CHECK_CUDA(cudaDeviceSynchronize());
  std::vector<double> hX(N, 0.0);
  CHECK_CUDA(cudaMemcpy(hX.data(), dX, N * sizeof(double), cudaMemcpyDeviceToHost));
  const double RES_FACTOR = residualInf(rowPtr, colIdx, values, rhs, hX);

  std::mt19937 perturbRng(0xC0FFEEu);
  std::uniform_real_distribution<double> perturb(-0.01, 0.01);
  std::vector<double> valuesPerturbed = values;
  auto setPerturbed = [&] {
    for (int i = 0; i < NNZ; ++i)
      valuesPerturbed[i] = values[i] * (1.0 + perturb(perturbRng));
    cudaMemcpy(dVal, valuesPerturbed.data(), NNZ * sizeof(double), cudaMemcpyHostToDevice);
    cudssMatrixSetValues(A, dVal);
  };

  // Verify one perturbed refactor solve (the operation actually measured).
  setPerturbed();
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_REFACTORIZATION, config, data, A, x, b));
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b));
  CHECK_CUDA(cudaDeviceSynchronize());
  CHECK_CUDA(cudaMemcpy(hX.data(), dX, N * sizeof(double), cudaMemcpyDeviceToHost));
  const double RES_REFACTOR = residualInf(rowPtr, colIdx, valuesPerturbed, rhs, hX);

  // Per-iter: perturb values (the Newton pattern), refactor, solve.
  auto runFn = [&] {
    setPerturbed();
    cudssExecute(handle, CUDSS_PHASE_REFACTORIZATION, config, data, A, x, b);
    cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b);
    cudaDeviceSynchronize();
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "cudss_refactor_iter");

  // perf.measured() divides each call's wall time by cfg.cycles; this probe
  // does one refactor+solve per call, so multiply back for per-iter us.
  const double PER_ITER_US = result.stats.median * perf.cycles();
  std::printf("  residual (initial factor)  = %.3e\n", RES_FACTOR);
  std::printf("  residual (perturbed refac) = %.3e\n", RES_REFACTOR);
  std::printf("  per iter (SetValues + Refactor + Solve + sync): %.1f us\n", PER_ITER_US);
  std::printf("  baselines: CPU sparse KLU = 543 us, GPU dense cuSOLVER = 9600 us\n");
  std::printf("  vs CPU sparse: %.2fx %s\n", 543.0 / PER_ITER_US,
              PER_ITER_US < 543.0 ? "(GPU sparse beats CPU)" : "(CPU sparse still wins)");

  EXPECT_LT(RES_FACTOR, 1e-6) << "initial factor solve residual too large";
  EXPECT_LT(RES_REFACTOR, 1e-6) << "refactor solve residual too large";

  CHECK_CUDSS(cudssMatrixDestroy(A));
  CHECK_CUDSS(cudssMatrixDestroy(x));
  CHECK_CUDSS(cudssMatrixDestroy(b));
  CHECK_CUDSS(cudssDataDestroy(handle, data));
  CHECK_CUDSS(cudssConfigDestroy(config));
  CHECK_CUDSS(cudssDestroy(handle));
  CHECK_CUDA(cudaFree(dRowPtr));
  CHECK_CUDA(cudaFree(dColIdx));
  CHECK_CUDA(cudaFree(dVal));
  CHECK_CUDA(cudaFree(dX));
  CHECK_CUDA(cudaFree(dB));
}

PERF_MAIN()
