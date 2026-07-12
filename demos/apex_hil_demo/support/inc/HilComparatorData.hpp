#ifndef APEX_HIL_DEMO_COMPARATOR_DATA_HPP
#define APEX_HIL_DEMO_COMPARATOR_DATA_HPP
/**
 * @file HilComparatorData.hpp
 * @brief Data structures for HilComparator.
 *
 * Tunable parameters (thresholds) and runtime state for the comparator.
 * Both registered with the executive registry for C2 inspection.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace appsim {
namespace support {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct ComparatorTunableParams
 * @brief Runtime-adjustable comparator configuration.
 *
 * Size: 8 bytes.
 */
struct ComparatorTunableParams {
  float warnThreshold{0.1F}; ///< Divergence magnitude warning threshold [N].
  float reserved{0.0F};      ///< Reserved for alignment.
};

static_assert(sizeof(ComparatorTunableParams) == 8, "ComparatorTunableParams size mismatch");

/* ----------------------------- State ----------------------------- */

/**
 * @struct ComparatorState
 * @brief Runtime state for HilComparator.
 *
 * Size: 16 bytes.
 */
struct ComparatorState {
  float maxDivergence{0.0F};     ///< Maximum observed divergence [N].
  std::uint32_t compareCount{0}; ///< Total comparisons executed.
  std::uint32_t warnCount{0};    ///< Threshold warnings issued.
  std::uint32_t reserved{0};     ///< Reserved for alignment.
};

static_assert(sizeof(ComparatorState) == 16, "ComparatorState size mismatch");

} // namespace support
} // namespace appsim

#endif // APEX_HIL_DEMO_COMPARATOR_DATA_HPP
