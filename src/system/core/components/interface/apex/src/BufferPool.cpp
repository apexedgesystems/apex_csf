/**
 * @file BufferPool.cpp
 * @brief Implementation of RT-safe buffer pool for zero-copy messaging.
 */

#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"

#include <cassert>
#include <utility>

namespace system_core {
namespace interface {

/* ----------------------------- Construction ----------------------------- */

BufferPool::BufferPool(std::size_t poolSize, std::size_t bufferCapacity)
    : freeList_(poolSize), poolSize_(poolSize), bufferCapacity_(bufferCapacity) {

  // Reserve space to avoid reallocations.
  buffers_.reserve(poolSize);
  dataArrays_.reserve(poolSize);

  // Pre-allocate all buffers and data arrays.
  for (std::size_t i = 0; i < poolSize; ++i) {
    // Allocate MessageBuffer.
    auto buf = std::make_unique<MessageBuffer>();

    // Allocate data array.
    auto dataArray = std::make_unique<std::uint8_t[]>(bufferCapacity);

    // Link buffer to data array.
    buf->data = dataArray.get();
    buf->capacity = bufferCapacity;
    buf->length = 0;

    // Clear metadata.
    buf->fullUid = 0;
    buf->opcode = 0;
    buf->sequence = 0;
    buf->internalOrigin = false;

#ifndef NDEBUG
    // Initialize debug fields.
    buf->magic = 0xDEADBEEF;
    buf->isAcquired = false;
    buf->acquireTimestamp = 0;
#endif

    // Add buffer to free list (raw pointer).
    MessageBuffer* rawPtr = buf.get();
    const bool pushed = freeList_.tryPush(rawPtr);
    assert(pushed && "Free list should never be full during construction!");
    (void)pushed; // Silence unused variable warning in release builds.

    // Store ownership in pool.
    buffers_.push_back(std::move(buf));
    dataArrays_.push_back(std::move(dataArray));
  }
}

BufferPool::~BufferPool() {
  // Buffers and data arrays are automatically freed via unique_ptr.
  // Check for leaks in debug mode.
#ifndef NDEBUG
  const std::size_t leakedBuffers = poolSize_ - available();
  if (leakedBuffers > 0) {
    // Log warning: leakedBuffers not returned to pool!
    // In production, this would go to a log system.
    // For now, just a comment. Could add fprintf(stderr, ...) if needed.
  }
#endif
}

/* ----------------------------- RT-Safe Operations ----------------------------- */

MessageBuffer* BufferPool::acquire(std::size_t requiredSize) noexcept {
  // Check if required size exceeds buffer capacity.
  if (requiredSize > bufferCapacity_) {
    // Size too large, cannot satisfy.
    return nullptr;
  }

  // Try to pop buffer from free list.
  MessageBuffer* buf = nullptr;
  if (!freeList_.tryPop(buf)) {
    // Pool exhausted, no buffers available.
    return nullptr;
  }

  // Validate buffer (should never fail if pool is correct).
  assert(buf != nullptr && "Free list returned null buffer!");
  assert(buf->isValid() && "Buffer from pool is invalid!");

#ifndef NDEBUG
  // Debug: Check buffer was not already acquired.
  assert(!buf->isAcquired && "Buffer already acquired!");
  buf->isAcquired = true;
  buf->acquireTimestamp = 0; // Timestamp tracking for leak detection.
  ++totalAcquires_;
#endif

  // Clear metadata for new message.
  buf->clear();

  return buf;
}

void BufferPool::release(MessageBuffer* buf) noexcept {
  // Defensive: Return early if null.
  if (buf == nullptr) {
    return;
  }

  // CRITICAL: Should never reach here with nullptr in correct usage.
  assert(buf != nullptr && "Attempted to release null buffer!");

  // Validate buffer.
  assert(buf->isValid() && "Attempted to release invalid buffer!");

  // Decrement refcount, only return to pool on last reference.
  if (!buf->decRef()) {
    // Not the last reference, other recipients still using this buffer.
    return;
  }

  // Last reference: return buffer to pool.
#ifndef NDEBUG
  // Debug: Check buffer was acquired.
  assert(buf->isAcquired && "Buffer was not acquired!");
  buf->isAcquired = false;
  buf->acquireTimestamp = 0;
  ++totalReleases_;
#endif

  // Clear metadata and reset refcount for next use.
  buf->clear();

  // Return buffer to free list.
  const bool pushed = freeList_.tryPush(buf);
  assert(pushed && "Free list should never be full during release!");
  (void)pushed; // Silence unused variable warning.

  // Note: If push fails (should never happen), buffer is leaked.
  // In debug mode, destructor will detect this.
}

/* ----------------------------- Statistics ----------------------------- */

std::size_t BufferPool::available() const noexcept {
  // LockFreeQueue::sizeApprox() is lock-free and RT-safe.
  return freeList_.sizeApprox();
}

} // namespace interface
} // namespace system_core
