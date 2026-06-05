/**
 * @file CudssBatchProbe_pTest.cu
 * @brief cuDSS uniform-batch refactor-and-solve probe at Dim1121.
 *
 * The GPU's L1-sim pivot is *batched* workloads (Monte Carlo, parameter sweep,
 * multi-4004) where the GPU saturates while the CPU serializes one 1121-dim
 * sparse solve at a time. This probe answers: at K parallel 4004 circuits
 * (identical sparsity, perturbed values), what is the *per-circuit* cost of a
 * cuDSS uniform-batch `REFACTORIZATION + SOLVE`?
 *
 * cuDSS solves a uniform batch (same structure, different values) by setting
 * CUDSS_CONFIG_UBATCH_SIZE and stacking the K value/RHS arrays -- the native
 * batch path the deprecated cusolverRfBatch never made viable at this size.
 * Per-circuit residuals ||b_k - A_k x_k||_inf confirm every system is solved.
 *
 * Baselines: CPU sparse KLU ~543 us/solve, GPU dense cuSOLVER ~9600 us/iter.
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
  dense[0] = 1.0;
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
                   const double* x) {
  double maxR = 0.0;
  for (int r = 0; r < N; ++r) {
    double ax = 0.0;
    for (int k = rowPtr[r]; k < rowPtr[r + 1]; ++k)
      ax += values[k] * x[colIdx[k]];
    maxR = std::max(maxR, std::abs(rhs[r] - ax));
  }
  return maxR;
}

// Scale the stacked values in place on the device so each measured iteration
// presents new values (forcing a real refactorization) without paying host
// RNG + copy cost that would grow with the batch size K.
__global__ void scaleValues(double* v, std::size_t n, double f) {
  const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    v[i] *= f;
}

namespace ub = vernier::bench;

// Solve K identical-structure circuits (perturbed values) as one cuDSS uniform
// batch; time the per-iter REFACTORIZATION + SOLVE and report per-circuit cost.
static void runBatchCase(ub::PerfCase& perf, int K, const char* label) {
  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseMna(dense, rhs);
  std::vector<int> rowPtr;
  std::vector<int> colIdx;
  std::vector<double> base;
  denseToCsr(dense, rowPtr, colIdx, base);
  const int NNZ = static_cast<int>(colIdx.size());

  std::printf("\n=== cuDSS Uniform-Batch Probe %s (K=%d, Dim%d) ===\n", label, K, N);
  std::printf("  NNZ = %d per circuit\n", NNZ);

  // Per-circuit values: each circuit is the base matrix with a small offset.
  std::mt19937 rng(0xC0FFEEu);
  std::uniform_real_distribution<double> spread(-0.01, 0.01);
  std::vector<std::vector<double>> valsPerCircuit(K, base);
  std::vector<double> valsStacked(static_cast<std::size_t>(K) * NNZ);
  auto refreshValues = [&] {
    for (int c = 0; c < K; ++c) {
      for (int i = 0; i < NNZ; ++i) {
        valsPerCircuit[c][i] = base[i] * (1.0 + spread(rng));
        valsStacked[static_cast<std::size_t>(c) * NNZ + i] = valsPerCircuit[c][i];
      }
    }
  };
  refreshValues();

  // Device: shared structure, stacked values / rhs / solution.
  int* dRowPtr = nullptr;
  int* dColIdx = nullptr;
  double* dVal = nullptr;
  double* dX = nullptr;
  double* dB = nullptr;
  CHECK_CUDA(cudaMalloc(&dRowPtr, (N + 1) * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dColIdx, NNZ * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dVal, static_cast<std::size_t>(K) * NNZ * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dX, static_cast<std::size_t>(K) * N * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dB, static_cast<std::size_t>(K) * N * sizeof(double)));
  CHECK_CUDA(cudaMemcpy(dRowPtr, rowPtr.data(), (N + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dColIdx, colIdx.data(), NNZ * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dVal, valsStacked.data(),
                        static_cast<std::size_t>(K) * NNZ * sizeof(double),
                        cudaMemcpyHostToDevice));
  std::vector<double> rhsStacked(static_cast<std::size_t>(K) * N);
  for (int c = 0; c < K; ++c)
    std::copy(rhs.begin(), rhs.end(), rhsStacked.begin() + static_cast<std::size_t>(c) * N);
  CHECK_CUDA(cudaMemcpy(dB, rhsStacked.data(), static_cast<std::size_t>(K) * N * sizeof(double),
                        cudaMemcpyHostToDevice));

  cudssHandle_t handle = nullptr;
  CHECK_CUDSS(cudssCreate(&handle));
  cudssConfig_t config = nullptr;
  CHECK_CUDSS(cudssConfigCreate(&config));
  cudssData_t data = nullptr;
  CHECK_CUDSS(cudssDataCreate(handle, &data));
  CHECK_CUDSS(cudssConfigSet(config, CUDSS_CONFIG_UBATCH_SIZE, &K, sizeof(K)));

  cudssMatrix_t A = nullptr;
  cudssMatrix_t x = nullptr;
  cudssMatrix_t b = nullptr;
  CHECK_CUDSS(cudssMatrixCreateCsr(&A, N, N, NNZ, dRowPtr, nullptr, dColIdx, dVal, CUDSS_R_32I,
                                   CUDSS_R_32I, CUDSS_R_64F, CUDSS_MTYPE_GENERAL, CUDSS_MVIEW_FULL,
                                   CUDSS_BASE_ZERO));
  CHECK_CUDSS(cudssMatrixCreateDn(&x, N, 1, N, dX, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));
  CHECK_CUDSS(cudssMatrixCreateDn(&b, N, 1, N, dB, CUDSS_R_64F, CUDSS_LAYOUT_COL_MAJOR));

  // Analyze + factor + solve the whole batch, then verify every circuit.
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_ANALYSIS, config, data, A, x, b));
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_FACTORIZATION, config, data, A, x, b));
  CHECK_CUDSS(cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b));
  CHECK_CUDA(cudaDeviceSynchronize());

  std::vector<double> hX(static_cast<std::size_t>(K) * N);
  CHECK_CUDA(cudaMemcpy(hX.data(), dX, static_cast<std::size_t>(K) * N * sizeof(double),
                        cudaMemcpyDeviceToHost));
  double worstRes = 0.0;
  for (int c = 0; c < K; ++c)
    worstRes = std::max(worstRes, residualInf(rowPtr, colIdx, valsPerCircuit[c], rhs,
                                              hX.data() + static_cast<std::size_t>(c) * N));

  const std::size_t valCount = static_cast<std::size_t>(K) * NNZ;
  int iter = 0;
  auto runFn = [&] {
    // Oscillate x1.01 / /1.01 so values change (real refactor) but stay bounded.
    const double f = (iter++ & 1) ? (1.0 / 1.01) : 1.01;
    scaleValues<<<static_cast<unsigned>((valCount + 255) / 256), 256>>>(dVal, valCount, f);
    cudssMatrixSetValues(A, dVal);
    cudssExecute(handle, CUDSS_PHASE_REFACTORIZATION, config, data, A, x, b);
    cudssExecute(handle, CUDSS_PHASE_SOLVE, config, data, A, x, b);
    cudaDeviceSynchronize();
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "cudss_ubatch_iter");

  // perf.measured() divides each call's wall time by cfg.cycles; one batch
  // refactor+solve per call, so multiply back, then divide by K for per-circuit.
  const double PER_BATCH_US = result.stats.median * perf.cycles();
  const double PER_CIRCUIT_US = PER_BATCH_US / K;
  std::printf("  worst per-circuit residual = %.3e\n", worstRes);
  std::printf("  per batch (Refactor + Solve, K=%d): %.1f us\n", K, PER_BATCH_US);
  std::printf("  per circuit: %.1f us  (vs CPU sparse 543 us: %.2fx %s)\n", PER_CIRCUIT_US,
              543.0 / PER_CIRCUIT_US, PER_CIRCUIT_US < 543.0 ? "GPU wins" : "CPU wins");

  EXPECT_LT(worstRes, 1e-6) << "uniform-batch residual too large -- a circuit solved wrong";

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

PERF_TEST(CudssBatch, K2) {
  UB_PERF_GUARD(perf);
  runBatchCase(perf, 2, "K2");
}

PERF_TEST(CudssBatch, K4) {
  UB_PERF_GUARD(perf);
  runBatchCase(perf, 4, "K4");
}

PERF_TEST(CudssBatch, K16) {
  UB_PERF_GUARD(perf);
  runBatchCase(perf, 16, "K16");
}

PERF_MAIN()
