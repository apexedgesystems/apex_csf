/**
 * @file ApexExecutive.cpp
 * @brief ApexExecutive construction, initialization, and orchestration.
 *
 * Core responsibilities:
 *  - Constructor: Signal setup, logging initialization
 *  - init(): Component initialization and configuration
 *  - run(): Thread orchestration and lifecycle management
 *
 * Primary thread implementations are in dedicated modules:
 *  - ApexExecutive_Startup.cpp: startup()
 *  - ApexExecutive_Shutdown.cpp: shutdown() and stage functions
 *  - ApexExecutive_Clock.cpp: clock(), handleClockPause(), setClockFrequency()
 *  - ApexExecutive_TaskExecution.cpp: executeTasks()
 *  - ApexExecutive_ExternalIO.cpp: externalIO(), pause(), resume(), fastForward()
 *  - ApexExecutive_Watchdog.cpp: watchdog(), watchdogCheck(), emitHeartbeat()
 */

#include "src/system/core/executive/apex/inc/ApexExecutive.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveStatus.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/ComponentRegistry.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/PackedTprm.hpp"
#include "src/system/core/executive/apex/inc/RTMode.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include "src/system/core/components/scheduler/apex/inc/SchedulerData.hpp"

#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"

#include "src/utilities/concurrency/inc/ThreadConfig.hpp"
#include "src/utilities/helpers/inc/Utilities.hpp"

// Use Status enum without qualification
using enum executive::Status;

#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>

using namespace executive;
using system_core::system_component::isAtsEntry;
using system_core::system_component::isRtsEntry;
using system_core::system_component::PackedTprmReader;
using system_core::system_component::sequenceSlot;
using system_core::system_component::TPRM_MAGIC;

// Constructor: Signal setup and logging initialization
ApexExecutive::ApexExecutive(const std::filesystem::path& execPath,
                             const std::vector<std::string>& args,
                             const std::filesystem::path& fsRoot)
    : execPath_(execPath), args_(args),
      // Enable ASYNC mode for logs called from RT threads
      sysLog_(std::make_shared<logs::SystemLog>((fsRoot / SYS_LOG_FN).string(),
                                                logs::SystemLog::Mode::ASYNC, 8192)),
      profLog_(std::make_shared<logs::SystemLog>((fsRoot / PROF_LOG_FN).string(),
                                                 logs::SystemLog::Mode::ASYNC, 4096)),
      // Heartbeat can stay SYNC - only 1Hz, not performance critical
      heartbeatLog_(std::make_shared<logs::SystemLog>((fsRoot / HEARTBEAT_LOG_FN).string())),
      fileSystem_(fsRoot), scheduler_(DEFAULT_CLOCK_FREQUENCY, fileSystem_.coreLogDir()) {
  // Initialize signal set for graceful shutdown
  // NOTE: SIGSEGV is intentionally NOT included -- segfaults should crash
  // immediately with core dumps for debugging
  sigemptyset(&signalSet_);
  sigaddset(&signalSet_, SIGINT);  // CTRL+C - user interrupt
  sigaddset(&signalSet_, SIGTERM); // kill command - termination request
  sigaddset(&signalSet_, SIGQUIT); // CTRL+\ - quit signal
  sigaddset(&signalSet_, SIGHUP);  // Terminal hangup

  // Block these signals so shutdown thread can handle them via sigwait
  if (pthread_sigmask(SIG_BLOCK, &signalSet_, nullptr) != 0) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_SIGNAL_BLOCK_FAILED),
                   fmt::format("Failed to block signals: {}", strerror(errno)));
  }

  // Log comprehensive system information for reproducibility
  sysLog_->info(label(), "=== System Information ===");
  sysLog_->info(label(), fmt::format("Executable: {}", execPath_.string()));

  // Get current working directory
  std::array<char, 512> cwdBuf{};
  if (getcwd(cwdBuf.data(), cwdBuf.size()) != nullptr) {
    sysLog_->info(label(), fmt::format("Working directory: {}", cwdBuf.data()));
  }

  // Log timestamp
  const auto NOW = std::chrono::system_clock::now();
  const std::time_t T = std::chrono::system_clock::to_time_t(NOW);
  std::tm tmLocal{};
#if defined(_WIN32)
  localtime_s(&tmLocal, &T);
#else
  localtime_r(&T, &tmLocal);
#endif
  std::array<char, 64> timeBuf{};
  std::strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%d %H:%M:%S", &tmLocal);
  sysLog_->info(label(), fmt::format("Timestamp: {}", timeBuf.data()));

  sysLog_->info(label(), "==========================");
  sysLog_->info(label(), "ApexExecutive constructed successfully");

  // Log async mode status
  if (sysLog_->isAsync()) {
    sysLog_->info(label(), "System log: ASYNC mode enabled (RT-safe)");
  }
  if (profLog_->isAsync()) {
    sysLog_->info(label(), "Profile log: ASYNC mode enabled (RT-safe)");
  }
}

