/**
 * @file PhaseFourInnerIter_pTest.cu
 * @brief Wall-time measurement for the Phase 4 inner NR iteration at
 *        the real Intel 4004 scale (1121 nets, 2242 MOSFETs).
 *
 * Validates the per-iter cost projection made in the optimization
 * analysis docs: ~944 us per iteration when all four kernels compose
 * on the device with no mid-iter D2H/H2D.
 *
 * This is a synthetic workload (random but reproducible transistor
 * connectivity and bias). It is not a real Intel 4004 simulation --
 * the scatter-table population from the actual `transistors_` list
 * is the next Phase 4 milestone. The purpose here is to measure the
 * inner-iter kernels at the target scale.
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <random>
#include <vector>

namespace ub = vernier::bench;
namespace mna_cuda = sim::electronics::algorithms::mna::cuda;
namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

constexpr int NET_COUNT = 1121;
constexpr int N_TRANSISTORS = 2242;
constexpr double GMIN = 1e-12;
constexpr double DIAG_GMIN = 1e-3;
constexpr double NR_LIMIT = 5.0;

struct Persistent {
  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetNets* dNets = nullptr;
  double* dG = nullptr;
  double* dI = nullptr;
  double* dPrevV = nullptr;
  double* dMaxDelta = nullptr;
  double* dDiagBias = nullptr; // preloaded diagonal bias template
  mna_cuda::MnaCudaWorkspace ws;
};

void buildInputs(Persistent& P, std::vector<nl_cuda::MosfetNets>& hNets,
                 std::vector<MosfetLevel1Params>& hParams,
                 std::vector<nl_cuda::MosfetBias>& hBiases, std::vector<double>& hPrevV,
                 std::vector<double>& hDiagBias) {
  std::mt19937 rng(0xFEED4004u);
  std::uniform_int_distribution<int> netDist(1, NET_COUNT - 1);
  std::uniform_real_distribution<double> voltDist(-1.0, 2.5);

  hNets.resize(N_TRANSISTORS);
  hParams.resize(N_TRANSISTORS);
  hBiases.resize(N_TRANSISTORS);
  hPrevV.assign(NET_COUNT, 0.0);
  hDiagBias.assign(NET_COUNT * NET_COUNT, 0.0);

  for (int i = 1; i < NET_COUNT; ++i) {
    hPrevV[i] = voltDist(rng);
    hDiagBias[i * NET_COUNT + i] = DIAG_GMIN;
  }
  // Ground row / col.
  for (int j = 0; j < NET_COUNT; ++j) {
    hDiagBias[0 * NET_COUNT + j] = (j == 0) ? 1.0 : 0.0;
    hDiagBias[j * NET_COUNT + 0] = (j == 0) ? 1.0 : 0.0;
  }

  for (int i = 0; i < N_TRANSISTORS; ++i) {
    hNets[i] = {netDist(rng), netDist(rng), netDist(rng)};
    hParams[i] = {.Kp = 5e-3, .Vth = 0.3, .lambda = 0.03, .Vsmooth = 0.1};
    hBiases[i].vgs = hPrevV[hNets[i].source] - hPrevV[hNets[i].gate];
    hBiases[i].vds = hPrevV[hNets[i].source] - hPrevV[hNets[i].drain];
  }
}

void allocateDevice(Persistent& P) {
  cudaMalloc(&P.dBiases, N_TRANSISTORS * sizeof(nl_cuda::MosfetBias));
  cudaMalloc(&P.dParams, N_TRANSISTORS * sizeof(MosfetLevel1Params));
  cudaMalloc(&P.dNets, N_TRANSISTORS * sizeof(nl_cuda::MosfetNets));
  cudaMalloc(&P.dG, NET_COUNT * NET_COUNT * sizeof(double));
  cudaMalloc(&P.dI, NET_COUNT * sizeof(double));
  cudaMalloc(&P.dPrevV, NET_COUNT * sizeof(double));
  cudaMalloc(&P.dMaxDelta, sizeof(double));
  cudaMalloc(&P.dDiagBias, NET_COUNT * NET_COUNT * sizeof(double));
  P.ws.prepare(NET_COUNT);
}

void freeDevice(Persistent& P) {
  cudaFree(P.dBiases);
  cudaFree(P.dParams);
  cudaFree(P.dNets);
  cudaFree(P.dG);
  cudaFree(P.dI);
  cudaFree(P.dPrevV);
  cudaFree(P.dMaxDelta);
  cudaFree(P.dDiagBias);
}

/**
 * @brief One NR iter: memset dG / dI, stamp, add diagonal bias +
 *        ground anchor (via a copy of dDiagBias), solve, update.
 *
 * The diagonal bias template `dDiagBias` is populated once at setup
 * (ground anchor + GMIN-to-ground). Each iter replaces `dG` with
 * `dDiagBias` (single memcpy) rather than re-building the template,
 * so the steady-state per-iter cost reflects what the real Phase 4
 * pipeline would pay.
 */
