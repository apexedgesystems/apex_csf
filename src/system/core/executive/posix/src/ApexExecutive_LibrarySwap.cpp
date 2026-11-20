/**
 * @file ApexExecutive_LibrarySwap.cpp
 * @brief Publication of a RELOAD_LIBRARY swap and reclamation of the
 *        plugin it replaced.
 *
 * The RELOAD_LIBRARY handler loads and initializes the replacement on the
 * thread that received the command. What it must not do there is rewrite
 * the pointers the task thread and the scheduler's workers work through,
 * or destroy an instance they may still be using. This file holds the two
 * steps that take care of that:
 *
 *  - Publication runs on the task thread while the scheduler reports
 *    every dispatched task settled. That thread drains commands to
 *    components and dispatches tasks, so at that point no worker and no
 *    drain holds the old instance, and none can pick it up afterwards.
 *  - The replaced plugin is destroyed on the external I/O thread, at the
 *    top of its loop. That thread may have resolved the old instance for
 *    a command of its own just before publication; at the top of its loop
 *    it holds no component pointer.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/executive/posix/inc/ExecutiveStatus.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/utilities/helpers/inc/Cpu.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

  // Every scheduled task must have a counterpart: an entry left on the
  // old instance would run it after the swap.
  out.tasksReplaced = scheduler_.replaceComponentTasks(fullUid, newComp);
  if (out.tasksReplaced < out.tasksExpected) {
    static_cast<void>(scheduler_.replaceComponentTasks(fullUid, oldComp));
    out.status = static_cast<std::uint8_t>(ExecCommandResult::TASK_MISMATCH);
    return out;
  }

  if (!registry_.updateComponent(fullUid, &newComp)) {
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

  // On the task thread nothing is being dispatched or drained while this
  // call runs. A worker may be a few instructions short of leaving a
  // finished task, so the settled state is polled for a short while.
  if (std::this_thread::get_id() == taskThreadId_.load(std::memory_order_acquire)) {
    const std::uint64_t DEADLINE =
        apex::helpers::cpu::getMonotonicNs() + LIBRARY_SWAP_INLINE_SETTLE_NS;
    for (;;) {
      LibrarySwapOutcome out = publishLibrarySwapHere(fullUid, oldComp, newComp);
      const std::uint64_t NOW_NS = apex::helpers::cpu::getMonotonicNs();
      if (out.status != static_cast<std::uint8_t>(ExecCommandResult::NOT_QUIESCENT) ||
          NOW_NS >= DEADLINE) {
        out.waitNs = NOW_NS - ASKED_NS;
        return out;
      }
      std::this_thread::yield();
    }
  }

  // Another thread: hand the swap to the task thread and wait for it.
  {
    std::lock_guard lock(swapMutex_);
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
      cvSwapDone_.wait_for(lock, std::chrono::nanoseconds(librarySwapWaitNs()),
                           [this]() { return swap_.phase == LibrarySwap::Phase::DONE; });
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

/* ----------------------------- Reclamation ----------------------------- */

void ApexExecutive::retirePlugin(PluginLoader&& loader) noexcept {
  std::lock_guard lock(swapMutex_);
  retiredPlugins_.push_back(std::move(loader));
  pluginsRetired_.store(true, std::memory_order_release);
}

void ApexExecutive::reclaimRetiredPlugins() noexcept {
  std::vector<PluginLoader> retired;
  {
    std::lock_guard lock(swapMutex_);
    retired.swap(retiredPlugins_);
    pluginsRetired_.store(false, std::memory_order_release);
  }
  // Destroyed here, outside the lock: each loader destroys its component
  // and closes its library.
  retired.clear();
}