// Initialize the ApexExecutive system
std::uint8_t ApexExecutive::doInit() noexcept {
  // Check for supervisor watchdog pipe (set by apex_watchdog via environment)
  if (const char* fdStr = std::getenv("APEX_WATCHDOG_FD")) {
    watchdogState_.supervisorFd = std::atoi(fdStr);
    if (watchdogState_.supervisorFd >= 0) {
      sysLog_->info(label(),
                    fmt::format("Supervisor heartbeat pipe: fd={}", watchdogState_.supervisorFd));
    }
  }

  // Process command-line arguments
  if (processArgs() != static_cast<std::uint8_t>(SUCCESS)) {
    return status();
  }

  // Initialize executive filesystem
  sysLog_->info(label(), "Initializing filesystem...");
  setStatus(fileSystem_.init());
  if (status() != 0) {
    const char* ERR_CTX = fileSystem_.lastError();
    sysLog_->error(label(), status(),
                   fmt::format("Filesystem init FAILED: {}", ERR_CTX ? ERR_CTX : "unknown error"));
    setStatus(static_cast<std::uint8_t>(ERROR_MODULE_INIT_FAIL));
    return status();
  }
  sysLog_->info(label(), fmt::format("Filesystem init: SUCCESS ({})", fileSystem_.label()));

  // Configure filesystem cleanup for destructor-based RAII cleanup
  fileSystem_.configureShutdownCleanup(!shutdownConfig_.skipCleanup, archivePath_);

  // Unpack master TPRM, load executive TPRM, apply CLI overrides
  if (!configPath_.empty()) {
    if (!unpackMasterTprm()) {
      return status(); // Error already set and logged
    }

    // Load executive TPRM
    if (!loadTprm(fileSystem_.tprmDir())) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
                       "Executive TPRM load failed, using defaults");
    }

    // CLI overrides take precedence over TPRM values
    applyCliOverrides();
  } else {
    sysLog_->info(label(), "No config file provided, using CLI args only");
  }

  // Initialize heartbeat log
  heartbeatLog_->info("HEARTBEAT", "timestamp_ns,clock_cycles,task_cycles,frame_overruns,status");

  // Log comprehensive system configuration (all settings in one place)
  sysLog_->info(label(), "=== System Configuration ===");
  sysLog_->info(label(), fmt::format("Clock frequency: {} Hz", clockFrequency_));
  sysLog_->info(label(), fmt::format("RT mode: {} (hard={})", rtModeToString(rtConfig_.mode),
                                     rtConfig_.isHardMode() ? "yes" : "no"));
  if (rtConfig_.mode == RTMode::SOFT_LAG_TOLERANT) {
    sysLog_->info(label(), fmt::format("RT max lag: {} ticks", rtConfig_.maxLagTicks));
  }

  // Profiling
  if (profilingState_.sampleEveryN > 0) {
    sysLog_->info(label(), fmt::format("Profiling: every {} tick{}", profilingState_.sampleEveryN,
                                       profilingState_.sampleEveryN == 1 ? "" : "s"));
  } else {
    sysLog_->info(label(), "Profiling: disabled");
  }

  // Verbosity
  sysLog_->info(label(), fmt::format("Debug verbosity: {}", sysLog_->verbosity()));

  // Watchdog
  sysLog_->info(label(), fmt::format("Watchdog: {} ms ({:.1f} Hz)", watchdogState_.intervalMs,
                                     1000.0 / watchdogState_.intervalMs));

  // Startup mode
  const char* startupModeStr = "AUTO";
  if (startupConfig_.mode == StartupConfig::INTERACTIVE) {
    startupModeStr = "INTERACTIVE";
  } else if (startupConfig_.mode == StartupConfig::SCHEDULED) {
    startupModeStr = "SCHEDULED";
  }
  sysLog_->info(label(), fmt::format("Startup mode: {}", startupModeStr));

  // Shutdown mode
  const char* shutdownModeStr = "SIGNAL_ONLY";
  if (shutdownConfig_.mode == ShutdownConfig::SCHEDULED) {
    shutdownModeStr = "SCHEDULED";
  } else if (shutdownConfig_.mode == ShutdownConfig::RELATIVE_TIME) {
    shutdownModeStr = "RELATIVE_TIME";
  } else if (shutdownConfig_.mode == ShutdownConfig::CLOCK_CYCLE) {
    shutdownModeStr = "CLOCK_CYCLE";
  } else if (shutdownConfig_.mode == ShutdownConfig::COMBINED) {
    shutdownModeStr = "COMBINED";
  }
  sysLog_->info(label(), fmt::format("Shutdown mode: {}", shutdownModeStr));

  // Tunable parameters configuration
  if (!configPath_.empty()) {
    sysLog_->info(label(), fmt::format("Config file: {}", configPath_.string()));
  } else {
    sysLog_->info(label(), "Config file: none");
  }

  // Filesystem configuration (paths from filesystem component)
  sysLog_->info(label(), fmt::format("Filesystem root: {}", fileSystem_.root().string()));
  sysLog_->info(label(), fmt::format("Log directory: {}", fileSystem_.logDir().string()));
  sysLog_->info(label(), fmt::format("TPRM directory: {}", fileSystem_.tprmDir().string()));
  sysLog_->info(label(), fmt::format("Database directory: {}", fileSystem_.dbDir().string()));

  // Archive configuration
  if (shutdownConfig_.skipCleanup) {
    sysLog_->info(label(), "Archive on shutdown: disabled (--skip-cleanup)");
  } else {
    sysLog_->info(label(), fmt::format("Archive on shutdown: enabled (to {})",
                                       fileSystem_.logDir().string()));
  }

  // Thread configuration
  sysLog_->info(label(), "=== Thread Configuration ===");
  auto logThreadConfig = [this](const char* name, const PrimaryThreadConfig& cfg) {
    const char* policyStr = (cfg.policy == SCHED_FIFO) ? "FIFO"
                            : (cfg.policy == SCHED_RR) ? "RR"
                                                       : "OTHER";
    std::string affinityStr = cfg.affinity.empty() ? "all" : "";
    for (std::size_t i = 0; i < cfg.affinity.size(); ++i) {
      if (i > 0)
        affinityStr += ",";
      affinityStr += std::to_string(cfg.affinity[i]);
    }
    sysLog_->info(label(), fmt::format("  {}: policy={}, priority={}, affinity=[{}]", name,
                                       policyStr, cfg.priority, affinityStr));
  };
  logThreadConfig("STARTUP", threadConfig_.startup);
  logThreadConfig("SHUTDOWN", threadConfig_.shutdown);
  logThreadConfig("CLOCK", threadConfig_.clock);
  logThreadConfig("TASK_EXEC", threadConfig_.taskExecution);
  logThreadConfig("EXT_IO", threadConfig_.externalIO);
  logThreadConfig("WATCHDOG", threadConfig_.watchdog);

  sysLog_->info(label(), "============================");

  sysLog_->info(label(), "Initialization successful");
  return static_cast<std::uint8_t>(SUCCESS);
}

/* ----------------------------- Action Engine Delegates ----------------------------- */

/**
 * @brief Resolver delegate for ActionInterface.
 *
 * Maps (fullUid, category) to a mutable byte pointer via the registry.
 * The const_cast is justified: the registry stores const pointers for safety,
 * but the underlying component data is mutable and owned by the component.
 * The action engine needs write access for DATA_WRITE operations.
 *
 * @param ctx ApexRegistry pointer.
 * @param fullUid Target component's full UID.
 * @param category Data category to resolve.
 * @return ResolvedData with mutable pointer, or empty if not found.
 */
