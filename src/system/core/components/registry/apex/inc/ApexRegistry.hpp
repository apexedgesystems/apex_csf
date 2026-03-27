#ifndef APEX_SYSTEM_CORE_REGISTRY_APEX_REGISTRY_HPP
#define APEX_SYSTEM_CORE_REGISTRY_APEX_REGISTRY_HPP
/**
 * @file ApexRegistry.hpp
 * @brief Default registry implementation using vector-based storage.
 *
 * ApexRegistry is the default implementation of RegistryBase. It provides:
 *   - Component tracking with full UID resolution
 *   - Task registry linking tasks to their owning components
 *   - Data registry with byte-level access for logging
 *   - O(n) linear scans for queries (suitable for typical component counts)
 *
 * For applications with many components or requiring faster lookups,
 * users can create custom RegistryBase implementations with hash-based
 * indexing or other optimizations.
 *
 * @see RegistryBase for the interface documentation.
 */

#include "src/system/core/components/registry/apex/inc/RegistryBase.hpp"

#include <vector>

namespace system_core {
namespace registry {

/* ----------------------------- ApexRegistry ----------------------------- */

/**
 * @class ApexRegistry
 * @brief Default registry implementation with vector-based storage.
 *
 * Uses std::vector for storage with O(n) linear scan queries.
 * Suitable for typical Apex applications with tens of components.
 *
 * @note Registration methods are NOT RT-safe (may allocate).
 * @note Query methods are RT-safe after freeze().
 */
class ApexRegistry : public RegistryBase {
public:
  /** @brief Component label for diagnostics. */
  [[nodiscard]] const char* label() const noexcept override { return "REGISTRY"; }

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  ApexRegistry() noexcept;

  /** @brief Destructor. */
  ~ApexRegistry() override = default;

  /* ----------------------------- Registration (NOT RT-safe) ----------------------------- */

  /**
   * @brief Register a component.
   * @param fullUid Component's full UID (componentId << 8 | instanceIndex).
   * @param name Human-readable component name.
   * @param component Pointer to component (not owned, for model lookups).
   * @param type Component type classification (default: CORE).
   * @return Status::SUCCESS or error code.
   * @note NOT RT-safe: May allocate.
   */
  [[nodiscard]] Status
  registerComponent(std::uint32_t fullUid, const char* name, void* component = nullptr,
                    system_component::ComponentType type =
                        system_component::ComponentType::CORE) noexcept override;

  /**
   * @brief Register a task.
   * @param fullUid Owner component's full UID.
   * @param taskUid Task UID within component.
   * @param name Human-readable task name.
   * @param task Pointer to task (not owned).
   * @return Status::SUCCESS or error code.
   * @note NOT RT-safe: May allocate.
   */
  [[nodiscard]] Status registerTask(std::uint32_t fullUid, std::uint8_t taskUid, const char* name,
                                    schedulable::SchedulableTask* task) noexcept override;

  /**
   * @brief Register a data block.
   * @param fullUid Owner component's full UID.
   * @param category Data category (STATE, TUNABLE_PARAM, etc.).
   * @param name Human-readable data name.
   * @param ptr Pointer to data (not owned, read-only access).
   * @param size Size of data in bytes.
   * @return Status::SUCCESS or error code.
   * @note NOT RT-safe: May allocate.
   */
  [[nodiscard]] Status registerData(std::uint32_t fullUid, data::DataCategory category,
                                    const char* name, const void* ptr,
                                    std::size_t size) noexcept override;

  /**
   * @brief Freeze the registry for runtime queries.
   * @return Status::SUCCESS or error code.
   * @note Call after all registrations, before run phase.
   * @note After freeze(), registration methods return ERROR_ALREADY_FROZEN.
   * @note After freeze(), query methods become available.
   */
  [[nodiscard]] Status freeze() noexcept override;

  /** @brief Check if registry is frozen. */
  [[nodiscard]] bool isFrozen() const noexcept override { return frozen_; }

  /* ----------------------------- Component Queries (RT-safe after freeze)
   * ----------------------------- */

  /**
   * @brief Get component entry by full UID.
   * @param fullUid Component's full UID.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze(): O(n) scan.
   */
  [[nodiscard]] ComponentEntry* getComponentEntry(std::uint32_t fullUid) noexcept override;

  /**
   * @brief Get component entry by full UID (const).
   * @param fullUid Component's full UID.
   * @return Pointer to entry, or nullptr if not found.
   */
  [[nodiscard]] const ComponentEntry*
  getComponentEntry(std::uint32_t fullUid) const noexcept override;

  /**
   * @brief Get all registered components.
   * @return Span over all component entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] apex::compat::span<ComponentEntry> getAllComponents() noexcept override;

  /**
   * @brief Get all registered components (const).
   * @return Span over all component entries.
   */
  [[nodiscard]] apex::compat::span<const ComponentEntry> getAllComponents() const noexcept override;

  /**
   * @brief Get number of registered components.
   * @return Component count.
   */
  [[nodiscard]] std::size_t componentCount() const noexcept override { return components_.size(); }

  /* ----------------------------- Task Queries (RT-safe after freeze) -----------------------------
   */

  /**
   * @brief Get task entry by owner UID and task UID.
   * @param fullUid Owner component's full UID.
   * @param taskUid Task UID within component.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze(): O(n) scan.
   */
  [[nodiscard]] TaskEntry* getTask(std::uint32_t fullUid, std::uint8_t taskUid) noexcept override;

