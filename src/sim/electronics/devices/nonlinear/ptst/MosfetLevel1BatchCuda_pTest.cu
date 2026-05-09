/**
 * @file MosfetLevel1BatchCuda_pTest.cu
 * @brief GPU performance test for the batch MOSFET stamp kernel.
 *
 * Uses Vernier's PERF_GPU_TEST harness so the CSV automatically reports
 * `kernelTimeUs`, `transferTimeUs`, `h2dBytes`, `d2hBytes`,
 * `memBandwidthGBs`, `occupancy`, `smClockMHz`, and `throttling` per
 * `docs/optimization_process.md` (Vernier built-in GPU metrics).
 *
 * Two regimes per workload:
 *   - End-to-end with H2D + kernel + D2H declared via withHostToDevice /
 *     withDeviceToHost (Vernier times each segment separately).
 *   - Device-resident kernel-only call (no transfers per measurement),
 *     for the case where a caller keeps biases/stamps on the GPU
 *     across many NR iterations.
 *
 * Usage:
 *   ./MosfetLevel1BatchCuda_PTEST --cycles 10 --repeats 15 --csv res.csv
 *   ./MosfetLevel1BatchCuda_PTEST --profile nsight --cycles 10
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cuda_runtime_api.h>
#include <random>
#include <vector>

namespace ub = vernier::bench;
namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Builders ----------------------------- */

static std::vector<nl_cuda::MosfetBias> buildBiases(std::size_t count) {
  std::vector<nl_cuda::MosfetBias> biases(count);
  std::mt19937 rng(0xC001D00D);
  std::uniform_real_distribution<double> dist(-1.0, 5.0);
  for (std::size_t i = 0; i < count; ++i) {
    biases[i].vgs = dist(rng);
    biases[i].vds = dist(rng);
  }
  return biases;
}

static std::vector<MosfetLevel1Params> buildParams(std::size_t count) {
  std::vector<MosfetLevel1Params> params(count);
  std::mt19937 rng(0xDEADBEEF);
  std::uniform_real_distribution<double> kpDist(1e-3, 10e-3);
  for (std::size_t i = 0; i < count; ++i) {
    params[i].Kp = kpDist(rng);
    params[i].Vth = (i % 7 == 0) ? -0.17 : 1.17;
    params[i].lambda = 0.03;
    params[i].Vsmooth = 0.1;
  }
  return params;
}

/* ----------------------------- Kernel-Only (device-resident) ----------------------------- */

static void runKernelOnly(std::size_t count, bool perDevice, const char* label) {
  if (!nl_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }

  UB_PERF_GPU_GUARD(perf);

  const auto biases = buildBiases(count);
  const auto paramsArr = buildParams(count);
  const MosfetLevel1Params UNIFORM{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03, .Vsmooth = 0.1};

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetStamp* dStamps = nullptr;
  cudaMalloc(&dBiases, count * sizeof(nl_cuda::MosfetBias));
  cudaMalloc(&dStamps, count * sizeof(nl_cuda::MosfetStamp));
  if (perDevice) cudaMalloc(&dParams, count * sizeof(MosfetLevel1Params));

  // One-time H2D for biases + params (amortises across many iterations in a real NR loop).
  cudaMemcpy(dBiases, biases.data(), count * sizeof(nl_cuda::MosfetBias), cudaMemcpyHostToDevice);
  if (perDevice) {
    cudaMemcpy(dParams, paramsArr.data(), count * sizeof(MosfetLevel1Params),
               cudaMemcpyHostToDevice);
  }

  std::printf("\n=== MosfetBatchCuda KernelOnly %s (N=%zu) ===\n", label, count);

  auto kernelFn = [&](cudaStream_t s) {
    if (perDevice) {
      nl_cuda::evalStampBatch(dBiases, dParams, dStamps, count, s);
    } else {
      nl_cuda::evalStampBatchUniform(dBiases, UNIFORM, dStamps, count, s);
    }
  };

  const auto cfg = nl_cuda::getLaunchConfig(count);
  const dim3 grid(static_cast<unsigned>(cfg.gridSize), 1, 1);
  const dim3 block(static_cast<unsigned>(cfg.blockSize), 1, 1);

  perf.cudaWarmup(kernelFn);
  auto result =
      perf.cudaKernel(kernelFn, "mosfet_kernel").withLaunchConfig(grid, block).measure();

  std::printf("  kernel: %.2f us  (%.2f ns/device)  occupancy=%.0f%%\n",
              result.stats.kernelTimeMedianUs,
              result.stats.kernelTimeMedianUs * 1000.0 / static_cast<double>(count),
              result.stats.occupancy.achievedOccupancy * 100.0);

  cudaFree(dBiases);
  cudaFree(dStamps);
  if (dParams) cudaFree(dParams);
}