static system_core::data::ResolvedData
actionResolverFn(void* ctx, std::uint32_t fullUid,
                 system_core::data::DataCategory category) noexcept {
  auto* registry = static_cast<system_core::registry::ApexRegistry*>(ctx);
  auto* entry = registry->getData(fullUid, category);
  if (entry == nullptr || !entry->isValid()) {
    return {};
  }
  return {const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(entry->dataPtr)),
          entry->size};
}

/**
 * @brief Command handler delegate for ActionInterface.
 *
 * Routes (fullUid, opcode, payload, len) to the target component's
 * handleCommand() via registry lookup. This enables COMMAND-type actions
 * in sequences to dispatch real commands to components.
 *
 * @param ctx ApexRegistry pointer.
 * @param fullUid Target component's full UID.
 * @param opcode Command opcode.
 * @param payload Payload bytes.
 * @param len Payload length.
 */
static void actionCommandHandlerFn(void* ctx, std::uint32_t fullUid, std::uint16_t opcode,
                                   const std::uint8_t* payload, std::uint8_t len) noexcept {
  auto* registry = static_cast<system_core::registry::ApexRegistry*>(ctx);
  auto* comp = registry->getComponent(fullUid);
  if (comp == nullptr) {
    return;
  }
  apex::compat::rospan<std::uint8_t> span(payload, len);
  std::vector<std::uint8_t> response;
  static_cast<void>(comp->handleCommand(opcode, span, response));
}

/* ----------------------------- Core Component Registration ----------------------------- */

bool ApexExecutive::registerCoreComponent(
    system_core::system_component::SystemComponentBase& comp) noexcept {
  comp.setInstanceIndex(0); // Core components are single-instance

  auto regStatus = registry_.registerComponent(comp.fullUid(), comp.componentName(),
                                               static_cast<void*>(&comp), comp.componentType());
  if (system_core::registry::isError(regStatus)) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(regStatus),
                     fmt::format("{} registry registration failed: {}", comp.componentName(),
                                 system_core::registry::toString(regStatus)));
    return false;
  }

  sysLog_->info(label(),
                fmt::format("Registered {} (componentId={}, instance={}, fullUid=0x{:06X})",
                            comp.componentName(), comp.componentId(), comp.instanceIndex(),
                            comp.fullUid()));
  return true;
}

bool ApexExecutive::registerComponent(system_core::system_component::SystemComponentBase* comp,
                                      const std::filesystem::path& logDir) noexcept {
  if (comp == nullptr) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_MODULE_INIT_FAIL),
                   "registerComponent: null component pointer");
    return false;
  }

  if (componentRegistry_ == nullptr) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_MODULE_INIT_FAIL),
                   "registerComponent: component registry not initialized");
    return false;
  }

  // Step 1: Register with collision detection (assigns instance index)
  std::uint8_t instanceIdx = 0;
  if (!componentRegistry_->registerComponent(comp, instanceIdx)) {
    sysLog_->error(
        label(), static_cast<std::uint8_t>(ERROR_COMPONENT_COLLISION),
        fmt::format("Component collision: componentId={} name='{}' conflicts with existing",
                    comp->componentId(), comp->componentName()));
    return false;
  }

  // Step 2: Register with ApexRegistry for unified metadata access
  auto regStatus = registry_.registerComponent(comp->fullUid(), comp->componentName(),
                                               static_cast<void*>(comp), comp->componentType());
  if (system_core::registry::isError(regStatus)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(regStatus),
                   fmt::format("Registry error for {} (fullUid=0x{:06X}): {}",
                               comp->componentName(), comp->fullUid(),
                               system_core::registry::toString(regStatus)));
    return false;
  }

  sysLog_->info(label(),
                fmt::format("Registered {} (componentId={}, instance={}, fullUid=0x{:06X})",
                            comp->label(), comp->componentId(), instanceIdx, comp->fullUid()));

  // Step 3: Initialize component log (uses instance index for filename)
  comp->initComponentLog(logDir);

  // Step 4: Load TPRM configuration
  if (!comp->loadTprm(fileSystem_.tprmDir())) {
    sysLog_->warning(
        label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
        fmt::format("loadTprm failed for {} (0x{:06X})", comp->label(), comp->fullUid()));
  }

  // Step 4.5: Provision transport for HW_MODEL components
  if (comp->componentType() == system_core::system_component::ComponentType::HW_MODEL) {
    auto* hwModel = static_cast<system_core::system_component::HwModelBase*>(comp);
    if (!hwModel->provisionTransport()) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
                       fmt::format("Transport provisioning failed for {} (0x{:06X})", comp->label(),
                                   comp->fullUid()));
    }
  }

  // Step 5: Initialize the component
  (void)comp->init();

  // Step 6: Register component's data descriptors with ApexRegistry
  for (std::size_t i = 0; i < comp->dataCount(); ++i) {
    const auto* desc = comp->dataDescriptor(i);
    if (desc != nullptr && desc->ptr != nullptr) {
      auto status = registry_.registerData(comp->fullUid(), desc->category, desc->name, desc->ptr,
                                           desc->size);
      if (system_core::registry::isError(status)) {
        sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                         fmt::format("Failed to register data '{}' for {} (0x{:06X}): {}",
                                     desc->name, comp->label(), comp->fullUid(),
                                     system_core::registry::toString(status)));
      }
    }
  }

  // Step 7: Track for auto-configuration after interface init
  registeredComponents_.push_back(comp);

  return true;
}

void ApexExecutive::configureRegisteredComponents() noexcept {
  if (interface_ == nullptr) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
                     "configureRegisteredComponents: interface not initialized");
    return;
  }

  for (auto* comp : registeredComponents_) {
    if (comp == nullptr) {
      continue;
    }

    // Allocate command/telemetry queues for all registered components
    auto* queues = interface_->allocateQueues(comp->fullUid());
    if (queues != nullptr) {
      sysLog_->debug(
          label(),
          fmt::format("Allocated queues for {} (0x{:06X})", comp->label(), comp->fullUid()), 2);
    } else {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_QUEUE_ALLOC_FAIL),
                       fmt::format("Failed to allocate queues for {} (0x{:06X})", comp->label(),
                                   comp->fullUid()));
    }

    // Set internal bus for all schedulable components (models + support + drivers).
    // Support components (SystemMonitor, TelemetryManager) use the bus for
    // postInternalTelemetry() to push data to external TCP clients.
    if (system_core::system_component::isSchedulable(comp->componentType())) {
      comp->setInternalBus(interface_.get());
      sysLog_->debug(
          label(),
          fmt::format("Set internal bus for {} (0x{:06X})", comp->label(), comp->fullUid()), 2);
    }
  }

  sysLog_->info(label(), fmt::format("Auto-configured {} registered components",
                                     registeredComponents_.size()));
}

