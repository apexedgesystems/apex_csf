/**
 * @file StreamCompactKernel.cu
 * @brief CUDA kernel implementation for stream compaction + classification.
 *
 * Compaction strategy:
 *   - Phase 1: Per-block scan with flag generation (threshold test)
 *   - Phase 2: Block-level prefix sum on per-block counts
 *   - Phase 3: Scatter selected elements using local + global offsets
 *
 * For simplicity and correctness, uses atomicAdd for the output count
 * and a single-pass compact kernel. This is optimal for moderate
 * selectivity (10-90% passing). For very low selectivity, a two-pass
 * approach with prefix-sum would be more efficient.
 */

#include "src/sim/gpu_compute/stream_compact/inc/StreamCompactKernel.cuh"
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

static constexpr int K_COMPACT_THREADS = 256;

/* ----------------------------- Compact Kernel ----------------------------- */

/**
 * Warp-level inclusive prefix sum using shuffle.
 */
__device__ std::uint32_t warpPrefixSum(std::uint32_t val) {
  for (int offset = 1; offset < 32; offset <<= 1) {
    const std::uint32_t N = __shfl_up_sync(0xFFFFFFFFu, val, offset);
    if ((threadIdx.x & 31) >= static_cast<unsigned>(offset)) {
      val += N;
    }
  }
  return val;
}

/**
 * Single-pass stream compaction: threshold + prefix-sum + scatter.
 *
 * Each warp computes a local prefix sum of flags, then uses atomicAdd
 * on the global count to get the warp's output offset. Threads scatter
 * their elements to the output at (warpOffset + localPrefix - 1).
 */
__global__ void compactKernel(const float* SIM_RESTRICT input, std::uint32_t elementCount,
                              float threshold, float* SIM_RESTRICT output,
                              std::uint32_t* SIM_RESTRICT globalCount) {
  const std::uint32_t IDX = blockIdx.x * blockDim.x + threadIdx.x;
  const std::uint32_t LANE = threadIdx.x & 31;

  // Each thread tests one element
  std::uint32_t flag = 0;
  float val = 0.0f;
  if (IDX < elementCount) {
    val = input[IDX];
    flag = (val >= threshold) ? 1u : 0u;
  }

  // Warp-level inclusive prefix sum of flags
  const std::uint32_t PREFIX = warpPrefixSum(flag);

  // Last active lane in warp has total count for this warp
  const std::uint32_t WARP_TOTAL = __shfl_sync(0xFFFFFFFFu, PREFIX, 31);

  // One thread per warp atomically reserves output space
  __shared__ std::uint32_t warpOffset[8]; // max 256 threads / 32 = 8 warps
  const std::uint32_t WARP_ID = threadIdx.x >> 5;

  if (LANE == 0 && WARP_TOTAL > 0) {
    warpOffset[WARP_ID] = atomicAdd(globalCount, WARP_TOTAL);
  }
  __syncwarp();

  // Scatter selected elements
  if (flag != 0u) {
    const std::uint32_t DST = warpOffset[WARP_ID] + PREFIX - 1;
    output[DST] = val;
  }
}

/* ----------------------------- Classify Kernel ----------------------------- */

__global__ void classifyKernel(const float* SIM_RESTRICT input, std::uint32_t count,
                               std::uint32_t bins, float minVal, float maxVal,
                               std::uint32_t* SIM_RESTRICT histogram) {
  const std::uint32_t IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= count) {
    return;
  }

  const float RANGE = maxVal - minVal;
  const float INV_BIN_WIDTH = (RANGE > 0.0f) ? static_cast<float>(bins) / RANGE : 0.0f;

  float val = input[IDX];
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

  atomicAdd(&histogram[bin], 1u);
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API ----------------------------- */

bool streamCompactCuda(const float* dInput, std::uint32_t elementCount, float threshold,
                       float* dOutput, std::uint32_t* dCount, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)elementCount;
  (void)threshold;
  (void)dOutput;
  (void)dCount;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dOutput == nullptr || dCount == nullptr || elementCount == 0) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);

  // Zero the count
  cudaMemsetAsync(dCount, 0, sizeof(std::uint32_t), s);

  const int BLOCKS = (static_cast<int>(elementCount) + K_COMPACT_THREADS - 1) / K_COMPACT_THREADS;
  compactKernel<<<BLOCKS, K_COMPACT_THREADS, 0, s>>>(dInput, elementCount, threshold, dOutput,
                                                     dCount);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool classifyHistogramCuda(const float* dInput, std::uint32_t count, std::uint32_t bins,
                           float minVal, float maxVal, std::uint32_t* dHistogram,
                           void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)count;
  (void)bins;
  (void)minVal;
  (void)maxVal;
  (void)dHistogram;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dHistogram == nullptr || count == 0 || bins == 0) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);

  // Zero the histogram
  cudaMemsetAsync(dHistogram, 0, bins * sizeof(std::uint32_t), s);

  const int BLOCKS = (static_cast<int>(count) + K_COMPACT_THREADS - 1) / K_COMPACT_THREADS;
  classifyKernel<<<BLOCKS, K_COMPACT_THREADS, 0, s>>>(dInput, count, bins, minVal, maxVal,
                                                      dHistogram);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

} // namespace cuda
} // namespace gpu_compute
} // namespace sim
