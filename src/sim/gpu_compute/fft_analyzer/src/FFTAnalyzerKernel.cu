/**
 * @file FFTAnalyzerKernel.cu
 * @brief CUDA kernel implementation for magnitude spectrum + peak detection.
 *
 * One thread block per channel. Each block:
 *   1. Computes |X[k]|^2 for all N/2+1 bins
 *   2. Converts to dB: 10*log10(|X[k]|^2 / N^2)
 *   3. Finds max bin via warp-shuffle reduction
 *   4. Estimates noise floor as mean magnitude
 *   5. Writes ChannelPeak result
 */

#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cstdint>

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace sim {
namespace gpu_compute {
namespace cuda {

#if COMPAT_CUDA_AVAILABLE

/* ----------------------------- Constants ----------------------------- */

static constexpr int K_FFT_THREADS = 256;
static constexpr float K_DB_FLOOR = -120.0f;
static constexpr unsigned int K_FULL_MASK = 0xFFFFFFFFu;

/* ----------------------------- Magnitude + Peak Kernel ----------------------------- */

__global__ void magnitudePeakKernel(const float* SIM_RESTRICT complexData,
                                    std::uint32_t channelCount, std::uint32_t fftSize,
                                    float sampleRateHz, ChannelPeak* SIM_RESTRICT peaks) {
  const std::uint32_t CH = blockIdx.x;
  if (CH >= channelCount) {
    return;
  }

  const std::uint32_t HALF_N = fftSize / 2 + 1;
  const float NORM_SQ = static_cast<float>(fftSize) * static_cast<float>(fftSize);
  const float FREQ_RES = sampleRateHz / static_cast<float>(fftSize);

  // Pointer to this channel's complex data (interleaved real/imag)
  const float* chData = complexData + CH * HALF_N * 2;

  // Thread-local peak tracking
  float localMaxDb = K_DB_FLOOR;
  std::uint32_t localMaxBin = 0;
  float localSumDb = 0.0f;
  std::uint32_t localCount = 0;

  // Strided iteration over frequency bins
  for (std::uint32_t k = threadIdx.x; k < HALF_N; k += blockDim.x) {
    const float RE = chData[k * 2];
    const float IM = chData[k * 2 + 1];
    const float MAG_SQ = (RE * RE + IM * IM) / NORM_SQ;

    float db;
    if (MAG_SQ > 1e-30f) {
      db = 10.0f * log10f(MAG_SQ);
    } else {
      db = K_DB_FLOOR;
    }

    if (db > localMaxDb) {
      localMaxDb = db;
      localMaxBin = k;
    }
    localSumDb += db;
    ++localCount;
  }

  // Warp-level reduction for peak finding
  for (int offset = 16; offset > 0; offset >>= 1) {
    const float OTHER_DB = __shfl_down_sync(K_FULL_MASK, localMaxDb, offset);
    const std::uint32_t OTHER_BIN = __shfl_down_sync(K_FULL_MASK, localMaxBin, offset);
    const float OTHER_SUM = __shfl_down_sync(K_FULL_MASK, localSumDb, offset);
    const std::uint32_t OTHER_COUNT = __shfl_down_sync(K_FULL_MASK, localCount, offset);

    if (OTHER_DB > localMaxDb) {
      localMaxDb = OTHER_DB;
      localMaxBin = OTHER_BIN;
    }
    localSumDb += OTHER_SUM;
    localCount += OTHER_COUNT;
  }

  // Cross-warp reduction via shared memory
  __shared__ float sMaxDb[8];
  __shared__ std::uint32_t sMaxBin[8];
  __shared__ float sSumDb[8];
  __shared__ std::uint32_t sCount[8];

  const int WARP_ID = static_cast<int>(threadIdx.x) >> 5;
  const int LANE_ID = static_cast<int>(threadIdx.x) & 31;

  if (LANE_ID == 0) {
    sMaxDb[WARP_ID] = localMaxDb;
    sMaxBin[WARP_ID] = localMaxBin;
    sSumDb[WARP_ID] = localSumDb;
    sCount[WARP_ID] = localCount;
  }

  __syncthreads();

  // Final reduction in first warp
  const int WARP_COUNT = (static_cast<int>(blockDim.x) + 31) >> 5;

  if (threadIdx.x < static_cast<unsigned>(WARP_COUNT)) {
    localMaxDb = sMaxDb[threadIdx.x];
    localMaxBin = sMaxBin[threadIdx.x];
    localSumDb = sSumDb[threadIdx.x];
    localCount = sCount[threadIdx.x];
  } else if (threadIdx.x < 32u) {
    localMaxDb = K_DB_FLOOR;
    localMaxBin = 0;
    localSumDb = 0.0f;
    localCount = 0;
  }

  if (threadIdx.x < 32u) {
    for (int offset = 16; offset > 0; offset >>= 1) {
      const float OTHER_DB = __shfl_down_sync(K_FULL_MASK, localMaxDb, offset);
      const std::uint32_t OTHER_BIN = __shfl_down_sync(K_FULL_MASK, localMaxBin, offset);
      const float OTHER_SUM = __shfl_down_sync(K_FULL_MASK, localSumDb, offset);
      const std::uint32_t OTHER_COUNT = __shfl_down_sync(K_FULL_MASK, localCount, offset);

      if (OTHER_DB > localMaxDb) {
        localMaxDb = OTHER_DB;
        localMaxBin = OTHER_BIN;
      }
      localSumDb += OTHER_SUM;
      localCount += OTHER_COUNT;
    }

    if (threadIdx.x == 0) {
      peaks[CH].peakFreqHz = static_cast<float>(localMaxBin) * FREQ_RES;
      peaks[CH].peakMagnitudeDb = localMaxDb;
      peaks[CH].peakBin = localMaxBin;
      peaks[CH].noiseFloorDb =
          (localCount > 0) ? localSumDb / static_cast<float>(localCount) : K_DB_FLOOR;
    }
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API ----------------------------- */

bool fftMagnitudePeaksCuda(const float* dComplex, std::uint32_t channelCount, std::uint32_t fftSize,
                           float sampleRateHz, ChannelPeak* dPeaks, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dComplex;
  (void)channelCount;
  (void)fftSize;
  (void)sampleRateHz;
  (void)dPeaks;
  (void)stream;
  return false;
#else
  if (dComplex == nullptr || dPeaks == nullptr || channelCount == 0 || fftSize == 0) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  magnitudePeakKernel<<<static_cast<int>(channelCount), K_FFT_THREADS, 0, s>>>(
      dComplex, channelCount, fftSize, sampleRateHz, dPeaks);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

} // namespace cuda
} // namespace gpu_compute
} // namespace sim