/* ----------------------------- Thread Orchestration ----------------------------- */

// Thread orchestration and lifecycle management
RunResult ApexExecutive::run() noexcept {
  std::array<std::thread, MAX_PRIMARY_THREADS> threads;
  std::array<std::promise<std::uint8_t>, MAX_PRIMARY_THREADS> promises;
  std::array<std::future<std::uint8_t>, MAX_PRIMARY_THREADS> workers;

  // Initialize component registry for collision detection
  componentRegistry_ = std::make_unique<system_core::system_component::ComponentRegistry>();

  std::filesystem::path tprmDir = fileSystem_.tprmDir();

  // Register application-specific components (derived class hook)
  if (!registerComponents()) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_COMPONENT_COLLISION),
                   "Failed to register application components");
    setStatus(static_cast<std::uint8_t>(ERROR_COMPONENT_COLLISION));
    return RunResult::ERROR_INIT;
  }

  // Wire scheduler to component resolver (registry) for task lookup
  scheduler_.setComponentResolver(&registry_);

  // Sync scheduler fundamental frequency with executive clock rate (must be before loadTprm)
  scheduler_.setFundamentalFreq(clockFrequency_);

  // Load scheduler TPRM and wire tasks (scheduler handles its own TPRM)
  if (!scheduler_.loadTprm(tprmDir)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_SCHEDULER_NO_TASKS),
                   fmt::format("Scheduler loadTprm failed: {}",
                               scheduler_.lastError() ? scheduler_.lastError() : "unknown"));
  }

  // Register scheduled tasks with registry (scheduler loaded them, executive registers for
  // introspection)
  for (const auto& entry : scheduler_.entries()) {
    if (entry.task != nullptr) {
      auto status = registry_.registerTask(entry.fullUid, entry.taskUid,
                                           entry.task->getLabel().data(), entry.task);
      if (system_core::registry::isError(status)) {
        sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                         fmt::format("Task registration failed for {} (fullUid=0x{:06X}): {}",
                                     entry.task->getLabel(), entry.fullUid,
                                     system_core::registry::toString(status)));
      }
    }
  }

  // Initialize interface for external command/telemetry (before registry freeze)
  sysLog_->info(label(), "Initializing interface...");
  interface_ = std::make_unique<system_core::interface::ApexInterface>();
  interface_->initInterfaceLog(fileSystem_.coreLogDir());

  // Load interface TPRM (component self-loads using its componentId)
  if (!interface_->loadTprm(tprmDir)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_MODULE_INIT_FAIL),
                   "Interface loadTprm FAILED");
  } else {
    const std::uint8_t IFACE_INIT = interface_->init();
    if (IFACE_INIT != 0) {
      sysLog_->error(label(), IFACE_INIT, "Interface init FAILED");
    } else {
      sysLog_->info(label(), "Interface init: SUCCESS");
      sysLog_->info(label(), fmt::format("Interface log: {}",
                                         (fileSystem_.coreLogDir() /
                                          system_core::interface::InterfaceBase::INTERFACE_LOG_FN)
                                             .string()));
    }
  }

  // Register core components in registry (executive, scheduler, filesystem, registry, interface)
  // Note: Executive must be first so it gets fullUid=0x000000 for command routing.
  setInstanceIndex(0); // Executive is always instance 0.
  auto execRegStatus = registry_.registerComponent(fullUid(), componentName(),
                                                   static_cast<void*>(this), componentType());
  if (system_core::registry::isError(execRegStatus)) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(execRegStatus),
                     fmt::format("Executive registry registration failed: {}",
                                 system_core::registry::toString(execRegStatus)));
  } else {
    sysLog_->info(label(),
                  fmt::format("Registered {} (fullUid=0x{:06X})", componentName(), fullUid()));
  }

  // Register executive tunable params for INSPECT readback
  {
    auto tprmStatus =
        registry_.registerData(fullUid(), system_core::data::DataCategory::TUNABLE_PARAM,
                               "tunableParams", &tunableParams_, sizeof(ExecutiveTunableParams));
    if (system_core::registry::isError(tprmStatus)) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(tprmStatus),
                       fmt::format("Executive tunableParams registration failed: {}",
                                   system_core::registry::toString(tprmStatus)));
    }
  }

  // Register executive health packet as OUTPUT for INSPECT readback.
  // The packet is populated on each GET_HEALTH call and on each INSPECT read.
  {
    getHealthPacket(); // Populate initial snapshot.
    auto status = registry_.registerData(fullUid(), system_core::data::DataCategory::OUTPUT,
                                         "health", &healthPacket_, sizeof(ExecutiveHealthPacket));
    if (system_core::registry::isError(status)) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                       fmt::format("Executive health registration failed: {}",
                                   system_core::registry::toString(status)));
    }
  }

  registerCoreComponent(scheduler_);
  registerCoreComponent(fileSystem_);
  registerCoreComponent(registry_);
  registerCoreComponent(*interface_);

  // Wire and register ActionComponent (watchpoints, sequences, data-write engine)
  actionComp_.setResolver(actionResolverFn, static_cast<void*>(&registry_));
  actionComp_.setCommandHandler(actionCommandHandlerFn, static_cast<void*>(&registry_));
  {
    const std::uint8_t ACTION_INIT = actionComp_.init();
    if (ACTION_INIT != 0) {
      sysLog_->error(label(), ACTION_INIT,
                     fmt::format("ActionComponent init FAILED: {}",
                                 actionComp_.lastError() ? actionComp_.lastError() : "unknown"));
    } else {
      sysLog_->info(label(), "ActionComponent init: SUCCESS");
    }
  }
  registerCoreComponent(actionComp_);

  // Load action engine TPRM (watchpoints, groups, sequences, notifications, actions)
  actionComp_.initComponentLog(fileSystem_.logDir());
  if (!actionComp_.loadTprm(fileSystem_.tprmDir())) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
                     fmt::format("loadTprm failed for {} (0x{:06X})", actionComp_.label(),
                                 actionComp_.fullUid()));
  }

  // Auto-load standalone RTS/ATS sequences from banked directories.
  // Files are named {slot:03d}.rts / {slot:03d}.ats (placed by unpackMasterTprm routing).
  {
    std::error_code ec;
    const std::filesystem::path RTS_DIR = fileSystem_.rtsDir();
    const std::filesystem::path ATS_DIR = fileSystem_.atsDir();

    for (std::filesystem::directory_iterator it(RTS_DIR, ec), end; !ec && it != end;
         it.increment(ec)) {
      const auto& ENTRY = *it;
      if (!ENTRY.is_regular_file(ec) || ENTRY.path().extension() != ".rts") {
        continue;
      }
      // Parse slot from filename: "004.rts" -> slot 4
      const std::string STEM = ENTRY.path().stem().string();
      char* endPtr = nullptr;
      const unsigned long SLOT = std::strtoul(STEM.c_str(), &endPtr, 10);
      if (endPtr == STEM.c_str() || SLOT >= system_core::data::SEQUENCE_TABLE_SIZE) {
        sysLog_->warning(label(), 0, fmt::format("Skipping RTS file with invalid slot: {}", STEM));
        continue;
      }
      if (!actionComp_.loadRts(static_cast<std::uint8_t>(SLOT), ENTRY.path())) {
        sysLog_->warning(label(), 0, fmt::format("Failed to load RTS slot {} from {}", SLOT, STEM));
      }
    }

    ec.clear();
    for (std::filesystem::directory_iterator it(ATS_DIR, ec), end; !ec && it != end;
         it.increment(ec)) {
      const auto& ENTRY = *it;
      if (!ENTRY.is_regular_file(ec) || ENTRY.path().extension() != ".ats") {
        continue;
      }
      const std::string STEM = ENTRY.path().stem().string();
      char* endPtr = nullptr;
      const unsigned long SLOT = std::strtoul(STEM.c_str(), &endPtr, 10);
      if (endPtr == STEM.c_str() || SLOT >= system_core::data::SEQUENCE_TABLE_SIZE) {
        sysLog_->warning(label(), 0, fmt::format("Skipping ATS file with invalid slot: {}", STEM));
        continue;
      }
      if (!actionComp_.loadAts(static_cast<std::uint8_t>(SLOT), ENTRY.path())) {
        sysLog_->warning(label(), 0, fmt::format("Failed to load ATS slot {} from {}", SLOT, STEM));
      }
    }
  }

  // Register action engine stats as OUTPUT data
  {
    auto status = registry_.registerData(
        actionComp_.fullUid(), system_core::data::DataCategory::OUTPUT, "health",
        const_cast<void*>(static_cast<const void*>(&actionComp_.stats())),
        sizeof(system_core::data::EngineStats));
    if (system_core::registry::isError(status)) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                       fmt::format("Action stats registration failed: {}",
                                   system_core::registry::toString(status)));
    } else {
      sysLog_->debug(label(),
                     fmt::format("Registered action stats as OUTPUT ({} bytes)",
                                 sizeof(system_core::data::EngineStats)),
                     2);
    }
  }

  // Register interface stats as OUTPUT data
  {
    auto status = registry_.registerData(
        interface_->fullUid(), system_core::data::DataCategory::OUTPUT, "health",
        const_cast<void*>(static_cast<const void*>(&interface_->stats())),
        sizeof(system_core::interface::ApexInterface::Stats));
    if (system_core::registry::isError(status)) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                       fmt::format("Interface stats registration failed: {}",
                                   system_core::registry::toString(status)));
    } else {
      sysLog_->debug(label(),
                     fmt::format("Registered interface stats as OUTPUT ({} bytes)",
                                 sizeof(system_core::interface::ApexInterface::Stats)),
                     2);
    }
  }

  // NOTE: Registry freeze moved to after all initialization (scheduler, interface, etc.)
  // so that core component registerData calls in doInit() are captured.

  // NOTE: Registry log/export moved to after freeze (so all data entries are captured)

  // Initialize scheduler
  sysLog_->info(label(), "Initializing scheduler...");
  const std::uint8_t SCHED_STATUS = scheduler_.init();
  if (SCHED_STATUS != 0) {
    const char* SCHED_ERR = scheduler_.lastError();
    sysLog_->error(
        label(), SCHED_STATUS,
        fmt::format("Scheduler init FAILED: {}", SCHED_ERR ? SCHED_ERR : "unknown error"));
    // Note: Currently continuing despite failure - scheduler is critical
  } else {
    sysLog_->info(label(), fmt::format("Scheduler init: SUCCESS ({})", scheduler_.label()));
  }

  // Configure scheduler for RT mode
  if (rtConfig_.mode == RTMode::SOFT_SKIP_ON_BUSY) {
    scheduler_.setSkipOnBusy(true);
    sysLog_->info(label(), "Scheduler configured for SKIP_ON_BUSY mode");
  }

  // Log scheduler log location
  sysLog_->info(
      label(),
      fmt::format("Scheduler log: {}",
                  (fileSystem_.coreLogDir() / system_core::scheduler::SCHED_LOG_FN).string()));

  // Export scheduler database
  auto schedExportStatus = scheduler_.exportSchedule(fileSystem_.dbDir());
  if (schedExportStatus == system_core::scheduler::Status::SUCCESS) {
    sysLog_->info(label(), fmt::format("Scheduler database: {}",
                                       (fileSystem_.dbDir() / "sched.rdat").string()));
  } else {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
                     fmt::format("Scheduler database export failed: {}",
                                 system_core::scheduler::toString(schedExportStatus)));
  }

  // Connect interface to component resolver for command routing.
  // RegistryBase implements IComponentResolver and is passed directly.
  interface_->setComponentResolver(&registry_);

  // Set filesystem root for file transfer handler.
  interface_->setFileSystemRoot(fileSystem_.root());

  // Auto-configure registered components: allocate queues and set internal bus for models
  configureRegisteredComponents();

  // Derived class hook for additional configuration (if needed)
  configureComponents();

  // Register core component data descriptors with registry.
  // Core components use registerCoreComponent() which doesn't iterate data
  // descriptors like registerComponent() does for app components. By this point
  // all core components have been initialized and their doInit() has run,
  // so their registerData() calls have populated their descriptor lists.
  {
    system_core::system_component::SystemComponentBase* coreComps[] = {
        &scheduler_, &fileSystem_, &registry_, interface_.get(), &actionComp_,
    };
    for (auto* comp : coreComps) {
      if (comp == nullptr)
        continue;
      for (std::size_t i = 0; i < comp->dataCount(); ++i) {
        const auto* desc = comp->dataDescriptor(i);
        if (desc != nullptr && desc->ptr != nullptr) {
          auto status = registry_.registerData(comp->fullUid(), desc->category, desc->name,
                                               desc->ptr, desc->size);
          if (system_core::registry::isError(status)) {
            sysLog_->warning(label(), static_cast<std::uint8_t>(status),
                             fmt::format("Failed to register data '{}' for {} (0x{:06X})",
                                         desc->name, comp->componentName(), comp->fullUid()));
          }
        }
      }
    }
  }

  // Freeze registry (moved here from earlier so all data descriptors are captured)
  auto freezeStatus = registry_.freeze();
  if (system_core::registry::isError(freezeStatus)) {
    sysLog_->error(
        label(), static_cast<std::uint8_t>(freezeStatus),
        fmt::format("Registry freeze failed: {}", system_core::registry::toString(freezeStatus)));
  } else {
    sysLog_->info(label(), fmt::format("Registry frozen: {} components, {} tasks, {} data entries",
                                       registry_.componentCount(), registry_.taskCount(),
                                       registry_.dataCount()));
  }

  // Initialize registry log and output comprehensive contents (after freeze)
  registry_.initRegistryLog(fileSystem_.coreLogDir());
  registry_.logRegistryContents();
  sysLog_->info(label(), fmt::format("Registry log: {}",
                                     (fileSystem_.coreLogDir() / "registry.log").string()));

  // Export registry database
  {
    auto exportStatus = registry_.exportDatabase(fileSystem_.dbDir());
    if (exportStatus == system_core::registry::Status::SUCCESS) {
      sysLog_->info(label(), fmt::format("Registry database: {}",
                                         (fileSystem_.dbDir() / "registry.rdat").string()));
    } else {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_REGISTRY_EXPORT),
                       fmt::format("Registry database export failed: {}",
                                   system_core::registry::toString(exportStatus)));
    }
  }

  // Allocate queues for interface itself (self-command routing for deterministic timing).
  // QueueManager maintains fullUid -> queues mapping; components use IInternalBus for messaging.
  {
    auto* ifaceQueues = interface_->allocateQueues(interface_->fullUid());
    if (ifaceQueues != nullptr) {
      sysLog_->debug(label(),
                     fmt::format("Allocated queues for {} (0x{:06X})", interface_->componentName(),
                                 interface_->fullUid()),
                     2);
    } else {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_QUEUE_ALLOC_FAIL),
                       fmt::format("Failed to allocate queues for {} (0x{:06X})",
                                   interface_->componentName(), interface_->fullUid()));
    }
  }

  // Freeze queue allocation (no more queues can be allocated after this).
  interface_->freezeQueues();

  for (std::uint8_t i = 0; i < MAX_PRIMARY_THREADS; ++i) {
    workers[i] = promises[i].get_future();
    threads[i] = std::thread(&ApexExecutive::runPrimaryThread, this, i, std::move(promises[i]));
  }

  workers[STARTUP].get();

  std::uint8_t future = workers[SHUTDOWN].get();

  future = workers[CLOCK].get();
  sysLog_->info(label(), fmt::format("Internal clock thread shutdown: {}", future));

  future = workers[TASK_EXECUTION].get();
  sysLog_->info(label(), fmt::format("Task execution thread shutdown: {}", future));

  future = workers[WATCHDOG].get();
  sysLog_->info(label(), fmt::format("Watchdog thread shutdown: {}", future));

  // External I/O uses poll() with timeout, should exit within 100ms
  externalIOShouldStop_.store(true, std::memory_order_release);

  if (workers[EXTERNAL_IO].wait_for(std::chrono::milliseconds(200)) == std::future_status::ready) {
    future = workers[EXTERNAL_IO].get();
    sysLog_->info(label(), fmt::format("External I/O thread shutdown: {}", future));
  } else {
    sysLog_->warning(
        label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
        "External I/O thread did not exit within 200ms (expected <100ms poll timeout)");
    future = workers[EXTERNAL_IO].get();
    sysLog_->info(label(), fmt::format("External I/O thread shutdown (delayed): {}", future));
  }

  for (size_t i = 0; i < threads.size(); ++i) {
    if (threads[i].joinable()) {
      threads[i].join();
    }
  }

  sysLog_->info(label(), "System shutdown completed");

  // CRITICAL: Flush all logs to disk before exit
  sysLog_->flush();
  profLog_->flush();
  heartbeatLog_->flush();

  return RunResult::SUCCESS;
}

