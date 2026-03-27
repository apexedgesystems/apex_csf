# Schedulable Task Library

Minimal task abstractions for RT-friendly scheduling. Tasks are lightweight callables (~24 bytes) with all scheduling configuration managed by the scheduler.

**Library:** `system_core_schedulable`
**Namespace:** `system_core::schedulable`
**Headers:** `inc/SchedulableTask.hpp`, `inc/TaskBuilder.hpp`, `inc/SequenceGroup.hpp`

---

## 1. Quick Reference

| Component             | Type           | Purpose                                                  | RT-Safe                                                |
| --------------------- | -------------- | -------------------------------------------------------- | ------------------------------------------------------ |
| `SchedulableTaskBase` | Abstract class | Base with callable + label interface                     | Construction: No, Runtime: Yes                         |
| `SchedulableTask`     | Class          | Concrete task wrapper (~24 bytes)                        | Construction: No, Runtime: Yes                         |
| `SchedulableTaskCUDA` | Class          | CUDA-enabled task with stream/event                      | Construction: No, Runtime: Yes (except `waitComplete`) |
| `TaskFn` (DelegateU8) | Type alias     | Function pointer + void\* context                        | Yes                                                    |
| `SequenceGroup`       | Class          | Phase-based task sequencing for intra-frame coordination | Yes (worker threads)                                   |
| `bindMember`          | Free function  | Zero-cost member function binding                        | Yes (constexpr)                                        |
| `bindLambda`          | Free function  | Zero-cost stateless lambda binding                       | Yes (constexpr)                                        |
| `bindFreeFunction`    | Free function  | Zero-cost free function binding                          | Yes (constexpr)                                        |

| Question                                          | Answer                                     |
| ------------------------------------------------- | ------------------------------------------ |
| How do I wrap a callable for scheduler execution? | `SchedulableTask`                          |
| How do I bind a member function to a task?        | `bindMember<T, &T::method>(obj)`           |
| How do I create a GPU-tracked task?               | `SchedulableTaskCUDA`                      |
| How do I coordinate task ordering within a frame? | `SequenceGroup`                            |
| What is the task footprint?                       | ~24 bytes (vtable + delegate + label view) |

---

## 2. When to Use

| Scenario                                       | Use This Library?                 |
| ---------------------------------------------- | --------------------------------- |
| Wrap a callable for scheduler execution        | Yes -- `SchedulableTask`          |
| Bind member/lambda/free function to task       | Yes -- `TaskBuilder` helpers      |
| GPU kernel task with completion tracking       | Yes -- `SchedulableTaskCUDA`      |
| Phase-based intra-frame task ordering          | Yes -- `SequenceGroup`            |
| Task scheduling configuration (freq, priority) | No -- scheduler owns config       |
| Thread pool management                         | No -- use `utilities_concurrency` |

**Design intent:** Tasks are minimal. Scheduler owns configuration. This separation enables zero-allocation hot paths and ~24 byte task footprint (vtable + delegate + label view).

---

## 3. Performance

### Task Dispatch Latency

| Operation                       | Median (us) | Calls/s | CV%  |
| ------------------------------- | ----------- | ------- | ---- |
| `execute` (baseline, with work) | 0.009       | 109.9M  | 7.8% |
| `execute` (noop delegate)       | 0.008       | 120.5M  | 4.4% |
| Virtual call overhead           | 0.009       | 106.4M  | 2.6% |
| Member binding overhead         | 0.010       | 100.0M  | 3.7% |
| Callable accessor               | 0.009       | 107.5M  | 4.3% |
| Construction                    | 0.012       | 86.2M   | 4.6% |

### Cache Effects

| Scenario                 | Tasks | Median (us) | Calls/s | CV%  |
| ------------------------ | ----- | ----------- | ------- | ---- |
| L1 fit (hot dispatch)    | 1000  | 4.810       | 207.9K  | 2.1% |
| Large working set (cold) | 1000  | 47.933      | 20.9K   | 3.1% |

### Task Count Scaling

| Tasks | Median (us) | Per-Task (ns) | CV%   |
| ----- | ----------- | ------------- | ----- |
| 1     | 0.023       | 23.0          | 33.5% |
| 10    | 0.132       | 13.2          | 6.6%  |
| 50    | 0.610       | 12.2          | 6.9%  |
| 100   | 1.216       | 12.2          | 3.5%  |
| 250   | 2.955       | 11.8          | 4.4%  |
| 500   | 5.844       | 11.7          | 3.6%  |
| 1000  | 11.690      | 11.7          | 2.9%  |

### Profiler Analysis (gperftools)

| Function                    | Self-Time | Type                                      |
| --------------------------- | --------- | ----------------------------------------- |
| `SchedulableTask::execute`  | 14.8%     | CPU-bound (delegate dispatch)             |
| `DelegateU8::operator()`    | 8.4%      | CPU-bound (function pointer call)         |
| `simpleTask` (test payload) | 8.6%      | CPU-bound (actual work)                   |
| `unique_ptr::operator->`    | 5.5%      | CPU-bound (iterator overhead, cache test) |

### Memory Footprint

| Component                      | Stack                                   | Heap                       |
| ------------------------------ | --------------------------------------- | -------------------------- |
| `SchedulableTask`              | ~24B (vtable + delegate + label view)   | 0                          |
| `SchedulableTaskCUDA`          | ~48B (base + stream ptr + CUDA event)   | 0 (CUDA driver owns event) |
| `SequenceGroup` registry entry | 4B (phase + maxPhase)                   | 0                          |
| `TaskFn` (DelegateU8)          | 16B (function pointer + void\* context) | 0                          |

