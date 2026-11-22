/**
 * @file ApexExecutive_LibrarySwap.cpp
 * @brief Publication of a RELOAD_LIBRARY swap, reclamation of the plugin
 *        it replaced, and the hand-over of a reload an action asked for.
 *
 * The RELOAD_LIBRARY handler loads and initializes the replacement on the
 * external I/O thread. What it must not do there is rewrite the bindings
 * the task thread and the scheduler's workers work through, or destroy an
 * instance they may still be using. This file holds the steps that take
 * care of that:
 *
 *  - Publication runs on the task thread while the scheduler reports
 *    every dispatched task settled and the I/O thread waits. The task
 *    thread drains commands to components, dispatches tasks and runs the
 *    action phase, so at that point no drain, worker or command holds the
 *    old instance. The scheduler's entries and the registry's entries are
 *    each bound to the replacement whole or not at all, so afterwards
 *    nothing they hand out belongs to the instance that was replaced.
 *  - The replaced plugin is destroyed on the I/O thread at the top of its
 *    next loop, after the handler that retired it has returned.
 *  - A reload that reaches the handler on the task thread is recorded and
 *    run by the I/O thread at the top of its loop: loading a library and
 *    initializing a component do not belong in a cycle.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/core.h>

using namespace executive;
using system_core::system_component::CommandResult;
using system_core::system_component::PluginLoader;
using system_core::system_component::SystemComponentBase;

/* ----------------------------- Publication ----------------------------- */

ApexExecutive::LibrarySwapOutcome
ApexExecutive::publishLibrarySwapHere(std::uint32_t fullUid, SystemComponentBase& oldComp,
                                      SystemComponentBase& newComp) noexcept {
  LibrarySwapOutcome out{};
  if (!scheduler_.settled()) {
    out.status = static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT);
    return out;
  }

  for (const auto& entry : scheduler_.entries()) {
    if (entry.fullUid == fullUid) {
      ++out.tasksExpected;
    }
  }

  // The registry and the scheduler each rebind whole or not at all. The
  // registry is asked first whether it can, so a refusal by either leaves
  // both on the old instance.
  using RegistryStatus = system_core::registry::Status;
  if (registry_.canRebindComponent(fullUid, newComp) != RegistryStatus::SUCCESS) {
    out.status = static_cast<std::uint8_t>(ExecCommandResult::REGISTRY_FAILED);
    return out;
  }

  out.tasksReplaced = scheduler_.replaceComponentTasks(fullUid, newComp);
  if (out.tasksReplaced < out.tasksExpected) {
    out.status = static_cast<std::uint8_t>(ExecCommandResult::TASK_MISMATCH);
    return out;
  }

  if (registry_.rebindComponent(fullUid, newComp) != RegistryStatus::SUCCESS) {
    static_cast<void>(scheduler_.replaceComponentTasks(fullUid, oldComp));
    out.status = static_cast<std::uint8_t>(ExecCommandResult::REGISTRY_FAILED);
    return out;
  }

  for (auto& regComp : registeredComponents_) {
    if (regComp == &oldComp) {
      regComp = &newComp;
      break;
    }
  }

  out.status = static_cast<std::uint8_t>(CommandResult::SUCCESS);
  return out;
}

void ApexExecutive::serviceLibrarySwap() noexcept {
  std::unique_lock lock(swapMutex_);
  if (swap_.phase != LibrarySwap::Phase::PENDING) {
    return;
  }
  const LibrarySwapOutcome OUT =
      publishLibrarySwapHere(swap_.fullUid, *swap_.oldComp, *swap_.newComp);
  if (OUT.status == static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT)) {
    return; // Still pending: the next settled point takes it.
  }
  swap_.outcome = OUT;
  swap_.phase = LibrarySwap::Phase::DONE;
  swapPending_.store(false, std::memory_order_release);
  lock.unlock();
  cvSwapDone_.notify_all();
}