PERF_GPU_TEST(MosfetBatchCudaKernel, Uniform_2242) { runKernelOnly(2242, false, "Uniform_2242"); }
PERF_GPU_TEST(MosfetBatchCudaKernel, PerDevice_2242) {
  runKernelOnly(2242, true, "PerDevice_2242");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, Uniform_32768) {
  runKernelOnly(32768, false, "Uniform_32768");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, PerDevice_32768) {
  runKernelOnly(32768, true, "PerDevice_32768");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, PerDevice_262144) {
  runKernelOnly(262144, true, "PerDevice_262144");
}

/* ----------------------------- SoA Kernel Only ----------------------------- */

static void runSoAKernelOnly(std::size_t count, const char* label) {
  if (!nl_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }

  UB_PERF_GPU_GUARD(perf);

  const auto biases = buildBiases(count);
  const auto paramsArr = buildParams(count);

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  double* dId = nullptr;
  double* dGm = nullptr;
  double* dGds = nullptr;
  cudaMalloc(&dBiases, count * sizeof(nl_cuda::MosfetBias));
  cudaMalloc(&dParams, count * sizeof(MosfetLevel1Params));
  cudaMalloc(&dId, count * sizeof(double));
  cudaMalloc(&dGm, count * sizeof(double));
  cudaMalloc(&dGds, count * sizeof(double));

  cudaMemcpy(dBiases, biases.data(), count * sizeof(nl_cuda::MosfetBias), cudaMemcpyHostToDevice);
  cudaMemcpy(dParams, paramsArr.data(), count * sizeof(MosfetLevel1Params),
             cudaMemcpyHostToDevice);

  std::printf("\n=== MosfetBatchCuda SoA KernelOnly %s (N=%zu) ===\n", label, count);

  auto kernelFn = [&](cudaStream_t s) {
    nl_cuda::evalStampBatchSoA(dBiases, dParams, dId, dGm, dGds, count, s);
  };

  const auto cfg = nl_cuda::getLaunchConfig(count);
  const dim3 grid(static_cast<unsigned>(cfg.gridSize), 1, 1);
  const dim3 block(static_cast<unsigned>(cfg.blockSize), 1, 1);

  perf.cudaWarmup(kernelFn);
  auto result =
      perf.cudaKernel(kernelFn, "mosfet_soa").withLaunchConfig(grid, block).measure();

  std::printf("  kernel: %.2f us  (%.2f ns/device)  occupancy=%.0f%%\n",
              result.stats.kernelTimeMedianUs,
              result.stats.kernelTimeMedianUs * 1000.0 / static_cast<double>(count),
              result.stats.occupancy.achievedOccupancy * 100.0);

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dId);
  cudaFree(dGm);
  cudaFree(dGds);
}

PERF_GPU_TEST(MosfetBatchCudaKernel, SoA_2242) { runSoAKernelOnly(2242, "SoA_2242"); }
PERF_GPU_TEST(MosfetBatchCudaKernel, SoA_32768) { runSoAKernelOnly(32768, "SoA_32768"); }
PERF_GPU_TEST(MosfetBatchCudaKernel, SoA_262144) { runSoAKernelOnly(262144, "SoA_262144"); }

/* ----------------------------- Multi-4004 Scaling ----------------------------- */

// Monte Carlo / parameter sweep scaling: K parallel 4004 circuits
// stamping simultaneously. Each circuit has its own 2242 MOSFETs
// with per-device params.
PERF_GPU_TEST(MosfetBatchCudaKernel, Multi4004_K4) {
  runKernelOnly(2242 * 4, true, "Multi4004_K4 (4 x 2242)");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, Multi4004_K16) {
  runKernelOnly(2242 * 16, true, "Multi4004_K16 (16 x 2242)");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, Multi4004_K64) {
  runKernelOnly(2242 * 64, true, "Multi4004_K64 (64 x 2242)");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, Multi4004_K256) {
  runKernelOnly(2242 * 256, true, "Multi4004_K256 (256 x 2242)");
}