void ApexExecutive::runPrimaryThread(std::uint8_t threadId,
                                     std::promise<std::uint8_t>&& p) noexcept {
  // Thread names for debugging and logging
  const char* threadNames[] = {"exec_startup", "exec_shutdown", "exec_clock",
                               "exec_tasks",   "exec_extio",    "exec_watchdog"};
  const char* threadName =
      (threadId < MAX_PRIMARY_THREADS) ? threadNames[threadId] : "exec_unknown";

  // Get thread configuration based on thread ID
  const PrimaryThreadConfig* config = nullptr;
  switch (threadId) {
  case STARTUP:
    config = &threadConfig_.startup;
    break;
  case SHUTDOWN:
    config = &threadConfig_.shutdown;
    break;
  case CLOCK:
    config = &threadConfig_.clock;
    break;
  case TASK_EXECUTION:
    config = &threadConfig_.taskExecution;
    break;
  case EXTERNAL_IO:
    config = &threadConfig_.externalIO;
    break;
  case WATCHDOG:
    config = &threadConfig_.watchdog;
    break;
  default:
    break;
  }

  // Apply thread configuration (name, affinity, scheduling policy/priority)
  if (config != nullptr) {
    apex::concurrency::ThreadConfig tc{};
    tc.policy = config->policy;
    tc.priority = config->priority;
    tc.affinity = config->affinity;
    if (!apex::concurrency::applyThreadConfig(tc, threadName)) {
      // Log warning but continue - config may require elevated privileges
      sysLog_->warning(
          label(), static_cast<std::uint8_t>(WARN_THREAD_CONFIG_FAIL),
          fmt::format("Failed to apply RT config to thread '{}' (may require CAP_SYS_NICE)",
                      threadName));
    }
  } else {
    // No config, just set thread name
#if defined(__linux__)
    pthread_setname_np(pthread_self(), threadName);
#elif defined(__APPLE__)
    pthread_setname_np(threadName);
#endif
  }

  switch (threadId) {
  case STARTUP:
    startup(std::move(p));
    break;
  case SHUTDOWN:
    shutdownThread(std::move(p));
    break;
  case TASK_EXECUTION:
    executeTasks(std::move(p));
    break;
  case CLOCK:
    clock(std::move(p));
    break;
  case EXTERNAL_IO:
    externalIO(std::move(p));
    break;
  case WATCHDOG:
    watchdog(std::move(p));
    break;
  default:
    sysLog_->error(label(), status(), fmt::format("Unknown thread id: {}", threadId));
    p.set_value(static_cast<std::uint8_t>(ERROR_RUNTIME_FAILURE));
    break;
  }
}

