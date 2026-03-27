#ifndef APEX_SYSTEM_CORE_REGISTRY_REGISTRYBASE_HPP
#define APEX_SYSTEM_CORE_REGISTRY_REGISTRYBASE_HPP
/**
 * @file RegistryBase.hpp
 * @brief Abstract base class for component/task/data registries.
 *
 * RegistryBase defines the interface for registry implementations. Users can
 * create custom registry implementations with different storage backends,
 * indexing strategies, or query optimizations.
 *
 * Lifecycle:
 *   1. Registration phase: Call register*() methods during component init
 *   2. Freeze: Call freeze() before entering run phase
 *   3. Query phase: Use get*() methods for RT-safe lookups
 *
 * RT-Safety:
 *   - Registration methods: NOT RT-safe (may allocate)
 *   - freeze(): NOT RT-safe (validates and finalizes)
 *   - Query methods: RT-safe after freeze() (fixed storage)
 */

#include "src/system/core/components/registry/apex/inc/RegistryData.hpp"
#include "src/system/core/components/registry/apex/inc/RegistryStatus.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/CoreComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/IComponentResolver.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstdint>
#include <filesystem>

namespace system_core {
namespace registry {

/* ----------------------------- RegistryBase ----------------------------- */

/**
 * @class RegistryBase
 * @brief Abstract base class for registry implementations.
 *
 * Defines the interface for component, task, and data registration and queries.
 * Derived classes implement the actual storage and lookup mechanisms.
 *
 * @note Registration methods are NOT RT-safe (may allocate).
 * @note Query methods are RT-safe after freeze().
 */
class RegistryBase : public system_component::CoreComponentBase,
                     public system_component::IComponentResolver {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (3 = Registry, system component range 1-100).
  static constexpr std::uint16_t COMPONENT_ID = 3;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Registry";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. */
  RegistryBase() noexcept { setConfigured(true); }

  /** @brief Virtual destructor. */
  ~RegistryBase() override = default;

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
  [[nodiscard]] virtual Status registerComponent(
      std::uint32_t fullUid, const char* name, void* component = nullptr,
      system_component::ComponentType type = system_component::ComponentType::CORE) noexcept = 0;

  /**
   * @brief Register a task.
   * @param fullUid Owner component's full UID.
   * @param taskUid Task UID within component.
   * @param name Human-readable task name.
   * @param task Pointer to task (not owned).
   * @return Status::SUCCESS or error code.
   * @note NOT RT-safe: May allocate.
   */
  [[nodiscard]] virtual Status registerTask(std::uint32_t fullUid, std::uint8_t taskUid,
                                            const char* name,
                                            schedulable::SchedulableTask* task) noexcept = 0;

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
  [[nodiscard]] virtual Status registerData(std::uint32_t fullUid, data::DataCategory category,
                                            const char* name, const void* ptr,
                                            std::size_t size) noexcept = 0;

  /**
   * @brief Freeze the registry for runtime queries.
   * @return Status::SUCCESS or error code.
   * @note Call after all registrations, before run phase.
   * @note After freeze(), registration methods return ERROR_ALREADY_FROZEN.
   */
  [[nodiscard]] virtual Status freeze() noexcept = 0;

  /**
   * @brief Check if registry is frozen.
   * @return true if freeze() has been called.
   */
  [[nodiscard]] virtual bool isFrozen() const noexcept = 0;

  /* ----------------------------- Component Queries (RT-safe after freeze)
   * ----------------------------- */

  /**
   * @brief Get component entry by full UID.
   * @param fullUid Component's full UID.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual ComponentEntry* getComponentEntry(std::uint32_t fullUid) noexcept = 0;

  /**
   * @brief Get component entry by full UID (const).
   * @param fullUid Component's full UID.
   * @return Pointer to entry, or nullptr if not found.
   */
  [[nodiscard]] virtual const ComponentEntry*
  getComponentEntry(std::uint32_t fullUid) const noexcept = 0;

  /**
   * @brief Get all registered components.
   * @return Span over all component entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual apex::compat::span<ComponentEntry> getAllComponents() noexcept = 0;

  /**
   * @brief Get all registered components (const).
   * @return Span over all component entries.
   */
  [[nodiscard]] virtual apex::compat::span<const ComponentEntry>
  getAllComponents() const noexcept = 0;

  /**
   * @brief Get number of registered components.
   * @return Component count.
   */
  [[nodiscard]] virtual std::size_t componentCount() const noexcept = 0;

  /* ----------------------------- IComponentResolver Interface ----------------------------- */

  /**
   * @brief Get component pointer by full UID (IComponentResolver implementation).
   * @param fullUid Component's full UID.
   * @return Pointer to SystemComponentBase, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] system_component::SystemComponentBase*
  getComponent(std::uint32_t fullUid) noexcept override {
    ComponentEntry* entry = getComponentEntry(fullUid);
    if (entry == nullptr || entry->component == nullptr) {
      return nullptr;
    }
    return static_cast<system_component::SystemComponentBase*>(entry->component);
  }

  /**
   * @brief Get component pointer by full UID (const, IComponentResolver implementation).
   * @param fullUid Component's full UID.
   * @return Const pointer to SystemComponentBase, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] const system_component::SystemComponentBase*
  getComponent(std::uint32_t fullUid) const noexcept override {
    const ComponentEntry* entry = getComponentEntry(fullUid);
    if (entry == nullptr || entry->component == nullptr) {
      return nullptr;
    }
    return static_cast<const system_component::SystemComponentBase*>(entry->component);
  }

  /**
   * @brief Update component pointer for a registered entry.
   * @param fullUid Component's full UID.
   * @param newComp New component pointer.
   * @return true if entry found and updated, false if not found.
   * @note Used during component hot-swap to point registry at new instance.
   */
  bool updateComponent(std::uint32_t fullUid,
                       system_component::SystemComponentBase* newComp) noexcept {
    ComponentEntry* entry = getComponentEntry(fullUid);
    if (entry == nullptr) {
      return false;
    }
    entry->component = newComp;
    return true;
  }

  /* ----------------------------- Task Queries (RT-safe after freeze) -----------------------------
   */

  /**
   * @brief Get task entry by owner UID and task UID.
   * @param fullUid Owner component's full UID.
   * @param taskUid Task UID within component.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual TaskEntry* getTask(std::uint32_t fullUid,
                                           std::uint8_t taskUid) noexcept = 0;

  /**
   * @brief Get all registered tasks.
   * @return Span over all task entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual apex::compat::span<TaskEntry> getAllTasks() noexcept = 0;

  /**
   * @brief Get all registered tasks (const).
   * @return Span over all task entries.
   */
  [[nodiscard]] virtual apex::compat::span<const TaskEntry> getAllTasks() const noexcept = 0;

  /**
   * @brief Get number of registered tasks.
   * @return Task count.
   */
  [[nodiscard]] virtual std::size_t taskCount() const noexcept = 0;

  /* ----------------------------- Data Queries (RT-safe after freeze) -----------------------------
   */

  /**
   * @brief Get data entry by owner UID and category.
   * @param fullUid Owner component's full UID.
   * @param category Data category.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual DataEntry* getData(std::uint32_t fullUid,
                                           data::DataCategory category) noexcept = 0;

  /**
   * @brief Get data entry by owner UID, category, and name.
   * @param fullUid Owner component's full UID.
   * @param category Data category.
   * @param name Data name.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual DataEntry* getData(std::uint32_t fullUid, data::DataCategory category,
                                           const char* name) noexcept = 0;

  /**
   * @brief Get all registered data entries.
   * @return Span over all data entries.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual apex::compat::span<DataEntry> getAllData() noexcept = 0;

  /**
   * @brief Get all registered data entries (const).
   * @return Span over all data entries.
   */
  [[nodiscard]] virtual apex::compat::span<const DataEntry> getAllData() const noexcept = 0;

  /**
   * @brief Get number of registered data entries.
   * @return Data entry count.
   */
  [[nodiscard]] virtual std::size_t dataCount() const noexcept = 0;

  /* ----------------------------- Component Data Access ----------------------------- */

  /**
   * @brief Get tasks for a specific component.
   * @param fullUid Component's full UID.
   * @param outTasks Output buffer for task pointers.
   * @param maxTasks Size of output buffer.
   * @return Number of tasks written to buffer.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual std::size_t getTasksForComponent(std::uint32_t fullUid,
                                                         TaskEntry** outTasks,
                                                         std::size_t maxTasks) noexcept = 0;

  /**
   * @brief Get data entries for a specific component.
   * @param fullUid Component's full UID.
   * @param outData Output buffer for data pointers.
   * @param maxData Size of output buffer.
   * @return Number of entries written to buffer.
   * @note RT-safe after freeze().
   */
  [[nodiscard]] virtual std::size_t getDataForComponent(std::uint32_t fullUid, DataEntry** outData,
                                                        std::size_t maxData) noexcept = 0;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get total registered data size across all entries.
   * @return Sum of all data entry sizes in bytes.
   */
  [[nodiscard]] virtual std::size_t totalDataSize() const noexcept = 0;

  /* ----------------------------- Logging ----------------------------- */

  /**
   * @brief Initialize registry log file.
   * @param logDir Directory for log file.
   */
  virtual void initRegistryLog(const std::filesystem::path& logDir) noexcept = 0;

  /**
   * @brief Log comprehensive registry contents.
   * @note Call after freeze() to log complete database snapshot.
   */
  virtual void logRegistryContents() noexcept = 0;

  /* ----------------------------- Export ----------------------------- */

  /**
   * @brief Export registry database to binary RDAT format.
   * @param dbDir Directory for database file (e.g., .apex_fs/db/).
   * @return Status::SUCCESS or error code.
   * @note NOT RT-safe: Performs file I/O.
   * @note Call after freeze() to export complete database.
   * @note Creates "registry.rdat" in the specified directory.
   */
  [[nodiscard]] virtual Status exportDatabase(const std::filesystem::path& dbDir) noexcept = 0;
};

} // namespace registry
} // namespace system_core

#endif // APEX_SYSTEM_CORE_REGISTRY_REGISTRYBASE_HPP