/* ----------------------------- Fused Stamp Scatter ----------------------------- */

static void runFusedStamp(std::size_t count, std::size_t netCount, const char* label) {
  if (!nl_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }
  UB_PERF_GPU_GUARD(perf);

  const auto biases = buildBiases(count);
  const auto paramsArr = buildParams(count);

  // Synthetic net assignment: scatter transistors across netCount-1 nets
  // (net 0 reserved for ground). Matches the real-circuit pattern of
  // sharing nets among multiple transistors (atomic contention).
  std::vector<nl_cuda::MosfetNets> nets(count);
  for (std::size_t i = 0; i < count; ++i) {
    nets[i] = {static_cast<int>(1 + (i * 5 + 3) % (netCount - 1)),
               static_cast<int>(1 + (i * 7 + 2) % (netCount - 1)),
               static_cast<int>(1 + (i * 11 + 5) % (netCount - 1))};
  }

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetNets* dNets = nullptr;
  double* dG = nullptr;
  double* dI = nullptr;
  cudaMalloc(&dBiases, count * sizeof(nl_cuda::MosfetBias));
  cudaMalloc(&dParams, count * sizeof(MosfetLevel1Params));
  cudaMalloc(&dNets, count * sizeof(nl_cuda::MosfetNets));
  cudaMalloc(&dG, netCount * netCount * sizeof(double));
  cudaMalloc(&dI, netCount * sizeof(double));
  cudaMemcpy(dBiases, biases.data(), count * sizeof(nl_cuda::MosfetBias), cudaMemcpyHostToDevice);
  cudaMemcpy(dParams, paramsArr.data(), count * sizeof(MosfetLevel1Params),
             cudaMemcpyHostToDevice);
  cudaMemcpy(dNets, nets.data(), count * sizeof(nl_cuda::MosfetNets), cudaMemcpyHostToDevice);

  std::printf("\n=== MosfetBatchCuda FusedStamp %s (N=%zu, nets=%zu) ===\n", label, count,
              netCount);

  auto kernelFn = [&](cudaStream_t s) {
    cudaMemsetAsync(dG, 0, netCount * netCount * sizeof(double), s);
    cudaMemsetAsync(dI, 0, netCount * sizeof(double), s);
    nl_cuda::stampMosfetL1Batch(dBiases, dParams, dNets, dG, dI, count, netCount, 1e-12, s);
  };

  const auto cfg = nl_cuda::getLaunchConfig(count);
  const dim3 grid(static_cast<unsigned>(cfg.gridSize), 1, 1);
  const dim3 block(static_cast<unsigned>(cfg.blockSize), 1, 1);

  perf.cudaWarmup(kernelFn);
  auto result =
      perf.cudaKernel(kernelFn, "mosfet_fused_stamp").withLaunchConfig(grid, block).measure();

  std::printf("  kernel: %.2f us  (%.2f ns/device)  occupancy=%.0f%%\n",
              result.stats.kernelTimeMedianUs,
              result.stats.kernelTimeMedianUs * 1000.0 / static_cast<double>(count),
              result.stats.occupancy.achievedOccupancy * 100.0);

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dNets);
  cudaFree(dG);
  cudaFree(dI);
}

PERF_GPU_TEST(MosfetBatchCudaKernel, FusedStamp_2242) {
  runFusedStamp(2242, 1121, "FusedStamp_2242 (single 4004)");
}
PERF_GPU_TEST(MosfetBatchCudaKernel, FusedStamp_K16) {
  runFusedStamp(2242 * 16, 1121 * 16, "FusedStamp_K16 (16 x 4004)");
}

/* ----------------------------- NR Update + Limiter ----------------------------- */