/* ----------------------------- TPRM Handling ----------------------------- */

bool ApexExecutive::unpackMasterTprm() noexcept {
  const std::filesystem::path TPRM_DIR = fileSystem_.tprmDir();

  // Verify config file exists
  if (!std::filesystem::exists(configPath_)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_CONFIG_NOT_FOUND),
                   fmt::format("Config file not found: {}", configPath_.string()));
    setStatus(static_cast<std::uint8_t>(ERROR_CONFIG_NOT_FOUND));
    return false;
  }

  // Load and extract packed TPRM
  PackedTprmReader reader;
  std::string error;

  if (!reader.load(configPath_, error)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_TPRM_UNPACK_FAIL),
                   fmt::format("Failed to load packed TPRM: {}", error));
    setStatus(static_cast<std::uint8_t>(ERROR_TPRM_UNPACK_FAIL));
    return false;
  }

  sysLog_->info(label(), fmt::format("Loaded packed TPRM: {} entries", reader.count()));

  if (!reader.extractAll(TPRM_DIR, error)) {
    sysLog_->error(label(), static_cast<std::uint8_t>(ERROR_TPRM_UNPACK_FAIL),
                   fmt::format("Failed to extract packed TPRM: {}", error));
    setStatus(static_cast<std::uint8_t>(ERROR_TPRM_UNPACK_FAIL));
    return false;
  }

  sysLog_->info(label(), fmt::format("Extracted TPRMs to: {}", TPRM_DIR.string()));

  // Route RTS/ATS entries from tprmDir to their banked sequence directories.
  // PackedTprmReader extracts all entries as {fullUid:06x}.tprm. Entries with
  // reserved fullUid ranges (0xFF0000 = RTS, 0xFE0000 = ATS) are moved to the
  // filesystem's rtsDir/atsDir with appropriate extensions.
  std::error_code ec;
  std::uint8_t rtsRouted = 0;
  std::uint8_t atsRouted = 0;

  for (const auto& entry : reader.entries()) {
    const std::uint32_t UID = entry.fullUid;
    const bool IS_RTS = isRtsEntry(UID);
    const bool IS_ATS = isAtsEntry(UID);

    if (!IS_RTS && !IS_ATS) {
      continue;
    }

    const std::uint8_t SLOT = sequenceSlot(UID);
    char srcName[32];
    std::snprintf(srcName, sizeof(srcName), "%06x.tprm", UID);
    const std::filesystem::path SRC_PATH = TPRM_DIR / srcName;

    char dstName[32];
    std::snprintf(dstName, sizeof(dstName), "%03d.%s", SLOT, IS_RTS ? "rts" : "ats");
    const std::filesystem::path DST_DIR = IS_RTS ? fileSystem_.rtsDir() : fileSystem_.atsDir();
    const std::filesystem::path DST_PATH = DST_DIR / dstName;

    std::filesystem::rename(SRC_PATH, DST_PATH, ec);
    if (ec) {
      sysLog_->warning(
          label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
          fmt::format("Failed to route {} to {}: {}", srcName, DST_PATH.string(), ec.message()));
      ec.clear();
    } else {
      IS_RTS ? ++rtsRouted : ++atsRouted;
    }
  }

  if (rtsRouted > 0 || atsRouted > 0) {
    sysLog_->info(label(), fmt::format("Routed sequences: {} RTS, {} ATS", rtsRouted, atsRouted));
  }

  return true;
}

