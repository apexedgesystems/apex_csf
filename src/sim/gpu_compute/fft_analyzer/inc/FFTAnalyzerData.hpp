#ifndef APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_DATA_HPP
#define APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_DATA_HPP
/**
 * @file FFTAnalyzerData.hpp
 * @brief Data structures for FFTAnalyzerModel.
 *
 * Batched 1D FFT over many sensor channels with spectral peak detection.
 * Uses cuFFT for the forward transform and a custom kernel for magnitude
 * spectrum computation and peak extraction.
 */

#include <cstdint>

namespace sim {
namespace gpu_compute {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct FFTAnalyzerTunableParams
 * @brief Runtime-adjustable configuration for GPU batched FFT analysis.
 *
 * Size: 24 bytes
 */
struct FFTAnalyzerTunableParams {
  std::uint32_t channelCount{256};       ///< Number of sensor channels.
  std::uint32_t samplesPerChannel{4096}; ///< Samples per channel (must be power of 2).
  float peakThresholdDb{-20.0f};         ///< Peak detection threshold (dB below max).
  float sampleRateHz{10000.0f};          ///< Sample rate for frequency conversion.
  std::uint32_t reserved0{0};
  std::uint32_t reserved1{0};
};

static_assert(sizeof(FFTAnalyzerTunableParams) == 24, "FFTAnalyzerTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct FFTAnalyzerState
 * @brief Internal state tracking GPU execution.
 */
struct FFTAnalyzerState {
  std::uint64_t kickCount{0};
  std::uint64_t completeCount{0};
  std::uint64_t busyCount{0};
  std::uint64_t errorCount{0};
  float lastPeakFreqHz{0.0f};      ///< Strongest peak frequency (channel 0).
  float lastPeakMagnitudeDb{0.0f}; ///< Strongest peak magnitude (channel 0).
  float lastDurationMs{0.0f};
  std::uint32_t reserved0{0};
};

/* ----------------------------- GPU Output ----------------------------- */

/**
 * @struct ChannelPeak
 * @brief Per-channel spectral peak result.
 */
struct ChannelPeak {
  float peakFreqHz;      ///< Frequency of strongest peak.
  float peakMagnitudeDb; ///< Magnitude in dB.
  std::uint32_t peakBin; ///< FFT bin index of peak.
  float noiseFloorDb;    ///< Average magnitude (noise estimate).
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_FFT_ANALYZER_DATA_HPP
