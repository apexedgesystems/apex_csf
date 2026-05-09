/**
 * @file CuSolverRfProbe_pTest.cu
 * @brief cusolverRf refactorisation probe at Dim1121 (Intel 4004 MNA).
 *
 * The Phase 4 dense-cuSOLVER per-iter wall is 9.6 ms at Dim1121 -- 18x
 * slower than CPU sparse KLU (~543 us per stamp+factor+solve at the
 * same scale). The cusolverRf API separates analyse-once from
 * refactor-per-iter, which is the design pattern Phase 4's NR loop
 * needs. This probe answers: at Dim1121 with the Intel 4004 sparsity
 * pattern, what is the per-iter cost of
 * `cusolverRfResetValues + cusolverRfRefactor + cusolverRfSolve`?
 *
 * Pipeline:
 *   - Build a sparse 1121-net MNA-like matrix on host (CSR).
 *   - Initial LU via `cusolverSp_LOWLEVEL_PREVIEW.h`
 *     (`cusolverSpDcsrluFactorHost` -> extract L, U, P, Q).
 *   - Setup `cusolverRf` with the host CSR L/U/P/Q +
 *     `cusolverRfAnalyze` (one-time).
 *   - Time `cusolverRfResetValues + cusolverRfRefactor +
 *     cusolverRfSolve` in a loop with perturbed values.
 *
 * Compares against:
 *   - 543 us  -- CPU CPU sparse KLU at full 4004 (`GridStampAll` ptest).
 *   - 9.6 ms  -- GPU dense LU at Dim1121 (Phase 4 wall measurement).
 */

#include "src/bench/inc/Perf.hpp"