bool ApexExecutive::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  // Generate filename from executive fullUid (componentId << 8 | instance 0 -> "000000.tprm")
  // Note: Executive is always instance 0 and loads TPRM before registration
  const std::uint32_t FULL_UID = static_cast<std::uint32_t>(componentId()) << 8;
  std::filesystem::path tprmPath =
      tprmDir / system_core::system_component::SystemComponentBase::tprmFilename(FULL_UID);

  if (!std::filesystem::exists(tprmPath)) {
    sysLog_->info(label(),
                  fmt::format("No executive TPRM found at {}, using defaults", tprmPath.string()));
    return true; // Not an error - use defaults
  }

  // Read binary TPRM
  std::ifstream file(tprmPath, std::ios::binary);
  if (!file) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
                     fmt::format("Failed to open executive TPRM: {}", tprmPath.string()));
    return false;
  }

  ExecutiveTunableParams params{};
  file.read(reinterpret_cast<char*>(&params), sizeof(params));
  if (file.gcount() != sizeof(params)) {
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
                     fmt::format("Executive TPRM size mismatch (got {}, expected {})",
                                 file.gcount(), sizeof(params)));
    return false;
  }

  // Apply loaded parameters to executive configuration
  tunableParams_ = params;
  clockFrequency_ = params.clockFrequencyHz;
  rtConfig_.mode = static_cast<RTMode>(params.rtMode);
  rtConfig_.maxLagTicks = params.rtMaxLagTicks;
  startupConfig_.mode = static_cast<StartupConfig::Mode>(params.startupMode);
  startupConfig_.delaySeconds = params.startupDelaySeconds;
  shutdownConfig_.mode = static_cast<ShutdownConfig::Mode>(params.shutdownMode);
  shutdownConfig_.skipCleanup = (params.skipCleanup != 0);
  shutdownConfig_.relativeSeconds = params.shutdownAfterSeconds;
  shutdownConfig_.targetClockCycle = params.shutdownAtCycle;
  watchdogState_.intervalMs = params.watchdogIntervalMs;
  profilingState_.sampleEveryN = params.profilingSampleEveryN;

  // Read thread configuration (follows tunable params in TPRM file)
  ExecutiveThreadConfigTprm threadConfigTprm{};
  file.read(reinterpret_cast<char*>(&threadConfigTprm), sizeof(threadConfigTprm));
  if (file.gcount() == sizeof(threadConfigTprm)) {
    // Convert TPRM entries to runtime config
    threadConfigFromTprm(threadConfigTprm.startup, threadConfig_.startup);
    threadConfigFromTprm(threadConfigTprm.shutdown, threadConfig_.shutdown);
    threadConfigFromTprm(threadConfigTprm.clock, threadConfig_.clock);
    threadConfigFromTprm(threadConfigTprm.taskExecution, threadConfig_.taskExecution);
    threadConfigFromTprm(threadConfigTprm.externalIO, threadConfig_.externalIO);
    threadConfigFromTprm(threadConfigTprm.watchdog, threadConfig_.watchdog);
  }
  // If thread config not present, defaults remain (all OTHER/0/[all])

  sysLog_->info(label(), fmt::format("Loaded executive TPRM from: {}", tprmPath.string()));
  return true;
}