PERF_GPU_TEST(MosfetBatchCudaKernel, NrUpdate_1121) {
  if (!nl_cuda::available()) GTEST_SKIP() << "CUDA unavailable.";
  UB_PERF_GPU_GUARD(perf);

  constexpr int N = 1121;
  std::vector<double> prev(N, 0.0), next(N);
  for (int i = 0; i < N; ++i) next[i] = 0.05 * std::sin(0.01 * i);

  double *dNewV = nullptr, *dPrevV = nullptr, *dMaxDelta = nullptr;
  cudaMalloc(&dNewV, N * sizeof(double));
  cudaMalloc(&dPrevV, N * sizeof(double));
  cudaMalloc(&dMaxDelta, sizeof(double));
  cudaMemcpy(dNewV, next.data(), N * sizeof(double), cudaMemcpyHostToDevice);
  cudaMemcpy(dPrevV, prev.data(), N * sizeof(double), cudaMemcpyHostToDevice);

  std::printf("\n=== MosfetBatchCuda NrUpdate (N=%d) ===\n", N);

  auto kernelFn = [&](cudaStream_t s) { nl_cuda::nrUpdateAndLimit(dNewV, dPrevV, dMaxDelta, N, 5.0, s); };

  const dim3 grid(((N + 255) / 256), 1, 1);
  const dim3 block(256, 1, 1);

  perf.cudaWarmup(kernelFn);
  auto result = perf.cudaKernel(kernelFn, "mosfet_nr_update").withLaunchConfig(grid, block).measure();

  std::printf("  kernel: %.2f us  occupancy=%.0f%%\n", result.stats.kernelTimeMedianUs,
              result.stats.occupancy.achievedOccupancy * 100.0);

  cudaFree(dNewV);
  cudaFree(dPrevV);
  cudaFree(dMaxDelta);
}

/* ----------------------------- End-to-End (with declared transfers) ----------------------------- */

static void runWithTransfers(std::size_t count, bool perDevice, const char* label) {
  if (!nl_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }

  UB_PERF_GPU_GUARD(perf);

  const auto biases = buildBiases(count);
  const auto paramsArr = buildParams(count);
  const MosfetLevel1Params UNIFORM{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03, .Vsmooth = 0.1};

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetStamp* dStamps = nullptr;
  cudaMalloc(&dBiases, count * sizeof(nl_cuda::MosfetBias));
  cudaMalloc(&dStamps, count * sizeof(nl_cuda::MosfetStamp));
  if (perDevice) {
    cudaMalloc(&dParams, count * sizeof(MosfetLevel1Params));
    cudaMemcpy(dParams, paramsArr.data(), count * sizeof(MosfetLevel1Params),
               cudaMemcpyHostToDevice);
  }

  std::vector<nl_cuda::MosfetStamp> stampsOut(count);

  std::printf("\n=== MosfetBatchCuda EndToEnd %s (N=%zu) ===\n", label, count);

  auto kernelFn = [&](cudaStream_t s) {
    if (perDevice) {
      nl_cuda::evalStampBatch(dBiases, dParams, dStamps, count, s);
    } else {
      nl_cuda::evalStampBatchUniform(dBiases, UNIFORM, dStamps, count, s);
    }
  };

  const auto cfg = nl_cuda::getLaunchConfig(count);
  const dim3 grid(static_cast<unsigned>(cfg.gridSize), 1, 1);
  const dim3 block(static_cast<unsigned>(cfg.blockSize), 1, 1);

  perf.cudaWarmup(kernelFn);
  auto result = perf.cudaKernel(kernelFn, "mosfet_e2e")
                    .withLaunchConfig(grid, block)
                    .withHostToDevice(biases.data(), dBiases,
                                      count * sizeof(nl_cuda::MosfetBias))
                    .withDeviceToHost(dStamps, stampsOut.data(),
                                      count * sizeof(nl_cuda::MosfetStamp))
                    .measure();

  std::printf("  kernel: %.2f us  H2D: %.2f us (%.1f GB/s)  D2H: %.2f us (%.1f GB/s)\n",
              result.stats.kernelTimeMedianUs, result.stats.transfers.h2dTimeUs,
              result.stats.transfers.h2dBandwidthGBs(), result.stats.transfers.d2hTimeUs,
              result.stats.transfers.d2hBandwidthGBs());

  cudaFree(dBiases);
  cudaFree(dStamps);
  if (dParams) cudaFree(dParams);
}

PERF_GPU_TEST(MosfetBatchCudaE2E, Uniform_2242) { runWithTransfers(2242, false, "Uniform_2242"); }
PERF_GPU_TEST(MosfetBatchCudaE2E, PerDevice_2242) {
  runWithTransfers(2242, true, "PerDevice_2242");
}
PERF_GPU_TEST(MosfetBatchCudaE2E, Uniform_32768) {
  runWithTransfers(32768, false, "Uniform_32768");
}
PERF_GPU_TEST(MosfetBatchCudaE2E, PerDevice_32768) {
  runWithTransfers(32768, true, "PerDevice_32768");
}
PERF_GPU_TEST(MosfetBatchCudaE2E, PerDevice_262144) {
  runWithTransfers(262144, true, "PerDevice_262144");
}

