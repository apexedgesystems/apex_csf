/**
 * @file GpuCompute_pTest.cu
 * @brief Performance tests for GPU compute kernels.
 *
 * Measures:
 *  - BatchStats: parallel reduction (min/max/mean/var) and histogram throughput
 *  - ConvFilter: 2D convolution with shared-memory tiling (direct and separable)
 *  - FFTAnalyzer: batched cuFFT R2C + magnitude peak detection
 *  - StreamCompact: threshold-based compaction and classification histogram
 *
 * Usage:
 *   ./GpuCompute_PTEST --csv results.csv
 *   ./GpuCompute_PTEST --quick --gtest_filter="*BatchStats*"
 *   ./GpuCompute_PTEST --profile nsight --gtest_filter="*ConvFilter*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/bench/inc/PerfGpu.hpp"
#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsKernel.cuh"
#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterKernel.cuh"
#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerKernel.cuh"
#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <cuda_runtime.h>
#include <cufft.h>

namespace ub = vernier::bench;
using sim::gpu_compute::ChannelPeak;
using sim::gpu_compute::GroupStats;

namespace {

static bool cudaAvailable() { return apex::compat::cuda::runtimeAvailable(); }

} // namespace

/* ----------------------------- BatchStats ----------------------------- */

/**
 * @brief BatchStats reduction: 1M elements, 4096-element groups.
 */
PERF_GPU_TEST(BatchStatsPerf, Reduction1M) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 1u << 20;
  constexpr std::uint32_t GROUP_SIZE = 4096;
  constexpr std::uint32_t GROUP_COUNT = N / GROUP_SIZE;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = sinf(static_cast<float>(i) * 0.001f);
  }

  float* dInput = nullptr;
  GroupStats* dOutput = nullptr;
  cudaMalloc(&dInput, N * sizeof(float));
  cudaMalloc(&dOutput, GROUP_COUNT * sizeof(GroupStats));
  cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::batchStatsCuda(dInput, N, GROUP_SIZE, dOutput, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::batchStatsCuda(dInput, N, GROUP_SIZE, dOutput, s);
                        },
                        "batchStats_1M_g4096")
                    .measure();

  std::printf("\n[BatchStats 1M/4096] Kernel: %.3f ms (%.0f Melements/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(N) / result.kernelTimeUs);

  cudaFree(dInput);
  cudaFree(dOutput);
}

/**
 * @brief BatchStats reduction: 16M elements, 4096-element groups (heavy load).
 */
PERF_GPU_TEST(BatchStatsPerf, Reduction16M) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 16u << 20;
  constexpr std::uint32_t GROUP_SIZE = 4096;
  constexpr std::uint32_t GROUP_COUNT = N / GROUP_SIZE;

  std::vector<float> hInput(N, 1.0f);

  float* dInput = nullptr;
  GroupStats* dOutput = nullptr;
  cudaMalloc(&dInput, N * sizeof(float));
  cudaMalloc(&dOutput, GROUP_COUNT * sizeof(GroupStats));
  cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::batchStatsCuda(dInput, N, GROUP_SIZE, dOutput, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::batchStatsCuda(dInput, N, GROUP_SIZE, dOutput, s);
                        },
                        "batchStats_16M_g4096")
                    .measure();

  std::printf("\n[BatchStats 16M/4096] Kernel: %.3f ms (%.0f Melements/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(N) / result.kernelTimeUs);

  cudaFree(dInput);
  cudaFree(dOutput);
}

/**
 * @brief BatchStats histogram: 4M elements, 64 bins.
 */
PERF_GPU_TEST(BatchStatsPerf, Histogram4M) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 4u << 20;
  constexpr std::uint32_t GROUP_SIZE = 4096;
  constexpr std::uint32_t GROUP_COUNT = N / GROUP_SIZE;
  constexpr std::uint32_t BINS = 64;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = sinf(static_cast<float>(i) * 0.0013f) * 5.0f;
  }

  float* dInput = nullptr;
  std::uint32_t* dHist = nullptr;
  cudaMalloc(&dInput, N * sizeof(float));
  cudaMalloc(&dHist, GROUP_COUNT * BINS * sizeof(std::uint32_t));
  cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::batchHistogramCuda(dInput, N, GROUP_SIZE, BINS, -10.0f, 10.0f, dHist,
                                               s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::batchHistogramCuda(dInput, N, GROUP_SIZE, BINS,
                                                                     -10.0f, 10.0f, dHist, s);
                        },
                        "batchHistogram_4M_64bins")
                    .measure();

  std::printf("\n[BatchStats Histogram 4M/64] Kernel: %.3f ms\n", result.kernelTimeUs / 1000.0);

  cudaFree(dInput);
  cudaFree(dHist);
}

