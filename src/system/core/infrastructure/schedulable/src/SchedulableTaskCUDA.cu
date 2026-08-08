/**
 * @file SchedulableTaskCUDA.cu
 * @brief Implementation of CUDA-capable schedulable task.
 *
 * Key design points:
 *  - Event created with cudaEventDisableTiming for minimal overhead
 *  - All completion methods are designed for RT-safety where noted
 *  - Move semantics properly transfer CUDA resource ownership
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskCUDA.cuh"

#include <cstdint>
#include <utility>

namespace system_core {
namespace schedulable {

/* ----------------------------- SchedulableTaskCUDA Methods ----------------------------- */

SchedulableTaskCUDA::SchedulableTaskCUDA(TaskFn task, std::string_view label) noexcept
    : SchedulableTask(task, label) {
  // Create event with timing disabled for minimal overhead (~100ns vs ~500ns)
  // cudaEventDisableTiming also avoids implicit synchronization
  cudaEventCreateWithFlags(&completionEvent_, cudaEventDisableTiming);
}

SchedulableTaskCUDA::~SchedulableTaskCUDA() noexcept {
  if (completionEvent_) {
    cudaEventDestroy(completionEvent_);
    completionEvent_ = nullptr;
  }
}

SchedulableTaskCUDA::SchedulableTaskCUDA(SchedulableTaskCUDA&& other) noexcept
    : SchedulableTask(std::move(other)), cudaStream_(other.cudaStream_),
      completionEvent_(other.completionEvent_),
      eventRecorded_(other.eventRecorded_.load(std::memory_order_relaxed)) {
  // Take ownership - null out source. Moves are single-threaded by
  // contract (config time), so relaxed ordering suffices here.
  other.cudaStream_ = nullptr;
  other.completionEvent_ = nullptr;
  other.eventRecorded_.store(false, std::memory_order_relaxed);
}

SchedulableTaskCUDA& SchedulableTaskCUDA::operator=(SchedulableTaskCUDA&& other) noexcept {
  if (this != &other) {
    // Destroy our event first
    if (completionEvent_) {
      cudaEventDestroy(completionEvent_);
    }

    // Move base
    SchedulableTask::operator=(std::move(other));

    // Take ownership of CUDA resources
    cudaStream_ = other.cudaStream_;
    completionEvent_ = other.completionEvent_;
    eventRecorded_.store(other.eventRecorded_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);

    // Null out source
    other.cudaStream_ = nullptr;
    other.completionEvent_ = nullptr;
    other.eventRecorded_.store(false, std::memory_order_relaxed);
  }
  return *this;
}

std::uint8_t SchedulableTaskCUDA::execute() noexcept {
  // Reset completion state for this execution. Release so a poll thread
  // never pairs a stale true with this execution's un-recorded event.
  eventRecorded_.store(false, std::memory_order_release);

  // Call the delegate - it should launch GPU work and call recordCompletion()
  return SchedulableTask::execute();
}

void SchedulableTaskCUDA::setCudaStream(cudaStream_t stream) noexcept { cudaStream_ = stream; }

cudaStream_t SchedulableTaskCUDA::getCudaStream() const noexcept { return cudaStream_; }

void SchedulableTaskCUDA::recordCompletion() noexcept {
  if (completionEvent_) {
    cudaEventRecord(completionEvent_, cudaStream_);
    eventRecorded_.store(true, std::memory_order_release);
  }
}

bool SchedulableTaskCUDA::isComplete() const noexcept {
  // If no event was recorded, consider it complete (no GPU work)
  if (!eventRecorded_.load(std::memory_order_acquire) || !completionEvent_) {
    return true;
  }

  // Non-blocking query - returns cudaSuccess if complete
  cudaError_t status = cudaEventQuery(completionEvent_);
  return (status == cudaSuccess);
}

void SchedulableTaskCUDA::waitComplete() noexcept {
  // WARNING: Blocking call - not RT-safe
  if (eventRecorded_.load(std::memory_order_acquire) && completionEvent_) {
    cudaEventSynchronize(completionEvent_);
  }
}

void SchedulableTaskCUDA::resetCompletion() noexcept {
  eventRecorded_.store(false, std::memory_order_release);
}

} // namespace schedulable
} // namespace system_core
