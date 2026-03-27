#ifndef APEX_SYSTEM_CORE_REGISTRY_REGISTRY_DATA_HPP
#define APEX_SYSTEM_CORE_REGISTRY_REGISTRY_DATA_HPP
/**
 * @file RegistryData.hpp
 * @brief Data structures for ApexRegistry entries.
 *
 * Defines the entry types stored in the registry:
 *   - ComponentEntry: Registered component with links to its tasks and data
 *   - TaskEntry: Registered task with scheduling metadata
 *   - DataEntry: Registered data block with byte-level access
 */

#include "src/system/core/infrastructure/data/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstdint>

namespace system_core {

// Forward declarations
namespace schedulable {
class SchedulableTask;
} // namespace schedulable

namespace registry {

/* ----------------------------- Constants ----------------------------- */

/// Maximum components that can be registered.
static constexpr std::size_t MAX_COMPONENTS = 64;

/// Maximum tasks that can be registered.
static constexpr std::size_t MAX_TASKS = 256;

/// Maximum data entries that can be registered.
static constexpr std::size_t MAX_DATA_ENTRIES = 512;

/// Maximum tasks per component (for component->tasks linkage).
static constexpr std::size_t MAX_TASKS_PER_COMPONENT = 16;

/// Maximum data entries per component (for component->data linkage).
static constexpr std::size_t MAX_DATA_PER_COMPONENT = 16;

/* ----------------------------- DataEntry ----------------------------- */

/**
 * @struct DataEntry
 * @brief Registry entry for a data block (state, tunables, inputs, outputs).
 *
 * Provides byte-level access to registered data for logging and inspection.
 * The dataPtr points to the actual data owned by the component/model.
 */
struct DataEntry {
  std::uint32_t fullUid{0};      ///< Owner component's full UID.
  data::DataCategory category{}; ///< Data category (STATE, TUNABLE_PARAM, etc.).
  const char* name{nullptr};     ///< Human-readable name (e.g., "tunableParams").
  const void* dataPtr{nullptr};  ///< Pointer to actual data (not owned, read-only).
  std::size_t size{0};           ///< Size of data in bytes.

  /**
   * @brief Get all bytes of the data block.
   * @return Span over the entire data block.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] apex::compat::span<const std::uint8_t> getBytes() const noexcept {
    if (dataPtr == nullptr || size == 0) {
      return {};
    }
    return {reinterpret_cast<const std::uint8_t*>(dataPtr), size};
  }

  /**
   * @brief Get specific bytes from the data block.
   * @param offset Byte offset from start.
   * @param length Number of bytes to return.
   * @return Span over the requested range, or empty if out of bounds.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] apex::compat::span<const std::uint8_t> getBytes(std::size_t offset,
                                                                std::size_t length) const noexcept {
    if (dataPtr == nullptr || size == 0) {
      return {};
    }
    if (offset >= size || offset + length > size) {
      return {}; // Bounds check failed
    }
    return {reinterpret_cast<const std::uint8_t*>(dataPtr) + offset, length};
  }

  /**
   * @brief Check if entry is valid (has data pointer and size).
   * @return true if entry contains valid data reference.
   */
  [[nodiscard]] bool isValid() const noexcept { return dataPtr != nullptr && size > 0; }
};

/* ----------------------------- TaskEntry ----------------------------- */

/**
 * @struct TaskEntry
 * @brief Registry entry for a scheduled task.
 *
 * Links a task to its owning component with metadata for identification.
 */
struct TaskEntry {
  std::uint32_t fullUid{0};                    ///< Owner component's full UID.
  std::uint8_t taskUid{0};                     ///< Task UID within component.
  const char* name{nullptr};                   ///< Human-readable name (e.g., "step").
  schedulable::SchedulableTask* task{nullptr}; ///< Pointer to task (not owned).

  /**
   * @brief Check if entry is valid (has task pointer).
   * @return true if entry contains valid task reference.
   */
  [[nodiscard]] bool isValid() const noexcept { return task != nullptr; }
};

/* ----------------------------- ComponentEntry ----------------------------- */

/**
 * @struct ComponentEntry
 * @brief Registry entry for a registered component.
 *
 * Groups a component's tasks and data entries for unified access.
 * The tasks and data arrays hold indices into the flat registry arrays.
 */
struct ComponentEntry {
  std::uint32_t fullUid{0};  ///< Component's full UID.
  const char* name{nullptr}; ///< Component name (e.g., "PolynomialModel").
  void* component{nullptr};  ///< Pointer to component (not owned, for model lookups).
  system_component::ComponentType type{system_component::ComponentType::CORE}; ///< Component type.

  /// Indices into registry's task array for this component's tasks.
  std::size_t taskIndices[MAX_TASKS_PER_COMPONENT]{};
  std::size_t taskCount{0};

  /// Indices into registry's data array for this component's data.
  std::size_t dataIndices[MAX_DATA_PER_COMPONENT]{};
  std::size_t dataCount{0};

  /**
   * @brief Check if entry is valid (has name).
   * @return true if entry contains valid component reference.
   */
  [[nodiscard]] bool isValid() const noexcept { return name != nullptr; }
};

} // namespace registry
} // namespace system_core

#endif // APEX_SYSTEM_CORE_REGISTRY_REGISTRY_DATA_HPP
