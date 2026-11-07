/**
 * @file Barrier.cpp
 * @brief Implementation of reusable synchronization barrier.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- Barrier Methods ----------------------------- */

Barrier::Barrier(std::size_t expected) noexcept
    : expected_(expected), remaining_(expected), generation_(0) {}

bool Barrier::arriveLocked() noexcept {
  // No participants left means every arrival completes a phase by itself.
  if (remaining_ > 1) {
    --remaining_;
    return false;
  }
  remaining_ = expected_.load(std::memory_order_relaxed);
  generation_.fetch_add(1, std::memory_order_release);
  return true;
}

void Barrier::arriveAndWait() {
  std::unique_lock<std::mutex> lk(mutex_);
  const std::size_t MY_GEN = generation_.load(std::memory_order_relaxed);

  if (arriveLocked()) {
    lk.unlock();
    cv_.notify_all();
    return;
  }

  // Wait for this generation to complete.
  cv_.wait(lk, [this, MY_GEN]() { return generation_.load(std::memory_order_relaxed) != MY_GEN; });
}

void Barrier::arriveAndDrop() {
  std::unique_lock<std::mutex> lk(mutex_);
  // One participant fewer from the next phase on; this arrival still counts
  // toward the current phase.
  const std::size_t EXPECTED = expected_.load(std::memory_order_relaxed);
  if (EXPECTED > 0) {
    expected_.store(EXPECTED - 1, std::memory_order_relaxed);
  }

  if (arriveLocked()) {
    lk.unlock();
    cv_.notify_all();
  }
}

std::size_t Barrier::expected() const {
  // Lock-free query.
  return expected_.load(std::memory_order_acquire);
}

std::size_t Barrier::generation() const {
  // Lock-free query.
  return generation_.load(std::memory_order_acquire);
}

} // namespace concurrency
} // namespace apex