#include <cuda_runtime_api.h>
#include <cusolverRf.h>
#include <cusolverSp.h>
#include <cusolverSp_LOWLEVEL_PREVIEW.h>
#include <cusparse.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#define CHECK_CUSOLVER(call)                                                                       \
  do {                                                                                             \
    cusolverStatus_t status = (call);                                                              \
    if (status != CUSOLVER_STATUS_SUCCESS) {                                                       \
      std::fprintf(stderr, "[cusolver] %s failed: status=%d\n", #call, (int)status);               \
      GTEST_FAIL() << #call << " status=" << (int)status;                                          \
    }                                                                                              \
  } while (0)

#define CHECK_CUDA(call)                                                                           \
  do {                                                                                             \
    cudaError_t status = (call);                                                                   \
    if (status != cudaSuccess) {                                                                   \
      std::fprintf(stderr, "[cuda] %s failed: %s\n", #call, cudaGetErrorString(status));           \
      GTEST_FAIL() << #call << " error=" << cudaGetErrorString(status);                            \
    }                                                                                              \
  } while (0)


constexpr int N = 1121;
constexpr int N_TRANSISTORS = 2242;
constexpr double GMIN = 1e-3;

// Build dense Intel-4004-like MNA matrix in row-major: 4 nz per
// synthetic transistor (gds and -gds at d/s slots) plus GMIN-to-ground
// diagonal and the ground-row anchor.
void buildSparseMna(std::vector<double>& dense, std::vector<double>& rhs) {
  dense.assign(N * N, 0.0);
  rhs.assign(N, 0.0);
  std::mt19937 rng(0xBEEF1121u);
  std::uniform_int_distribution<int> netDist(1, N - 1);
  std::uniform_real_distribution<double> gDist(1e-4, 5e-3);
  auto add = [&](int r, int c, double v) {
    if (r <= 0 || c <= 0 || r >= N || c >= N) return;
    dense[r * N + c] += v;
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
      const double v = dense[r * N + c];
      if (v != 0.0) {
        colIdx.push_back(c);
        values.push_back(v);
      }
    }
  }
  rowPtr[N] = static_cast<int>(colIdx.size());
}


namespace ub = vernier::bench;

PERF_TEST(CuSolverRf, RefactorAndSolve_Dim1121) {
  UB_PERF_GUARD(perf);

  // === 1. Build matrix ===
  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseMna(dense, rhs);

  std::vector<int> hRowPtrA;
  std::vector<int> hColIndA;
  std::vector<double> hValA;
  denseToCsr(dense, hRowPtrA, hColIndA, hValA);
  const int nnzA = static_cast<int>(hColIndA.size());

  std::printf("\n=== cusolverRf Probe at Dim%d ===\n", N);
  std::printf("  NNZ_A = %d  (%.2f%% density)\n", nnzA,
              100.0 * nnzA / static_cast<double>(N * N));

  // === 2. Compute AMD reordering (critical for MNA-style matrices) ===
  cusolverSpHandle_t spHandle = nullptr;
  CHECK_CUSOLVER(cusolverSpCreate(&spHandle));

  cusparseMatDescr_t descrA = nullptr;
  cusparseCreateMatDescr(&descrA);
  cusparseSetMatType(descrA, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descrA, CUSPARSE_INDEX_BASE_ZERO);

  // AMD permutation -- reduces fill-in massively for MNA matrices.
  std::vector<int> hPerm(N);
  CHECK_CUSOLVER(cusolverSpXcsrsymamdHost(spHandle, N, nnzA, descrA, hRowPtrA.data(),
                                          hColIndA.data(), hPerm.data()));

  // Apply the permutation to A: A' = P A P^T (used as both P and Q).
  // We mutate the CSR pattern in place per cusolverSpXcsrpermHost.
  std::vector<int> hRowPtrAP = hRowPtrA;
  std::vector<int> hColIndAP = hColIndA;
  std::vector<int> permMap(nnzA);
  size_t permBufBytes = 0;
  CHECK_CUSOLVER(cusolverSpXcsrperm_bufferSizeHost(spHandle, N, N, nnzA, descrA,
                                                   hRowPtrAP.data(), hColIndAP.data(),
                                                   hPerm.data(), hPerm.data(), &permBufBytes));
  std::vector<char> permBuf(permBufBytes);
  for (int i = 0; i < nnzA; ++i) permMap[i] = i;
  CHECK_CUSOLVER(cusolverSpXcsrpermHost(spHandle, N, N, nnzA, descrA, hRowPtrAP.data(),
                                        hColIndAP.data(), hPerm.data(), hPerm.data(),
                                        permMap.data(), permBuf.data()));
  // Reorder values: hValAP[i] = hValA[permMap[i]].
  std::vector<double> hValAP(nnzA);
  for (int i = 0; i < nnzA; ++i) hValAP[i] = hValA[permMap[i]];

  csrluInfoHost_t luInfo = nullptr;
  CHECK_CUSOLVER(cusolverSpCreateCsrluInfoHost(&luInfo));

  // Symbolic analysis on the PERMUTED matrix.
  CHECK_CUSOLVER(cusolverSpXcsrluAnalysisHost(spHandle, N, nnzA, descrA, hRowPtrAP.data(),
                                              hColIndAP.data(), luInfo));

  size_t internalBytes = 0, workBytes = 0;
  CHECK_CUSOLVER(cusolverSpDcsrluBufferInfoHost(spHandle, N, nnzA, descrA, hValAP.data(),
                                                hRowPtrAP.data(), hColIndAP.data(), luInfo,
                                                &internalBytes, &workBytes));
  std::vector<char> workBuf(workBytes);

  // Numerical factor on the permuted matrix.
  CHECK_CUSOLVER(cusolverSpDcsrluFactorHost(spHandle, N, nnzA, descrA, hValAP.data(),
                                            hRowPtrAP.data(), hColIndAP.data(), luInfo,
                                            /*pivot_threshold=*/0.1, workBuf.data()));

  // Extract L and U.
  int nnzL = 0, nnzU = 0;
  CHECK_CUSOLVER(cusolverSpXcsrluNnzHost(spHandle, &nnzL, &nnzU, luInfo));
  std::printf("  NNZ_L = %d, NNZ_U = %d  (fill-in %.2fx vs A)\n", nnzL, nnzU,
              static_cast<double>(nnzL + nnzU) / nnzA);

  std::vector<int> hRowPtrL(N + 1), hColIndL(nnzL);
  std::vector<double> hValL(nnzL);
  std::vector<int> hRowPtrU(N + 1), hColIndU(nnzU);
  std::vector<double> hValU(nnzU);
  std::vector<int> hP(N), hQ(N);

  cusparseMatDescr_t descrL = nullptr;
  cusparseCreateMatDescr(&descrL);
  cusparseSetMatType(descrL, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descrL, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatFillMode(descrL, CUSPARSE_FILL_MODE_LOWER);
  cusparseSetMatDiagType(descrL, CUSPARSE_DIAG_TYPE_UNIT);

  cusparseMatDescr_t descrU = nullptr;
  cusparseCreateMatDescr(&descrU);
  cusparseSetMatType(descrU, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descrU, CUSPARSE_INDEX_BASE_ZERO);
  cusparseSetMatFillMode(descrU, CUSPARSE_FILL_MODE_UPPER);
  cusparseSetMatDiagType(descrU, CUSPARSE_DIAG_TYPE_NON_UNIT);

  CHECK_CUSOLVER(cusolverSpDcsrluExtractHost(spHandle, hP.data(), hQ.data(), descrL,
                                             hValL.data(), hRowPtrL.data(), hColIndL.data(),
                                             descrU, hValU.data(), hRowPtrU.data(),
                                             hColIndU.data(), luInfo, workBuf.data()));

  // The L/U from cusolverSp factor the *permuted* A (P_amd * A * P_amd^T).
  // Compose the AMD permutation with the inner LU pivot perms so that
  // cusolverRf gets the full row/col permutations from original A to L/U:
  //   P_full[i] = hPerm[ hP[i] ]   (compose row perms)
  //   Q_full[i] = hPerm[ hQ[i] ]   (compose col perms)
  std::vector<int> hPFull(N), hQFull(N);
  for (int i = 0; i < N; ++i) {
    hPFull[i] = hPerm[hP[i]];
    hQFull[i] = hPerm[hQ[i]];
  }

  // === 3. cusolverRf setup ===
  cusolverRfHandle_t rfHandle = nullptr;
  CHECK_CUSOLVER(cusolverRfCreate(&rfHandle));
  CHECK_CUSOLVER(cusolverRfSetResetValuesFastMode(rfHandle, CUSOLVERRF_RESET_VALUES_FAST_MODE_ON));

  CHECK_CUSOLVER(cusolverRfSetupHost(N, nnzA, hRowPtrA.data(), hColIndA.data(), hValA.data(),
                                     nnzL, hRowPtrL.data(), hColIndL.data(), hValL.data(), nnzU,
                                     hRowPtrU.data(), hColIndU.data(), hValU.data(), hPFull.data(),
                                     hQFull.data(), rfHandle));

  CHECK_CUSOLVER(cusolverRfAnalyze(rfHandle));

  // === 4. Move A, P, Q, B to device for the per-iter loop ===
  int *dRowPtrA = nullptr, *dColIndA = nullptr, *dP = nullptr, *dQ = nullptr;
  double *dValA = nullptr, *dB = nullptr, *dT = nullptr;
  CHECK_CUDA(cudaMalloc(&dRowPtrA, (N + 1) * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dColIndA, nnzA * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dValA, nnzA * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dP, N * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dQ, N * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dB, N * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dT, N * sizeof(double)));

  CHECK_CUDA(cudaMemcpy(dRowPtrA, hRowPtrA.data(), (N + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dColIndA, hColIndA.data(), nnzA * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dValA, hValA.data(), nnzA * sizeof(double), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dP, hPFull.data(), N * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dQ, hQFull.data(), N * sizeof(int), cudaMemcpyHostToDevice));

  // === 5. Per-iter loop: ResetValues + Refactor + Solve ===
  std::mt19937 perturbRng(0xC0FFEEu);
  std::uniform_real_distribution<double> perturb(-0.01, 0.01);
  std::vector<double> hValAPerturbed = hValA;

  auto runFn = [&] {
    // Perturb the A values modestly (each iter sees slightly new values,
    // matching the NR pattern). Structure stays identical.
    for (int i = 0; i < nnzA; ++i) {
      hValAPerturbed[i] = hValA[i] * (1.0 + perturb(perturbRng));
    }
    cudaMemcpy(dValA, hValAPerturbed.data(), nnzA * sizeof(double), cudaMemcpyHostToDevice);
    cudaMemcpy(dB, rhs.data(), N * sizeof(double), cudaMemcpyHostToDevice);

    cusolverRfResetValues(N, nnzA, dRowPtrA, dColIndA, dValA, dP, dQ, rfHandle);
    cusolverRfRefactor(rfHandle);
    cusolverRfSolve(rfHandle, dP, dQ, /*nrhs=*/1, dT, /*ldt=*/N, dB, /*ldxf=*/N);
    cudaDeviceSynchronize();
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "cusolverRf_iter");

  const double PER_ITER_US = result.stats.median;
  std::printf("  per iter (ResetValues + Refactor + Solve + sync): %.2f us\n", PER_ITER_US);
  std::printf("  baselines: CPU sparse KLU stamp+factor+solve = 543 us (GridStampAll)\n");
  std::printf("             GPU dense cuSOLVER per iter @ Dim1121 = 9600 us (pass 11)\n");
  std::printf("  vs CPU sparse: %.2fx %s\n", 543.0 / PER_ITER_US,
              PER_ITER_US < 543.0 ? "(GPU sparse beats CPU)" : "(CPU sparse still wins)");

  // === Cleanup ===
  cudaFree(dRowPtrA); cudaFree(dColIndA); cudaFree(dValA); cudaFree(dP); cudaFree(dQ);
  cudaFree(dB); cudaFree(dT);
  cusolverRfDestroy(rfHandle);
  cusparseDestroyMatDescr(descrL);
  cusparseDestroyMatDescr(descrU);
  cusparseDestroyMatDescr(descrA);
  cusolverSpDestroyCsrluInfoHost(luInfo);
  cusolverSpDestroy(spHandle);
}

PERF_MAIN()
