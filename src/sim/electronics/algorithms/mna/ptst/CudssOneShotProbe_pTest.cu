/**
 * @file CudssOneShotProbe_pTest.cu
 * @brief cuDSS cold sparse-LU solve probe at Dim1121 (Intel 4004 MNA size).
 *
 * The dense Phase 4 inner iter sits at ~9.6 ms with cuSOLVER dgetrf at
 * Dim1121. Whether a GPU sparse *direct* solve can beat that is the Phase 4
 * open question. This probe builds a representative Intel-4004-like MNA matrix,
 * uploads it as CSR, and times one *cold* cuDSS solve -- reorder + symbolic +
 * numeric factorization + solve from scratch (a fresh solver-data handle each
 * iteration). A residual check ||b - A x||_inf confirms the solve is correct,
 * not just fast.
 *
 * cuDSS (NVIDIA Direct Sparse Solver) is the supported successor to the
 * deprecated cuSolverSp / cuSolverRf host APIs; it performs the reordering and
 * symbolic factorization internally in the ANALYSIS phase. The companion
 * CudssRefactorProbe measures the *warm* (analysis-amortized) per-iter cost
 * that the Newton loop actually pays.
 *
 * Goal: a ballpark cold-solve number for sparse LU at Dim1121.
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

constexpr int NET_COUNT = 1121;
constexpr int N_TRANSISTORS = 2242;
constexpr double GMIN = 1e-3;

// Build a dense MNA-like matrix with ~4-5 non-zeros per row: each synthetic
// "transistor" contributes gds to (drain,drain),(source,source) and -gds to
// (drain,source),(source,drain). Plus GMIN on the diagonal. Intentionally
// matches the sparsity pattern the real stamp produces.
void buildSparseLikeMna(std::vector<double>& dense, std::vector<double>& rhs) {
  dense.assign(NET_COUNT * NET_COUNT, 0.0);
  rhs.assign(NET_COUNT, 0.0);
  std::mt19937 rng(0xBEEF1121u);
  std::uniform_int_distribution<int> netDist(1, NET_COUNT - 1);
  std::uniform_real_distribution<double> gDist(1e-4, 5e-3);

  auto add = [&](int r, int c, double v) {
    if (r <= 0 || c <= 0 || r >= NET_COUNT || c >= NET_COUNT)
      return;
    dense[r * NET_COUNT + c] += v;
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

  // GMIN-to-ground diagonal bias + ground anchor.
  for (int i = 1; i < NET_COUNT; ++i) {
    dense[i * NET_COUNT + i] += GMIN;
    rhs[i] = 0.01 * std::sin(0.37 * i);
  }
  dense[0] = 1.0; // G[0,0] = 1 for ground anchor.
}

// Convert a row-major dense matrix to zero-based CSR.
void denseToCsr(const std::vector<double>& dense, std::vector<int>& rowPtr,
                std::vector<int>& colIdx, std::vector<double>& values) {
  rowPtr.assign(NET_COUNT + 1, 0);
  colIdx.clear();
  values.clear();
  for (int r = 0; r < NET_COUNT; ++r) {
    rowPtr[r] = static_cast<int>(colIdx.size());
    for (int c = 0; c < NET_COUNT; ++c) {
      const double V = dense[r * NET_COUNT + c];
      if (V != 0.0) {
        colIdx.push_back(c);
        values.push_back(V);
      }
    }
  }
  rowPtr[NET_COUNT] = static_cast<int>(colIdx.size());
}

// Host residual ||b - A x||_inf using the original CSR.
double residualInf(const std::vector<int>& rowPtr, const std::vector<int>& colIdx,
                   const std::vector<double>& values, const std::vector<double>& rhs,
                   const std::vector<double>& x) {
  double maxR = 0.0;
  for (int r = 0; r < NET_COUNT; ++r) {
    double ax = 0.0;
    for (int k = rowPtr[r]; k < rowPtr[r + 1]; ++k)
      ax += values[k] * x[colIdx[k]];
    maxR = std::max(maxR, std::abs(rhs[r] - ax));
  }
  return maxR;
}

namespace ub = vernier::bench;

PERF_TEST(CudssOneShot, SparseLU_Dim1121) {
  UB_PERF_GUARD(perf);

  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseLikeMna(dense, rhs);

  std::vector<int> rowPtr;
  std::vector<int> colIdx;
  std::vector<double> values;
  denseToCsr(dense, rowPtr, colIdx, values);
  const int NNZ = static_cast<int>(colIdx.size());

  std::printf("\n=== cuDSS One-Shot (cold) Probe at Dim%d ===\n", NET_COUNT);
  std::printf("  NNZ = %d  (%.2f%% density)\n", NNZ,
              100.0 * NNZ / static_cast<double>(NET_COUNT * NET_COUNT));

  // Device CSR + dense solution/rhs.
  int* dRowPtr = nullptr;
  int* dColIdx = nullptr;
  double* dVal = nullptr;
  double* dX = nullptr;
  double* dB = nullptr;
  CHECK_CUDA(cudaMalloc(&dRowPtr, (NET_COUNT + 1) * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dColIdx, NNZ * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dVal, NNZ * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dX, NET_COUNT * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dB, NET_COUNT * sizeof(double)));
  CHECK_CUDA(
      cudaMemcpy(dRowPtr, rowPtr.data(), (NET_COUNT + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dColIdx, colIdx.data(), NNZ * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dVal, values.data(), NNZ * sizeof(double), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dB, rhs.data(), NET_COUNT * sizeof(double), cudaMemcpyHostToDevice));

  cudssHandle_t handle = nullptr;
  CHECK_CUDSS(cudssCreate(&handle));
  cudssConfig_t config = nullptr;
  CHECK_CUDSS(cudssConfigCreate(&config));

  cudssMatrix_t A = nullptr;
  cudssMatrix_t x = nullptr;
  cudssMatrix_t b = nullptr;
  CHECK_CUDSS(cudssMatrixCreateCsr(&A, NET_COUNT, NET_COUNT, NNZ, dRowPtr, nullptr, dColIdx, dVal,
                                   CUDSS_R_32I, CUDSS_R_32I, CUDSS_R_64F, CUDSS_MTYPE_GENERAL,
                                   CUDSS_MVIEW_FULL, CUDSS_BASE_ZERO));
  CHECK_CUDSS(
      cudssMatrixCreateDn(&x, NET_COUNT, 1, NET_COUNT, dX, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));
  CHECK_CUDSS(
      cudssMatrixCreateDn(&b, NET_COUNT, 1, NET_COUNT, dB, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));

  // A full cold solve: fresh solver-data handle, then analysis + factor + solve.
  auto coldSolve = [&] {
    cudssData_t data = nullptr;
    cudssDataCreate(handle, &data);
    cudssExecute(handle, CUDSS_PHASE_ANALYSIS, config, data, A, x, b);
    cudssExecute(handle, CUDSS_PHASE_FACTORIZATION, config, data, A, x, b);
    cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b);
    cudaDeviceSynchronize();
    cudssDataDestroy(handle, data);
  };

  // Correctness: run one cold solve with checks, then verify ||b - A x||_inf.
  {
    cudssData_t data = nullptr;
    CHECK_CUDSS(cudssDataCreate(handle, &data));
    CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_ANALYSIS, config, data, A, x, b));
    CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_FACTORIZATION, config, data, A, x, b));
    CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b));
    CHECK_CUDA(cudaDeviceSynchronize());
    CHECK_CUDSS(cudssDataDestroy(handle, data));
  }
  std::vector<double> hX(NET_COUNT, 0.0);
  CHECK_CUDA(cudaMemcpy(hX.data(), dX, NET_COUNT * sizeof(double), cudaMemcpyDeviceToHost));
  const double RES = residualInf(rowPtr, colIdx, values, rhs, hX);

  perf.warmup(coldSolve);
  auto result = perf.measured(coldSolve, "cudss_cold_solve");

  // perf.measured() divides each call's wall time by cfg.cycles (it assumes the
  // body runs `cycles` units of work). This probe does one solve per call, so
  // multiply back to recover the per-solve microseconds.
  const double PER_SOLVE_US = result.stats.median * perf.cycles();
  std::printf("  residual ||b - A x||_inf = %.3e\n", RES);
  std::printf("  per cold solve (analysis+factor+solve): %.1f us\n", PER_SOLVE_US);
  std::printf("  dense cuSOLVER baseline @ Dim1121: 9600 us per iter\n");

  EXPECT_LT(RES, 1e-6) << "cuDSS solve residual too large -- solution is wrong";

  CHECK_CUDSS(cudssMatrixDestroy(A));
  CHECK_CUDSS(cudssMatrixDestroy(x));
  CHECK_CUDSS(cudssMatrixDestroy(b));
  CHECK_CUDSS(cudssConfigDestroy(config));
  CHECK_CUDSS(cudssDestroy(handle));
  CHECK_CUDA(cudaFree(dRowPtr));
  CHECK_CUDA(cudaFree(dColIdx));
  CHECK_CUDA(cudaFree(dVal));
  CHECK_CUDA(cudaFree(dX));
  CHECK_CUDA(cudaFree(dB));
}

PERF_MAIN()
