#ifndef APEX_SYSTEM_CORE_EXECUTIVE_APEX_EXECUTIVE_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_APEX_EXECUTIVE_HPP
/**
 * @file ApexExecutive.hpp
 * @brief Default executive with full lifecycle management, scheduling, and real-time support.
 *
 * Features:
 *  - Configurable startup modes (AUTO, INTERACTIVE, SCHEDULED)
 *  - Multiple shutdown modes (SIGNAL_ONLY, SCHEDULED, RELATIVE_TIME, CLOCK_CYCLE, COMBINED)
 *  - Multi-threaded execution with thread pool scheduler
 *  - Real-time clock synchronization with frame overrun detection
 *  - Pause/resume/fast-forward controls
 *  - Graceful shutdown with staged cleanup
 *  - Comprehensive logging with optional profiling and verbosity control
 *
 * Performance:
 *  - Profiling overhead: ~0% when disabled, ~0.01% when enabled at 100Hz
 *  - Frame overrun detection: <100ns per tick
 *  - Dependency coordination: Sub-microsecond callback latency
 */

#include "src/system/core/infrastructure/system_component/apex/inc/ComponentRegistry.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/PluginLoader.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveStatus.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveState.hpp"
#include "src/system/core/executive/apex/inc/ApexExecutive_Startup.hpp"
#include "src/system/core/executive/apex/inc/ApexExecutive_Shutdown.hpp"
#include "src/system/core/executive/apex/inc/ApexExecutive_CLI.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveData.hpp"
#include "src/system/core/executive/apex/inc/RTMode.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveBase.hpp"
#include "src/system/core/components/filesystem/apex/inc/ApexFileSystem.hpp"
#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/components/scheduler/apex/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/interface/apex/inc/ApexInterface.hpp"
#include "src/utilities/compatibility/inc/compat_concurrency.hpp"
#include "src/utilities/helpers/inc/Utilities.hpp"

#include <csignal>
#include <cstdint>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace executive {

/* ----------------------------- Constants ----------------------------- */

static constexpr std::string_view SYS_LOG_FN = "system.log";
static constexpr std::string_view PROF_LOG_FN = "profile.log";
static constexpr std::string_view HEARTBEAT_LOG_FN = "heartbeat.csv";
static constexpr std::uint16_t DEFAULT_CLOCK_FREQUENCY = 100;

/* ----------------------------- ApexExecutive ----------------------------- */

class ApexExecutive : public ExecutiveBase {
public:
  // Primary threads
  enum PrimaryThreads : std::uint8_t {
    STARTUP = 0,
    SHUTDOWN,
    CLOCK,
    TASK_EXECUTION,
    EXTERNAL_IO,
    WATCHDOG,
    MAX_PRIMARY_THREADS
  };

  /**
   * @brief Construct executive with execution context.
   * @param execPath Path to executable.
   * @param args Command-line arguments.
   * @param fsRoot Filesystem root directory.
   */
  ApexExecutive(const std::filesystem::path& execPath, const std::vector<std::string>& args,
                const std::filesystem::path& fsRoot);

  ~ApexExecutive() override = default;

  /**
   * @brief Run the primary control loop.
   * @return RunResult indicating success or failure mode.
   */
  [[nodiscard]] RunResult run() noexcept override;

  /** @brief Request graceful shutdown (IExecutive interface). */
  void shutdown() noexcept override;

  /** @brief Check if shutdown has been requested (IExecutive interface). */
  [[nodiscard]] bool isShutdownRequested() const noexcept override;

  /** @brief Get number of completed execution cycles (IExecutive interface). */
  [[nodiscard]] uint64_t cycleCount() const noexcept override;

  /**
   * @brief Component label.
   */
  [[nodiscard]] const char* label() const noexcept override { return "EXECUTIVE_DEFAULT"; }

  /**
   * @brief Update clock frequency dynamically.
   * @param newFrequency New frequency in Hz.
   */

  /**
   * @brief Request system pause (takes effect after current frame completes).
   */
  void pause() noexcept;

  /**
   * @brief Resume paused system.
   */
  void resume() noexcept;

  /**
   * @brief Request fast-forward mode (run without real-time throttling).
   * @note Not yet implemented - logs request and returns.
   */
  void fastForward() noexcept;

  /**
   * @brief Get current health packet snapshot.
   *
   * Populates and returns a snapshot of current executive state for
   * health monitoring and telemetry. Thread-safe.
   *
   * @return ExecutiveHealthPacket with current state.
   */
  [[nodiscard]] const ExecutiveHealthPacket& getHealthPacket() const noexcept;

