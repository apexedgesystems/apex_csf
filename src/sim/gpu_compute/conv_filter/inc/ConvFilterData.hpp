#ifndef APEX_SIM_GPU_COMPUTE_CONV_FILTER_DATA_HPP
#define APEX_SIM_GPU_COMPUTE_CONV_FILTER_DATA_HPP
/**
 * @file ConvFilterData.hpp
 * @brief Data structures for ConvFilterModel.
 *
 * 2D convolution model that applies an NxN kernel to a large grayscale
 * image buffer on GPU. Demonstrates shared memory tiling with halo
 * exchange, relevant to image preprocessing and denoising pipelines.
 */

#include <cstdint>

namespace sim {
namespace gpu_compute {

/* ----------------------------- Constants ----------------------------- */

static constexpr std::uint32_t CONV_MAX_KERNEL_RADIUS = 15; ///< Max supported radius (31x31).

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct ConvFilterTunableParams
 * @brief Runtime-adjustable configuration for GPU 2D convolution.
 *
 * Size: 24 bytes
 */
struct ConvFilterTunableParams {
  std::uint32_t imageWidth{2048};  ///< Image width in pixels.
  std::uint32_t imageHeight{2048}; ///< Image height in pixels.
  std::uint32_t kernelRadius{3};   ///< Convolution kernel radius (diameter = 2R+1).
  std::uint32_t kernelType{0};     ///< 0=Gaussian, 1=Sobel-X, 2=Sobel-Y, 3=Box.
  float gaussianSigma{1.5f};       ///< Sigma for Gaussian kernel.
  std::uint32_t reserved0{0};      ///< Alignment padding.
};

static_assert(sizeof(ConvFilterTunableParams) == 24, "ConvFilterTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct ConvFilterState
 * @brief Internal state tracking GPU execution.
 */
struct ConvFilterState {
  std::uint64_t kickCount{0};     ///< Total kick invocations.
  std::uint64_t completeCount{0}; ///< Successful GPU completions.
  std::uint64_t busyCount{0};     ///< Times kick found GPU still busy.
  std::uint64_t errorCount{0};    ///< Kernel launch failures.
  float lastOutputMin{0.0f};      ///< Min pixel in last output.
  float lastOutputMax{0.0f};      ///< Max pixel in last output.
  float lastOutputMean{0.0f};     ///< Mean pixel in last output.
  float lastDurationMs{0.0f};     ///< Wall-clock GPU duration (ms).
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_CONV_FILTER_DATA_HPP
