/**
 * @file SchedulableTaskCUDA.cuh
 * @brief CUDA-capable schedulable task with stream and event support.
 *
 * Design goals:
 *  - Minimal wrapper over SchedulableTask for CUDA integration
 *  - Stream binding for kernel execution
 *  - Event-based completion tracking (RT-safe, non-blocking)
 *  - Zero overhead when CUDA features not used
 *
 * Usage pattern:
 *  1. Create task with CUDA-aware callable
 *  2. Bind stream via setCudaStream()
 *  3. Callable launches kernels to stream, then calls recordCompletion()
 *  4. Scheduler checks isComplete() for non-blocking completion status
 *
 * RT-Safety:
 *  - execute(): Same as SchedulableTask (sub-microsecond)
 *  - isComplete(): Non-blocking cudaEventQuery (~100ns)
 *  - recordCompletion(): Non-blocking cudaEventRecord (~100ns)
 *  - DO NOT use waitComplete() in RT paths (blocking)
 */

#ifndef APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKCUDA_CUH
#define APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKCUDA_CUH

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"

#include <cuda_runtime_api.h>
#include <cstdint>
#include <string_view>

namespace system_core {
namespace schedulable {

/**
 * @class SchedulableTaskCUDA
 * @brief Schedulable task with CUDA stream and event support.
 *
 * Extends SchedulableTask with:
 *  - CUDA stream for kernel execution
 *  - CUDA event for non-blocking completion tracking
 *
 * The callable should:
 *  1. Launch kernels to getCudaStream()
 *  2. Call recordCompletion() after all GPU work is enqueued
 *
 * The scheduler can then use isComplete() to check if GPU work is done
 * without blocking the RT thread.
 */
class SchedulableTaskCUDA : public SchedulableTask {
public:
  /**
   * @brief Construct a CUDA-capable task.
   * @param task   Delegate (fnptr + context).
   * @param label  Non-owning label (caller ensures lifetime).
   *
   * Creates completion event with cudaEventDisableTiming for minimal overhead.
   */
  SchedulableTaskCUDA(TaskFn task, std::string_view label) noexcept;

  /**
   * @brief Destructor - destroys CUDA event.
   */
  ~SchedulableTaskCUDA() noexcept override;

  // Non-copyable (owns CUDA resources)
  SchedulableTaskCUDA(const SchedulableTaskCUDA&) = delete;
  SchedulableTaskCUDA& operator=(const SchedulableTaskCUDA&) = delete;

  // Movable
  SchedulableTaskCUDA(SchedulableTaskCUDA&& other) noexcept;
  SchedulableTaskCUDA& operator=(SchedulableTaskCUDA&& other) noexcept;

  /**
   * @brief Execute task callable.
   * @return Task-specific status code.
   *
   * Just calls the delegate. The callable is responsible for:
   *  1. Launching GPU work to getCudaStream()
   *  2. Calling recordCompletion() when done enqueuing
   */
  [[nodiscard]] std::uint8_t execute() noexcept override;

  /* ----------------------------- Stream Management ----------------------------- */

  /**
   * @brief Bind CUDA stream for kernel execution.
   * @param stream CUDA stream handle (can be nullptr for default stream).
   */
  void setCudaStream(cudaStream_t stream) noexcept;

  /**
   * @brief Get currently bound CUDA stream.
   * @return CUDA stream handle (nullptr = default stream).
   */
  [[nodiscard]] cudaStream_t getCudaStream() const noexcept;

  /* ----------------------------- Completion Tracking ----------------------------- */

  /**
   * @brief Record completion event on the bound stream.
   *
   * Call this after all GPU work is enqueued to the stream.
   * This is a non-blocking operation (~100ns).
   *
   * RT-safe: Yes (non-blocking)
   */
  void recordCompletion() noexcept;

  /**
   * @brief Check if GPU work is complete (non-blocking).
   * @return true if all work before recordCompletion() has finished.
   *
   * Uses cudaEventQuery() which returns immediately.
   * Returns true if:
   *  - Event was never recorded (no GPU work)
   *  - All GPU work has completed
   *
   * RT-safe: Yes (non-blocking, ~100ns)
   */
  [[nodiscard]] bool isComplete() const noexcept;

  /**
   * @brief Block until GPU work is complete.
   *
   * WARNING: NOT RT-safe. Only use in non-RT paths (init, shutdown).
   * Uses cudaEventSynchronize() which blocks the calling thread.
   */
  void waitComplete() noexcept;

  /**
   * @brief Reset completion state for next execution.
   *
   * Call before re-executing if you need fresh completion tracking.
   */
  void resetCompletion() noexcept;

private:
  cudaStream_t cudaStream_{nullptr};
  cudaEvent_t completionEvent_{nullptr};
  bool eventRecorded_{false};
};

} // namespace schedulable
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKCUDA_CUH