  /* ----------------------------- Command Handling ----------------------------- */

  /**
   * @enum Opcode
   * @brief Executive-specific opcodes (0x0100+).
   *
   * Query commands (0x0100-0x010F): No payload, return data.
   * Control commands (0x0110-0x011F): No payload, trigger action.
   * Set commands (0x0121-0x012F): Payload required, set parameter.
   */
  enum class Opcode : std::uint16_t {
    // Query commands (no payload, return data)
    GET_HEALTH = 0x0100,       ///< Get ExecutiveHealthPacket (48 bytes).
    EXEC_NOOP = 0x0101,        ///< No-op for connectivity testing.
    GET_CLOCK_FREQ = 0x0102,   ///< Get current clock frequency (2 bytes).
    GET_RT_MODE = 0x0103,      ///< Get current RT mode (1 byte).
    GET_CLOCK_CYCLES = 0x0104, ///< Get current clock cycle count (8 bytes).

    // Control commands (no payload, trigger action)
    CMD_PAUSE = 0x0110,        ///< Pause execution.
    CMD_RESUME = 0x0111,       ///< Resume execution.
    CMD_SHUTDOWN = 0x0112,     ///< Request graceful shutdown.
    CMD_FAST_FORWARD = 0x0113, ///< Enter fast-forward mode (non-RT).

    // Set commands (payload required)
    SET_VERBOSITY = 0x0121, ///< Set log verbosity (1-byte payload).

    // System mode commands
    CMD_SLEEP = 0x0116, ///< Enter sleep mode (clock ticks, tasks paused).
    CMD_WAKE = 0x0117,  ///< Exit sleep mode (resume task dispatch).

    // Runtime update commands
    CMD_LOCK_COMPONENT = 0x0114,   ///< Lock component (4-byte fullUid payload).
    CMD_UNLOCK_COMPONENT = 0x0115, ///< Unlock component (4-byte fullUid payload).
    RELOAD_TPRM = 0x0125,          ///< Reload TPRM for component (4-byte fullUid payload).
    RELOAD_LIBRARY = 0x0126,   ///< Hot-swap component .so (4-byte fullUid payload, .so on disk).
    RELOAD_EXECUTIVE = 0x0127, ///< Restart executive via execve (no payload).

    // Ground test / inspection commands
    INSPECT = 0x0130, ///< Read registered data (9-byte payload: u32 fullUid, u8 category, u16
                      ///< offset, u16 len).

    // Runtime self-description commands
    GET_REGISTRY = 0x0140,     ///< Dump component registry (no payload, packed response).
    GET_DATA_CATALOG = 0x0141, ///< Dump data entry catalog (no payload, packed response).
  };

  /**
   * @brief Handle command dispatched to executive.
   *
   * Query commands (no payload, return data):
   *   - 0x0100: GET_HEALTH - Returns ExecutiveHealthPacket (48 bytes).
   *   - 0x0101: EXEC_NOOP - Test no-op, responds with ACK (no payload).
   *   - 0x0102: GET_CLOCK_FREQ - Returns clock frequency (2 bytes).
   *   - 0x0103: GET_RT_MODE - Returns RT mode (1 byte).
   *   - 0x0104: GET_CLOCK_CYCLES - Returns clock cycle count (8 bytes).
   *
   * Control commands (no payload, trigger action):
   *   - 0x0110: CMD_PAUSE - Pause execution.
   *   - 0x0111: CMD_RESUME - Resume execution.
   *   - 0x0112: CMD_SHUTDOWN - Request graceful shutdown.
   *   - 0x0113: CMD_FAST_FORWARD - Enter fast-forward mode.
   *
   * Set commands (payload required):
   *   - 0x0120: (removed -- fundamental frequency is a design-time parameter, not runtime).
   *   - 0x0121: SET_VERBOSITY - Set log verbosity (1-byte payload).
   *
   * Ground test commands (payload required):
   *   - 0x0130: INSPECT - Read registered data (9-byte payload, returns raw bytes).
   *
   * @param opcode Command opcode.
   * @param payload Command payload (may be empty for query/control commands).
   * @param response Output buffer for response data.
   * @return Status code (0 = success, 1 = unknown opcode, 2 = invalid payload).
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override;

protected:
  /** @brief Boot-time setup. Called by base init(). */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

