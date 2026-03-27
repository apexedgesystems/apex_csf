/**
 * @file FFTAnalyzerKernel_uTest.cu
 * @brief Unit tests for FFTAnalyzerKernel (magnitude + peak detection).
 *
 * Uses cuFFT to generate realistic complex data, then tests the peak
 * detection kernel against known input signals.
 */

#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cmath>

#include <vector>

#include <gtest/gtest.h>

#if defined(__has_include)
#if __has_include(<cuda_runtime_api.h>)
#define LOCAL_HAS_CUDA_RUNTIME 1
#include <cuda_runtime_api.h>
#include <cufft.h>
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif

using sim::gpu_compute::ChannelPeak;
using sim::gpu_compute::cuda::fftMagnitudePeaksCuda;

static constexpr float K_PI = 3.14159265358979f;

/* ----------------------------- Test Fixture ----------------------------- */

class FFTAnalyzerKernelFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Null Parameter Tests ----------------------------- */

/** @test fftMagnitudePeaksCuda returns false with null input. */
TEST_F(FFTAnalyzerKernelFixture, NullInputReturnsFalse) {
  EXPECT_FALSE(
      fftMagnitudePeaksCuda(nullptr, 1, 1024, 10000.0f, reinterpret_cast<ChannelPeak*>(1)));
}

/** @test fftMagnitudePeaksCuda returns false with null output. */
TEST_F(FFTAnalyzerKernelFixture, NullOutputReturnsFalse) {
  EXPECT_FALSE(
      fftMagnitudePeaksCuda(reinterpret_cast<const float*>(1), 1, 1024, 10000.0f, nullptr));
}

/** @test fftMagnitudePeaksCuda returns false with zero channels. */
TEST_F(FFTAnalyzerKernelFixture, ZeroChannelsReturnsFalse) {
  EXPECT_FALSE(fftMagnitudePeaksCuda(reinterpret_cast<const float*>(1), 0, 1024, 10000.0f,
                                     reinterpret_cast<ChannelPeak*>(1)));
}

/* ----------------------------- Single Tone Detection ----------------------------- */

/** @test Detect a single 1000 Hz tone in a 10 kHz sampled signal. */
TEST_F(FFTAnalyzerKernelFixture, SingleToneDetection) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 4096;
  constexpr std::uint32_t CH = 1;
  constexpr float FS = 10000.0f;
  constexpr float TONE_FREQ = 1000.0f;
  constexpr std::uint32_t HALF_N = N / 2 + 1;

  // Generate pure tone
  std::vector<float> hInput(N);
  for (std::uint32_t i = 0; i < N; ++i) {
    hInput[i] = sinf(2.0f * K_PI * TONE_FREQ * static_cast<float>(i) / FS);
  }

  // Allocate device memory
  float* dInput = nullptr;
  float* dComplex = nullptr;
  ChannelPeak* dPeaks = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dComplex, HALF_N * 2 * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPeaks, sizeof(ChannelPeak)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  // Run cuFFT R2C
  cufftHandle plan;
  ASSERT_EQ(cufftPlan1d(&plan, N, CUFFT_R2C, CH), CUFFT_SUCCESS);
  ASSERT_EQ(cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex)), CUFFT_SUCCESS);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Run peak detection
  ASSERT_TRUE(fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Read result
  ChannelPeak result{};
  ASSERT_EQ(cudaMemcpy(&result, dPeaks, sizeof(ChannelPeak), cudaMemcpyDeviceToHost), cudaSuccess);

  // Expected bin: 1000 Hz / (10000 Hz / 4096) = 409.6 -> bin 410
  const float FREQ_RES = FS / static_cast<float>(N);
  EXPECT_NEAR(result.peakFreqHz, TONE_FREQ, FREQ_RES * 1.5f);
  EXPECT_GT(result.peakMagnitudeDb, -10.0f);              // Strong signal
  EXPECT_LT(result.noiseFloorDb, result.peakMagnitudeDb); // Noise below peak

  cufftDestroy(plan);
  cudaFree(dInput);
  cudaFree(dComplex);
  cudaFree(dPeaks);
#endif
}

/* ----------------------------- Multi-Channel Tests ----------------------------- */

