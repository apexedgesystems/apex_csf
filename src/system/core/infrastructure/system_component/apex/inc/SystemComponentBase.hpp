#ifndef APEX_SYSTEM_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_BASE_HPP
/**
 * @file SystemComponentBase.hpp
 * @brief Minimal abstract base for system components (status, init(), error context).
 *
 * Design:
 *  - Non-templated base for polymorphic component management.
 *  - Provides lifecycle (init, reset), status tracking, and error context.
 *  - Labels and error strings are literals for zero-allocation diagnostics.
 *  - Template method pattern: init() is non-virtual and calls doInit() hook.
 *  - Executive queries status()/lastError() to log component lifecycle.
 *
 * RT Lifecycle Constraints:
 *  - Construct and call init() on all components BEFORE entering RT phase.
 *  - status(), isInitialized(), isConfigured(), label(), lastError() are RT-safe.
 *  - Never destroy components during RT execution.
 *  - init(), reset() are boot-time only; do not call from RT context.
 *
 * Configuration Requirement:
 *  - init() requires isConfigured() == true (ERROR_NOT_CONFIGURED otherwise).
 *  - For SystemComponent<TParams>, load() sets isConfigured().
 *  - For simple components, call setConfigured(true) before init().
 */

#include "src/system/core/infrastructure/system_component/apex/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for schedulable infrastructure.
namespace system_core {
namespace schedulable {
class SchedulableTask;
class SequenceGroup;
} // namespace schedulable
} // namespace system_core

// Forward declaration for component logging
namespace logs {
class SystemLog;
} // namespace logs

namespace system_core {
namespace system_component {

// Forward declaration for internal messaging
class IInternalBus;

/* ----------------------------- Constants ----------------------------- */

/// Maximum length for component name (including null terminator).
constexpr std::size_t COMPONENT_NAME_MAX_LEN = 32;

/// Invalid full UID (returned on registration failure).
constexpr std::uint32_t INVALID_COMPONENT_UID = 0xFFFFFFFF;

/// Maximum data descriptors per component.
constexpr std::size_t MAX_DATA_PER_COMPONENT = 16;

/* ----------------------------- DataDescriptor ----------------------------- */

/**
 * @struct DataDescriptor
 * @brief Describes a data block for registry integration.
 *
 * Components register their data during doInit() and the executive reads these
 * descriptors to populate the unified registry. This keeps components decoupled
 * from the registry implementation.
 */
struct DataDescriptor {
  data::DataCategory category{}; ///< Data category.
  const char* name{nullptr};     ///< Human-readable name.
  const void* ptr{nullptr};      ///< Pointer to data (const for read-only access).
  std::size_t size{0};           ///< Size in bytes.
};

/* ----------------------------- SystemComponentBase ----------------------------- */

/**
 * @class SystemComponentBase
 * @brief Non-templated base providing status, init(), reset(), and error context.
 *
 * Derived classes must implement:
 *  - uint8_t doInit() noexcept; (initialization logic, called by init())
 *
 * Optionally override:
 *  - void doReset() noexcept; (cleanup logic, called by reset())
 *  - const char* label() const noexcept; (component name for diagnostics)
 *
 * @note RT-safe queries: status(), isInitialized(), isConfigured(), label(), lastError().
 * @note NOT RT-safe: init(), reset() (may allocate, do I/O).
 */
class SystemComponentBase : public IComponent {
public:
  /** @brief Default constructor. */
  SystemComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  virtual ~SystemComponentBase() = default;

  // Non-copyable (components have identity)
  SystemComponentBase(const SystemComponentBase&) = delete;
  SystemComponentBase& operator=(const SystemComponentBase&) = delete;

  // Movable (for container storage)
  SystemComponentBase(SystemComponentBase&&) noexcept = default;
  SystemComponentBase& operator=(SystemComponentBase&&) noexcept = default;