  /**
   * @brief Register application-specific components (models, sensors, actuators, etc.).
   *
   * Override in derived class to instantiate and register components. Called from run()
   * after TPRM unpacking but before scheduler task registration.
   *
   * Default implementation does nothing (no components). Derived classes should:
   * 1. Instantiate component objects (as member variables for lifetime management)
   * 2. Register each component with the registry
   *
   * @return true on success, false on registration failure.
   * @note Components must be member variables of derived class to ensure lifetime.
   */
  [[nodiscard]] virtual bool registerComponents() noexcept { return true; }

  /**
   * @brief Configure components after interface initialization.
   *
   * Called after interface is initialized. Derived classes should:
   * 1. Allocate command/telemetry queues for each component
   * 2. Set internal bus pointer for component-to-component messaging
   *
   * Default implementation does nothing.
   *
   * @note Components must already be registered via registerComponents().
   */
  virtual void configureComponents() noexcept {}

  /**
   * @brief Get the interface for derived class access (queue allocation).
   * @return Pointer to interface component (may be null before run()).
   */
  [[nodiscard]] system_core::interface::ApexInterface* interface() noexcept {
    return interface_.get();
  }

  /**
   * @brief Get the filesystem for derived class access.
   * @return Reference to filesystem component.
   */
  [[nodiscard]] system_core::filesystem::ApexFileSystem& fileSystem() noexcept {
    return fileSystem_;
  }

  /**
   * @brief Get the registry for derived class access.
   * @return Reference to registry component.
   */
  [[nodiscard]] system_core::registry::ApexRegistry& registry() noexcept { return registry_; }

  /**
   * @brief Get the component registry for collision detection.
   * @return Pointer to component registry (may be null before run()).
   */
  [[nodiscard]] system_core::system_component::ComponentRegistry* componentRegistry() noexcept {
    return componentRegistry_.get();
  }

  /**
   * @brief Get the system log for derived class access.
   * @return Pointer to system log.
   */
  [[nodiscard]] logs::SystemLog* sysLog() noexcept { return sysLog_.get(); }

  /**
   * @brief Get the action component for derived class configuration.
   * @return Reference to ActionComponent.
   */
  [[nodiscard]] system_core::action::ActionComponent& actionComponent() noexcept {
    return actionComp_;
  }

  /**
   * @brief Register a component with the executive.
   *
   * Performs collision detection, registry registration, component log initialization,
   * TPRM loading, initialization, and data descriptor registration.
   *
   * @param comp Pointer to component.
   * @param logDir Directory for component log file (e.g., fileSystem().coreLogDir()).
   * @return true on success, false on collision or error.
   * @note Called during registerComponents() phase.
   * @note Component must not be null.
   * @note Log file created as "{componentName}_{instanceIndex}.log" in logDir.
   */
  [[nodiscard]] bool registerComponent(system_core::system_component::SystemComponentBase* comp,
                                       const std::filesystem::path& logDir) noexcept;

  // Lifecycle management (thread entrypoints)
  void startup(std::promise<std::uint8_t>&& p) noexcept;
  void shutdownThread(std::promise<std::uint8_t>&& p) noexcept;
  void executeTasks(std::promise<std::uint8_t>&& p) noexcept;
  void clock(std::promise<std::uint8_t>&& p) noexcept;
  void externalIO(std::promise<std::uint8_t>&& p) noexcept;
  void watchdog(std::promise<std::uint8_t>&& p) noexcept;

  // Shutdown stage helpers
  void shutdownStage1SignalReceived(int signum) noexcept;
  void shutdownStage2StopClock() noexcept;
  void shutdownStage3DrainTasks() noexcept;
  void shutdownStage4CleanupResources() noexcept;
  void shutdownStage5FinalStats() noexcept;

  // Shutdown utility helpers
  void broadcastShutdownRequest() noexcept;

  // Watchdog helpers
  void watchdogCheck() noexcept;
  void emitHeartbeat() noexcept;

  // Clock helpers
  void handleClockPause() noexcept;

  // Thread execution helper
  void runPrimaryThread(std::uint8_t threadId, std::promise<std::uint8_t>&& p) noexcept;

private:
  // Initialization helpers
  [[nodiscard]] std::uint8_t processArgs() noexcept;

  /**
   * @brief Unpack master TPRM to filesystem tprm directory.
   *
   * Extracts component TPRMs from packed master.tprm to fileSystem_.tprmDir().
   *
   * @return true on success, false on failure (status set).
   */
  [[nodiscard]] bool unpackMasterTprm() noexcept;