/* ----------------------------- ConvFilter ----------------------------- */

/**
 * @brief ConvFilter: 2048x2048 Gaussian R=3 (7x7 kernel).
 */
PERF_GPU_TEST(ConvFilterPerf, Gaussian2048R3) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 2048;
  constexpr std::uint32_t H = 2048;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel(hKernel.data(), R, 1.5f);
  sim::gpu_compute::cuda::convSetKernel(hKernel.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup(
      [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); },
              "conv2d_2048x2048_R3")
          .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter 2048x2048 R=3] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
}

/**
 * @brief ConvFilter: 4096x4096 Gaussian R=3 (large image).
 */
PERF_GPU_TEST(ConvFilterPerf, Gaussian4096R3) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 4096;
  constexpr std::uint32_t H = 4096;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel(hKernel.data(), R, 1.5f);
  sim::gpu_compute::cuda::convSetKernel(hKernel.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup(
      [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); },
              "conv2d_4096x4096_R3")
          .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter 4096x4096 R=3] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
}

/**
 * @brief ConvFilter: 2048x2048 Gaussian R=7 (15x15 kernel, heavier compute).
 */
PERF_GPU_TEST(ConvFilterPerf, Gaussian2048R7) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 2048;
  constexpr std::uint32_t H = 2048;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 7;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel(DIAM * DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel(hKernel.data(), R, 3.0f);
  sim::gpu_compute::cuda::convSetKernel(hKernel.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup(
      [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t s) { sim::gpu_compute::cuda::conv2dCuda(dIn, W, H, R, dOut, s); },
              "conv2d_2048x2048_R7")
          .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter 2048x2048 R=7] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
}

/* ----------------------------- ConvFilter Separable ----------------------------- */

/**
 * @brief Separable ConvFilter: 2048x2048 Gaussian R=3 (two 1D passes).
 */
PERF_GPU_TEST(ConvFilterSepPerf, SepGaussian2048R3) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 2048;
  constexpr std::uint32_t H = 2048;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel1D(DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel1D(hKernel1D.data(), R, 1.5f);
  sim::gpu_compute::cuda::convSetKernel1D(hKernel1D.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr, *dTemp = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMalloc(&dTemp, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
                        },
                        "conv2d_sep_2048x2048_R3")
                    .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter SEP 2048x2048 R=3] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dTemp);
}

/**
 * @brief Separable ConvFilter: 4096x4096 Gaussian R=3 (large image).
 */
PERF_GPU_TEST(ConvFilterSepPerf, SepGaussian4096R3) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 4096;
  constexpr std::uint32_t H = 4096;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 3;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel1D(DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel1D(hKernel1D.data(), R, 1.5f);
  sim::gpu_compute::cuda::convSetKernel1D(hKernel1D.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr, *dTemp = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMalloc(&dTemp, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
                        },
                        "conv2d_sep_4096x4096_R3")
                    .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter SEP 4096x4096 R=3] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dTemp);
}

/**
 * @brief Separable ConvFilter: 2048x2048 Gaussian R=7.
 */
