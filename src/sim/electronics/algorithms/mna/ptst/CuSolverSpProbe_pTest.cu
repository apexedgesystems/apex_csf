/**
 * @file CuSolverSpProbe_pTest.cu
 * @brief One-shot cuSolverSP probe at Dim1121 (Intel 4004 MNA size).
 *
 * The dense Phase 4 inner iter sits at ~9.6 ms with cuSOLVER dgetrf.
 * Whether sparse LU can beat that is the Phase 4 open question. This
 * probe converts a representative Intel-4004-like MNA matrix to CSR
 * and times a single `cusolverSpDcsrlsvluHost` call (which does
 * reorder + symbolic factor + numerical factor + solve in one shot).
 *
 * Goal: a first ballpark number for sparse LU at Dim1121. If it is
 * 10+ ms (same as dense), the sparse path loses. If it is 1-2 ms,
 * the `cusolverRfHost`-based refactorization workflow (symbolic
 * factor once, refactor-per-iter) is worth pursuing.
 *
 * This is a probe, not a production solver. The Host variant is used
 * because the device variant is harder to wire without a full
 * infrastructure investment and we want a directional number first.
 */

#include "src/bench/inc/Perf.hpp"

#include <cuda_runtime_api.h>
#include <cusolverSp.h>
#include <cusparse.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>


constexpr int NET_COUNT = 1121;
constexpr int N_TRANSISTORS = 2242;
constexpr double GMIN = 1e-3;

// Build a dense MNA-like matrix with ~4-5 non-zeros per row: each
// synthetic "transistor" contributes gds to (drain,drain),(source,source)
// and -gds to (drain,source),(source,drain). Plus GMIN on the diagonal.
// Intentionally matches the sparsity pattern the real stamp produces.
void buildSparseLikeMna(std::vector<double>& dense, std::vector<double>& rhs) {
  dense.assign(NET_COUNT * NET_COUNT, 0.0);
  rhs.assign(NET_COUNT, 0.0);
  std::mt19937 rng(0xBEEF1121u);
  std::uniform_int_distribution<int> netDist(1, NET_COUNT - 1);
  std::uniform_real_distribution<double> gDist(1e-4, 5e-3);

  auto add = [&](int r, int c, double v) {
    if (r <= 0 || c <= 0 || r >= NET_COUNT || c >= NET_COUNT) return;
    dense[r * NET_COUNT + c] += v;
  };

  for (int i = 0; i < N_TRANSISTORS; ++i) {
    const int d = netDist(rng);
    const int s = netDist(rng);
    if (d == s) continue;
    const double g = gDist(rng);
    add(d, d, g);
    add(s, s, g);
    add(d, s, -g);
    add(s, d, -g);
  }

  // GMIN-to-ground diagonal bias + ground anchor.
  for (int i = 1; i < NET_COUNT; ++i) {
    dense[i * NET_COUNT + i] += GMIN;
    rhs[i] = 0.01 * std::sin(0.37 * i);
  }
  dense[0] = 1.0; // G[0,0] = 1 for ground anchor.
}

// Convert a row-major dense matrix to CSR (1-indexed: 0).
void denseToCsr(const std::vector<double>& dense, std::vector<int>& rowPtr,
                std::vector<int>& colIdx, std::vector<double>& values) {
  rowPtr.assign(NET_COUNT + 1, 0);
  colIdx.clear();
  values.clear();
  for (int r = 0; r < NET_COUNT; ++r) {
    rowPtr[r] = static_cast<int>(colIdx.size());
    for (int c = 0; c < NET_COUNT; ++c) {
      const double v = dense[r * NET_COUNT + c];
      if (v != 0.0) {
        colIdx.push_back(c);
        values.push_back(v);
      }
    }
  }
  rowPtr[NET_COUNT] = static_cast<int>(colIdx.size());
}


namespace ub = vernier::bench;

PERF_TEST(CuSolverSp, OneShotLU_Dim1121) {
  UB_PERF_GUARD(perf);

  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseLikeMna(dense, rhs);

  std::vector<int> rowPtr;
  std::vector<int> colIdx;
  std::vector<double> values;
  denseToCsr(dense, rowPtr, colIdx, values);

  const int nnz = static_cast<int>(colIdx.size());
  std::printf("\n=== CuSolverSp Probe at Dim%d ===\n", NET_COUNT);
  std::printf("  NNZ = %d  (%.2f%% density)\n", nnz,
              100.0 * nnz / static_cast<double>(NET_COUNT * NET_COUNT));

  cusolverSpHandle_t handle = nullptr;
  ASSERT_EQ(cusolverSpCreate(&handle), CUSOLVER_STATUS_SUCCESS);
  cusparseMatDescr_t descr = nullptr;
  ASSERT_EQ(cusparseCreateMatDescr(&descr), CUSPARSE_STATUS_SUCCESS);
  cusparseSetMatType(descr, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descr, CUSPARSE_INDEX_BASE_ZERO);

  std::vector<double> solution(NET_COUNT, 0.0);
  int singularity = 0;

  auto runFn = [&] {
    // `cusolverSpDcsrlsvluHost` does reorder (symrcm) + symbolic
    // factor + numerical factor + solve in one host-driven call.
    cusolverSpDcsrlsvluHost(handle, NET_COUNT, nnz, descr, values.data(), rowPtr.data(),
                            colIdx.data(), rhs.data(), 1e-12, 0, solution.data(), &singularity);
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "cusolver_sp_one_shot");

  const double PER_CALL_US = result.stats.median;
  std::printf("  per call: %.2f us  (singularity=%d)\n", PER_CALL_US, singularity);
  std::printf("  projected 150 iters/byte -> %.2f ms/byte\n", PER_CALL_US * 150.0 / 1000.0);
  std::printf("  dense cuSOLVER baseline:  9600 us -> 1440 ms/byte at 150 iters\n");

  cusparseDestroyMatDescr(descr);
  cusolverSpDestroy(handle);
}

PERF_MAIN()