  /**
   * @brief Load executive tunable parameters from TPRM directory.
   *
   * Overrides SystemComponentBase::loadTprm() to load executive-specific
   * configuration (clock frequency, RT mode, startup/shutdown settings, etc.).
   *
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false if load failed.
   */
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /**
   * @brief Apply CLI argument overrides to TPRM-loaded configuration.
   *
   * CLI args take precedence over TPRM values. Call after loadTprm().
   */
  void applyCliOverrides() noexcept;

  /**
   * @brief Register a core component with the registry.
   *
   * Sets instance index to 0 (core components are single-instance), registers
   * with registry, and logs the result.
   *
   * @param comp Component to register.
   * @return true on success, false on registration failure.
   */
  bool registerCoreComponent(system_core::system_component::SystemComponentBase& comp) noexcept;

  /**
   * @brief Configure all registered components after interface initialization.
   *
   * For each component in registeredComponents_:
   *   - Allocates command/telemetry queues via interface
   *   - For models (componentType == MODEL): sets internal bus pointer
   *
   * Called automatically after interface init, before scheduler task registration.
   */
  void configureRegisteredComponents() noexcept;

  // General executive information
  std::filesystem::path execPath_{};
  std::vector<std::string> args_{};

  // Deferred restart target (set by RELOAD_EXECUTIVE, consumed by main loop).
  // Written before controlState_.restartPending is set (release ordering).
  std::filesystem::path restartExecTarget_{};
  bool restartDidSwapBinary_{false};

  // Parsed CLI arguments
  apex::helpers::args::ParsedArgs parsedArgs_{};

  // Startup configuration
  StartupConfig startupConfig_{};

  // Shutdown configuration
  ShutdownConfig shutdownConfig_{};
  std::atomic<std::int64_t> startupCompletedNs_{0};

  // Archive configuration
  std::filesystem::path archivePath_{};

  // Component registry with collision detection (for model registration)
  std::unique_ptr<system_core::system_component::ComponentRegistry> componentRegistry_{nullptr};

  // Tunable parameters configuration
  std::filesystem::path configPath_{};
  ExecutiveTunableParams tunableParams_{};

  // Logging (shared across system)
  std::shared_ptr<logs::SystemLog> sysLog_{nullptr};
  std::shared_ptr<logs::SystemLog> profLog_{nullptr};
  std::shared_ptr<logs::SystemLog> heartbeatLog_{nullptr};

  // Filesystem management
  system_core::filesystem::ApexFileSystem fileSystem_;

  // Registry management
  system_core::registry::ApexRegistry registry_;

  // Scheduler management
  system_core::scheduler::SchedulerMultiThread scheduler_;

  // Interface management (external command/telemetry)
  std::unique_ptr<system_core::interface::ApexInterface> interface_{nullptr};

  // Action engine (watchpoints, sequences, data-write orchestration)
  system_core::action::ActionComponent actionComp_;

  // Signal handling
  sigset_t signalSet_{};

  // Synchronization primitives
  std::mutex cvMutex_{};
  std::condition_variable cvStartup_{};
  std::condition_variable cvShutdown_{};
  std::condition_variable cvClockTick_{};
  std::condition_variable cvPause_{};
  std::atomic<bool> externalIOShouldStop_{false};

  // Consolidated state structs (see ExecutiveState.hpp)
  ControlState controlState_{};     ///< Control flags (pause, shutdown, etc.).
  ClockState clockState_{};         ///< Clock thread state.
  TaskExecutionState taskState_{};  ///< Task execution thread state.
  WatchdogState watchdogState_{};   ///< Watchdog thread state.
  ExternalIOState ioState_{};       ///< External I/O thread state.
  ShutdownState shutdownState_{};   ///< Shutdown sequence state.
  ProfilingState profilingState_{}; ///< Profiling subsystem state.

  // Output packet (populated on demand, registered as OUTPUT for INSPECT)
  mutable ExecutiveHealthPacket healthPacket_{}; ///< Current health snapshot.

  // Registered components (for auto-configuration after interface init)
  std::vector<system_core::system_component::SystemComponentBase*> registeredComponents_{};

  // Dynamically loaded component plugins (keyed by fullUid)
  std::unordered_map<std::uint32_t, system_core::system_component::PluginLoader> plugins_{};

  // Simulation configuration
  std::uint16_t clockFrequency_{DEFAULT_CLOCK_FREQUENCY};
  RTConfig rtConfig_{RTMode::HARD_TICK_COMPLETE}; ///< Real-time execution mode configuration.

  // Thread configuration for primary threads
  ExecutiveThreadConfig threadConfig_{}; ///< RT config for primary threads.
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_APEX_EXECUTIVE_HPP