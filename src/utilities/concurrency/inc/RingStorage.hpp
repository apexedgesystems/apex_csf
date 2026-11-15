#ifndef APEX_UTILITIES_CONCURRENCY_RING_STORAGE_HPP
#define APEX_UTILITIES_CONCURRENCY_RING_STORAGE_HPP
/**
 * @file RingStorage.hpp
 * @brief The slot array of a ring queue: allocated once at the slot type's
 *        alignment, every slot constructed up front and destroyed with it.
 *
 * The ring queues allocate nothing after construction. Their slots are
 * constructed when the queue is, so a push assigns into a live object and a
 * pop moves out of one, and an over-aligned slot type gets the alignment it
 * declares.
 */

#include <cstddef>
#include <memory>
#include <new>

namespace apex {
namespace concurrency {

/* ----------------------------- RingStorage ----------------------------- */

/**
 * @class RingStorage
 * @tparam Slot The slot type.
 * @note NOT RT-safe to construct or destroy: allocates and releases. Element
 *       access is RT-safe.
 */
template <class Slot> class RingStorage {
public:
  /**
   * @brief Allocate `count` slots and value-construct each.
   * @param count Number of slots.
   */
  explicit RingStorage(std::size_t count)
      : RingStorage(count, [](void* where, std::size_t) { ::new (where) Slot(); }) {}

  /**
   * @brief Allocate `count` slots and construct slot i with `construct(where, i)`.
   * @param count Number of slots.
   * @param construct Callable that constructs a Slot in place at `where` for index `i`.
   */
  template <class Construct>
  RingStorage(std::size_t count, Construct construct)
      : slots_(static_cast<Slot*>(
            ::operator new[](count * sizeof(Slot), std::align_val_t{alignof(Slot)}))),
        count_(count) {
    std::size_t built = 0;
    try {
      for (; built < count_; ++built) {
        construct(static_cast<void*>(&slots_[built]), built);
      }
    } catch (...) {
      destroyAndRelease(built);
      throw;
    }
  }

  ~RingStorage() { destroyAndRelease(count_); }

  // Non-copyable / non-movable (owns raw storage).
  RingStorage(const RingStorage&) = delete;
  RingStorage& operator=(const RingStorage&) = delete;
  RingStorage(RingStorage&&) = delete;
  RingStorage& operator=(RingStorage&&) = delete;

  /// @note RT-safe: pointer arithmetic.
  [[nodiscard]] Slot& operator[](std::size_t index) noexcept { return slots_[index]; }
  /// @note RT-safe: pointer arithmetic.
  [[nodiscard]] const Slot& operator[](std::size_t index) const noexcept { return slots_[index]; }

  /// @return The number of slots.
  /// @note RT-safe: returns stored value.
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
  /// Destroy the first `built` slots in reverse order and release the storage.
  void destroyAndRelease(std::size_t built) noexcept {
    for (std::size_t i = built; i-- > 0;) {
      std::destroy_at(&slots_[i]);
    }
    ::operator delete[](slots_, std::align_val_t{alignof(Slot)});
  }

  Slot* slots_;       ///< The slot array.
  std::size_t count_; ///< Number of slots.
};

} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_RING_STORAGE_HPP
