#ifndef APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
#define APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
/**
 * @file BufferPool.hpp
 * @brief RT-safe buffer pool for zero-copy APROTO messaging.
 *
 * Design:
 *   - Pre-allocate MessageBuffers at startup (NOT RT-safe, done once)
 *   - RT-safe acquire/release via lock-free free list
 *   - Fixed pool size (no runtime allocation)
 *   - Exhaustion handled gracefully (return nullptr)
 *
 * Usage:
 *   BufferPool pool(100, 4096);  // 100 buffers, 4KB each (startup, NOT RT-safe)
 *   auto* buf = pool.acquire(200);  // RT-safe
 *   // ... use buffer ...
 *   pool.release(buf);  // RT-safe
 *
 * RT-Safety:
 *   - Constructor: NOT RT-safe (allocates memory)
 *   - acquire(): RT-safe (lock-free pop from free list)
 *   - release(): RT-safe (lock-free push to free list)
 */

#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"

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
 * @brief RT-safe pool of MessageBuffers for zero-copy messaging.
 *
 * Allocation Strategy:
 *   - All buffers pre-allocated at construction (NOT RT-safe, done once)
 *   - Fixed pool size (no runtime growth)
 *   - Lock-free free list for RT-safe acquire/release
 *
 * Exhaustion Handling:
 *   - acquire() returns nullptr if pool exhausted
 *   - Caller must check for nullptr
 *   - Buffers return to pool when released
 *
 * Thread Safety:
 *   - acquire() and release() are RT-safe (lock-free)
 *   - Can acquire from one thread, release from another
 *   - Pool must outlive all acquired buffers
 *
 * Memory Ownership:
 *   - Pool owns all MessageBuffer instances
 *   - Pool owns all allocated data arrays
 *   - Caller owns buffer between acquire() and release()
 *
 * @note NOT RT-safe: Constructor, destructor
 * @note RT-safe: acquire(), release(), size(), available()
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

  /* ----------------------------- RT-Safe Operations ----------------------------- */

  /**
   * @brief Acquire buffer from pool (RT-safe).
   * @param requiredSize Minimum buffer size required (bytes).
   * @return Pointer to buffer, or nullptr if pool exhausted or size too large.
   * @note RT-safe: Lock-free pop from free list.
   * @note Caller MUST release buffer when done (or leak!).
   */
  [[nodiscard]] MessageBuffer* acquire(std::size_t requiredSize) noexcept;

  /**
   * @brief Release buffer back to pool (RT-safe).
   * @param buf Buffer to release (must have been acquired from this pool).
   * @note RT-safe: Lock-free push to free list.
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
   * @brief Get number of available buffers.
   * @return Number of buffers currently in free list.
   * @note RT-safe: Lock-free read.
   * @note Approximate (may change immediately after call).
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
  std::uint64_t totalAcquires_{0};

  /// Debug: Total release count.
  std::uint64_t totalReleases_{0};
#endif
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