void ApexExecutive::applyCliOverrides() noexcept {
  // CLI arguments take precedence over TPRM values.
  // Note: processArgs() already parsed CLI args into parsedArgs_ and applied some directly.
  // This function ensures CLI overrides are re-applied after TPRM loading.

  // Clock frequency (not exposed via CLI currently, but could be added)

  // RT mode override
  if (parsedArgs_.count(RT_MODE)) {
    std::string_view modeStr = parsedArgs_[RT_MODE][0];
    RTMode mode{};
    if (parseRTMode(modeStr, mode)) {
      rtConfig_.mode = mode;
    }
  }

  if (parsedArgs_.count(RT_MAX_LAG)) {
    rtConfig_.maxLagTicks = std::stoul(std::string(parsedArgs_[RT_MAX_LAG][0]));
  }

  // Startup mode override
  if (parsedArgs_.count(STARTUP_MODE)) {
    std::string_view mode = parsedArgs_[STARTUP_MODE][0];
    if (mode == "auto") {
      startupConfig_.mode = StartupConfig::AUTO;
    } else if (mode == "interactive") {
      startupConfig_.mode = StartupConfig::INTERACTIVE;
    } else if (mode == "scheduled") {
      startupConfig_.mode = StartupConfig::SCHEDULED;
    }
  }

  if (parsedArgs_.count(STARTUP_DELAY)) {
    startupConfig_.delaySeconds = std::stoul(std::string(parsedArgs_[STARTUP_DELAY][0]));
  }

  if (parsedArgs_.count(START_AT)) {
    startupConfig_.startAtEpochNs = std::stoll(std::string(parsedArgs_[START_AT][0]));
  }

  // Shutdown mode override
  if (parsedArgs_.count(SHUTDOWN_MODE)) {
    std::string_view mode = parsedArgs_[SHUTDOWN_MODE][0];
    if (mode == "signal") {
      shutdownConfig_.mode = ShutdownConfig::SIGNAL_ONLY;
    } else if (mode == "scheduled") {
      shutdownConfig_.mode = ShutdownConfig::SCHEDULED;
    } else if (mode == "relative") {
      shutdownConfig_.mode = ShutdownConfig::RELATIVE_TIME;
    } else if (mode == "cycle") {
      shutdownConfig_.mode = ShutdownConfig::CLOCK_CYCLE;
    } else if (mode == "combined") {
      shutdownConfig_.mode = ShutdownConfig::COMBINED;
    }
  }

  if (parsedArgs_.count(SHUTDOWN_AT)) {
    shutdownConfig_.shutdownAtEpochNs = std::stoll(std::string(parsedArgs_[SHUTDOWN_AT][0]));
  }

  if (parsedArgs_.count(SHUTDOWN_AFTER)) {
    shutdownConfig_.relativeSeconds = std::stoul(std::string(parsedArgs_[SHUTDOWN_AFTER][0]));
  }

  if (parsedArgs_.count(SHUTDOWN_CYCLE)) {
    shutdownConfig_.targetClockCycle = std::stoull(std::string(parsedArgs_[SHUTDOWN_CYCLE][0]));
  }

  // Archive configuration
  if (parsedArgs_.count(SKIP_CLEANUP)) {
    shutdownConfig_.skipCleanup = true;
  }

  if (parsedArgs_.count(ARCHIVE_PATH)) {
    archivePath_ = parsedArgs_[ARCHIVE_PATH][0];
  }

  // Watchdog interval override
  if (parsedArgs_.count(WATCHDOG_INTERVAL)) {
    watchdogState_.intervalMs = std::stoul(std::string(parsedArgs_[WATCHDOG_INTERVAL][0]));
  }

  // Profiling override
  if (parsedArgs_.count(ENABLE_PROFILING)) {
    if (parsedArgs_.count(PROFILE_INTERVAL)) {
      profilingState_.sampleEveryN = std::stoul(std::string(parsedArgs_[PROFILE_INTERVAL][0]));
    } else {
      profilingState_.sampleEveryN = 1;
    }
  }

  // Verbosity override (already applied in processArgs, but re-apply for clarity)
  if (parsedArgs_.count(VERBOSITY)) {
    const std::uint8_t verbosity =
        static_cast<std::uint8_t>(std::stoul(std::string(parsedArgs_[VERBOSITY][0])));
    sysLog_->setVerbosity(verbosity);
  }
}

/* ----------------------------- IExecutive Interface ----------------------------- */

void ApexExecutive::shutdown() noexcept {
  controlState_.shutdownRequested.store(true, std::memory_order_release);
  cvShutdown_.notify_all();
  cvClockTick_.notify_all();
  cvPause_.notify_all();
}

bool ApexExecutive::isShutdownRequested() const noexcept {
  return controlState_.shutdownRequested.load(std::memory_order_acquire);
}

uint64_t ApexExecutive::cycleCount() const noexcept {
  return clockState_.cycles.load(std::memory_order_acquire);
}