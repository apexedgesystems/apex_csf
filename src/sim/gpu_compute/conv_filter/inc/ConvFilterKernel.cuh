#ifndef APEX_SIM_GPU_COMPUTE_CONV_FILTER_KERNEL_CUH
#define APEX_SIM_GPU_COMPUTE_CONV_FILTER_KERNEL_CUH
/**
 * @file ConvFilterKernel.cuh
 * @brief CUDA kernel API for 2D image convolution.
 *
 * Applies an NxN convolution kernel to a grayscale float image using
 * shared memory tiling with halo exchange. The convolution kernel
 * weights are stored in constant memory for fast broadcast.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstdint>

namespace sim {
namespace gpu_compute {
namespace cuda {

/**
 * @brief Upload convolution kernel weights to device constant memory.
 *
 * @param hKernel Host pointer to kernel weights ((2R+1)*(2R+1) floats).
 * @param radius Kernel radius (diameter = 2R+1).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note NOT RT-safe (synchronous memcpy to constant memory).
 */
bool convSetKernel(const float* hKernel, std::uint32_t radius) noexcept;

/**
 * @brief Apply 2D convolution to a grayscale float image.
 *
 * Uses shared memory tiling: each thread block loads a tile plus halo
 * into shared memory, then computes the convolution for the interior.
 *
 * @param dInput Device pointer to input image (width * height floats).
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param radius Convolution kernel radius.
 * @param dOutput Device pointer to output image (same size as input).
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool conv2dCuda(const float* dInput, std::uint32_t width, std::uint32_t height,
                std::uint32_t radius, float* dOutput, void* stream = nullptr) noexcept;

/**
 * @brief Upload 1D separable kernel weights to device constant memory.
 *
 * For separable kernels (Gaussian, box), stores a single row of (2R+1) weights.
 * Used by conv2dSeparableCuda() for the two-pass decomposition.
 *
 * @param hKernel1D Host pointer to 1D weights ((2R+1) floats).
 * @param radius Kernel radius.
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note NOT RT-safe (synchronous memcpy to constant memory).
 */
bool convSetKernel1D(const float* hKernel1D, std::uint32_t radius) noexcept;

/**
 * @brief Apply 2D convolution via separable two-pass decomposition.
 *
 * Executes two 1D passes (horizontal then vertical) using the 1D weights
 * uploaded via convSetKernel1D(). Requires a device temp buffer of the
 * same size as the image for the intermediate result.
 *
 * For a kernel of radius R, this does 2*(2R+1) multiplies per pixel
 * instead of (2R+1)^2. For R=7: 30 vs 225 = 7.5x fewer operations.
 *
 * @param dInput Device pointer to input image.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param radius Convolution kernel radius.
 * @param dTemp Device pointer to temp buffer (width * height floats).
 * @param dOutput Device pointer to output image.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure or CUDA unavailable.
 *
 * @note RT-SAFE with pre-allocated device buffers.
 */
bool conv2dSeparableCuda(const float* dInput, std::uint32_t width, std::uint32_t height,
                         std::uint32_t radius, float* dTemp, float* dOutput,
                         void* stream = nullptr) noexcept;

/**
 * @brief Generate 1D Gaussian kernel weights on host.
 *
 * @param hKernel1D Output buffer for 1D weights ((2R+1) floats).
 * @param radius Kernel radius.
 * @param sigma Gaussian standard deviation.
 *
 * @note NOT RT-safe (math on host).
 */
void generateGaussianKernel1D(float* hKernel1D, std::uint32_t radius, float sigma) noexcept;

/**
 * @brief Generate Gaussian kernel weights on host.
 *
 * @param hKernel Output buffer for kernel weights ((2R+1)*(2R+1) floats).
 * @param radius Kernel radius.
 * @param sigma Gaussian standard deviation.
 *
 * @note NOT RT-safe (math on host).
 */
void generateGaussianKernel(float* hKernel, std::uint32_t radius, float sigma) noexcept;

/**
 * @brief Generate box (averaging) kernel weights on host.
 *
 * @param hKernel Output buffer for kernel weights.
 * @param radius Kernel radius.
 * @note NOT RT-safe (math on host).
 */
void generateBoxKernel(float* hKernel, std::uint32_t radius) noexcept;

} // namespace cuda
} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_CONV_FILTER_KERNEL_CUH