  /**
   * @brief Boot-time initialization (template method pattern).
   * @return Status code (SUCCESS on success).
   * @note NOT RT-safe: May allocate, perform I/O, or block.
   * @note FAILS with ERROR_NOT_CONFIGURED if isConfigured() is false.
   * @note Idempotent: returns SUCCESS if already initialized.
   */
  [[nodiscard]] std::uint8_t init() noexcept override {
    if (!configured_) {
      setStatus(static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
      setLastError("init() called before configuration");
      return status();
    }
    if (initialized_) {
      return status(); // Idempotent
    }
    preInit(); // Hook for derived classes (e.g., apply staged → active)
    const std::uint8_t RESULT = doInit();
    setStatus(RESULT);
    if (RESULT == static_cast<std::uint8_t>(Status::SUCCESS)) {
      initialized_ = true;
      setLastError(nullptr);
    }
    return RESULT;
  }

  /**
   * @brief Reset component state for re-initialization.
   * @note NOT RT-safe: May deallocate or perform cleanup.
   * @note Calls doReset() hook, then clears initialized flag.
   * @note Does NOT clear configured flag (params remain loaded).
   */
  void reset() noexcept override {
    doReset();
    initialized_ = false;
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    setLastError(nullptr);
  }

  /**
   * @brief Get component type identifier (defined by derived class).
   * @return 16-bit component type ID.
   * @note UID ranges: 0 = Executive, 1-100 = System components, 101+ = Models.
   * @note Must be unique per component type. Multiple instances share the same ID.
   */
  [[nodiscard]] virtual std::uint16_t componentId() const noexcept override = 0;

  /**
   * @brief Get component name for collision detection.
   * @return Null-terminated string (max COMPONENT_NAME_MAX_LEN chars).
   * @note Used to distinguish multi-instance (same ID + same name) from
   *       collision (same ID + different name).
   */
  [[nodiscard]] virtual const char* componentName() const noexcept override = 0;

  /**
   * @brief Get component type classification.
   * @return Component type (EXECUTIVE, CORE, MODEL, SUPPORT).
   * @note Default is CORE; override in derived classes (e.g., SimModelBase returns MODEL).
   * @note Used for logging directory selection and registry organization.
   */
  [[nodiscard]] virtual ComponentType componentType() const noexcept override {
    return ComponentType::CORE;
  }

  /**
   * @brief Set instance index and compute full UID.
   * @param instanceIdx Instance index assigned by executive (0 for first, 1 for second, etc.).
   * @note Called by executive during registration.
   * @note Full UID = (componentId << 8) | instanceIndex.
   */
  void setInstanceIndex(std::uint8_t instanceIdx) noexcept {
    instanceIndex_ = instanceIdx;
    fullUid_ = (static_cast<std::uint32_t>(componentId()) << 8) | instanceIdx;
    registered_ = true;
  }

  /** @brief Get full UID (componentId << 8 | instanceIndex). Valid after registration. */
  [[nodiscard]] std::uint32_t fullUid() const noexcept override { return fullUid_; }

  /** @brief Get instance index (0 for first instance, 1 for second, etc.). */
  [[nodiscard]] std::uint8_t instanceIndex() const noexcept override { return instanceIndex_; }

  /** @brief Check if component is registered with executive. */
  [[nodiscard]] bool isRegistered() const noexcept override { return registered_; }

  /// Common component opcodes (0x0080-0x00FF range, below system 0x0100+).
  enum ComponentOpcode : std::uint16_t {
    GET_COMMAND_COUNT = 0x0080,   ///< Get total commands received.
    GET_STATUS_INFO = 0x0081,     ///< Get component status summary.
    RESET_COMMAND_COUNT = 0x0082, ///< Reset command counter.
  };

  /**
   * @brief Handle incoming APROTO command.
   * @param opcode Command opcode (component-specific interpretation).
   * @param payload Command payload bytes (may be empty).
   * @param response Output buffer for response payload (caller provides capacity).
   * @return Status code: 0=SUCCESS (ACK), nonzero=error code (NAK).
   * @note RT-safe if derived implementation is RT-safe.
   * @note Default handles common opcodes (0x0080-0x00FF), returns 1 for unknown.
   * @note Response payload is optional; leave empty for simple ACK/NAK.
   *
   * Standard system opcodes (0x0000-0x007F) are handled by the interface.
   * Common component opcodes (0x0080-0x00FF) are handled by base class.
   * Component-specific opcodes (0x0100+) should be handled by derived classes.
   *
   * Derived classes should call base handleCommand for unrecognized opcodes.
   */
  [[nodiscard]] virtual std::uint8_t handleCommand(std::uint16_t opcode,
                                                   apex::compat::rospan<std::uint8_t> payload,
                                                   std::vector<std::uint8_t>& response) noexcept;

  /**
   * @brief Get total commands received by this component.
   * @return Command count since startup or last reset.
   */
  [[nodiscard]] std::uint64_t commandCount() const noexcept { return commandCount_; }

  /**
   * @brief Get count of rejected/failed commands.
   * @return Rejected command count since startup or last reset.
   */
  [[nodiscard]] std::uint64_t rejectedCommandCount() const noexcept {
    return rejectedCommandCount_;
  }

  /**
   * @brief Increment command counter (called automatically by processCommandQueue).
   * @note Also call when handling direct commands outside queue processing.
   */
  void incrementCommandCount() noexcept { ++commandCount_; }

  /**
   * @brief Increment rejected command counter.
   * @note Call when command handling returns non-zero status.
   */
  void incrementRejectedCount() noexcept { ++rejectedCommandCount_; }

  /**
   * @brief Set internal message bus for component-to-component messaging.
   * @param bus Pointer to internal bus (owned by interface).
   * @note Called by executive during component registration.
   * @note Enables components to send internal commands/telemetry.
   */
  void setInternalBus(IInternalBus* bus) noexcept { internalBus_ = bus; }

  /**
   * @brief Get internal message bus.
   * @return Pointer to internal bus, or nullptr if not set.
   * @note RT-safe.
   * @note Use to send commands to other components or telemetry to external interface.
   */
  [[nodiscard]] IInternalBus* internalBus() const noexcept { return internalBus_; }

  /**
   * @brief Generate TPRM filename from fullUid.
   * @param fullUid Full component UID (componentId << 8 | instanceIndex).
   * @return Filename in format "{fullUid:06x}.tprm" (e.g., "000100.tprm" for scheduler).
   * @note Static helper for executive and component use.
   */
  static std::string tprmFilename(std::uint32_t fullUid) noexcept;

  /**
   * @brief Load tunable parameters from TPRM directory.
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false if load failed (component uses defaults).
   * @note Uses componentId() to generate filename: "{componentId:06x}.tprm".
   * @note Default implementation does nothing (component has no TPRM).
   * @note Override in derived class to load component-specific TPRM.
   * @note NOT RT-safe: File I/O.
   */
  virtual bool loadTprm(const std::filesystem::path& /*tprmDir*/) noexcept { return true; }

  /**
   * @brief Post-init hook called after all components are registered and the
   *        internal bus is wired.
   *
   * Override this to issue internal commands to other components during startup.
   * The internal bus (postInternalCommand) is available when this is called.
   * All queued commands are drained before runtime starts.
   *
   * @note NOT RT-safe. Called once during executive init.
   * @note Default implementation does nothing.
   */
  virtual void onBusReady() noexcept {}

  /**
   * @brief Look up a schedulable task by UID.
   * @param uid Task identifier within this component.
   * @return Pointer to SchedulableTask, or nullptr if not found.
   * @note Override in components that own tasks (e.g., SimModelBase).
   * @note Default returns nullptr (component has no tasks).
   */
  [[nodiscard]] virtual schedulable::SchedulableTask* taskByUid(std::uint8_t /*uid*/) noexcept {
    return nullptr;
  }

  /**
   * @brief Get a sequence group by index.
   * @param idx Group index.
   * @return Pointer to SequenceGroup, or nullptr if not found.
   * @note Override in components that use sequencing.
   * @note Default returns nullptr (component has no sequence groups).
   */
  [[nodiscard]] virtual schedulable::SequenceGroup* sequenceGroup(std::uint8_t /*idx*/) noexcept {
    return nullptr;
  }

  /** @brief Get number of registered data descriptors. */
  [[nodiscard]] std::size_t dataCount() const noexcept { return dataCount_; }

  /**
   * @brief Get data descriptor by index.
   * @param idx Descriptor index.
   * @return Pointer to DataDescriptor, or nullptr if out of range.
   * @note RT-safe after init().
   */
  [[nodiscard]] const DataDescriptor* dataDescriptor(std::size_t idx) const noexcept {
    if (idx < dataCount_) {
      return &data_[idx];
    }
    return nullptr;
  }

  /**
   * @brief Component label (string literal).
   * @return Static label for diagnostics.
   * @note RT-safe: Returns pointer to static string.
   */
  [[nodiscard]] virtual const char* label() const noexcept override { return "SYSTEM_COMPONENT"; }

  /**
   * @brief Last operation status code.
   * @return Current status.
   * @note RT-safe: Simple member read.
   */
  [[nodiscard]] std::uint8_t status() const noexcept override { return status_; }

  /**
   * @brief Last error context (string literal or nullptr).
   * @return Error description, or nullptr if no error.
   * @note RT-safe: Simple pointer read.
   * @note Executive queries this to log detailed error context.
   */
  [[nodiscard]] const char* lastError() const noexcept { return lastError_; }

  /**
   * @brief True after successful init().
   * @note RT-safe: Simple member read.
   */
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /**
   * @brief True if component has been configured (params loaded or set ready).
   * @note RT-safe: Simple member read.
   * @note For SystemComponent<TParams>, load() sets this.
   * @note For simple components, call setConfigured(true) before init().
   */
  [[nodiscard]] bool isConfigured() const noexcept { return configured_; }

  /**
   * @brief Lock component for runtime update (reflash, TPRM reload).
   * @note While locked: scheduler skips tasks, interface NAKs commands, action engine skips
   * watchpoints.
   * @note NOT RT-safe: Control-plane only (called by executive on CMD_LOCK_COMPONENT).
   */
  void lock() noexcept { locked_ = true; }

  /**
   * @brief Unlock component after runtime update completes.
   * @note NOT RT-safe: Control-plane only (called by executive on CMD_UNLOCK_COMPONENT).
   */
  void unlock() noexcept { locked_ = false; }

  /**
   * @brief Check if component is locked for runtime update.
   * @return true if locked (scheduler should skip, interface should NAK).
   * @note RT-safe: Simple member read.
   */
  [[nodiscard]] bool isLocked() const noexcept { return locked_; }

  /**
   * @brief Initialize dedicated component log file.
   * @param logDir Directory for component log files (e.g., ".apex_fs/logs/core").
   * @note Creates log file named "{componentName}_{instanceIndex}.log" in the specified directory.
   * @note Must be called AFTER setInstanceIndex() (needs instance index for filename).
   * @note NOT RT-safe: File I/O.
   */
  void initComponentLog(const std::filesystem::path& logDir) noexcept;

protected:
  /**
   * @brief Set dedicated component log (for operational logging).
   * @param log Shared log instance (components create their own).
   * @note NOT RT-safe: Should be called during init phase.
   * @note Separate from system-wide log; each component has its own log file.
   * @note Example: scheduler.log, filesystem.log
   */
  void setComponentLog(std::shared_ptr<logs::SystemLog> log) noexcept {
    componentLog_ = std::move(log);
  }

  /**
   * @brief Get dedicated component log (may be nullptr).
   * @return Pointer to component log, or nullptr if none set.
   * @note Use for detailed operational logging within the component.
   */
  [[nodiscard]] logs::SystemLog* componentLog() const noexcept { return componentLog_.get(); }

  /**
   * @brief Initialization hook (pure virtual).
   * @return Status code (SUCCESS on success).
   * @note Called by init() after configuration check passes.
   * @note Derived classes implement actual initialization logic here.
   * @note On failure, call setLastError() with context before returning.
   */
  [[nodiscard]] virtual std::uint8_t doInit() noexcept = 0;

  /**
   * @brief Pre-init hook (optional override).
   * @note Called by init() after configuration check, before doInit().
   * @note Override to perform pre-initialization setup (e.g., apply staged params).
   */
  virtual void preInit() noexcept {}

  /**
   * @brief Reset hook (optional override).
   * @note Called by reset() before clearing initialized flag.
   * @note Override to perform cleanup (release resources, reset state).
   */
  virtual void doReset() noexcept {}

  /** @brief Update last status (for derived classes). */
  void setStatus(std::uint8_t s) noexcept { status_ = s; }

  /**
   * @brief Set error context (string literal).
   * @param err Error description (must be static string or nullptr).
   * @note RT-safe: Just stores pointer.
   */
  void setLastError(const char* err) noexcept { lastError_ = err; }

  /**
   * @brief Mark component as configured (ready for init).
   * @param v Configuration state.
   * @note For SystemComponent<TParams>, load() calls this on success.
   * @note For simple components, call setConfigured(true) before init().
   */
  void setConfigured(bool v) noexcept { configured_ = v; }

  /**
   * @brief Register a data block for registry integration.
   * @param category Data category.
   * @param name Human-readable name (must be static string).
   * @param ptr Pointer to data (const for read-only registry access).
   * @param size Size in bytes.
   * @return true on success, false if capacity exceeded.
   * @note Call during doInit() to register component data.
   */
  bool registerData(data::DataCategory category, const char* name, const void* ptr,
                    std::size_t size) noexcept {
    if (dataCount_ >= MAX_DATA_PER_COMPONENT) {
      return false;
    }
    auto& entry = data_[dataCount_++];
    entry.category = category;
    entry.name = name;
    entry.ptr = ptr;
    entry.size = size;
    return true;
  }

private:
  std::shared_ptr<logs::SystemLog> componentLog_{nullptr};
  IInternalBus* internalBus_{nullptr};
  std::uint8_t status_{static_cast<std::uint8_t>(Status::SUCCESS)};
  const char* lastError_{nullptr};
  std::uint32_t fullUid_{INVALID_COMPONENT_UID};
  std::uint8_t instanceIndex_{0};
  bool configured_{false};
  bool initialized_{false};
  bool registered_{false};
  bool locked_{false};

  // Command statistics (system-wide tracking).
  std::uint64_t commandCount_{0};
  std::uint64_t rejectedCommandCount_{0};

  std::array<DataDescriptor, MAX_DATA_PER_COMPONENT> data_{};
  std::size_t dataCount_{0};
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_BASE_HPP
