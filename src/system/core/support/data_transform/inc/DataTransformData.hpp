#ifndef APEX_SUPPORT_DATA_TRANSFORM_DATA_HPP
#define APEX_SUPPORT_DATA_TRANSFORM_DATA_HPP
/**
 * @file DataTransformData.hpp
 * @brief Data structures for the DataTransform support component.
 *
 * Defines the transform entry table, stats counters, and command opcodes.
 *
 * @note RT-safe: Pure POD structures.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DataTarget.hpp"
#include "src/utilities/data_proxy/inc/ByteMaskProxy.hpp"

#include <array>
#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of transform entries.
constexpr std::size_t TRANSFORM_MAX_ENTRIES = 8;

/* ----------------------------- TransformEntry ----------------------------- */

/**
 * @struct TransformEntry
 * @brief A single data mutation target with its own ByteMaskProxy.
 *
 * Each entry targets a specific byte range in a registered data block
 * and owns an independent mask proxy for applying transforms.
 */
struct TransformEntry {
  data::DataTarget target{};         ///< Where to apply transforms.
  data_proxy::ByteMaskProxy proxy{}; ///< Mask queue for this target.
  bool armed{false};                 ///< Whether this entry is active.
};

/* ----------------------------- TransformStats ----------------------------- */

/**
 * @struct TransformStats
 * @brief Diagnostic counters for the DataTransform component.
 */
struct TransformStats {
  std::uint32_t applyCycles{0};     ///< Total apply() task invocations.
  std::uint32_t masksApplied{0};    ///< Successful mask applications.
  std::uint32_t resolveFailures{0}; ///< Target resolution failures.
  std::uint32_t applyFailures{0};   ///< Mask application failures.
  std::uint32_t entriesArmed{0};    ///< Currently armed entry count.
};

/* ----------------------------- DataTransformOpcode ----------------------------- */

/**
 * @enum DataTransformOpcode
 * @brief Command opcodes for DataTransform (component-specific range 0x0600+).
 */
enum class DataTransformOpcode : std::uint16_t {
  GET_STATS = 0x0600,        ///< Return transform health telemetry.
  ARM_ENTRY = 0x0601,        ///< Arm a transform entry by index.
  DISARM_ENTRY = 0x0602,     ///< Disarm a transform entry by index.
  PUSH_ZERO_MASK = 0x0603,   ///< Push zero mask to entry's proxy queue.
  PUSH_HIGH_MASK = 0x0604,   ///< Push high mask to entry's proxy queue.
  PUSH_FLIP_MASK = 0x0605,   ///< Push flip mask to entry's proxy queue.
  PUSH_CUSTOM_MASK = 0x0606, ///< Push custom AND/XOR mask to entry's proxy.
  CLEAR_MASKS = 0x0607,      ///< Clear all masks on an entry.
  CLEAR_ALL = 0x0608,        ///< Disarm all entries and clear all masks.
  SET_TARGET = 0x0609,       ///< Set target (fullUid, category, offset, len) for an entry.
  APPLY_ENTRY = 0x060A,      ///< Resolve target and apply front mask immediately.
  APPLY_ALL = 0x060B         ///< Apply front mask on all armed entries immediately.
};

/* ----------------------------- DataTransformTlm ----------------------------- */

/**
 * @struct DataTransformTlm
 * @brief Health telemetry payload for DataTransform.
 *
 * Returned by GET_STATS (opcode 0x0600).
 */
struct __attribute__((packed)) DataTransformTlm {
  std::uint32_t applyCycles{0};
  std::uint32_t masksApplied{0};
  std::uint32_t resolveFailures{0};
  std::uint32_t applyFailures{0};
  std::uint32_t entriesArmed{0};
};

static_assert(sizeof(DataTransformTlm) == 20, "DataTransformTlm size mismatch");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_DATA_TRANSFORM_DATA_HPP
