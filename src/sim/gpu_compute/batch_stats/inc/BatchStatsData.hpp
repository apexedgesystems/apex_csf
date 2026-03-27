#ifndef APEX_SIM_GPU_COMPUTE_BATCH_STATS_DATA_HPP
#define APEX_SIM_GPU_COMPUTE_BATCH_STATS_DATA_HPP
/**
 * @file BatchStatsData.hpp
 * @brief Data structures for BatchStatsModel.
 *
 * Parallel reduction model that computes min/max/mean/variance over large
 * float arrays on GPU. Demonstrates warp-shuffle reduction and atomic
 * histogram patterns relevant to sensor fusion and anomaly detection.
 */

#include <cstdint>

namespace sim {
namespace gpu_compute {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct BatchStatsTunableParams
 * @brief Runtime-adjustable configuration for GPU batch statistics.
 *
 * Size: 24 bytes
 */
struct BatchStatsTunableParams {
  std::uint32_t elementCount{1u << 20}; ///< Total float elements (default 1M).
  std::uint32_t groupSize{4096};        ///< Elements per reduction group.
  std::uint32_t histogramBins{64};      ///< Bins for per-group histogram.
  float histogramMin{-10.0f};           ///< Histogram lower bound.
  float histogramMax{10.0f};            ///< Histogram upper bound.
  std::uint32_t reserved0{0};           ///< Alignment padding.
};

static_assert(sizeof(BatchStatsTunableParams) == 24, "BatchStatsTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct BatchStatsState
 * @brief Internal state tracking GPU execution.
 */
struct BatchStatsState {
  std::uint64_t kickCount{0};     ///< Total kick invocations.
  std::uint64_t completeCount{0}; ///< Successful GPU completions.
  std::uint64_t busyCount{0};     ///< Times kick found GPU still busy.
  std::uint64_t errorCount{0};    ///< Kernel launch failures.
  float lastMinVal{0.0f};         ///< Min from last completed run.
  float lastMaxVal{0.0f};         ///< Max from last completed run.
  float lastMeanVal{0.0f};        ///< Mean from last completed run.
  float lastVariance{0.0f};       ///< Variance from last completed run.
  float lastDurationMs{0.0f};     ///< Wall-clock GPU duration (ms).
  std::uint32_t reserved0{0};     ///< Alignment padding.
};

/* ----------------------------- GPU Output ----------------------------- */

/**
 * @struct GroupStats
 * @brief Per-group reduction result (produced by GPU kernel).
 */
struct GroupStats {
  float minVal;        ///< Minimum value in group.
  float maxVal;        ///< Maximum value in group.
  float sum;           ///< Sum of values (mean = sum / count).
  float sumSq;         ///< Sum of squared values (for variance).
  std::uint32_t count; ///< Element count in group.
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_BATCH_STATS_DATA_HPP
