#ifndef APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_KERNEL_CUH
#define APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_KERNEL_CUH
/**
 * @file StreamCompactKernel.cuh
 * @brief CUDA kernel API for threshold + stream compaction + histogram.
 *
 * Three-phase pipeline:
 *   1. Threshold: Mark elements above threshold (produces flag array)
 *   2. Compact: Prefix sum on flags, scatter selected elements to output
 *   3. Classify: Bin the compacted elements into a histogram
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstdint>

namespace sim {
namespace gpu_compute {
namespace cuda {

/**
 * @brief Threshold + compact: select elements above threshold, write contiguously.
 *
 * @param dInput Device pointer to input float array.
 * @param elementCount Total number of elements.
 * @param threshold Elements >= threshold are selected.
 * @param dOutput Device pointer to compacted output (must hold elementCount floats).
 * @param dCount Device pointer to single uint32_t (number of selected elements).
 * @param stream CUDA stream.
 * @return true on success.
 *
 * @note RT-SAFE with pre-allocated buffers.
 */
bool streamCompactCuda(const float* dInput, std::uint32_t elementCount, float threshold,
                       float* dOutput, std::uint32_t* dCount, void* stream = nullptr) noexcept;

/**
 * @brief Classify compacted elements into histogram bins.
 *
 * @param dInput Device pointer to compacted float array.
 * @param count Number of elements in compacted array.
 * @param bins Number of classification bins.
 * @param minVal Lower bound for binning.
 * @param maxVal Upper bound for binning.
 * @param dHistogram Device pointer to histogram output (bins * uint32_t).
 * @param stream CUDA stream.
 * @return true on success.
 *
 * @note RT-SAFE with pre-allocated buffers.
 */
bool classifyHistogramCuda(const float* dInput, std::uint32_t count, std::uint32_t bins,
                           float minVal, float maxVal, std::uint32_t* dHistogram,
                           void* stream = nullptr) noexcept;

} // namespace cuda
} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_KERNEL_CUH