PERF_GPU_TEST(PhaseFourInner, FullNrIter_Dim1121) {
  if (!nl_cuda::available() || !mna_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }
  UB_PERF_GPU_GUARD(perf);

  Persistent P;
  std::vector<nl_cuda::MosfetNets> hNets;
  std::vector<MosfetLevel1Params> hParams;
  std::vector<nl_cuda::MosfetBias> hBiases;
  std::vector<double> hPrevV;
  std::vector<double> hDiagBias;

  buildInputs(P, hNets, hParams, hBiases, hPrevV, hDiagBias);
  allocateDevice(P);
  ASSERT_TRUE(P.ws.initialized);

  cudaMemcpy(P.dBiases, hBiases.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetBias),
             cudaMemcpyHostToDevice);
  cudaMemcpy(P.dParams, hParams.data(), N_TRANSISTORS * sizeof(MosfetLevel1Params),
             cudaMemcpyHostToDevice);
  cudaMemcpy(P.dNets, hNets.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetNets),
             cudaMemcpyHostToDevice);
  cudaMemcpy(P.dPrevV, hPrevV.data(), NET_COUNT * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(P.dDiagBias, hDiagBias.data(), NET_COUNT * NET_COUNT * sizeof(double),
             cudaMemcpyHostToDevice);

  std::printf("\n=== Phase 4 inner NR iter at Dim1121 (2242 MOSFETs) ===\n");

  auto iterFn = [&](cudaStream_t s) {
    // Replace dG with diagonal bias template (ground anchored,
    // GMIN-to-ground on every DOF). This is the "reset matrix"
    // step. Then stamp MOSFET contributions on top.
    cudaMemcpyAsync(P.dG, P.dDiagBias, NET_COUNT * NET_COUNT * sizeof(double),
                    cudaMemcpyDeviceToDevice, s);
    cudaMemsetAsync(P.dI, 0, NET_COUNT * sizeof(double), s);
    nl_cuda::stampMosfetL1Batch(P.dBiases, P.dParams, P.dNets, P.dG, P.dI, N_TRANSISTORS, NET_COUNT,
                                GMIN, s);
    mna_cuda::solveCudaDeviceResident(P.ws, P.dG, P.dI, NET_COUNT);
    nl_cuda::nrUpdateAndLimit(P.dI, P.dPrevV, P.dMaxDelta, NET_COUNT, NR_LIMIT, s);
  };

  // Warm up cuSOLVER workspace + launch caches.
  perf.cudaWarmup(iterFn);

  auto result =
      perf.cudaKernel(iterFn, "phase4_nr_iter").withLaunchConfig(dim3(1), dim3(256)).measure();

  std::printf("  per-iter (Vernier CUDA events): %.2f us  "
              "(projected 150 iters/byte -> %.2f ms/byte)\n",
              result.stats.kernelTimeMedianUs, result.stats.kernelTimeMedianUs * 150.0 / 1000.0);

  // Direct host-wall measurement for cross-check. Run N_CHECK iters
  // back-to-back with one sync at the end and divide. This captures
  // everything cuSOLVER does internally (multiple kernel launches per
  // solve) in a single wall interval.
  constexpr int N_CHECK = 50;
  cudaDeviceSynchronize();
  cudaEvent_t evStart, evStop;
  cudaEventCreate(&evStart);
  cudaEventCreate(&evStop);
  cudaEventRecord(evStart, 0);
  for (int k = 0; k < N_CHECK; ++k) {
    iterFn(0);
  }
  cudaEventRecord(evStop, 0);
  cudaEventSynchronize(evStop);
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, evStart, evStop);
  cudaEventDestroy(evStart);
  cudaEventDestroy(evStop);
  const double US_PER_ITER = (ms * 1000.0) / static_cast<double>(N_CHECK);
  std::printf("  per-iter (host wall / %d iters):  %.2f us  "
              "(projected 150 iters/byte -> %.2f ms/byte)\n",
              N_CHECK, US_PER_ITER, US_PER_ITER * 150.0 / 1000.0);

  freeDevice(P);
}

PERF_GPU_MAIN()
