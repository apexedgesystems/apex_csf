#ifndef APEX_SIM_GPU_COMPUTE_BATCH_STATS_KERNEL_CUH
#define APEX_SIM_GPU_COMPUTE_BATCH_STATS_KERNEL_CUH
/**
 * @file BatchStatsKernel.cuh
 * @brief CUDA kernel API for parallel batch statistics.
 *
 * Computes per-group min/max/sum/sumSq over a large float array using
 * warp-shuffle reduction. Each thread block processes one group.
 *
 * @note RT-SAFE with pre-allocated device buffers. All functions return
 *       bool and never throw.
 */

#include "src/sim/gpu_compute/batch_stats/inc/BatchStatsData.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstddef>
#include <cstdint>

namespace sim {
namespace gpu_compute {
namespace cuda {

/**
 * @brief Compute per-group statistics over a float array.
 *
 * @param dInput Device pointer to input float array.
 * @param elementCount Total number of elements.
 * @param groupSize Elements per reduction group.
 * @param dOutput Device pointer to output GroupStats array (one per group).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool batchStatsCuda(const float* dInput, std::uint32_t elementCount, std::uint32_t groupSize,
                    GroupStats* dOutput, void* stream = nullptr) noexcept;

/**
 * @brief Compute per-group histogram over a float array.
 *
 * @param dInput Device pointer to input float array.
 * @param elementCount Total number of elements.
 * @param groupSize Elements per reduction group.
 * @param bins Number of histogram bins.
 * @param minVal Lower bound for binning.
 * @param maxVal Upper bound for binning.
 * @param dHistogram Device pointer to histogram output (groupCount * bins uint32_t).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool batchHistogramCuda(const float* dInput, std::uint32_t elementCount, std::uint32_t groupSize,
                        std::uint32_t bins, float minVal, float maxVal, std::uint32_t* dHistogram,
                        void* stream = nullptr) noexcept;

} // namespace cuda
} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_BATCH_STATS_KERNEL_CUH
