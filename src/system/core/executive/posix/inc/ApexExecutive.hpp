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

#include "src/system/core/infrastructure/system_component/posix/inc/ComponentRegistry.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/PluginLoader.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveState.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive_Startup.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive_Shutdown.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive_CLI.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveData.hpp"
#include "src/system/core/executive/posix/inc/RTMode.hpp"
#include "src/system/core/executive/posix/inc/PosixExecutiveBase.hpp"
#include "src/system/core/components/filesystem/posix/inc/ApexFileSystem.hpp"
#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerMultiThread.hpp"
#include "src/system/core/components/action/apex/inc/ActionComponent.hpp"
#include "src/system/core/components/time_server/apex/inc/TimeServer.hpp"
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

class ApexExecutive : public PosixExecutiveBase {
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
   * Builds a snapshot of the executive's counters and flags from their
   * atomics and returns it by value, touching no state another caller
   * could be writing: callable from any thread at any time.
   *
   * @return ExecutiveHealthPacket with current state.
   */
  [[nodiscard]] ExecutiveHealthPacket getHealthPacket() const noexcept;

  /// Cycles on which the action phase was skipped because a task was
  /// still running when the in-flight wait expired. Readable from any
  /// thread.
  [[nodiscard]] std::size_t actionPhaseSkips() const noexcept {
    return scheduler_.totalInFlightWaitTimeouts();
  }

