/**
 * @file ApexExecutive_Commands.cpp
 * @brief Implementation of command handling and health packet for ApexExecutive.
 *
 * Handles all executive opcodes (0x0100+):
 *  - Query commands: GET_HEALTH, GET_CLOCK_FREQ, GET_RT_MODE, GET_CLOCK_CYCLES
 *  - Control commands: PAUSE, RESUME, SHUTDOWN, FAST_FORWARD, SLEEP, WAKE
 *  - Set commands: SET_CLOCK_FREQ, SET_VERBOSITY
 *  - Runtime update: LOCK/UNLOCK, RELOAD_TPRM, RELOAD_LIBRARY, RELOAD_EXECUTIVE
 *  - Ground test: INSPECT
 */

#include "src/system/core/executive/apex/inc/ApexExecutive.hpp"
#include "src/system/core/executive/apex/inc/ExecutiveStatus.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/PackedTprm.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include "src/system/core/components/scheduler/apex/inc/SchedulerData.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>

#include <fmt/core.h>

using namespace executive;
using enum executive::Status;
using executive::ExecCommandResult;
using system_core::system_component::CommandResult;

/* ----------------------------- Health Packet ----------------------------- */

ExecutiveHealthPacket ApexExecutive::getHealthPacket() const noexcept {
  ExecutiveHealthPacket packet{};

  // Clock state
  packet.clockCycles = clockState_.cycles.load(std::memory_order_acquire);
  packet.clockFrequencyHz = clockFrequency_;
  packet.frameOverrunCount = clockState_.overrunCount.load(std::memory_order_acquire);

  // Task execution state
  packet.taskExecutionCycles = taskState_.cycles.load(std::memory_order_acquire);

  // Watchdog state
  packet.watchdogWarningCount = watchdogState_.warningCount;

  // RT mode
  packet.rtMode = static_cast<std::uint8_t>(rtConfig_.mode);

  // Control state flags
  packet.flags = 0;
  if (clockState_.isRunning.load(std::memory_order_acquire)) {
    packet.flags |= ExecutiveHealthPacket::FLAG_CLOCK_RUNNING;
  }
  if (controlState_.isPaused.load(std::memory_order_acquire)) {
    packet.flags |= ExecutiveHealthPacket::FLAG_PAUSED;
  }
  if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    packet.flags |= ExecutiveHealthPacket::FLAG_SHUTDOWN_REQUESTED;
  }
  if (taskState_.lagThresholdExceeded.load(std::memory_order_acquire)) {
    packet.flags |= ExecutiveHealthPacket::FLAG_LAG_EXCEEDED;
  }
  if (scheduler_.isSleeping()) {
    packet.flags |= ExecutiveHealthPacket::FLAG_SLEEPING;
  }

  // External I/O stats
  packet.commandsProcessed = ioState_.commandsProcessed.load(std::memory_order_acquire);

  return packet;
}

/* ----------------------------- Command Dispatch ----------------------------- */

