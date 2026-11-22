#ifndef APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
#define APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
/**
 * @file BufferPool.hpp
 * @brief Lock-free buffer pool for zero-copy APROTO messaging.
 *
 * Design:
 *   - Pre-allocate MessageBuffers at startup (NOT RT-safe, done once)
 *   - Lock-free acquire/release via a free list
 *   - Fixed pool size (no runtime allocation)
 *   - Exhaustion handled gracefully (return nullptr)
 *
 * Usage:
 *   BufferPool pool(100, 4096);  // 100 buffers, 4KB each (startup, NOT RT-safe)
 *   auto* buf = pool.acquire(200);  // lock-free, see RT-Safety
 *   // ... use buffer ...
 *   pool.release(buf);  // lock-free, see RT-Safety
 *
 * RT-Safety:
 *   - Constructor: NOT RT-safe (allocates memory)
 *   - acquire(): RT-DEPENDS (lock-free pop from the free list, no per-caller retry bound)
 *   - release(): RT-DEPENDS (lock-free push to the free list, no per-caller retry bound)
 */

#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace system_core {
namespace interface {

/* ----------------------------- Constants ----------------------------- */

/// Default pool size (number of buffers).
inline constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 128;

/// Default buffer capacity (bytes per buffer).
inline constexpr std::size_t DEFAULT_BUFFER_CAPACITY = 4096;

/* ----------------------------- BufferPool ----------------------------- */

/**
 * @class BufferPool
 * @brief Lock-free pool of MessageBuffers for zero-copy messaging.
 *
 * Allocation Strategy:
 *   - All buffers pre-allocated at construction (NOT RT-safe, done once)
 *   - Fixed pool size (no runtime growth)
 *   - Lock-free free list: acquire/release take no lock and allocate nothing
 *
 * Exhaustion Handling:
 *   - acquire() returns nullptr if pool exhausted
 *   - Caller must check for nullptr
 *   - Buffers return to pool when released
 *
 * Thread Safety:
 *   - acquire() and release() are lock-free and may run on different threads at once
 *   - Can acquire from one thread, release from another
 *   - Pool must outlive all acquired buffers
 *
 * Memory Ownership:
 *   - Pool owns all MessageBuffer instances
 *   - Pool owns all allocated data arrays
 *   - Caller owns buffer between acquire() and release()
 *
 * @note NOT RT-safe: Constructor, destructor
 * @note RT-DEPENDS: acquire(), release() (LockFreeQueue::tryPop()/tryPush(): no lock,
 *       allocation or syscall, and no per-caller retry bound while other threads
 *       acquire or release)
 * @note RT-safe: size(), available()
 */
class BufferPool {
public:
  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct buffer pool with pre-allocated buffers.
   * @param poolSize Number of buffers to allocate.
   * @param bufferCapacity Size of each buffer in bytes.
   * @note NOT RT-safe: Allocates memory.
   */
  explicit BufferPool(std::size_t poolSize = DEFAULT_BUFFER_POOL_SIZE,
                      std::size_t bufferCapacity = DEFAULT_BUFFER_CAPACITY);

  /**
   * @brief Destructor (frees all buffers).
   * @note NOT RT-safe: Deallocates memory.
   */
  ~BufferPool();

  // Non-copyable, non-movable (owns resources).
  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;
  BufferPool(BufferPool&&) = delete;
  BufferPool& operator=(BufferPool&&) = delete;

  /* ----------------------------- Lock-Free Operations ----------------------------- */

  /**
   * @brief Acquire buffer from pool (lock-free).
   * @param requiredSize Minimum buffer size required (bytes).
   * @return Pointer to buffer, or nullptr if pool exhausted or size too large.
   * @note RT-DEPENDS: lock-free pop from the free list, as LockFreeQueue::tryPop():
   *       no per-caller retry bound while other threads acquire.
   * @note Caller MUST release buffer when done (or leak!).
   */
  [[nodiscard]] MessageBuffer* acquire(std::size_t requiredSize) noexcept;

  /**
   * @brief Release buffer back to pool (lock-free).
   * @param buf Buffer to release (must have been acquired from this pool).
   * @note RT-DEPENDS: lock-free push to the free list, as LockFreeQueue::tryPush():
   *       no per-caller retry bound while other threads release.
   * @note CRITICAL: Do not access buffer after releasing!
   * @note CRITICAL: Do not release nullptr!
   * @note CRITICAL: Do not release same buffer twice!
   */
  void release(MessageBuffer* buf) noexcept;

  /* ----------------------------- Statistics (RT-Safe) ----------------------------- */

  /**
   * @brief Get total pool size.
   * @return Total number of buffers in pool.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] std::size_t size() const noexcept { return poolSize_; }

  /**
   * @brief Get an estimate of the available buffers.
   * @return The free list's cursor distance: the buffers in the list when no acquire or release
   *         is in progress.
   * @note RT-safe: Lock-free read.
   * @note Approximate: it counts acquires and releases in flight and may change immediately
   *       after the call, so a result above zero does not promise that acquire() succeeds.
   */
  [[nodiscard]] std::size_t available() const noexcept;

  /**
   * @brief Get number of acquired buffers.
   * @return Number of buffers currently acquired.
   * @note RT-safe: Computed from size() - available().
   */
  [[nodiscard]] std::size_t acquired() const noexcept { return poolSize_ - available(); }

private:
  /* ----------------------------- Internal State ----------------------------- */

  /// Pool of MessageBuffer instances (owned by pool).
  std::vector<std::unique_ptr<MessageBuffer>> buffers_;

  /// Pool of data arrays (owned by pool, pointed to by MessageBuffer::data).
  std::vector<std::unique_ptr<std::uint8_t[]>> dataArrays_;

  /// Lock-free free list (MPMC queue for multi-thread acquire/release).
  apex::concurrency::LockFreeQueue<MessageBuffer*> freeList_;

  /// Total pool size (constant after construction).
  std::size_t poolSize_;

  /// Buffer capacity (constant after construction).
  std::size_t bufferCapacity_;

#ifndef NDEBUG
  /// Debug: Total acquire count.
  std::atomic<std::uint64_t> totalAcquires_{0};

  /// Debug: Total release count.
  std::atomic<std::uint64_t> totalReleases_{0};
#endif
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
