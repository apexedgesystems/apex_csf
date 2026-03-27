#ifndef APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_KERNEL_CUH
#define APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_KERNEL_CUH
/**
 * @file FFTAnalyzerKernel.cuh
 * @brief CUDA kernel API for batched FFT analysis with peak detection.
 *
 * Two-phase pipeline:
 *   1. cuFFT batched forward R2C transform
 *   2. Custom kernel: magnitude spectrum (dB) + per-channel peak extraction
 *
 * @note RT-SAFE with pre-allocated device buffers and cuFFT plan.
 */

#include "src/sim/gpu_compute/fft_analyzer/inc/FFTAnalyzerData.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstdint>

namespace sim {
namespace gpu_compute {
namespace cuda {

/**
 * @brief Compute magnitude spectrum (dB) from complex FFT output and extract peaks.
 *
 * For each channel, computes |X[k]| in dB, finds the maximum bin, and
 * estimates noise floor from average magnitude.
 *
 * @param dComplex Device pointer to interleaved complex output from R2C FFT.
 *                 Layout: channelCount * (N/2+1) * 2 floats (real, imag pairs).
 * @param channelCount Number of channels.
 * @param fftSize Original signal length (N).
 * @param sampleRateHz Sample rate for frequency conversion.
 * @param dPeaks Device pointer to output ChannelPeak array (one per channel).
 * @param stream CUDA stream.
 * @return true on success.
 *
 * @note RT-SAFE with pre-allocated buffers.
 */
bool fftMagnitudePeaksCuda(const float* dComplex, std::uint32_t channelCount, std::uint32_t fftSize,
                           float sampleRateHz, ChannelPeak* dPeaks,
                           void* stream = nullptr) noexcept;

} // namespace cuda
} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_KERNEL_CUH
