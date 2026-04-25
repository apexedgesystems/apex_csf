/**
 * @file ApexExecutive_Commands.cpp
 * @brief Implementation of command handling and health packet for ApexExecutive.
 *
 * Handles all executive opcodes (0x0100+):
 *  - Query commands: GET_HEALTH, GET_CLOCK_FREQ, GET_RT_MODE, GET_CLOCK_CYCLES
 *  - Control commands: PAUSE, RESUME, SHUTDOWN, FAST_FORWARD, SLEEP, WAKE
 *  - Set commands: SET_VERBOSITY
 *  - Runtime update: LOCK/UNLOCK, RELOAD_TPRM, RELOAD_LIBRARY, RELOAD_EXECUTIVE
 *  - Ground test: INSPECT
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/PackedTprm.hpp"
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

const ExecutiveHealthPacket& ApexExecutive::getHealthPacket() const noexcept {
  // Populate the cached member (mutable for const access).
  healthPacket_.clockCycles = clockState_.cycles.load(std::memory_order_acquire);
  healthPacket_.clockFrequencyHz = clockFrequency_;
  healthPacket_.frameOverrunCount = clockState_.overrunCount.load(std::memory_order_acquire);
  healthPacket_.taskExecutionCycles = taskState_.cycles.load(std::memory_order_acquire);
  healthPacket_.watchdogWarningCount = watchdogState_.warningCount;
  healthPacket_.rtMode = static_cast<std::uint8_t>(rtConfig_.mode);

  healthPacket_.flags = 0;
  if (clockState_.isRunning.load(std::memory_order_acquire)) {
    healthPacket_.flags |= ExecutiveHealthPacket::FLAG_CLOCK_RUNNING;
  }
  if (controlState_.isPaused.load(std::memory_order_acquire)) {
    healthPacket_.flags |= ExecutiveHealthPacket::FLAG_PAUSED;
  }
  if (controlState_.shutdownRequested.load(std::memory_order_acquire)) {
    healthPacket_.flags |= ExecutiveHealthPacket::FLAG_SHUTDOWN_REQUESTED;
  }
  if (taskState_.lagThresholdExceeded.load(std::memory_order_acquire)) {
    healthPacket_.flags |= ExecutiveHealthPacket::FLAG_LAG_EXCEEDED;
  }
  if (scheduler_.isSleeping()) {
    healthPacket_.flags |= ExecutiveHealthPacket::FLAG_SLEEPING;
  }

  healthPacket_.commandsProcessed = ioState_.commandsProcessed.load(std::memory_order_acquire);

  return healthPacket_;
}

/* ----------------------------- Command Dispatch ----------------------------- */

std::uint8_t ApexExecutive::handleCommand(std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload,
                                          std::vector<std::uint8_t>& response) noexcept {
  switch (opcode) {
    // === Query commands (no payload, return data) ===

  case static_cast<std::uint16_t>(Opcode::GET_HEALTH): {
    // Return 48-byte health packet (also updates the registered OUTPUT snapshot).
    const auto& PACKET = getHealthPacket();
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
    // Deferred restart: prepare the exec target and set the restartPending
    // flag. The main loop checks this flag after draining commands, flushes
    // the ACK to the wire, then calls execv(). This guarantees the client
    // receives the SUCCESS ACK before the connection drops.

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

    // Stash target and set flag. The main loop will exec after sending the ACK.
    // Write target before flag (release ordering on the atomic store).
    restartExecTarget_ = execTarget;
    restartDidSwapBinary_ = didSwapBinary;
    controlState_.restartPending.store(true, std::memory_order_release);

    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
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

    // === Runtime self-description commands ===

  case static_cast<std::uint16_t>(Opcode::GET_REGISTRY): {
    // No payload required. Response: packed array of component entries.
    // Per entry (44 bytes):
    //   fullUid:u32(4) type:u8(1) taskCount:u8(1) dataCount:u8(1) reserved:u8(1)
    //   name:char[32](32) instanceIndex:u8(1) padding:u8[3](3)
    static constexpr std::size_t ENTRY_SIZE = 44;

    const auto COMPONENTS = registry_.getAllComponents();
    response.resize(COMPONENTS.size() * ENTRY_SIZE);
    std::memset(response.data(), 0, response.size());

    for (std::size_t i = 0; i < COMPONENTS.size(); ++i) {
      const auto& COMP = COMPONENTS[i];
      std::uint8_t* dst = response.data() + i * ENTRY_SIZE;

      std::memcpy(dst + 0, &COMP.fullUid, 4);
      dst[4] = static_cast<std::uint8_t>(COMP.type);
      dst[5] = static_cast<std::uint8_t>(COMP.taskCount);
      dst[6] = static_cast<std::uint8_t>(COMP.dataCount);
      dst[7] = 0; // reserved

      if (COMP.name != nullptr) {
        std::strncpy(reinterpret_cast<char*>(dst + 8), COMP.name, 31);
        dst[8 + 31] = '\0';
      }

      dst[40] = static_cast<std::uint8_t>(COMP.fullUid & 0xFF); // instanceIndex
      // dst[41..43] = padding (already zeroed)
    }

    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(Opcode::GET_DATA_CATALOG): {
    // No payload required. Response: packed array of data entries.
    // Per entry (44 bytes):
    //   fullUid:u32(4) category:u8(1) reserved:u8[3](3) size:u32(4) name:char[32](32)
    static constexpr std::size_t ENTRY_SIZE = 44;

    const auto DATA = registry_.getAllData();
    response.resize(DATA.size() * ENTRY_SIZE);
    std::memset(response.data(), 0, response.size());

    for (std::size_t i = 0; i < DATA.size(); ++i) {
      const auto& D = DATA[i];
      std::uint8_t* dst = response.data() + i * ENTRY_SIZE;

      std::memcpy(dst + 0, &D.fullUid, 4);
      dst[4] = static_cast<std::uint8_t>(D.category);
      // dst[5..7] = reserved (already zeroed)

      const auto SZ = static_cast<std::uint32_t>(D.size);
      std::memcpy(dst + 8, &SZ, 4);

      if (D.name != nullptr) {
        std::strncpy(reinterpret_cast<char*>(dst + 12), D.name, 31);
        dst[12 + 31] = '\0';
      }
    }

    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  default:
    // Delegate to base class for common opcodes (0x0080-0x00FF).
    return PosixExecutiveBase::handleCommand(opcode, payload, response);
  }
}
