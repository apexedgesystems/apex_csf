/**
 * @file LogDrainService.cpp
 * @brief Implementation of the shared async-log drain thread.
 */

#include "src/system/core/infrastructure/logs/inc/LogDrainService.hpp"

#if defined(__linux__)

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"

#include <chrono>

#include <pthread.h>
#include <csignal>
#include <sys/eventfd.h>
#include <unistd.h>

namespace logs {

namespace {

/// Entries drained from one backend before moving to the next (fairness).
constexpr std::size_t SWEEP_BATCH = 64;

void blockSignalsInThread() noexcept {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGHUP);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

} // namespace

LogDrainService& LogDrainService::instance() noexcept {
  static LogDrainService service;
  return service;
}

LogDrainService::LogDrainService() noexcept { wakeFd_ = ::eventfd(0, EFD_CLOEXEC); }

LogDrainService::~LogDrainService() noexcept {
  if (running_.load(std::memory_order_acquire)) {
    running_.store(false, std::memory_order_release);
    notify();
    if (drainThread_.joinable()) {
      drainThread_.join();
    }
  }
  if (wakeFd_ >= 0) {
    ::close(wakeFd_);
    wakeFd_ = -1;
  }
}

bool LogDrainService::registerBackend(AsyncLogBackend* backend) noexcept {
  if (backend == nullptr || wakeFd_ < 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(registryMutex_);

  // Start the drain thread on first use.
  if (!running_.load(std::memory_order_acquire)) {
    running_.store(true, std::memory_order_release);
    try {
      drainThread_ = std::thread([this]() {
        blockSignalsInThread();
        drainLoop();
      });
    } catch (...) {
      running_.store(false, std::memory_order_release);
      return false;
    }
  }

  for (std::size_t i = 0; i < MAX_BACKENDS; ++i) {
    AsyncLogBackend* expected = nullptr;
    if (slots_[i].compare_exchange_strong(expected, backend, std::memory_order_release,
                                          std::memory_order_relaxed)) {
      const std::size_t USED = i + 1;
      std::size_t hw = highWater_.load(std::memory_order_relaxed);
      while (hw < USED && !highWater_.compare_exchange_weak(hw, USED, std::memory_order_release,
                                                            std::memory_order_relaxed)) {
      }
      return true;
    }
  }
  return false; // Registry full.
}

void LogDrainService::unregisterBackend(AsyncLogBackend* backend) noexcept {
  if (backend == nullptr) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(registryMutex_);
    for (std::size_t i = 0; i < MAX_BACKENDS; ++i) {
      AsyncLogBackend* expected = backend;
      if (slots_[i].compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
        break;
      }
    }
  }

  // The drain thread may still be inside a sweep that loaded the pointer
  // before the slot cleared. Two full sweep boundaries guarantee hand-off;
  // poke the eventfd so a parked thread advances through them.
  const std::uint64_t E0 = sweepEpoch_.load(std::memory_order_acquire);
  std::unique_lock<std::mutex> lock(registryMutex_);
  while (running_.load(std::memory_order_acquire) &&
         sweepEpoch_.load(std::memory_order_acquire) < E0 + 2) {
    notify();
    idleCv_.wait_for(lock, std::chrono::milliseconds(1));
  }
}

void LogDrainService::notify() noexcept {
  if (wakeFd_ >= 0) {
    (void)::eventfd_write(wakeFd_, 1);
  }
}

void LogDrainService::drainLoop() noexcept {
  while (true) {
    sweepEpoch_.fetch_add(1, std::memory_order_release);

    std::size_t drained = 0;
    const std::size_t BOUND = highWater_.load(std::memory_order_acquire);
    for (std::size_t i = 0; i < BOUND; ++i) {
      AsyncLogBackend* backend = slots_[i].load(std::memory_order_acquire);
      if (backend != nullptr) {
        drained += backend->drainBatch(SWEEP_BATCH);
      }
    }

    // Unregister waiters advance on sweep boundaries.
    {
      std::lock_guard<std::mutex> lock(registryMutex_);
      idleCv_.notify_all();
    }

    if (drained > 0) {
      continue; // Keep sweeping while work exists.
    }

    if (!running_.load(std::memory_order_acquire)) {
      break; // All rings empty and shutdown requested.
    }

    // All rings empty: park on the shared eventfd. A producer's counter
    // write that raced ahead of this read leaves it non-zero, so the read
    // returns immediately -- no lost wakeup.
    eventfd_t wakes = 0;
    (void)::eventfd_read(wakeFd_, &wakes);
  }
}

} // namespace logs

#endif // defined(__linux__)
