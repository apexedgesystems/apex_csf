/**
 * @file BatchStatsKernel.cu
 * @brief CUDA kernel implementation for parallel batch statistics.
 *
 * Two kernels:
 *   1. batchStatsKernel: Warp-shuffle reduction for min/max/sum/sumSq per group.
 *   2. batchHistogramKernel: Atomic histogram per group.
 *
 * Each thread block processes one group. Block size is clamped to groupSize
 * so no thread idles. Uses __shfl_down_sync for the final warp reduction
 * to avoid shared memory bank conflicts.
 */

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsKernel.cuh"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cfloat>
#include <cstdint>

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace sim {
namespace gpu_compute {
namespace cuda {

#if COMPAT_CUDA_AVAILABLE

/* ----------------------------- Constants ----------------------------- */

static constexpr int K_THREADS_PER_BLOCK = 256;
static constexpr unsigned int K_FULL_MASK = 0xFFFFFFFFu;

/* ----------------------------- Stats Kernel ----------------------------- */

__global__ void batchStatsKernel(const float* SIM_RESTRICT input, std::uint32_t elementCount,
                                 std::uint32_t groupSize, GroupStats* SIM_RESTRICT output) {
  const std::uint32_t GROUP_IDX = blockIdx.x;
  const std::uint32_t GROUP_START = GROUP_IDX * groupSize;
  const std::uint32_t GROUP_END =
      (GROUP_START + groupSize < elementCount) ? GROUP_START + groupSize : elementCount;

  if (GROUP_START >= elementCount) {
    return;
  }

  // Thread-local accumulators
  float localMin = FLT_MAX;
  float localMax = -FLT_MAX;
  float localSum = 0.0f;
  float localSumSq = 0.0f;
  std::uint32_t localCount = 0;

  // Strided load across the group
  for (std::uint32_t i = GROUP_START + threadIdx.x; i < GROUP_END; i += blockDim.x) {
    const float VAL = input[i];
    if (VAL < localMin) {
      localMin = VAL;
    }
    if (VAL > localMax) {
      localMax = VAL;
    }
    localSum += VAL;
    localSumSq += VAL * VAL;
    ++localCount;
  }

  // Warp-level reduction via shuffle
  for (int offset = 16; offset > 0; offset >>= 1) {
    const float OTHER_MIN = __shfl_down_sync(K_FULL_MASK, localMin, offset);
    const float OTHER_MAX = __shfl_down_sync(K_FULL_MASK, localMax, offset);
    const float OTHER_SUM = __shfl_down_sync(K_FULL_MASK, localSum, offset);
    const float OTHER_SUM_SQ = __shfl_down_sync(K_FULL_MASK, localSumSq, offset);
    const std::uint32_t OTHER_COUNT = __shfl_down_sync(K_FULL_MASK, localCount, offset);

    if (OTHER_MIN < localMin) {
      localMin = OTHER_MIN;
    }
    if (OTHER_MAX > localMax) {
      localMax = OTHER_MAX;
    }
    localSum += OTHER_SUM;
    localSumSq += OTHER_SUM_SQ;
    localCount += OTHER_COUNT;
  }

  // Per-warp results to shared memory
  __shared__ float sMin[8];
  __shared__ float sMax[8];
  __shared__ float sSum[8];
  __shared__ float sSumSq[8];
  __shared__ std::uint32_t sCount[8];

  const int WARP_ID = static_cast<int>(threadIdx.x) >> 5;
  const int LANE_ID = static_cast<int>(threadIdx.x) & 31;

  if (LANE_ID == 0) {
    sMin[WARP_ID] = localMin;
    sMax[WARP_ID] = localMax;
    sSum[WARP_ID] = localSum;
    sSumSq[WARP_ID] = localSumSq;
    sCount[WARP_ID] = localCount;
  }

  __syncthreads();

  // Final reduction in first warp
  const int WARP_COUNT = (static_cast<int>(blockDim.x) + 31) >> 5;

  if (threadIdx.x < static_cast<unsigned>(WARP_COUNT)) {
    localMin = sMin[threadIdx.x];
    localMax = sMax[threadIdx.x];
    localSum = sSum[threadIdx.x];
    localSumSq = sSumSq[threadIdx.x];
    localCount = sCount[threadIdx.x];
  } else if (threadIdx.x < 32u) {
    localMin = FLT_MAX;
    localMax = -FLT_MAX;
    localSum = 0.0f;
    localSumSq = 0.0f;
    localCount = 0;
  }

  if (threadIdx.x < 32u) {
    for (int offset = 16; offset > 0; offset >>= 1) {
      const float OTHER_MIN = __shfl_down_sync(K_FULL_MASK, localMin, offset);
      const float OTHER_MAX = __shfl_down_sync(K_FULL_MASK, localMax, offset);
      const float OTHER_SUM = __shfl_down_sync(K_FULL_MASK, localSum, offset);
      const float OTHER_SUM_SQ = __shfl_down_sync(K_FULL_MASK, localSumSq, offset);
      const std::uint32_t OTHER_COUNT = __shfl_down_sync(K_FULL_MASK, localCount, offset);

      if (OTHER_MIN < localMin) {
        localMin = OTHER_MIN;
      }
      if (OTHER_MAX > localMax) {
        localMax = OTHER_MAX;
      }
      localSum += OTHER_SUM;
      localSumSq += OTHER_SUM_SQ;
      localCount += OTHER_COUNT;
    }

    if (threadIdx.x == 0) {
      output[GROUP_IDX].minVal = localMin;
      output[GROUP_IDX].maxVal = localMax;
      output[GROUP_IDX].sum = localSum;
      output[GROUP_IDX].sumSq = localSumSq;
      output[GROUP_IDX].count = localCount;
    }
  }
}

/* ----------------------------- Histogram Kernel ----------------------------- */

__global__ void batchHistogramKernel(const float* SIM_RESTRICT input, std::uint32_t elementCount,
                                     std::uint32_t groupSize, std::uint32_t bins, float minVal,
                                     float maxVal, std::uint32_t* SIM_RESTRICT histogram) {
  const std::uint32_t GROUP_IDX = blockIdx.x;
  const std::uint32_t GROUP_START = GROUP_IDX * groupSize;
  const std::uint32_t GROUP_END =
      (GROUP_START + groupSize < elementCount) ? GROUP_START + groupSize : elementCount;

  if (GROUP_START >= elementCount) {
    return;
  }

  std::uint32_t* groupHist = histogram + GROUP_IDX * bins;
  const float RANGE = maxVal - minVal;
  const float INV_BIN_WIDTH = (RANGE > 0.0f) ? static_cast<float>(bins) / RANGE : 0.0f;

  // Zero the histogram (cooperatively)
  for (std::uint32_t i = threadIdx.x; i < bins; i += blockDim.x) {
    groupHist[i] = 0;
  }
  __syncthreads();

  // Bin elements
  for (std::uint32_t i = GROUP_START + threadIdx.x; i < GROUP_END; i += blockDim.x) {
    float val = input[i];
    // Clamp to [minVal, maxVal)
    if (val < minVal) {
      val = minVal;
    }
    if (val >= maxVal) {
      val = maxVal - 1e-7f;
    }
    auto bin = static_cast<std::uint32_t>((val - minVal) * INV_BIN_WIDTH);
    if (bin >= bins) {
      bin = bins - 1;
    }
    atomicAdd(&groupHist[bin], 1u);
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API ----------------------------- */

bool batchStatsCuda(const float* dInput, std::uint32_t elementCount, std::uint32_t groupSize,
                    GroupStats* dOutput, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)elementCount;
  (void)groupSize;
  (void)dOutput;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dOutput == nullptr || elementCount == 0 || groupSize == 0) {
    return false;
  }

  const std::uint32_t GROUP_COUNT = (elementCount + groupSize - 1) / groupSize;
  const int THREADS = (groupSize < static_cast<std::uint32_t>(K_THREADS_PER_BLOCK))
                          ? static_cast<int>(groupSize)
                          : K_THREADS_PER_BLOCK;

  auto s = static_cast<cudaStream_t>(stream);
  batchStatsKernel<<<static_cast<int>(GROUP_COUNT), THREADS, 0, s>>>(dInput, elementCount,
                                                                     groupSize, dOutput);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool batchHistogramCuda(const float* dInput, std::uint32_t elementCount, std::uint32_t groupSize,
                        std::uint32_t bins, float minVal, float maxVal, std::uint32_t* dHistogram,
                        void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)elementCount;
  (void)groupSize;
  (void)bins;
  (void)minVal;
  (void)maxVal;
  (void)dHistogram;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dHistogram == nullptr || elementCount == 0 || groupSize == 0 ||
      bins == 0) {
    return false;
  }

  const std::uint32_t GROUP_COUNT = (elementCount + groupSize - 1) / groupSize;
  const int THREADS = (groupSize < static_cast<std::uint32_t>(K_THREADS_PER_BLOCK))
                          ? static_cast<int>(groupSize)
                          : K_THREADS_PER_BLOCK;

  auto s = static_cast<cudaStream_t>(stream);
  batchHistogramKernel<<<static_cast<int>(GROUP_COUNT), THREADS, 0, s>>>(
      dInput, elementCount, groupSize, bins, minVal, maxVal, dHistogram);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

} // namespace cuda
} // namespace gpu_compute
} // namespace sim
