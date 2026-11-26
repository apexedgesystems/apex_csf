/**
 * @file BufferPool.cpp
 * @brief Implementation of the lock-free buffer pool for zero-copy messaging.
 */

#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"

#include <bit>
#include <cassert>
#include <utility>

namespace system_core {
namespace interface {

/* ----------------------------- Construction ----------------------------- */

BufferPool::BufferPool(std::size_t poolSize, std::size_t bufferCapacity)
    : buffers_(poolSize), freeMap_((poolSize + MAP_WORD_BITS - 1U) / MAP_WORD_BITS),
      poolSize_(poolSize), bufferCapacity_(bufferCapacity) {

  dataArrays_.reserve(poolSize);

  for (std::size_t i = 0; i < poolSize; ++i) {
    auto dataArray = std::make_unique<std::uint8_t[]>(bufferCapacity);

    // Link buffer to data array; its other members keep their initializers.
    buffers_[i].data = dataArray.get();
    buffers_[i].capacity = bufferCapacity;

    dataArrays_.push_back(std::move(dataArray));
  }

  // Every buffer starts in the pool: all bits set below poolSize, none above.
  for (std::size_t word = 0; word < freeMap_.size(); ++word) {
    const std::size_t REMAINING = poolSize - (word * MAP_WORD_BITS);
    const std::uint64_t BITS =
        (REMAINING >= MAP_WORD_BITS) ? ~std::uint64_t{0} : ((std::uint64_t{1} << REMAINING) - 1U);
    freeMap_[word].store(BITS, std::memory_order_release);
  }
}

/* ----------------------------- Acquire and Release ----------------------------- */

MessageBuffer* BufferPool::acquire(std::size_t requiredSize) noexcept {
  // Check if required size exceeds buffer capacity.
  if (requiredSize > bufferCapacity_) {
    return nullptr;
  }

  for (std::size_t word = 0; word < freeMap_.size(); ++word) {
    std::uint64_t bits = freeMap_[word].load(std::memory_order_acquire);
    while (bits != 0U) {
      // Take the lowest free buffer of this word. A failed exchange reloads bits: another
      // thread took or returned a buffer of the word, and the choice is made again.
      const std::uint64_t TAKEN = bits & (~bits + 1U);
      if (!freeMap_[word].compare_exchange_strong(bits, bits & ~TAKEN, std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        continue;
      }

      MessageBuffer* buf =
          &buffers_[(word * MAP_WORD_BITS) + static_cast<std::size_t>(std::countr_zero(TAKEN))];
      assert(buf->isValid() && "Buffer from pool is invalid!");

#ifndef NDEBUG
      assert(!buf->isAcquired && "Buffer already acquired!");
      buf->isAcquired = true;
#endif

      // Clear metadata for new message.
      buf->clear();
      return buf;
    }
  }

  // Pool exhausted: one pass found no free buffer.
  return nullptr;
}

void BufferPool::release(MessageBuffer* buf) noexcept {
  if (buf == nullptr) {
    return;
  }

  assert(buf->isValid() && "Attempted to release invalid buffer!");

  // The holder of the only reference returns the buffer without writing the count: no other
  // thread holds the buffer, so none can change the count. A shared buffer takes the atomic
  // decrement, and only its last reference goes on.
  if (buf->getRefCount() != 1U && !buf->decRef()) {
    return;
  }

  const std::size_t INDEX = static_cast<std::size_t>(buf - buffers_.data());
  assert(INDEX < poolSize_ && "Buffer is not from this pool!");

#ifndef NDEBUG
  assert(buf->isAcquired && "Buffer was not acquired!");
  buf->isAcquired = false;
#endif

  // Clear metadata and reset refcount for next use.
  buf->clear();

  // Return the buffer: setting its bit hands it to the next acquire() and cannot fail.
  const std::uint64_t BIT = std::uint64_t{1} << (INDEX % MAP_WORD_BITS);
  const std::uint64_t BEFORE =
      freeMap_[INDEX / MAP_WORD_BITS].fetch_or(BIT, std::memory_order_release);
  assert((BEFORE & BIT) == 0U && "Buffer released twice!");
  (void)BEFORE;
}

/* ----------------------------- Statistics ----------------------------- */

std::size_t BufferPool::available() const noexcept {
  std::size_t free = 0;
  for (std::size_t word = 0; word < freeMap_.size(); ++word) {
    free += static_cast<std::size_t>(std::popcount(freeMap_[word].load(std::memory_order_acquire)));
  }
  return free;
}

} // namespace interface
} // namespace system_core