---

## 4. Design Principles

- **Tasks are minimal** -- ~24 byte footprint, no configuration state
- **Scheduler owns configuration** -- Frequency, priority, sequencing live in scheduler
- **Zero allocations in hot paths** -- `execute()` is a direct delegate call (~9ns)
- **RT-safe runtime path** -- All dispatch operations are bounded-time, no exceptions
- **Zero-cost binding** -- `bindMember`, `bindLambda`, `bindFreeFunction` are constexpr
- **Linear scaling** -- Per-task cost converges to ~12ns regardless of task count
- **CUDA integration** -- Non-blocking GPU completion tracking via cudaEventQuery
- **Phase-based sequencing** -- `SequenceGroup` enables intra-frame task ordering with hybrid spin/park

---

## 5. API Reference

### SchedulableTaskBase

```cpp
class SchedulableTaskBase {
public:
  using TaskFn = apex::concurrency::DelegateU8;

  /// @note NOT RT-safe: May involve string operations.
  SchedulableTaskBase(TaskFn callable, std::string_view label) noexcept;

  /// @note RT-safe: Direct delegate call.
  virtual std::uint8_t execute() = 0;

  /// @note RT-safe: Returns string_view.
  [[nodiscard]] std::string_view getLabel() const noexcept;
};
```

### SchedulableTask

```cpp
class SchedulableTask : public SchedulableTaskBase {
public:
  /// @note NOT RT-safe: Base construction.
  SchedulableTask(TaskFn callable, std::string_view label) noexcept;

  /// @note RT-safe: Direct delegate call (~9ns).
  std::uint8_t execute() override;

  /// @note RT-safe: Returns delegate reference.
  [[nodiscard]] const TaskFn& callable() const noexcept;
};
```

### SchedulableTaskCUDA

```cpp
class SchedulableTaskCUDA : public SchedulableTaskBase {
public:
  /// @note NOT RT-safe: Creates CUDA event.
  SchedulableTaskCUDA(TaskFn callable, std::string_view label);

  /// @note RT-safe: Pointer assignment.
  void setCudaStream(cudaStream_t stream) noexcept;

  /// @note RT-safe: Returns pointer.
  [[nodiscard]] cudaStream_t getCudaStream() const noexcept;

  /// @note RT-safe: cudaEventRecord (~100ns).
  void recordCompletion() noexcept;

  /// @note RT-safe: cudaEventQuery (~100ns), non-blocking.
  [[nodiscard]] bool isComplete() const noexcept;

  /// @note NOT RT-safe: cudaEventSynchronize (blocking).
  void waitComplete() const;

  /// @note RT-safe: Resets event state.
  void resetCompletion() noexcept;
};
```

### TaskBuilder (Binding Helpers)

```cpp
/// @note RT-safe: Constexpr, zero-cost.
template <typename T, std::uint8_t (T::*Method)() noexcept>
DelegateU8 bindMember(T* obj) noexcept;

/// @note RT-safe: Constexpr, zero-cost.
template <typename Lambda>
DelegateU8 bindLambda(Lambda* lambda) noexcept;

/// @note RT-safe: Constexpr, zero-cost.
DelegateU8 bindFreeFunction(std::uint8_t (*fn)(void*) noexcept, void* ctx = nullptr) noexcept;
```

### SequenceGroup

```cpp
class SequenceGroup {
public:
  /// @note RT-safe (worker threads): Hybrid spin/park wait.
  void waitForPhase(std::uint8_t phase) noexcept;

  /// @note RT-safe (worker threads): Atomic CAS advance.
  void advancePhase() noexcept;
};
```

---

## 6. Usage Examples

### Basic Task Creation

```cpp
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

namespace sched = system_core::schedulable;

struct SensorReader {
  std::uint8_t read() noexcept { /* ... */ return 0; }
};

SensorReader sensor;
auto delegate = sched::bindMember<SensorReader, &SensorReader::read>(&sensor);
sched::SchedulableTask task(delegate, "sensor_read");

std::uint8_t result = task.execute();  // ~9ns
```

### CUDA Task

```cpp
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskCUDA.cuh"

namespace sched = system_core::schedulable;

sched::SchedulableTaskCUDA cudaTask(delegate, "gpu_kernel");
cudaTask.setCudaStream(stream);

// In callable: launch kernels, then record completion
// myKernel<<<blocks, threads, 0, cudaTask.getCudaStream()>>>(...);
// cudaTask.recordCompletion();

// Scheduler can check completion (non-blocking, ~100ns)
if (cudaTask.isComplete()) {
    // GPU work finished
}
```

---

## 7. Testing

### Test Organization

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 34    | Yes                   |
| `ptst/`   | Performance tests | 16    | No (manual)           |

### Test Requirements

- All tests are platform-agnostic (no hardware dependencies, CUDA tests mock events)
- Tests verify construction, execute, callable access, delegate binding
- Tests verify CUDA stream/event lifecycle and completion tracking
- Tests verify task count scaling linearity

---

## 8. See Also

- `src/system/core/components/scheduler/` -- Scheduler implementations (owns task configuration)
- `src/utilities/concurrency/inc/Delegate.hpp` -- DelegateU8 implementation
- `src/utilities/concurrency/` -- Thread pool, lock-free queues
