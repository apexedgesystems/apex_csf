#ifndef APEX_SYSTEM_CORE_DATA_CATEGORY_HPP
#define APEX_SYSTEM_CORE_DATA_CATEGORY_HPP
/**
 * @file DataCategory.hpp
 * @brief Semantic categories for model and component data.
 *
 * Distinguishes between parameter, state, input, and output data
 * to enable appropriate access control and data flow management.
 *
 * All functions are RT-safe: O(1), no allocation, noexcept.
 */

#include <cstdint>

namespace system_core {
namespace data {

/* ----------------------------- DataCategory ----------------------------- */

/**
 * @enum DataCategory
 * @brief Semantic category for data blocks.
 *
 * Each category implies different access patterns:
 *   - STATIC_PARAM: Read-only constants (e.g., physical constants)
 *   - TUNABLE_PARAM: Runtime-adjustable parameters (e.g., gains)
 *   - STATE: Internal model state (e.g., integrator values)
 *   - INPUT: External data fed to model (e.g., sensor readings)
 *   - OUTPUT: Data produced by model (e.g., computed values)
 */
enum class DataCategory : std::uint8_t {
  STATIC_PARAM = 0,  ///< Read-only constants.
  TUNABLE_PARAM = 1, ///< Runtime-adjustable parameters.
  STATE = 2,         ///< Internal model state.
  INPUT = 3,         ///< External data fed to model.
  OUTPUT = 4         ///< Data produced by model.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Human-readable string for DataCategory.
 * @param cat Category value.
 * @return Static string (no allocation).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(DataCategory cat) noexcept {
  switch (cat) {
  case DataCategory::STATIC_PARAM:
    return "STATIC_PARAM";
  case DataCategory::TUNABLE_PARAM:
    return "TUNABLE_PARAM";
  case DataCategory::STATE:
    return "STATE";
  case DataCategory::INPUT:
    return "INPUT";
  case DataCategory::OUTPUT:
    return "OUTPUT";
  }
  return "UNKNOWN";
}

/**
 * @brief Check if category is a parameter type.
 * @param cat Category value.
 * @return true if STATIC_PARAM or TUNABLE_PARAM.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isParam(DataCategory cat) noexcept {
  return cat == DataCategory::STATIC_PARAM || cat == DataCategory::TUNABLE_PARAM;
}

/**
 * @brief Check if category is read-only.
 * @param cat Category value.
 * @return true if STATIC_PARAM.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isReadOnly(DataCategory cat) noexcept {
  return cat == DataCategory::STATIC_PARAM;
}

/**
 * @brief Check if category represents data flow into model.
 * @param cat Category value.
 * @return true if INPUT or any PARAM type.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isModelInput(DataCategory cat) noexcept {
  return cat == DataCategory::INPUT || isParam(cat);
}

/**
 * @brief Check if category represents data flow out of model.
 * @param cat Category value.
 * @return true if OUTPUT.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isModelOutput(DataCategory cat) noexcept {
  return cat == DataCategory::OUTPUT;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_CATEGORY_HPP