PERF_GPU_TEST(ConvFilterSepPerf, SepGaussian2048R7) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t W = 2048;
  constexpr std::uint32_t H = 2048;
  constexpr std::uint32_t N = W * H;
  constexpr std::uint32_t R = 7;
  constexpr std::uint32_t DIAM = 2 * R + 1;

  std::vector<float> hKernel1D(DIAM);
  sim::gpu_compute::cuda::generateGaussianKernel1D(hKernel1D.data(), R, 3.0f);
  sim::gpu_compute::cuda::convSetKernel1D(hKernel1D.data(), R);

  std::vector<float> hInput(N, 0.5f);

  float *dIn = nullptr, *dOut = nullptr, *dTemp = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMalloc(&dTemp, N * sizeof(float));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::conv2dSeparableCuda(dIn, W, H, R, dTemp, dOut, s);
                        },
                        "conv2d_sep_2048x2048_R7")
                    .measure();

  double mpixPerSec = static_cast<double>(N) / result.kernelTimeUs;
  std::printf("\n[ConvFilter SEP 2048x2048 R=7] Kernel: %.3f ms (%.0f Mpix/s)\n",
              result.kernelTimeUs / 1000.0, mpixPerSec);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dTemp);
}

/* ----------------------------- FFTAnalyzer ----------------------------- */

/**
 * @brief FFTAnalyzer: 256 channels x 4096 samples (cuFFT + peak detect).
 */
PERF_GPU_TEST(FFTAnalyzerPerf, Batch256x4096) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t CH = 256;
  constexpr std::uint32_t N = 4096;
  constexpr float FS = 10000.0f;
  constexpr std::uint32_t HALF_N = N / 2 + 1;
  constexpr float K_PI = 3.14159265f;

  std::vector<float> hInput(CH * N);
  for (std::uint32_t ch = 0; ch < CH; ++ch) {
    const float FREQ = 500.0f + static_cast<float>(ch) * 10.0f;
    for (std::uint32_t i = 0; i < N; ++i) {
      hInput[ch * N + i] = sinf(2.0f * K_PI * FREQ * static_cast<float>(i) / FS);
    }
  }

  float* dInput = nullptr;
  float* dComplex = nullptr;
  ChannelPeak* dPeaks = nullptr;
  cudaMalloc(&dInput, CH * N * sizeof(float));
  cudaMalloc(&dComplex, CH * HALF_N * 2 * sizeof(float));
  cudaMalloc(&dPeaks, CH * sizeof(ChannelPeak));
  cudaMemcpy(dInput, hInput.data(), CH * N * sizeof(float), cudaMemcpyHostToDevice);

  int n = static_cast<int>(N);
  cufftHandle plan;
  cufftPlanMany(&plan, 1, &n, nullptr, 1, n, nullptr, 1, static_cast<int>(HALF_N), CUFFT_R2C, CH);

  perf.cudaWarmup([&](cudaStream_t) {
    cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex));
    sim::gpu_compute::cuda::fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks);
  });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t) {
                cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex));
                sim::gpu_compute::cuda::fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks);
              },
              "fft_256ch_4096samp")
          .measure();

  std::printf("\n[FFTAnalyzer 256x4096] Total: %.3f ms (%.0f channels/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(CH) * 1e6 / result.kernelTimeUs);

  cufftDestroy(plan);
  cudaFree(dInput);
  cudaFree(dComplex);
  cudaFree(dPeaks);
}

/**
 * @brief FFTAnalyzer: 1024 channels x 8192 samples (heavy load).
 */
PERF_GPU_TEST(FFTAnalyzerPerf, Batch1024x8192) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t CH = 1024;
  constexpr std::uint32_t N = 8192;
  constexpr float FS = 20000.0f;
  constexpr std::uint32_t HALF_N = N / 2 + 1;
  constexpr float K_PI = 3.14159265f;

  std::vector<float> hInput(CH * N);
  for (std::uint32_t ch = 0; ch < CH; ++ch) {
    const float FREQ = 1000.0f;
    for (std::uint32_t i = 0; i < N; ++i) {
      hInput[ch * N + i] = sinf(2.0f * K_PI * FREQ * static_cast<float>(i) / FS);
    }
  }

  float* dInput = nullptr;
  float* dComplex = nullptr;
  ChannelPeak* dPeaks = nullptr;
  cudaMalloc(&dInput, CH * N * sizeof(float));
  cudaMalloc(&dComplex, CH * HALF_N * 2 * sizeof(float));
  cudaMalloc(&dPeaks, CH * sizeof(ChannelPeak));
  cudaMemcpy(dInput, hInput.data(), CH * N * sizeof(float), cudaMemcpyHostToDevice);

  int n = static_cast<int>(N);
  cufftHandle plan;
  cufftPlanMany(&plan, 1, &n, nullptr, 1, n, nullptr, 1, static_cast<int>(HALF_N), CUFFT_R2C, CH);

  perf.cudaWarmup([&](cudaStream_t) {
    cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex));
    sim::gpu_compute::cuda::fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks);
  });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t) {
                cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex));
                sim::gpu_compute::cuda::fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks);
              },
              "fft_1024ch_8192samp")
          .measure();

  std::printf("\n[FFTAnalyzer 1024x8192] Total: %.3f ms (%.0f channels/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(CH) * 1e6 / result.kernelTimeUs);

  cufftDestroy(plan);
  cudaFree(dInput);
  cudaFree(dComplex);
  cudaFree(dPeaks);
}