std::uint64_t ApexExecutive::librarySwapWaitNs() const noexcept {
  constexpr std::uint64_t FLOOR_NS = 2'000'000'000U;
  constexpr std::uint64_t ONE_SECOND_NS = 1'000'000'000U;
  const std::uint64_t PERIOD_NS =
      ONE_SECOND_NS / std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(clockFrequency_));
  return std::max(FLOOR_NS, 4U * PERIOD_NS);
}

ApexExecutive::LibrarySwapOutcome
ApexExecutive::publishLibrarySwap(std::uint32_t fullUid, SystemComponentBase& oldComp,
                                  SystemComponentBase& newComp) noexcept {
  const std::uint64_t ASKED_NS = apex::helpers::cpu::getMonotonicNs();

  // Hand the swap to the task thread and wait for it. Once that thread
  // has ended nothing publishes, and the swap is refused at once.
  {
    std::lock_guard lock(swapMutex_);
    if (swapClosed_) {
      LibrarySwapOutcome closed{};
      closed.status = static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT);
      return closed;
    }
    swap_ = LibrarySwap{};
    swap_.phase = LibrarySwap::Phase::PENDING;
    swap_.fullUid = fullUid;
    swap_.oldComp = &oldComp;
    swap_.newComp = &newComp;
    swapPending_.store(true, std::memory_order_release);
  }
  {
    // The task thread may be waiting for a tick or parked in a pause;
    // the flag changes under the mutex those waits use, so neither misses it.
    std::lock_guard lock(cvMutex_);
    swapKick_.store(true, std::memory_order_release);
  }
  cvClockTick_.notify_all();
  cvPause_.notify_all();

  std::unique_lock lock(swapMutex_);
  const bool DONE =
      cvSwapDone_.wait_for(
          lock, std::chrono::nanoseconds(librarySwapWaitNs()),
          [this]() { return swap_.phase == LibrarySwap::Phase::DONE || swapClosed_; }) &&
      swap_.phase == LibrarySwap::Phase::DONE;
  LibrarySwapOutcome out{};
  if (DONE) {
    out = swap_.outcome;
  } else {
    // Not taken: publication holds this mutex from start to end, so the
    // swap is withdrawn whole and nothing was changed.
    out.status = static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT);
    swapPending_.store(false, std::memory_order_release);
  }
  swap_ = LibrarySwap{};
  out.waitNs = apex::helpers::cpu::getMonotonicNs() - ASKED_NS;
  return out;
}

/* ----------------------------- Control thread ----------------------------- */

void ApexExecutive::serviceLibraryControl() noexcept {
  // Each retired loader destroys its component and closes its library.
  // The handler that retired it has returned, and with it the last use
  // of the old instance on this thread.
  if (!retiredPlugins_.empty()) {
    const std::size_t COUNT = retiredPlugins_.size();
    const std::uint64_t BEGIN_NS = apex::helpers::cpu::getMonotonicNs();
    retiredPlugins_.clear();
    if (auto* sl = fileSystem_.swapLog()) {
      sl->info("SWAP", fmt::format("RELOAD_LIBRARY_RECLAIMED: plugins={} us={}", COUNT,
                                   (apex::helpers::cpu::getMonotonicNs() - BEGIN_NS) / 1000U));
    }
  }

  if (deferredReloadUid_.load(std::memory_order_acquire) != NO_DEFERRED_RELOAD) {
    const std::uint32_t UID =
        deferredReloadUid_.exchange(NO_DEFERRED_RELOAD, std::memory_order_acq_rel);
    std::array<std::uint8_t, sizeof(UID)> payload{};
    std::memcpy(payload.data(), &UID, sizeof(UID));
    std::vector<std::uint8_t> response;
    // The outcome goes to the system log and the swap log; the action
    // that asked has long returned.
    static_cast<void>(handleCommand(
        static_cast<std::uint16_t>(Opcode::RELOAD_LIBRARY),
        apex::compat::rospan<std::uint8_t>{payload.data(), payload.size()}, response));
  }
}
