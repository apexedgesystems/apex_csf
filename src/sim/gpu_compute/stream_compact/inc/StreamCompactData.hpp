#ifndef APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_DATA_HPP
#define APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_DATA_HPP
/**
 * @file StreamCompactData.hpp
 * @brief Data structures for StreamCompactModel.
 *
 * Threshold + stream compaction + classification histogram model.
 * Simulates a detection pipeline: filter raw sensor data, keep only
 * interesting elements via prefix-sum compaction, classify them.
 */

#include <cstdint>

namespace sim {
namespace gpu_compute {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct StreamCompactTunableParams
 * @brief Runtime-adjustable configuration for GPU stream compaction.
 *
 * Size: 24 bytes
 */
struct StreamCompactTunableParams {
  std::uint32_t fieldWidth{2048};  ///< Input field width.
  std::uint32_t fieldHeight{2048}; ///< Input field height.
  float threshold{0.5f};           ///< Threshold for element selection.
  std::uint32_t classCount{8};     ///< Number of classification bins.
  float classMin{0.0f};            ///< Lower bound for classification.
  float classMax{1.0f};            ///< Upper bound for classification.
};

static_assert(sizeof(StreamCompactTunableParams) == 24, "StreamCompactTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct StreamCompactState
 * @brief Internal state tracking GPU execution.
 */
struct StreamCompactState {
  std::uint64_t kickCount{0};
  std::uint64_t completeCount{0};
  std::uint64_t busyCount{0};
  std::uint64_t errorCount{0};
  std::uint32_t lastCompactedCount{0}; ///< Elements surviving threshold.
  std::uint32_t lastTotalCount{0};     ///< Total input elements.
  float lastSelectivity{0.0f};         ///< Fraction passing threshold.
  float lastDurationMs{0.0f};
};

} // namespace gpu_compute
} // namespace sim

#endif // APEX_SIM_GPU_COMPUTE_STREAM_COMPACT_DATA_HPP