  /// Cycles on which the in-flight wait was a single poll because the
  /// task loop was behind the clock. Readable from any thread.
  /**
   * @brief The longest wait, in ns, the action phase gave a cycle that
   *        started behind the clock. A cycle behind gets a poll, so this
   *        is the poll's own cost (microseconds) when the budget is bound
   *        to the processed cycle, and most of a frame when it is not.
   */
  [[nodiscard]] std::uint64_t actionWaitBehindMaxNs() const noexcept {
    return actionWaitBehindMaxNs_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t actionWaitBacklogPolls() const noexcept {
    return actionWaitBacklogPolls_.load(std::memory_order_acquire);
  }

  /**
   * @brief The clock's wake-lateness record for the run.
   *
   * Complete once run() has returned; the clock thread owns it while it
   * runs, so read it only after that.
   *
   * @return Lateness distribution of every clock wake against its deadline.
   */
  [[nodiscard]] const TickLateness& clockLateness() const noexcept { return clockState_.lateness; }

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
    INSPECT = 0x0130,       ///< Read registered data (9-byte payload: u32 fullUid, u8 category, u16
                            ///< offset, u16 len).
    READBACK_TPRM = 0x0131, ///< Digest of the staged (inactive-bank) TPRM payloads
                            ///< (optional 2-byte page-offset payload; see TprmReadback.hpp).
    VERIFY_TPRM = 0x0132,   ///< Verify one staged payload without applying (4-byte fullUid
                            ///< payload; 12-byte verdict response; see TprmReadback.hpp).

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
   * @brief The seam between selecting the action phase's wait deadline
   *        and waiting on it, once per cycle.
   *
   * Called with the cycle and the selected wait deadline, a
   * CLOCK_MONOTONIC instant (0 for a cycle that gets a poll). A derived
   * executive can observe the selection here; a test can hold the thread
   * here to stand in for a preemption between the two. The deadline is
   * an instant, so time spent here is taken from the wait's remaining
   * budget and never renews it. Each sleep the wait then requests is
   * capped at 250 us and at the remainder as of the clock read before
   * it, and none is requested once a read finds the deadline passed.
   * Preemption can delay both issuing a request and its return, so the
   * deadline does not bound the instant a request is made or the wait
   * returns (see SchedulerMultiThread::waitForInFlightTasksUntil).
   */
  virtual void onWaitDeadlineSelected(std::uint64_t taskCycles, std::uint64_t waitEndNs) noexcept {
    (void)taskCycles;
    (void)waitEndNs;
  }

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
   * @brief Get the time server for application-level wiring (PPS source,
   *        broadcast delegate).
   * @return Reference to TimeServer.
   * @note By default the executive runs TimeServer with no PPS source and
   *       no broadcast delegate. Applications wire those in via this
   *       accessor before runtime.
   */
  [[nodiscard]] system_core::time_server::TimeServer& timeServer() noexcept { return timeServer_; }

  /**
   * @brief Register a component with the executive.
   *
   * Performs collision detection, registry registration, component log initialization,
   * TPRM loading, initialization, and data descriptor registration.
   *
   * @param comp Pointer to component.
   * @param logDir Root of the log tree: fileSystem().logDir().
   * @return true on success, false on collision or error.
   * @note Called during registerComponents() phase.
   * @note Component must not be null.
   * @note Log file created as "{componentName}_{instanceIndex}.log" in the
   *       subdirectory of logDir for the component's type (core, models,
   *       support, drivers). A log that cannot be opened is a warning,
   *       not a refusal.
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
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /**
   * @brief Apply CLI argument overrides to TPRM-loaded configuration.
   *
   * CLI args take precedence over TPRM values. Call after loadTprm().
   * Tunable-mapped flags edit a copy of the wire struct and re-enter
   * adoptTunables(); session-only settings apply directly.
   */
  void applyCliOverrides() noexcept;

  /**
   * @brief Validate, clamp, and adopt a tunable-parameter set.
   *
   * The single door for tunable ingestion whatever the source (tprm
   * payload or CLI override pass): vocabulary validation, floor
   * clamps, then the wire struct and every derived working config
   * assigned together. One writer keeps the registered TUNABLE block
   * (INSPECT) and the running configuration in agreement.
   *
   * @param params Candidate parameter set (clamped locally on adopt).
   * @param valErr Receives a static description on validation failure.
   * @return true on adoption; false if validation rejected the set
   *         (no state is modified).
   */
  [[nodiscard]] bool adoptTunables(ExecutiveTunableParams params, const char** valErr) noexcept;

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

  /* ----------------------------- TPRM Ingest Policy ----------------------------- */

  /**
   * @brief Boot posture toward missing or rejected TPRMs.
   *
   * STRICT (the default) refuses to run a misconfigured vehicle: a
   * rejected payload, or a missing one for a component whose params
   * are not declared optional, fails init with the component and
   * check named. LENIENT keeps warn-and-run-defaults for dev
   * ergonomics; a rejected payload is fatal under both postures (a
   * present-but-refused file is never intentional).
   */
  enum class IngestPolicy : std::uint8_t {
    STRICT = 0,
    LENIENT = 1,
  };

  /// One component's failed ingest, recorded during boot and judged
  /// at the post-registration barrier so every offender is named.
  struct IngestFailure {
    std::uint32_t fullUid{0};
    const char* componentLabel{nullptr};
    system_core::system_component::TprmIngest state{};
    bool fatalAlways{false}; ///< True for rejected payloads: fatal under every policy.
  };

  /**
   * @brief Record one component's ingest outcome against the policy.
   *
   * Called after the component's init() so its registered data blocks
   * are visible: a component that registers TUNABLE_PARAM data but
   * reports NONE is the absent-declaration case strictness exists to
   * catch.
   */
  void recordIngestOutcome(system_core::system_component::SystemComponentBase* comp,
                           system_core::system_component::TprmIngest ingest) noexcept;

  /**
   * @brief Judge recorded ingest failures at the boot barrier.
   *
   * Logs every failure with its component and state; returns false
   * when any is fatal under the active policy (rejected payloads
   * always; missing ones under STRICT unless declared optional).
   */
  [[nodiscard]] bool ingestPolicyHolds() noexcept;

  IngestPolicy ingestPolicy_{IngestPolicy::STRICT};
  std::vector<IngestFailure> ingestFailures_{};

  /**
   * @brief One bounded A/B recovery when the active bank fails ingest.
   *
   * If no fallback was attempted yet and the other bank has staged
   * payloads, writes the attempt marker, flips active_bank, and
   * re-execs the executive (argv preserved, --fs-root included) so
   * the vehicle boots the known-good bank. Returns false when the
   * flip is impossible: fallback already attempted, nothing staged,
   * or exec failed.
   */
  [[nodiscard]] bool attemptIngestFallback() noexcept;

  /// Marker recording that this boot chain already flipped banks.
  [[nodiscard]] std::filesystem::path ingestFallbackMarker() const noexcept;

  bool bootedOnFallback_{false}; ///< This boot runs the fallback bank.

  /**
   * @brief SAFE ingest-hold: both banks failed, but the vehicle stays
   *        reachable.
   *
   * The system boots permanently paused -- clock ticks and command
   * drains run, tasks never dispatch -- so ground can inspect the
   * staged banks (READBACK_TPRM), upload a corrected payload, verify
   * it, and command RELOAD_EXECUTIVE to reboot onto the fix. RESUME
   * and WAKE are refused while held: a misconfigured vehicle never
   * runs by accident.
   */
  bool ingestHold_{false};

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
  bool runTaskCensus_{false}; ///< --task-census: pre-clock cost census.

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
  /// In-flight waits reduced to a poll because the task loop was behind.
  std::atomic<std::uint64_t> actionWaitBacklogPolls_{0};
  std::atomic<std::uint64_t> actionWaitBehindMaxNs_{0}; ///< See actionWaitBehindMaxNs().

  // Interface management (external command/telemetry)
  std::unique_ptr<system_core::interface::ApexInterface> interface_{nullptr};

  // Action engine (watchpoints, sequences, data-write orchestration)
  system_core::action::ActionComponent actionComp_;

  // Time server (PPS-driven UTC distribution, sole time authority)
  system_core::time_server::TimeServer timeServer_;

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
  /// The health block registered for INSPECT readback. Init writes the
  /// first snapshot before any thread starts; afterwards the GET_HEALTH
  /// handler refreshes it on whichever thread dispatches that command:
  /// the I/O thread for wire commands, the task thread for a direct
  /// action COMMAND aimed at the executive. The I/O thread's INSPECT
  /// reads it as last refreshed and does not refresh it; a task-thread
  /// refresh is not serialized against that read. Callers wanting a
  /// snapshot of their own take one from getHealthPacket().
  ExecutiveHealthPacket healthPacket_{};

  // Registered components (for auto-configuration after interface init)
  std::vector<system_core::system_component::SystemComponentBase*> registeredComponents_{};

  // Dynamically loaded component plugins (keyed by fullUid). Touched only
  // by the RELOAD_LIBRARY handler, on the external I/O thread.
  std::unordered_map<std::uint32_t, system_core::system_component::PluginLoader> plugins_{};

  /* ----------------------------- Library swap ----------------------------- */
  //
  // RELOAD_LIBRARY prepares the replacement on the external I/O thread,
  // the control thread: a command that reaches the handler on the task
  // thread (an action's command) is handed over to it first. The swap is
  // published on the task thread, at a point where the scheduler reports
  // every dispatched task settled: the task thread is the one that drains
  // commands to components and dispatches, and the I/O thread is waiting,
  // so nothing resolves the component while its bindings change. The
  // plugin the swap replaced is destroyed afterwards on the I/O thread.

  /// Outcome of publishing a swap.
  struct LibrarySwapOutcome {
    std::uint8_t status{0};        ///< SUCCESS, NOT_QUIESCENT, TASK_MISMATCH or REGISTRY_FAILED.
    std::uint8_t tasksExpected{0}; ///< Scheduler entries of the component.
    std::uint8_t tasksReplaced{0}; ///< Entries the replacement provided a task for.
    std::uint64_t waitNs{0};       ///< From the request to the end of publication.
  };

  /// A swap waiting for the task thread.
  struct LibrarySwap {
    enum class Phase : std::uint8_t { NONE, PENDING, DONE };
    Phase phase{Phase::NONE};
    std::uint32_t fullUid{0};
    system_core::system_component::SystemComponentBase* oldComp{nullptr};
    system_core::system_component::SystemComponentBase* newComp{nullptr};
    /// Set for a parameter reload, which goes through the same hand-over:
    /// the directory oldComp loads from. There is then no replacement.
    const std::filesystem::path* tprmDir{nullptr};
    system_core::system_component::TprmIngest tprmIngest{
        system_core::system_component::TprmIngest::NONE}; ///< What that load returned.
    std::uint64_t tprmLoadNs{0};                          ///< How long that load held the thread.
    LibrarySwapOutcome outcome{};
  };

  /// What a parameter reload handed to the task thread came to.
  struct TprmApplyOutcome {
    bool applied{false}; ///< False when the tasks did not settle within the wait.
    system_core::system_component::TprmIngest ingest{
        system_core::system_component::TprmIngest::NONE}; ///< The load's result when applied.
    std::uint64_t waitNs{0};                              ///< From the request to its end.
    std::uint64_t loadNs{0}; ///< How long the load held the task thread.
  };

  /**
   * @brief Bind the scheduler's entries, the registry's entries and the
   *        component list to the replacement.
   * @return status NOT_QUIESCENT when the scheduler is not settled;
   *         REGISTRY_FAILED when the replacement lacks a registered data
   *         block or task; TASK_MISMATCH when it lacks a scheduled task
   *         or sequences one differently. Nothing has changed in any of
   *         those cases.
   * @note Task thread only, with the external I/O thread waiting.
   */
  [[nodiscard]] LibrarySwapOutcome
  publishLibrarySwapHere(std::uint32_t fullUid,
                         system_core::system_component::SystemComponentBase& oldComp,
                         system_core::system_component::SystemComponentBase& newComp) noexcept;

  /**
   * @brief Hand a swap to the task thread and wait for its outcome.
   *
   * The caller blocks until the swap is published, librarySwapWaitNs()
   * passes or the task thread ends; a swap not taken by then is
   * withdrawn and reported NOT_QUIESCENT.
   * @note External I/O thread only: the task thread cannot wait for itself.
   */
  [[nodiscard]] LibrarySwapOutcome
  publishLibrarySwap(std::uint32_t fullUid,
                     system_core::system_component::SystemComponentBase& oldComp,
                     system_core::system_component::SystemComponentBase& newComp) noexcept;

  /**
   * @brief Have the task thread load a component's parameters from DIR
   *        between cycles, and wait for it.
   *
   * The load runs with every dispatched task settled and none dispatched
   * meanwhile, so it cannot meet a run of the component's own tasks, the
   * dispatch that reads the scheduler's tables, or the action phase. The
   * caller blocks as for a library swap; a load not taken within
   * librarySwapWaitNs() is withdrawn and reported not applied.
   * @note External I/O thread only: the task thread cannot wait for itself.
   */
  [[nodiscard]] TprmApplyOutcome
  applyTprmAtSettledPoint(system_core::system_component::SystemComponentBase& comp,
                          const std::filesystem::path& dir) noexcept;

  /// Hand REQUEST to the task thread and wait for it as publishLibrarySwap()
  /// describes. @return true when the task thread carried it out; DONE is
  /// then the request as the task thread left it.
  [[nodiscard]] bool handOverToTaskThread(const LibrarySwap& request, LibrarySwap& done,
                                          std::uint64_t& waitNs) noexcept;

  /// Task thread: carry out the pending swap or parameter reload if the
  /// scheduler is settled.
  void serviceLibrarySwap() noexcept;

  /// External I/O thread, top of its loop: destroy the plugins retired
  /// since the last pass, then run the reload an action asked for.
  void serviceLibraryControl() noexcept;

  /// How long a swap from another thread waits for the task thread: two
  /// seconds, or four clock periods when that is longer.
  [[nodiscard]] std::uint64_t librarySwapWaitNs() const noexcept;

  /// deferredReloadUid_ when no reload is waiting for the I/O thread.
  static constexpr std::uint32_t NO_DEFERRED_RELOAD = 0xFFFFFFFFU;

  std::mutex swapMutex_{};               ///< Guards swap_.
  std::condition_variable cvSwapDone_{}; ///< Signalled when swap_ reaches DONE.
  LibrarySwap swap_{};                   ///< The one swap in hand-over.
  std::atomic<bool> swapPending_{false}; ///< swap_ is PENDING; the task thread reads it.
  bool swapClosed_{false};               ///< The task thread has ended; guarded by swapMutex_.
  std::atomic<bool> swapKick_{false};    ///< Wakes a waiting task thread for one attempt.
  /// Target of a RELOAD_LIBRARY that arrived on the task thread and waits
  /// for the external I/O thread to run it.
  std::atomic<std::uint32_t> deferredReloadUid_{NO_DEFERRED_RELOAD};
  /// Plugins a swap replaced, kept until the top of the external I/O
  /// thread's next loop. Touched by that thread only.
  std::vector<system_core::system_component::PluginLoader> retiredPlugins_{};
  std::atomic<std::thread::id> taskThreadId_{}; ///< Set by the task thread when it starts.

  // Simulation configuration
  std::uint16_t clockFrequency_{DEFAULT_CLOCK_FREQUENCY};
  /// Real-time execution mode configuration. Deliberately inherits the
  /// struct's own stock default (soft) -- a member-site override here is
  /// how the stock posture ended up hard in three places at once.
  RTConfig rtConfig_{};

  // Thread configuration for primary threads
  ExecutiveThreadConfig threadConfig_{}; ///< RT config for primary threads.
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_APEX_EXECUTIVE_HPP