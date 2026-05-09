/**
 * @file CuSolverRfBatchProbe_pTest.cu
 * @brief cusolverRfBatch probe at Dim1121 for K parallel 4004 circuits.
 *
 * Pass 13 measured single-circuit cusolverRf at 750 us/iter -- 0.72x
 * vs CPU KLU. Single-circuit L1 sim is genuinely won by CPU KLU at
 * this scale.
 *
 * The GPU's L1 sim pivot is **batched** workloads (Monte Carlo,
 * parameter sweep, multi-4004) where the GPU saturates while CPU
 * serializes. The MOSFET stamp side already shows 13.6x effective
 * speedup at K=16 (pass 4). The remaining bottleneck is a batched
 * 1121-dim sparse solver. This probe answers: at K parallel circuits
 * sharing the same sparsity pattern but different values, what is
 * the per-circuit cost of `cusolverRfBatchRefactor + Solve`?
 *
 * Compares to:
 *   - 543 us  -- CPU KLU per circuit (serial -> 543 * K us total).
 *   - 750 us  -- cusolverRf single-circuit (pass 13).
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
    cusolverStatus_t s = (call);                                                                   \
    if (s != CUSOLVER_STATUS_SUCCESS) {                                                            \
      std::fprintf(stderr, "[cusolver] %s failed: status=%d\n", #call, (int)s);                    \
      GTEST_FAIL() << #call << " status=" << (int)s;                                               \
    }                                                                                              \
  } while (0)

#define CHECK_CUDA(call)                                                                           \
  do {                                                                                             \
    cudaError_t s = (call);                                                                        \
    if (s != cudaSuccess) {                                                                        \
      std::fprintf(stderr, "[cuda] %s failed: %s\n", #call, cudaGetErrorString(s));                \
      GTEST_FAIL() << #call << " error=" << cudaGetErrorString(s);                                 \
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
    if (r <= 0 || c <= 0 || r >= N || c >= N) return;
    dense[r * N + c] += v;
  };
  for (int i = 0; i < N_TRANSISTORS; ++i) {
    const int d = netDist(rng);
    const int s = netDist(rng);
    if (d == s) continue;
    const double g = gDist(rng);
    add(d, d, g); add(s, s, g); add(d, s, -g); add(s, d, -g);
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
      const double v = dense[r * N + c];
      if (v != 0.0) { colIdx.push_back(c); values.push_back(v); }
    }
  }
  rowPtr[N] = static_cast<int>(colIdx.size());
}


namespace ub = vernier::bench;

template <typename PerfHarness>
static void runBatchCase(PerfHarness& perf, int batchSize, const char* label) {
  // === 1. Build pattern + initial A values ===
  std::vector<double> dense;
  std::vector<double> rhs;
  buildSparseMna(dense, rhs);
  std::vector<int> hRowPtrA, hColIndA;
  std::vector<double> hValA;
  denseToCsr(dense, hRowPtrA, hColIndA, hValA);
  const int nnzA = static_cast<int>(hColIndA.size());

  std::printf("\n=== cusolverRfBatch %s (batchSize=%d, Dim%d) ===\n", label, batchSize, N);
  std::printf("  NNZ_A = %d  (%.2f%% density)\n", nnzA,
              100.0 * nnzA / static_cast<double>(N * N));

  // === 2. AMD reorder, factor permuted A, extract L/U ===
  cusolverSpHandle_t spHandle = nullptr;
  CHECK_CUSOLVER(cusolverSpCreate(&spHandle));
  cusparseMatDescr_t descrA = nullptr;
  cusparseCreateMatDescr(&descrA);
  cusparseSetMatType(descrA, CUSPARSE_MATRIX_TYPE_GENERAL);
  cusparseSetMatIndexBase(descrA, CUSPARSE_INDEX_BASE_ZERO);

  std::vector<int> hPerm(N);
  CHECK_CUSOLVER(cusolverSpXcsrsymamdHost(spHandle, N, nnzA, descrA, hRowPtrA.data(),
                                          hColIndA.data(), hPerm.data()));

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
  std::vector<double> hValAP(nnzA);
  for (int i = 0; i < nnzA; ++i) hValAP[i] = hValA[permMap[i]];

  csrluInfoHost_t luInfo = nullptr;
  CHECK_CUSOLVER(cusolverSpCreateCsrluInfoHost(&luInfo));
  CHECK_CUSOLVER(cusolverSpXcsrluAnalysisHost(spHandle, N, nnzA, descrA, hRowPtrAP.data(),
                                              hColIndAP.data(), luInfo));
  size_t internalBytes = 0, workBytes = 0;
  CHECK_CUSOLVER(cusolverSpDcsrluBufferInfoHost(spHandle, N, nnzA, descrA, hValAP.data(),
                                                hRowPtrAP.data(), hColIndAP.data(), luInfo,
                                                &internalBytes, &workBytes));
  std::vector<char> workBuf(workBytes);
  CHECK_CUSOLVER(cusolverSpDcsrluFactorHost(spHandle, N, nnzA, descrA, hValAP.data(),
                                            hRowPtrAP.data(), hColIndAP.data(), luInfo, 0.1,
                                            workBuf.data()));

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

  // Work in the AMD-permuted frame: cusolverRf sees the permuted A
  // as its "input A", and L * U = P_inner * A_perm * Q_inner^T is
  // self-consistent without composing AMD with the LU pivots.
  // Each per-circuit value array also gets permuted via permMap.
  // (No need to compose into hPFull/hQFull -- pass hP/hQ directly.)

  // === 3. cusolverRfBatch setup ===
  // Per-circuit value arrays in the AMD-permuted ordering (matches
  // the L/U we extracted). Each circuit gets slightly perturbed
  // values -- simulating MC parameter variation.
  std::vector<std::vector<double>> hValAPerCircuit(batchSize, hValAP);
  std::mt19937 rng(0xC0FFEEu);
  std::uniform_real_distribution<double> jitter(-0.02, 0.02);
  for (int b = 1; b < batchSize; ++b) {
    for (int i = 0; i < nnzA; ++i) {
      hValAPerCircuit[b][i] = hValAP[i] * (1.0 + jitter(rng));
    }
  }
  std::vector<double*> hValAPtrs(batchSize);
  for (int b = 0; b < batchSize; ++b) hValAPtrs[b] = hValAPerCircuit[b].data();

  // Stream-parallel approach: K independent cusolverRf handles, each
  // its own implicit CUDA stream. Run K independent refactor+solve
  // calls per iter and let the runtime overlap them at the SM level.
  // The cusolverRfBatch API turned out to be unviable at Dim1121
  // (BatchAnalyze hangs); this is the alternative path to batched
  // throughput.
  std::vector<cusolverRfHandle_t> rfHandles(batchSize, nullptr);
  for (int b = 0; b < batchSize; ++b) {
    CHECK_CUSOLVER(cusolverRfCreate(&rfHandles[b]));
    CHECK_CUSOLVER(cusolverRfSetResetValuesFastMode(rfHandles[b],
                                                    CUSOLVERRF_RESET_VALUES_FAST_MODE_ON));
    CHECK_CUSOLVER(cusolverRfSetupHost(N, nnzA, hRowPtrAP.data(), hColIndAP.data(),
                                       hValAPerCircuit[b].data(), nnzL, hRowPtrL.data(),
                                       hColIndL.data(), hValL.data(), nnzU, hRowPtrU.data(),
                                       hColIndU.data(), hValU.data(), hP.data(), hQ.data(),
                                       rfHandles[b]));
    CHECK_CUSOLVER(cusolverRfAnalyze(rfHandles[b]));
  }

  // === 4. Move per-circuit A values + RHS to device ===
  // Layout: dValA holds [batchSize x nnzA] doubles; dValAPtrs[b] = dValA + b*nnzA.
  // Layout: dB holds [batchSize x N] doubles; dBPtrs[b] = dB + b*N.
  int *dRowPtrA = nullptr, *dColIndA = nullptr, *dP = nullptr, *dQ = nullptr;
  double *dValA = nullptr, *dB = nullptr, *dT = nullptr;
  CHECK_CUDA(cudaMalloc(&dRowPtrA, (N + 1) * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dColIndA, nnzA * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dValA, batchSize * nnzA * sizeof(double)));
  CHECK_CUDA(cudaMalloc(&dP, N * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dQ, N * sizeof(int)));
  CHECK_CUDA(cudaMalloc(&dB, batchSize * N * sizeof(double)));
  // cusolverRfBatchSolve requires Temp of size 2 * batchSize * (n * nrhs).
  CHECK_CUDA(cudaMalloc(&dT, 2 * batchSize * N * sizeof(double)));

  CHECK_CUDA(cudaMemcpy(dRowPtrA, hRowPtrAP.data(), (N + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dColIndA, hColIndAP.data(), nnzA * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dP, hP.data(), N * sizeof(int), cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dQ, hQ.data(), N * sizeof(int), cudaMemcpyHostToDevice));

  // Device pointer arrays for the per-circuit value / RHS slices.
  std::vector<double*> hDValAPtrs(batchSize);
  std::vector<double*> hDBPtrs(batchSize);
  for (int b = 0; b < batchSize; ++b) {
    hDValAPtrs[b] = dValA + b * nnzA;
    hDBPtrs[b] = dB + b * N;
  }
  double **dValAPtrs = nullptr, **dBPtrs = nullptr;
  CHECK_CUDA(cudaMalloc(&dValAPtrs, batchSize * sizeof(double*)));
  CHECK_CUDA(cudaMalloc(&dBPtrs, batchSize * sizeof(double*)));
  CHECK_CUDA(cudaMemcpy(dValAPtrs, hDValAPtrs.data(), batchSize * sizeof(double*),
                        cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(dBPtrs, hDBPtrs.data(), batchSize * sizeof(double*),
                        cudaMemcpyHostToDevice));

  // === 5. Per-iter loop: ResetValues + Refactor + Solve for the whole batch ===
  std::mt19937 perturbRng(0xDADA00u);
  std::uniform_real_distribution<double> perturb(-0.01, 0.01);

  auto runFn = [&] {
    // Perturb each circuit's permuted values modestly per iter (NR pattern).
    for (int b = 0; b < batchSize; ++b) {
      for (int i = 0; i < nnzA; ++i) {
        hValAPerCircuit[b][i] = hValAP[i] * (1.0 + perturb(perturbRng));
      }
    }
    // Single H2D for the whole batch (contiguous).
    std::vector<double> flatVals(batchSize * nnzA);
    for (int b = 0; b < batchSize; ++b) {
      std::copy(hValAPerCircuit[b].begin(), hValAPerCircuit[b].end(),
                flatVals.begin() + b * nnzA);
    }
    cudaMemcpy(dValA, flatVals.data(), batchSize * nnzA * sizeof(double), cudaMemcpyHostToDevice);

    // RHS: replicate the same RHS across all circuits for simplicity.
    // Permute the RHS to match the AMD-permuted A frame: b_perm[hPerm[i]] = b[i].
    std::vector<double> rhsPerm(N, 0.0);
    for (int i = 0; i < N; ++i) rhsPerm[hPerm[i]] = rhs[i];
    std::vector<double> flatB(batchSize * N);
    for (int b = 0; b < batchSize; ++b) {
      std::copy(rhsPerm.begin(), rhsPerm.end(), flatB.begin() + b * N);
    }
    cudaMemcpy(dB, flatB.data(), batchSize * N * sizeof(double), cudaMemcpyHostToDevice);

    // K independent refactor+solve calls; runtime overlaps them on streams.
    for (int b = 0; b < batchSize; ++b) {
      cusolverRfResetValues(N, nnzA, dRowPtrA, dColIndA, dValA + b * nnzA, dP, dQ, rfHandles[b]);
      cusolverRfRefactor(rfHandles[b]);
      cusolverRfSolve(rfHandles[b], dP, dQ, /*nrhs=*/1, dT + b * N, /*ldt=*/N, dB + b * N,
                      /*ldxf=*/N);
    }
    cudaDeviceSynchronize();
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "cusolverRfBatch_iter");

  const double PER_ITER_US = result.stats.median;
  const double PER_CIRCUIT_US = PER_ITER_US / batchSize;
  const double CPU_SERIAL_US = 543.0 * batchSize;
  std::printf("  per iter (whole batch): %.2f us\n", PER_ITER_US);
  std::printf("  per circuit:            %.2f us  (CPU KLU baseline: 543 us)\n", PER_CIRCUIT_US);
  std::printf("  vs CPU serial K * 543us: %.2fx %s\n", CPU_SERIAL_US / PER_ITER_US,
              PER_ITER_US < CPU_SERIAL_US ? "(GPU batch wins)" : "(CPU still wins)");

  // Cleanup
  cudaFree(dRowPtrA); cudaFree(dColIndA); cudaFree(dValA); cudaFree(dP); cudaFree(dQ);
  cudaFree(dB); cudaFree(dT); cudaFree(dValAPtrs); cudaFree(dBPtrs);
  for (auto h : rfHandles) cusolverRfDestroy(h);
  cusparseDestroyMatDescr(descrL);
  cusparseDestroyMatDescr(descrU);
  cusparseDestroyMatDescr(descrA);
  cusolverSpDestroyCsrluInfoHost(luInfo);
  cusolverSpDestroy(spHandle);
}

PERF_TEST(CuSolverRfBatch, K2)   { UB_PERF_GUARD(perf); runBatchCase(perf, 2,   "K2");   }
PERF_TEST(CuSolverRfBatch, K4)   { UB_PERF_GUARD(perf); runBatchCase(perf, 4,   "K4");   }
PERF_TEST(CuSolverRfBatch, K16)  { UB_PERF_GUARD(perf); runBatchCase(perf, 16,  "K16");  }

PERF_MAIN()
