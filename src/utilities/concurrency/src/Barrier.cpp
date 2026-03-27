/**
 * @file Barrier.cpp
 * @brief Implementation of reusable synchronization barrier.
 */

#include "src/utilities/concurrency/inc/Barrier.hpp"

namespace apex {
namespace concurrency {

/* ----------------------------- Barrier Methods ----------------------------- */

Barrier::Barrier(std::size_t expected) noexcept
    : expected_(expected), arrived_(0), generation_(0) {}

void Barrier::arriveAndWait() {
  std::unique_lock<std::mutex> lk(mutex_);
  const std::size_t MY_GEN = generation_.load(std::memory_order_relaxed);
  ++arrived_;

  if (arrived_ >= expected_.load(std::memory_order_relaxed)) {
    // Last thread to arrive: release all and reset for next phase.
    arrived_ = 0;
    generation_.fetch_add(1, std::memory_order_release);
    lk.unlock();
    cv_.notify_all();
    return;
  }

  // Wait for this generation to complete.
  cv_.wait(lk, [this, MY_GEN]() { return generation_.load(std::memory_order_relaxed) != MY_GEN; });
}

void Barrier::arriveAndDrop() {
  std::unique_lock<std::mutex> lk(mutex_);
  expected_.fetch_sub(1, std::memory_order_relaxed);
  ++arrived_;

  if (arrived_ >= expected_.load(std::memory_order_relaxed)) {
    // This arrival completed the phase.
    arrived_ = 0;
    generation_.fetch_add(1, std::memory_order_release);
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