/* ----------------------------- 4004-realistic NR loop pattern ----------------------------- */

/**
 * @brief 150 iterations x 2242 devices, mimicking one Intel 4004 L1 byte's
 *        NR loop call pattern. Compares persistent-buffer driver vs CPU loop.
 *
 * Per-iteration the bias array changes (NR moves to a new operating
 * point); params are set up once at the top. The CPU baseline is the
 * same 150 x 2242 calls into MosfetLevel1::stampValues.
 *
 * If the GPU driver beats the CPU here, a hybrid CPU-driver +
 * GPU-stamp Phase 4 is viable. If not, only a fully GPU-resident
 * Phase 4 (state never leaves the GPU) can win.
 */
PERF_TEST(MosfetStampDriver, NrLoopPattern_4004) {
  if (!nl_cuda::available()) {
    GTEST_SKIP() << "CUDA unavailable.";
  }

  UB_PERF_GUARD(perf);

  constexpr std::size_t COUNT = 2242;     // Intel 4004 transistor count
  constexpr int NR_ITERS = 150;           // ~ NR iters per byte (CPU measurement)
  const auto biasesSource = buildBiases(COUNT);
  const auto params = buildParams(COUNT);

  // Pinned host buffers for max PCIe throughput on H2D/D2H. Pageable
  // memory caps at ~5-7 GB/s; pinned hits 25+ GB/s.
  nl_cuda::MosfetBias* biases = nullptr;
  nl_cuda::MosfetStamp* stamps = nullptr;
  cudaMallocHost(&biases, COUNT * sizeof(nl_cuda::MosfetBias));
  cudaMallocHost(&stamps, COUNT * sizeof(nl_cuda::MosfetStamp));
  std::memcpy(biases, biasesSource.data(), COUNT * sizeof(nl_cuda::MosfetBias));

  // Persistent-buffer GPU driver, params uploaded once.
  nl_cuda::MosfetStampDriver driver(COUNT);
  if (!driver.ready() || !driver.setParams(params.data(), COUNT)) {
    cudaFreeHost(biases);
    cudaFreeHost(stamps);
    GTEST_SKIP() << "Driver init failed.";
  }

  std::printf("\n=== MosfetStampDriver NrLoopPattern_4004 (N=%zu, %d iters, pinned) ===\n", COUNT,
              NR_ITERS);

  auto gpuLoop = [&] {
    for (int it = 0; it < NR_ITERS; ++it) {
      driver.evalBatch(biases, stamps, COUNT);
    }
  };

  std::vector<nl_cuda::MosfetStamp> cpuStamps(COUNT);
  auto cpuLoop = [&] {
    for (int it = 0; it < NR_ITERS; ++it) {
      for (std::size_t i = 0; i < COUNT; ++i) {
        const auto sv = MosfetLevel1::stampValues(biases[i].vgs, biases[i].vds, params[i]);
        cpuStamps[i].id = sv.id;
        cpuStamps[i].gm = sv.gm;
        cpuStamps[i].gds = sv.gds;
      }
    }
  };

  perf.warmup(gpuLoop);
  auto gpuResult = perf.measured(gpuLoop, "gpu_nr_loop");

  perf.warmup(cpuLoop);
  auto cpuResult = perf.measured(cpuLoop, "cpu_nr_loop");

  const double GPU_US = gpuResult.stats.median;
  const double CPU_US = cpuResult.stats.median;
  const double SPEEDUP = CPU_US / GPU_US;
  const double GPU_PER_ITER = GPU_US / NR_ITERS;
  const double CPU_PER_ITER = CPU_US / NR_ITERS;
  std::printf("  GPU loop: %8.2f us total  (%.2f us/iter)\n", GPU_US, GPU_PER_ITER);
  std::printf("  CPU loop: %8.2f us total  (%.2f us/iter)\n", CPU_US, CPU_PER_ITER);
  std::printf("  Speedup:  %.2fx %s\n", SPEEDUP, SPEEDUP > 1.0 ? "GPU wins" : "CPU wins");

  cudaFreeHost(biases);
  cudaFreeHost(stamps);
}

PERF_GPU_MAIN()