/** @test Multi-channel FFT detects different frequencies per channel. */
TEST_F(FFTAnalyzerKernelFixture, MultiChannelDetection) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 4096;
  constexpr std::uint32_t CH = 4;
  constexpr float FS = 10000.0f;
  constexpr std::uint32_t HALF_N = N / 2 + 1;

  // Each channel has a different tone
  const float FREQS[CH] = {500.0f, 1000.0f, 2000.0f, 3000.0f};

  std::vector<float> hInput(CH * N);
  for (std::uint32_t ch = 0; ch < CH; ++ch) {
    for (std::uint32_t i = 0; i < N; ++i) {
      hInput[ch * N + i] = sinf(2.0f * K_PI * FREQS[ch] * static_cast<float>(i) / FS);
    }
  }

  float* dInput = nullptr;
  float* dComplex = nullptr;
  ChannelPeak* dPeaks = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, CH * N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dComplex, CH * HALF_N * 2 * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPeaks, CH * sizeof(ChannelPeak)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), CH * N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  // Batched R2C FFT
  int n = static_cast<int>(N);
  cufftHandle plan;
  ASSERT_EQ(cufftPlanMany(&plan, 1, &n, nullptr, 1, n, nullptr, 1, static_cast<int>(HALF_N),
                          CUFFT_R2C, CH),
            CUFFT_SUCCESS);
  ASSERT_EQ(cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex)), CUFFT_SUCCESS);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Peak detection
  ASSERT_TRUE(fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<ChannelPeak> results(CH);
  ASSERT_EQ(cudaMemcpy(results.data(), dPeaks, CH * sizeof(ChannelPeak), cudaMemcpyDeviceToHost),
            cudaSuccess);

  const float FREQ_RES = FS / static_cast<float>(N);

  for (std::uint32_t ch = 0; ch < CH; ++ch) {
    EXPECT_NEAR(results[ch].peakFreqHz, FREQS[ch], FREQ_RES * 1.5f) << "channel " << ch;
    EXPECT_GT(results[ch].peakMagnitudeDb, -10.0f) << "channel " << ch;
  }

  cufftDestroy(plan);
  cudaFree(dInput);
  cudaFree(dComplex);
  cudaFree(dPeaks);
#endif
}

/* ----------------------------- Large Scale Tests ----------------------------- */

/** @test Large batched FFT (256 channels x 4096 samples) completes without error. */
TEST_F(FFTAnalyzerKernelFixture, LargeBatchCompletes) {
#if !LOCAL_HAS_CUDA_RUNTIME
  GTEST_SKIP() << "CUDA runtime headers not available.";
#else
  constexpr std::uint32_t N = 4096;
  constexpr std::uint32_t CH = 256;
  constexpr float FS = 10000.0f;
  constexpr std::uint32_t HALF_N = N / 2 + 1;

  // All channels: 1 kHz tone
  std::vector<float> hInput(CH * N);
  for (std::uint32_t ch = 0; ch < CH; ++ch) {
    for (std::uint32_t i = 0; i < N; ++i) {
      hInput[ch * N + i] = sinf(2.0f * K_PI * 1000.0f * static_cast<float>(i) / FS);
    }
  }

  float* dInput = nullptr;
  float* dComplex = nullptr;
  ChannelPeak* dPeaks = nullptr;
  ASSERT_EQ(cudaMalloc(&dInput, CH * N * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dComplex, CH * HALF_N * 2 * sizeof(float)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPeaks, CH * sizeof(ChannelPeak)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dInput, hInput.data(), CH * N * sizeof(float), cudaMemcpyHostToDevice),
            cudaSuccess);

  int n = static_cast<int>(N);
  cufftHandle plan;
  ASSERT_EQ(cufftPlanMany(&plan, 1, &n, nullptr, 1, n, nullptr, 1, static_cast<int>(HALF_N),
                          CUFFT_R2C, CH),
            CUFFT_SUCCESS);
  ASSERT_EQ(cufftExecR2C(plan, dInput, reinterpret_cast<cufftComplex*>(dComplex)), CUFFT_SUCCESS);
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  ASSERT_TRUE(fftMagnitudePeaksCuda(dComplex, CH, N, FS, dPeaks));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Spot check first and last channel
  std::vector<ChannelPeak> results(CH);
  ASSERT_EQ(cudaMemcpy(results.data(), dPeaks, CH * sizeof(ChannelPeak), cudaMemcpyDeviceToHost),
            cudaSuccess);

  const float FREQ_RES = FS / static_cast<float>(N);
  EXPECT_NEAR(results[0].peakFreqHz, 1000.0f, FREQ_RES * 1.5f);
  EXPECT_NEAR(results[CH - 1].peakFreqHz, 1000.0f, FREQ_RES * 1.5f);

  cufftDestroy(plan);
  cudaFree(dInput);
  cudaFree(dComplex);
  cudaFree(dPeaks);
#endif
}