  /**
   * @brief Get all registered tasks.
   * @return Span over all task entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] apex::compat::span<TaskEntry> getAllTasks() noexcept override;

  /**
   * @brief Get all registered tasks (const).
   * @return Span over all task entries.
   */
  [[nodiscard]] apex::compat::span<const TaskEntry> getAllTasks() const noexcept override;

  /**
   * @brief Get number of registered tasks.
   * @return Task count.
   */
  [[nodiscard]] std::size_t taskCount() const noexcept override { return tasks_.size(); }

  /* ----------------------------- Data Queries (RT-safe after freeze) -----------------------------
   */

  /**
   * @brief Get data entry by owner UID and category.
   * @param fullUid Owner component's full UID.
   * @param category Data category.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze(): O(n) scan.
   * @note If multiple data blocks share fullUid+category, returns first match.
   */
  [[nodiscard]] DataEntry* getData(std::uint32_t fullUid,
                                   data::DataCategory category) noexcept override;

  /**
   * @brief Get data entry by owner UID, category, and name.
   * @param fullUid Owner component's full UID.
   * @param category Data category.
   * @param name Data name.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze(): O(n) scan.
   */
  [[nodiscard]] DataEntry* getData(std::uint32_t fullUid, data::DataCategory category,
                                   const char* name) noexcept override;

  /**
   * @brief Get all registered data entries.
   * @return Span over all data entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] apex::compat::span<DataEntry> getAllData() noexcept override;

  /**
   * @brief Get all registered data entries (const).
   * @return Span over all data entries.
   */
  [[nodiscard]] apex::compat::span<const DataEntry> getAllData() const noexcept override;

  /**
   * @brief Get number of registered data entries.
   * @return Data entry count.
   */
  [[nodiscard]] std::size_t dataCount() const noexcept override { return data_.size(); }

  /* ----------------------------- Component Data Access ----------------------------- */

  /**
   * @brief Get tasks for a specific component.
   * @param fullUid Component's full UID.
   * @param outTasks Output buffer for task pointers.
   * @param maxTasks Size of output buffer.
   * @return Number of tasks written to buffer.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] std::size_t getTasksForComponent(std::uint32_t fullUid, TaskEntry** outTasks,
                                                 std::size_t maxTasks) noexcept override;

  /**
   * @brief Get data entries for a specific component.
   * @param fullUid Component's full UID.
   * @param outData Output buffer for data pointers.
   * @param maxData Size of output buffer.
   * @return Number of entries written to buffer.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] std::size_t getDataForComponent(std::uint32_t fullUid, DataEntry** outData,
                                                std::size_t maxData) noexcept override;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get total registered data size across all entries.
   * @return Sum of all data entry sizes in bytes.
   */
  [[nodiscard]] std::size_t totalDataSize() const noexcept override;

  /* ----------------------------- Logging ----------------------------- */

  /**
   * @brief Initialize registry log file.
   * @param logDir Directory for log file.
   * @note Creates "registry.log" in the specified directory.
   */
  void initRegistryLog(const std::filesystem::path& logDir) noexcept override;

  /**
   * @brief Log comprehensive registry contents.
   * @note Call after freeze() to log complete database snapshot.
   *
   * Outputs:
   *   - Component summary (fullUid, name, task count, data count)
   *   - Per-component task details (taskUid, name, frequency)
   *   - Per-component data details (category, name, size, address)
   *   - Statistics summary
   */
  void logRegistryContents() noexcept override;

  /* ----------------------------- Export ----------------------------- */

  /**
   * @brief Export registry database to binary RDAT format.
   * @param dbDir Directory for database file.
   * @return Status::SUCCESS or error code.
   * @note Creates "registry.rdat" in the specified directory.
   */
  [[nodiscard]] Status exportDatabase(const std::filesystem::path& dbDir) noexcept override;

protected:
  /* ----------------------------- Lifecycle Hooks ----------------------------- */

  /**
   * @brief Initialize the registry.
   * @return Status code.
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

  /**
   * @brief Reset the registry (clears all entries, unfreezes).
   */
  void doReset() noexcept override;

private:
  /* ----------------------------- Internal Helpers ----------------------------- */

  /**
   * @brief Find component index by full UID.
   * @param fullUid Component's full UID.
   * @return Index in components_, or SIZE_MAX if not found.
   */
  [[nodiscard]] std::size_t findComponentIndex(std::uint32_t fullUid) const noexcept;

  /**
   * @brief Link a task to its owning component.
   * @param compIdx Component index.
   * @param taskIdx Task index.
   * @return true if linked successfully.
   */
  bool linkTaskToComponent(std::size_t compIdx, std::size_t taskIdx) noexcept;

  /**
   * @brief Link a data entry to its owning component.
   * @param compIdx Component index.
   * @param dataIdx Data index.
   * @return true if linked successfully.
   */
  bool linkDataToComponent(std::size_t compIdx, std::size_t dataIdx) noexcept;

  /* ----------------------------- Data Members ----------------------------- */

  std::vector<ComponentEntry> components_; ///< Registered components.
  std::vector<TaskEntry> tasks_;           ///< Registered tasks.
  std::vector<DataEntry> data_;            ///< Registered data entries.
  bool frozen_{false};                     ///< True after freeze() called.
};

} // namespace registry
} // namespace system_core

#endif // APEX_SYSTEM_CORE_REGISTRY_APEX_REGISTRY_HPP
