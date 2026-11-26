#ifndef APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
#define APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
/**
 * @file BufferPool.hpp
 * @brief Lock-free buffer pool for zero-copy APROTO messaging.
 *
 * Design:
 *   - Pre-allocate MessageBuffers at startup (NOT RT-safe, done once)
 *   - Free map: one bit per buffer, set while the pool holds it
 *   - Fixed pool size (no runtime allocation)
 *   - Exhaustion handled gracefully (return nullptr)
 *
 * Free map:
 *   A buffer changes hands in one atomic operation on its bit. release() sets
 *   the bit unconditionally, so a return cannot be refused or lost, whatever
 *   other threads are doing and wherever they are stopped. acquire() clears a
 *   set bit it finds with a compare-exchange; a thread stopped between
 *   reading a word and exchanging it holds nothing.
 *
 * Usage:
 *   BufferPool pool(100, 4096);  // 100 buffers, 4KB each (startup, NOT RT-safe)
 *   auto* buf = pool.acquire(200);  // lock-free, see RT-Safety
 *   // ... use buffer ...
 *   pool.release(buf);  // cannot be refused, see RT-Safety
 *
 * RT-Safety:
 *   - Constructor: NOT RT-safe (allocates memory)
 *   - acquire(): RT-DEPENDS (one pass over the free map; an exchange is retried only when
 *     another thread changed the same word, with no per-caller retry bound)
 *   - release(): RT-DEPENDS (no loop in the pool: a shared buffer takes one atomic update of
 *     its reference count, and the last or only reference one of the free map; bounded
 *     where the target and the build bound each update, see Atomic updates)
 *
 * Atomic updates:
 *   The pool's own work is atomic read-modify-write operations: in acquire() one exchange
 *   per attempt, and in release() at most two, the reference-count decrement of a shared
 *   buffer and the free-map OR. How long one of them takes belongs to the target and the
 *   build:
 *     - x86-64, and AArch64 with the LSE atomic instructions (Armv8.1 and later): one
 *       instruction each in a release build.
 *     - AArch64 without LSE: each is an exclusive load and store repeated until the store
 *       succeeds, with no per-caller bound on the repeats. A build that does not fix the
 *       instruction set can choose between the two forms on the running CPU.
 *     - Debug builds: release() checks the bit's previous value, which the compiler may
 *       build as a compare-exchange loop on any target.
 */

#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
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
 *   - Free map: acquire/release take no lock and allocate nothing
 *
 * Exhaustion Handling:
 *   - acquire() returns nullptr if one pass over the free map finds no buffer
 *   - Caller must check for nullptr
 *   - Buffers return to pool when released; a release always completes
 *
 * Thread Safety:
 *   - acquire() and release() may run on any threads at once
 *   - Can acquire from one thread, release from another
 *   - Pool must outlive all acquired buffers
 *
 * Reuse Order:
 *   - Unspecified: which free buffer acquire() returns is not part of the contract
 *
 * Memory Ownership:
 *   - Pool owns all MessageBuffer instances
 *   - Pool owns all allocated data arrays
 *   - Caller owns buffer between acquire() and release()
 *
 * @note NOT RT-safe: Constructor, destructor
 * @note RT-DEPENDS: acquire() (no lock, allocation or syscall; at most one pass over
 *       the free map, one word per 64 buffers with a partial last word included, and no
 *       per-caller retry bound while other threads acquire or release buffers of the
 *       same word)
 * @note RT-DEPENDS: release() (no lock, allocation, syscall or loop in the pool; bounded
 *       where the target and the build bound an atomic update, see Atomic updates in the
 *       file comment)
 * @note RT-safe: size(), available(), acquired()
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
  ~BufferPool() = default;

  // Non-copyable, non-movable (owns resources).
  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;
  BufferPool(BufferPool&&) = delete;
  BufferPool& operator=(BufferPool&&) = delete;

  /* ----------------------------- Acquire and Release ----------------------------- */

  /**
   * @brief Acquire buffer from pool (lock-free).
   * @param requiredSize Minimum buffer size required (bytes).
   * @return Pointer to buffer, or nullptr if pool exhausted or size too large. A buffer
   *         released behind the pass while it runs is not seen by it.
   * @note RT-DEPENDS: lock-free; an exchange is retried only when another thread
   *       acquired or released a buffer of the same word, with no per-caller bound.
   * @note Caller MUST release buffer when done (or leak!).
   */
  [[nodiscard]] MessageBuffer* acquire(std::size_t requiredSize) noexcept;

  /**
   * @brief Release buffer back to pool (cannot be refused).
   * @param buf Buffer to release (must have been acquired from this pool).
   * @note RT-DEPENDS: a buffer with one reference is returned with one atomic update of the
   *       free map, which cannot be refused, and its count is read, not written. A shared
   *       buffer takes one atomic update of its count first, and only its last reference
   *       goes on to the free map. No loop in the pool; each update is bounded where the
   *       target and the build bound it (Atomic updates in the file comment).
   * @note Each call gives up one counted reference of the caller's: a count of one means
   *       no other thread holds the buffer.
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
   * @return The set bits of the free map, read one word at a time: the buffers in the pool
   *         when no acquire or release runs during the call.
   * @note RT-safe: one read per word of the free map.
   * @note Approximate: the words are not read at one instant and the count may change
   *       immediately after the call, so a result above zero does not promise that
   *       acquire() succeeds. Never above size().
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

  /// Buffers per word of the free map.
  static constexpr std::size_t MAP_WORD_BITS = 64;

  /// MessageBuffer instances (owned by pool), sized once at construction. A buffer's index is
  /// its position here.
  std::vector<MessageBuffer> buffers_;

  /// Pool of data arrays (owned by pool, pointed to by MessageBuffer::data).
  std::vector<std::unique_ptr<std::uint8_t[]>> dataArrays_;

  /// Free map, sized once at construction: bit (i % 64) of word (i / 64) is set while the pool
  /// holds buffer i. Bits at and above size() in the last word stay clear.
  std::vector<std::atomic<std::uint64_t>> freeMap_;

  /// Total pool size (constant after construction).
  std::size_t poolSize_;

  /// Buffer capacity (constant after construction).
  std::size_t bufferCapacity_;
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_BUFFER_POOL_HPP
