#ifndef APEX_SYSTEM_CORE_INTERFACE_COMPONENT_QUEUES_HPP
#define APEX_SYSTEM_CORE_INTERFACE_COMPONENT_QUEUES_HPP
/**
 * @file ComponentQueues.hpp
 * @brief Per-component queue pairs for async command/telemetry routing.
 *
 * Each component gets a dedicated pair of queues:
 * - cmdInbox: MPMC queue (multiple producers: EXTERNAL_IO + TASK_EXEC threads)
 * - tlmOutbox: SPSC queue (single producer: TASK_EXEC, single consumer: TASK_EXEC)
 *
 * This decouples network I/O timing from scheduler-driven component execution.
 *
 * Zero-Copy Architecture:
 * - Queues now store MessageBuffer* (8-byte pointers) instead of AprotoMessage (4KB structs)
 * - Memory reduction: 384KB -> 768 bytes per component (99.8%)
 * - Queue operations: ~50-100x faster (cache-friendly pointer passing)
 */

#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace system_core {
namespace interface {

/* ----------------------------- Constants ----------------------------- */

/// Default command inbox capacity per component.
inline constexpr std::size_t DEFAULT_CMD_QUEUE_CAPACITY = 32;

/// Default telemetry outbox capacity per component.
inline constexpr std::size_t DEFAULT_TLM_QUEUE_CAPACITY = 64;

/* ----------------------------- ComponentQueues ----------------------------- */

/**
 * @struct ComponentQueues
 * @brief RT-safe queue pair for a single component.
 *
 * Message Flow:
 * 1. Interface receives APROTO command, allocates MessageBuffer, pushes pointer to cmdInbox
 * 2. Interface drains cmdInbox, calls component handleCommand(), releases buffer
 * 3. Component calls internalBus->postInternalTelemetry() with payload
 * 4. Interface allocates MessageBuffer, encodes APROTO, pushes pointer to tlmOutbox
 * 5. Interface drains tlmOutbox, transmits to TCP, releases buffer
 *
 * Design Constraints:
 * - Components never touch MessageBuffer or BufferPool directly
 * - Components use IInternalBus interface for sending messages
 * - Interface owns all buffer lifecycle management
 * - Maintains layering (system_component does not depend on interface library)
 *
 * Ownership:
 * - cmdInbox: Interface allocates → pushes → pops → releases
 * - tlmOutbox: Interface allocates → pushes → pops → releases
 */
struct ComponentQueues {
  apex::concurrency::LockFreeQueue<MessageBuffer*>
      cmdInbox; ///< MPMC: EXTERNAL_IO + TASK_EXEC producers.
  apex::concurrency::SPSCQueue<MessageBuffer*> tlmOutbox; ///< SPSC: TASK_EXEC -> TASK_EXEC.

  /**
   * @brief Construct queue pair with specified capacities.
   * @param cmdCapacity Command inbox capacity.
   * @param tlmCapacity Telemetry outbox capacity.
   * @note NOT RT-safe: Allocates queue storage.
   */
  ComponentQueues(std::size_t cmdCapacity = DEFAULT_CMD_QUEUE_CAPACITY,
                  std::size_t tlmCapacity = DEFAULT_TLM_QUEUE_CAPACITY)
      : cmdInbox(cmdCapacity), tlmOutbox(tlmCapacity) {}

  // Non-copyable / non-movable (SPSCQueue is non-copyable).
  ComponentQueues(const ComponentQueues&) = delete;
  ComponentQueues& operator=(const ComponentQueues&) = delete;
  ComponentQueues(ComponentQueues&&) = delete;
  ComponentQueues& operator=(ComponentQueues&&) = delete;
};

/* ----------------------------- QueueManager ----------------------------- */

/**
 * @class QueueManager
 * @brief Manages per-component queue pairs.
 *
 * The interface owns a QueueManager that allocates queue pairs during
 * component registration (before freeze). After freeze, queue allocation
 * is disabled and only queue operations are allowed.
 *
 * @note Thread safety: Allocation is NOT RT-safe, queue operations ARE RT-safe.
 */
class QueueManager {
public:
  /**
   * @brief Construct manager with default queue capacities.
   * @param cmdCapacity Default command inbox capacity per component.
   * @param tlmCapacity Default telemetry outbox capacity per component.
   */
  explicit QueueManager(std::size_t cmdCapacity = DEFAULT_CMD_QUEUE_CAPACITY,
                        std::size_t tlmCapacity = DEFAULT_TLM_QUEUE_CAPACITY) noexcept
      : cmdCapacity_(cmdCapacity), tlmCapacity_(tlmCapacity) {}

  /**
   * @brief Allocate queue pair for a component.
   * @param fullUid Component's fullUid.
   * @return Pointer to allocated queues, or nullptr if frozen or already exists.
   * @note NOT RT-safe: Allocates memory.
   */
  ComponentQueues* allocate(std::uint32_t fullUid) noexcept;

  /**
   * @brief Get queue pair for a component.
   * @param fullUid Component's fullUid.
   * @return Pointer to queues, or nullptr if not found.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] ComponentQueues* get(std::uint32_t fullUid) noexcept;

  /**
   * @brief Freeze manager (disable further allocation).
   * @note Call after all components are registered.
   */
  void freeze() noexcept { frozen_ = true; }

  /**
   * @brief Check if manager is frozen.
   * @return True if frozen.
   */
  [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

  /**
   * @brief Get number of registered components.
   * @return Queue pair count.
   */
  [[nodiscard]] std::size_t size() const noexcept { return queues_.size(); }

  /**
   * @brief Iterate over all queue pairs (for draining outboxes).
   * @tparam F Callable with signature void(std::uint32_t fullUid, ComponentQueues& q).
   * @param fn Function to call for each component.
   * @note RT-safe if fn is RT-safe.
   */
  template <typename F> void forEach(F&& fn) noexcept {
    for (auto& [uid, q] : queues_) {
      fn(uid, *q);
    }
  }

private:
  std::unordered_map<std::uint32_t, std::unique_ptr<ComponentQueues>> queues_;
  std::size_t cmdCapacity_;
  std::size_t tlmCapacity_;
  bool frozen_{false};
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_COMPONENT_QUEUES_HPP
