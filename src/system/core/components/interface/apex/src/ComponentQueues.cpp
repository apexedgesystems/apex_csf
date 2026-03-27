/**
 * @file ComponentQueues.cpp
 * @brief Implementation of QueueManager for per-component queue pairs.
 */

#include "src/system/core/components/interface/apex/inc/ComponentQueues.hpp"

#include <memory>

namespace system_core {
namespace interface {

/* ----------------------------- QueueManager ----------------------------- */

ComponentQueues* QueueManager::allocate(std::uint32_t fullUid) noexcept {
  // Allocation disabled after freeze.
  if (frozen_) {
    return nullptr;
  }

  // Check if already exists.
  auto it = queues_.find(fullUid);
  if (it != queues_.end()) {
    return it->second.get();
  }

  // Allocate new queue pair.
  try {
    auto q = std::make_unique<ComponentQueues>(cmdCapacity_, tlmCapacity_);
    auto* ptr = q.get();
    queues_.emplace(fullUid, std::move(q));
    return ptr;
  } catch (...) {
    return nullptr;
  }
}

ComponentQueues* QueueManager::get(std::uint32_t fullUid) noexcept {
  auto it = queues_.find(fullUid);
  if (it != queues_.end()) {
    return it->second.get();
  }
  return nullptr;
}

} // namespace interface
} // namespace system_core