/* ----------------------------- StreamCompact ----------------------------- */

/**
 * @brief StreamCompact: 4M elements, 50% selectivity.
 */
PERF_GPU_TEST(StreamCompactPerf, Compact4M_50pct) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 4u << 20;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = (i % 2 == 0) ? 0.8f : 0.2f;
  }

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMalloc(&dCount, sizeof(std::uint32_t));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::streamCompactCuda(dIn, N, 0.5f, dOut, dCount, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::streamCompactCuda(dIn, N, 0.5f, dOut, dCount, s);
                        },
                        "compact_4M_50pct")
                    .measure();

  std::printf("\n[StreamCompact 4M/50%%] Kernel: %.3f ms (%.0f Melements/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(N) / result.kernelTimeUs);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
}

/**
 * @brief StreamCompact: 16M elements, 10% selectivity (sparse selection).
 */
PERF_GPU_TEST(StreamCompactPerf, Compact16M_10pct) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 16u << 20;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = (i % 10 == 0) ? 0.8f : 0.2f;
  }

  float* dIn = nullptr;
  float* dOut = nullptr;
  std::uint32_t* dCount = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dOut, N * sizeof(float));
  cudaMalloc(&dCount, sizeof(std::uint32_t));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::streamCompactCuda(dIn, N, 0.5f, dOut, dCount, s);
  });

  auto result = perf.cudaKernel(
                        [&](cudaStream_t s) {
                          sim::gpu_compute::cuda::streamCompactCuda(dIn, N, 0.5f, dOut, dCount, s);
                        },
                        "compact_16M_10pct")
                    .measure();

  std::printf("\n[StreamCompact 16M/10%%] Kernel: %.3f ms (%.0f Melements/s)\n",
              result.kernelTimeUs / 1000.0, static_cast<double>(N) / result.kernelTimeUs);

  cudaFree(dIn);
  cudaFree(dOut);
  cudaFree(dCount);
}

/**
 * @brief StreamCompact classify: 2M elements, 16 bins.
 */
PERF_GPU_TEST(StreamCompactPerf, Classify2M) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA not available";
  }
  UB_PERF_GPU_GUARD(perf);

  constexpr std::uint32_t N = 2u << 20;
  constexpr std::uint32_t BINS = 16;

  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = static_cast<float>(i % 1000) / 1000.0f;
  }

  float* dIn = nullptr;
  std::uint32_t* dHist = nullptr;
  cudaMalloc(&dIn, N * sizeof(float));
  cudaMalloc(&dHist, BINS * sizeof(std::uint32_t));
  cudaMemcpy(dIn, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice);

  perf.cudaWarmup([&](cudaStream_t s) {
    sim::gpu_compute::cuda::classifyHistogramCuda(dIn, N, BINS, 0.0f, 1.0f, dHist, s);
  });

  auto result =
      perf.cudaKernel(
              [&](cudaStream_t s) {
                sim::gpu_compute::cuda::classifyHistogramCuda(dIn, N, BINS, 0.0f, 1.0f, dHist, s);
              },
              "classify_2M_16bins")
          .measure();

  std::printf("\n[StreamCompact Classify 2M/16] Kernel: %.3f ms\n", result.kernelTimeUs / 1000.0);

  cudaFree(dIn);
  cudaFree(dHist);
}

PERF_MAIN()
