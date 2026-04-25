#ifndef APEX_SYSTEM_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_BASE_HPP
/**
 * @file SystemComponentBase.hpp
 * @brief POSIX-tier IComponent base with TPRM, bus, logging, command handling.
 *
 * Layers POSIX-specific component capabilities on top of ComponentCore:
 *  - TPRM file loading (filesystem-backed parameter storage)
 *  - Internal bus access for component-to-component messaging
 *  - Component log files (one per component instance)
 *  - Data descriptors for registry integration
 *  - APROTO command handling
 *  - Configured / locked semantics for runtime updates
 *
 * Configuration Requirement:
 *  - init() requires isConfigured() == true (ERROR_NOT_CONFIGURED otherwise).
 *  - For SystemComponent<TParams>, load() sets isConfigured().
 *  - For simple components, call setConfigured(true) before init().
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/core/inc/ComponentCore.hpp"
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
 * @brief POSIX-tier component base layered on ComponentCore.
 *
 * Derived classes must implement (from ComponentCore / IComponent):
 *  - componentId(), componentName()
 *  - doInit() (initialization logic, called by init())
 *
 * Optionally override:
 *  - componentType() (default returns CORE)
 *  - label() (default returns "SYSTEM_COMPONENT")
 *  - doReset() (default no-op)
 *  - preInit() (default no-op; called after configured check passes)
 *
 * @note RT-safe queries: status(), isInitialized(), isConfigured(), label(), lastError().
 * @note NOT RT-safe: init(), reset() (may allocate, do I/O).
 */
class SystemComponentBase : public ComponentCore {
public:
  /** @brief Default constructor. */
  SystemComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~SystemComponentBase() override = default;

  // Non-copyable (components have identity)
  SystemComponentBase(const SystemComponentBase&) = delete;
  SystemComponentBase& operator=(const SystemComponentBase&) = delete;

  // Movable (for container storage)
  SystemComponentBase(SystemComponentBase&&) noexcept = default;
  SystemComponentBase& operator=(SystemComponentBase&&) noexcept = default;

  /**
   * @brief Get component type identifier (defined by derived class).
   * @return 16-bit component type ID.
   * @note UID ranges: 0 = Executive, 1-100 = System components, 101+ = Models.
   * @note Must be unique per component type. Multiple instances share the same ID.
   */
  [[nodiscard]] std::uint16_t componentId() const noexcept override = 0;

  /**
   * @brief Get component name for collision detection.
   * @return Null-terminated string (max COMPONENT_NAME_MAX_LEN chars).
   * @note Used to distinguish multi-instance (same ID + same name) from
   *       collision (same ID + different name).
   */
  [[nodiscard]] const char* componentName() const noexcept override = 0;

  /**
   * @brief Get component type classification.
   * @return Component type (EXECUTIVE, CORE, MODEL, SUPPORT).
   * @note Default is CORE; override in derived classes (e.g., SimModelBase returns MODEL).
   * @note Used for logging directory selection and registry organization.
   */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::CORE;
  }

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
  [[nodiscard]] const char* label() const noexcept override { return "SYSTEM_COMPONENT"; }

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
   * @brief Pre-condition check before init() runs doInit().
   * @return SUCCESS to proceed, ERROR_NOT_CONFIGURED if not yet configured.
   * @note Overrides ComponentCore default (which always returns 0).
   */
  [[nodiscard]] std::uint8_t preInitCheck() noexcept override {
    if (!configured_) {
      setLastError("init() called before configuration");
      return static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED);
    }
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

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
  bool configured_{false};
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