std::uint8_t ApexExecutive::handleCommand(std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload,
                                          std::vector<std::uint8_t>& response) noexcept {
  switch (opcode) {
    // === Query commands (no payload, return data) ===

  case static_cast<std::uint16_t>(Opcode::GET_HEALTH): {
    // Return 48-byte health packet.
    const ExecutiveHealthPacket PACKET = getHealthPacket();
    response.resize(sizeof(ExecutiveHealthPacket));
    std::memcpy(response.data(), &PACKET, sizeof(ExecutiveHealthPacket));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::EXEC_NOOP):
    // No-op for connectivity testing - just ACK.
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::GET_CLOCK_FREQ): {
    // Return 2-byte clock frequency.
    response.resize(2);
    std::memcpy(response.data(), &clockFrequency_, 2);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::GET_RT_MODE): {
    // Return 1-byte RT mode.
    response.resize(1);
    response[0] = static_cast<std::uint8_t>(rtConfig_.mode);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::GET_CLOCK_CYCLES): {
    // Return 8-byte clock cycle count.
    const std::uint64_t CYCLES = clockState_.cycles.load(std::memory_order_acquire);
    response.resize(8);
    std::memcpy(response.data(), &CYCLES, 8);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

    // === Control commands (no payload, trigger action) ===

  case static_cast<std::uint16_t>(Opcode::CMD_PAUSE):
    pause();
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::CMD_RESUME):
    resume();
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::CMD_SHUTDOWN):
    controlState_.shutdownRequested.store(true, std::memory_order_release);
    sysLog_->info(label(), "Shutdown requested via command");
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::CMD_FAST_FORWARD):
    fastForward();
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::CMD_SLEEP):
    if (scheduler_.isSleeping()) {
      sysLog_->debug(label(), "Sleep requested but system already sleeping", 2);
    } else {
      scheduler_.sleep();
      sysLog_->info(label(), fmt::format("System entering sleep mode at cycle {}",
                                         clockState_.cycles.load(std::memory_order_acquire)));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->info("SWAP", fmt::format("SLEEP: cycle={}",
                                     clockState_.cycles.load(std::memory_order_acquire)));
      }
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  case static_cast<std::uint16_t>(Opcode::CMD_WAKE):
    if (!scheduler_.isSleeping()) {
      sysLog_->debug(label(), "Wake requested but system not sleeping", 2);
    } else {
      scheduler_.wake();
      sysLog_->info(label(), fmt::format("System waking from sleep at cycle {}",
                                         clockState_.cycles.load(std::memory_order_acquire)));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->info("SWAP",
                 fmt::format("WAKE: cycle={}", clockState_.cycles.load(std::memory_order_acquire)));
      }
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

    // === Set commands (payload required) ===

  case static_cast<std::uint16_t>(Opcode::SET_CLOCK_FREQ): {
    if (payload.size() < 2) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint16_t newFreq = 0;
    std::memcpy(&newFreq, payload.data(), 2);
    setClockFrequency(newFreq);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::SET_VERBOSITY): {
    if (payload.size() < 1) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    const std::uint8_t VERBOSITY = payload[0];
    sysLog_->setVerbosity(VERBOSITY);
    sysLog_->info(label(), fmt::format("Verbosity set to {} via command", VERBOSITY));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

    // === Runtime update commands ===

  case static_cast<std::uint16_t>(Opcode::CMD_LOCK_COMPONENT): {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint32_t targetUid = 0;
    std::memcpy(&targetUid, payload.data(), 4);
    auto* comp = registry_.getComponent(targetUid);
    if (comp == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::TARGET_NOT_FOUND);
    }
    comp->lock();
    sysLog_->info(label(), fmt::format("Component 0x{:06X} locked for update", targetUid));
    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("LOCK: uid=0x{:06X} name={}", targetUid, comp->componentName()));
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::CMD_UNLOCK_COMPONENT): {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint32_t targetUid = 0;
    std::memcpy(&targetUid, payload.data(), 4);
    auto* comp = registry_.getComponent(targetUid);
    if (comp == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::TARGET_NOT_FOUND);
    }
    comp->unlock();
    sysLog_->info(label(), fmt::format("Component 0x{:06X} unlocked", targetUid));
    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("UNLOCK: uid=0x{:06X}", targetUid));
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::RELOAD_TPRM): {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint32_t targetUid = 0;
    std::memcpy(&targetUid, payload.data(), 4);
    auto* comp = registry_.getComponent(targetUid);
    if (comp == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::TARGET_NOT_FOUND);
    }

    const std::string TPRM_FN =
        system_core::system_component::SystemComponentBase::tprmFilename(targetUid);

    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_TPRM_BEGIN: uid=0x{:06X} file={} src={}", targetUid,
                                   TPRM_FN, fileSystem_.inactiveTprmDir().string()));
    }

    // Lock the component so the scheduler skips its tasks during reload.
    // This prevents data races when loadTprm() modifies shared state
    // (e.g. scheduler's entries_ and schedule_ vectors).
    comp->lock();

    // Load TPRM from inactive bank (C2 uploaded it there via FILE_TRANSFER).
    if (!comp->loadTprm(fileSystem_.inactiveTprmDir())) {
      comp->unlock();
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_TPRM_LOAD_FAIL),
                       fmt::format("TPRM reload failed for component 0x{:06X}", targetUid));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(CommandResult::LOAD_FAILED),
                    fmt::format("RELOAD_TPRM_FAIL: uid=0x{:06X} reason=loadTprm", targetUid));
      }
      return static_cast<std::uint8_t>(CommandResult::LOAD_FAILED);
    }
    // Swap the TPRM file between banks (inactive -> active, active -> inactive).
    if (!fileSystem_.swapBankFile(system_core::filesystem::TPRM_DIR, TPRM_FN)) {
      // Non-fatal: TPRM reloaded in memory but on-disk state is stale.
      sysLog_->warning(
          label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
          fmt::format("TPRM bank file swap failed for {}, on-disk state may be stale", TPRM_FN));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                    fmt::format("RELOAD_TPRM_WARN: uid=0x{:06X} bank_swap_failed file={}",
                                targetUid, TPRM_FN));
      }
    }

    comp->unlock();

    sysLog_->info(label(),
                  fmt::format("TPRM reloaded for component 0x{:06X} (bank swapped)", targetUid));
    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_TPRM_OK: uid=0x{:06X} file={}", targetUid, TPRM_FN));
    }
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::RELOAD_LIBRARY): {
    if (payload.size() < 4) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint32_t targetUid = 0;
    std::memcpy(&targetUid, payload.data(), 4);

    // Component must exist and be locked.
    auto* oldComp = registry_.getComponent(targetUid);
    if (oldComp == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::TARGET_NOT_FOUND);
    }
    if (!oldComp->isLocked()) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: component 0x{:06X} not locked", targetUid));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(ExecCommandResult::NOT_SWAPPABLE),
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=not_locked", targetUid));
      }
      return static_cast<std::uint8_t>(ExecCommandResult::NOT_SWAPPABLE);
    }

    // Build .so name and look in inactive bank.
    const std::string SO_NAME =
        fmt::format("{}_{}.so", oldComp->componentName(), oldComp->instanceIndex());
    const std::filesystem::path SO_PATH = fileSystem_.inactiveLibDir() / SO_NAME;

    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_LIBRARY_BEGIN: uid=0x{:06X} so={} src={}", targetUid,
                                   SO_NAME, SO_PATH.string()));
    }

    if (!std::filesystem::exists(SO_PATH)) {
      sysLog_->warning(
          label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
          fmt::format("RELOAD_LIBRARY: .so not found in inactive bank: {}", SO_PATH.string()));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning(
            "SWAP", static_cast<std::uint8_t>(ExecCommandResult::DLOPEN_FAILED),
            fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=so_not_found", targetUid));
      }
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::DLOPEN_FAILED);
    }

    // Load new plugin from inactive bank.
    system_core::system_component::PluginLoader loader;
    const std::uint8_t LOAD_RC = loader.load(SO_PATH);
    if (LOAD_RC != 0) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: load failed (rc={}): {}", LOAD_RC,
                                   loader.lastError() ? loader.lastError() : "unknown"));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", LOAD_RC,
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=dlopen_failed rc={}",
                                targetUid, LOAD_RC));
      }
      oldComp->unlock();
      return LOAD_RC;
    }

    auto* newComp = loader.component();

    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_LIBRARY_LOAD_OK: uid=0x{:06X} componentId={}", targetUid,
                                   newComp->componentId()));
    }

    // Validate: new component must match identity.
    if (newComp->componentId() != oldComp->componentId()) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: componentId mismatch (old={}, new={})",
                                   oldComp->componentId(), newComp->componentId()));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning(
            "SWAP", static_cast<std::uint8_t>(ExecCommandResult::NOT_SWAPPABLE),
            fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=id_mismatch old={} new={}",
                        targetUid, oldComp->componentId(), newComp->componentId()));
      }
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::NOT_SWAPPABLE);
    }

    // Transfer identity from old component.
    newComp->setInstanceIndex(oldComp->instanceIndex());

    // Initialize new component: loadTprm from active bank (same config).
    if (!newComp->loadTprm(fileSystem_.tprmDir())) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       "RELOAD_LIBRARY: new component loadTprm failed");
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(ExecCommandResult::INIT_FAILED),
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=loadTprm", targetUid));
      }
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::INIT_FAILED);
    }
    const std::uint8_t INIT_RC = newComp->init();
    if (INIT_RC != 0) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: new component init failed (rc={})", INIT_RC));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(ExecCommandResult::INIT_FAILED),
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=init_failed rc={}",
                                targetUid, INIT_RC));
      }
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::INIT_FAILED);
    }

    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_LIBRARY_INIT_OK: uid=0x{:06X}", targetUid));
    }

    // Archive old component log before destroying old component.
    // Construct log path from component identity (componentLog() is protected).
    {
      const std::string LOG_FN =
          fmt::format("{}_{}.log", oldComp->componentName(), oldComp->instanceIndex());
      std::filesystem::path logPath;
      switch (oldComp->componentType()) {
      case system_core::system_component::ComponentType::SW_MODEL:
      case system_core::system_component::ComponentType::HW_MODEL:
        logPath = fileSystem_.modelLogDir() / LOG_FN;
        break;
      case system_core::system_component::ComponentType::SUPPORT:
        logPath = fileSystem_.supportLogDir() / LOG_FN;
        break;
      case system_core::system_component::ComponentType::DRIVER:
        logPath = fileSystem_.driverLogDir() / LOG_FN;
        break;
      default:
        logPath = fileSystem_.coreLogDir() / LOG_FN;
        break;
      }
      const auto ARCHIVED = fileSystem_.archiveComponentLog(logPath, oldComp->componentName(),
                                                            oldComp->instanceIndex());
      if (auto* sl = fileSystem_.swapLog()) {
        if (!ARCHIVED.empty()) {
          sl->info("SWAP", fmt::format("RELOAD_LIBRARY_LOG_ARCHIVED: uid=0x{:06X} dest={}",
                                       targetUid, ARCHIVED.parent_path().filename().string()));
        }
      }
    }

    // Count how many scheduler entries reference this component.
    std::uint8_t expectedTasks = 0;
    for (const auto& entry : scheduler_.entries()) {
      if (entry.fullUid == targetUid) {
        ++expectedTasks;
      }
    }

    // Re-wire scheduler task pointers.
    const std::uint8_t TASKS_REPLACED = scheduler_.replaceComponentTasks(targetUid, *newComp);

    // Validate: all tasks must be replaced. If any are missing, the scheduler
    // would point to freed memory when the old .so is unloaded.
    if (TASKS_REPLACED < expectedTasks) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: task mismatch (expected={}, replaced={}), "
                                   "aborting swap to prevent use-after-free",
                                   expectedTasks, TASKS_REPLACED));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(ExecCommandResult::TASK_MISMATCH),
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=task_mismatch "
                                "expected={} replaced={}",
                                targetUid, expectedTasks, TASKS_REPLACED));
      }
      // Restore old task pointers (new component had partial matches).
      scheduler_.replaceComponentTasks(targetUid, *oldComp);
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::TASK_MISMATCH);
    }

    // Update registry entry to point to new component.
    if (!registry_.updateComponent(targetUid, newComp)) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: registry update failed for 0x{:06X}, "
                                   "restoring old task pointers",
                                   targetUid));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(ExecCommandResult::REGISTRY_FAILED),
                    fmt::format("RELOAD_LIBRARY_FAIL: uid=0x{:06X} reason=registry_update_failed",
                                targetUid));
      }
      scheduler_.replaceComponentTasks(targetUid, *oldComp);
      oldComp->unlock();
      return static_cast<std::uint8_t>(ExecCommandResult::REGISTRY_FAILED);
    }

    // Update registeredComponents_ vector.
    for (auto& regComp : registeredComponents_) {
      if (regComp == oldComp) {
        regComp = newComp;
        break;
      }
    }

    // Store plugin loader (keeps .so loaded, destroys previous if any).
    plugins_[targetUid] = std::move(loader);

    // Swap the .so file between banks (inactive -> active, active -> inactive).
    // This keeps the active bank in sync with what's running, and the inactive
    // bank holds the previous version for rollback.
    if (!fileSystem_.swapBankFile(system_core::filesystem::LIB_DIR, SO_NAME)) {
      // Non-fatal: the swap succeeded in memory but the on-disk state is stale.
      // Next restart will load the old .so unless manually corrected.
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                       fmt::format("RELOAD_LIBRARY: bank file swap failed for {}, "
                                   "on-disk state may be stale",
                                   SO_NAME));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                    fmt::format("RELOAD_LIBRARY_WARN: uid=0x{:06X} bank_swap_failed so={}",
                                targetUid, SO_NAME));
      }
    }

    sysLog_->info(label(),
                  fmt::format("RELOAD_LIBRARY: component 0x{:06X} swapped ({} tasks re-wired), "
                              ".so={}",
                              targetUid, TASKS_REPLACED, SO_NAME));

    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_LIBRARY_OK: uid=0x{:06X} tasks={} so={}", targetUid,
                                   TASKS_REPLACED, SO_NAME));
    }

    // Unlock the new component so it resumes execution.
    newComp->unlock();
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::RELOAD_EXECUTIVE): {
    // Check for a new binary in the inactive bank.
    const std::filesystem::path EXEC_NAME = execPath_.filename();
    const std::filesystem::path INACTIVE_BIN = fileSystem_.inactiveBinDir() / EXEC_NAME;
    std::filesystem::path execTarget = execPath_; // Default: re-exec self.
    bool didSwapBinary = false;

    if (std::filesystem::exists(INACTIVE_BIN)) {
      // New binary available in inactive bank. Swap to active bank before exec.
      const std::string FN = EXEC_NAME.string();
      const std::filesystem::path ACTIVE_BIN = fileSystem_.binDir() / FN;

      if (auto* sl = fileSystem_.swapLog()) {
        sl->info("SWAP", fmt::format("RELOAD_EXECUTIVE_BEGIN: new_binary={}", FN));
      }

      // Copy current binary to active bank if not already there.
      std::error_code ec;
      if (!std::filesystem::exists(ACTIVE_BIN, ec)) {
        std::filesystem::copy_file(execPath_, ACTIVE_BIN, ec);
      }

      // Swap: inactive binary -> active, active -> inactive.
      if (!fileSystem_.swapBankFile(system_core::filesystem::BIN_DIR, FN)) {
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                         fmt::format("RELOAD_EXECUTIVE: bank swap failed for {}", FN));
        if (auto* sl = fileSystem_.swapLog()) {
          sl->warning("SWAP", static_cast<std::uint8_t>(CommandResult::EXEC_FAILED),
                      fmt::format("RELOAD_EXECUTIVE_FAIL: bank_swap_failed binary={}", FN));
        }
        return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
      }
      didSwapBinary = true;

      // Exec the newly active binary.
      execTarget = ACTIVE_BIN;
      sysLog_->info(label(),
                    fmt::format("Executive restart with new binary: {}", execTarget.string()));
      if (auto* sl = fileSystem_.swapLog()) {
        sl->info("SWAP", fmt::format("RELOAD_EXECUTIVE_READY: target={}", execTarget.string()));
      }
    } else {
      sysLog_->info(label(), "Executive restart (same binary)");
      if (auto* sl = fileSystem_.swapLog()) {
        sl->info("SWAP", "RELOAD_EXECUTIVE_BEGIN: same_binary_restart");
      }
    }

    // Flush all logs before exec.
    sysLog_->flush();
    if (auto* sl = fileSystem_.swapLog()) {
      sl->flush();
    }

    // Re-exec with original arguments.
    std::vector<const char*> argv;
    const std::string EXEC_STR = execTarget.string();
    argv.push_back(EXEC_STR.c_str());
    for (const auto& arg : args_) {
      argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    execv(EXEC_STR.c_str(), const_cast<char* const*>(argv.data()));

    // If execv returns, it failed. Roll back the bank swap so the next
    // restart loads the known-good binary, not the failed one.
    const int EXEC_ERRNO = errno;
    sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                     fmt::format("execv failed (errno={}): {} -- system continues with old binary",
                                 EXEC_ERRNO, std::strerror(EXEC_ERRNO)));
    if (didSwapBinary) {
      const std::string FN = EXEC_NAME.string();
      if (fileSystem_.swapBankFile(system_core::filesystem::BIN_DIR, FN)) {
        sysLog_->info(label(), "RELOAD_EXECUTIVE: bank swap rolled back after execv failure");
      } else {
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_SWAP_FAILED),
                         "RELOAD_EXECUTIVE: bank swap rollback failed, on-disk state inconsistent");
      }
      if (auto* sl = fileSystem_.swapLog()) {
        sl->warning("SWAP", static_cast<std::uint8_t>(CommandResult::EXEC_FAILED),
                    fmt::format("RELOAD_EXECUTIVE_FAIL: execv_failed errno={} rollback={}",
                                EXEC_ERRNO, didSwapBinary ? "attempted" : "n/a"));
      }
    }
    return static_cast<std::uint8_t>(CommandResult::EXEC_FAILED);
  }

    // === Ground test / inspection commands ===

  case static_cast<std::uint16_t>(Opcode::INSPECT): {
    // Payload: u32 targetFullUid | u8 category | u16 offset | u16 len = 9 bytes.
    if (payload.size() < 9) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    std::uint32_t targetUid = 0;
    std::memcpy(&targetUid, payload.data(), 4);
    const auto CAT = static_cast<system_core::data::DataCategory>(payload[4]);
    std::uint16_t offset = 0;
    std::uint16_t len = 0;
    std::memcpy(&offset, payload.data() + 5, 2);
    std::memcpy(&len, payload.data() + 7, 2);

    auto* entry = registry_.getData(targetUid, CAT);
    if (entry == nullptr) {
      return static_cast<std::uint8_t>(CommandResult::TARGET_NOT_FOUND);
    }
    if (offset >= entry->size) {
      return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
    }
    const std::size_t AVAIL = entry->size - offset;
    const std::size_t COPY_LEN = (len == 0 || len > AVAIL) ? AVAIL : len;
    auto bytes = entry->getBytes();
    response.resize(COPY_LEN);
    std::memcpy(response.data(), bytes.data() + offset, COPY_LEN);
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  default:
    // Delegate to base class for common opcodes (0x0080-0x00FF).
    return ExecutiveBase::handleCommand(opcode, payload, response);
  }
}
