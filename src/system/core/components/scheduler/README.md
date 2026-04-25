# Scheduler Library

**Namespace:** `system_core::scheduler`
**Platform:** Linux (POSIX), bare-metal MCU (McuScheduler)
**C++ Standard:** C++23

RT-friendly task scheduling with scheduler-owned configuration, N/D frequency decimation, and support for single-threaded, multi-threaded, and bare-metal execution models.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [Architecture](#4-architecture)
5. [Key Features](#5-key-features)
6. [API Reference](#6-api-reference)
7. [Usage Examples](#7-usage-examples)
8. [Requirements](#8-requirements)
9. [Testing](#9-testing)
10. [See Also](#10-see-also)

---

## 1. Quick Reference

| Component               | Purpose                                 | RT-Safe |
| ----------------------- | --------------------------------------- | ------- |
| `IScheduler`            | Pure virtual scheduler interface        | Yes     |
| `SchedulerBase`         | Abstract scheduler with task management | Partial |
| `SchedulerSingleThread` | Sequential execution                    | Partial |
| `SchedulerMultiThread`  | Parallel execution with ThreadPool      | Partial |
| `McuScheduler<N>`       | Static-table scheduler for MCUs         | Partial |
| `TaskConfig`            | Scheduling configuration (POD)          | Yes     |
| `TaskEntry`             | Task + config + runtime state           | Yes     |
| `SequenceGroup`         | Phase-based task sequencing             | Yes     |
| `TaskBuilder`           | Delegate binding helpers                | Yes     |
| `Status`                | Typed status codes                      | Yes     |

### Quick Example

```cpp
#include "src/system/core/components/scheduler/posix/inc/SchedulerSingleThread.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

using system_core::scheduler::SchedulerSingleThread;
using system_core::scheduler::TaskConfig;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::bindMember;

struct SensorReader {
  std::uint8_t read() noexcept { return 0; }
};

SensorReader sensor;
auto delegate = bindMember<SensorReader, &SensorReader::read>(&sensor);
SchedulableTask task(delegate, "sensor_read");

SchedulerSingleThread scheduler(nullptr, 1000, "/tmp/logs");
TaskConfig config(100, 1, 0, PRIORITY_HIGH);
scheduler.addTask(task, config);
scheduler.init();
scheduler.executeTasksOnTickSingle(0);
```

---

## 2. When to Use

| Scenario                                     | Use This Library?                              |
| -------------------------------------------- | ---------------------------------------------- |
| Deterministic single-threaded task execution | Yes -- `SchedulerSingleThread`                 |
| Parallel task execution with thread pools    | Yes -- `SchedulerMultiThread`                  |
| Bare-metal MCU scheduling (no heap)          | Yes -- `McuScheduler<N>`                       |
| N/D frequency decimation with phase offsets  | Yes -- `TaskConfig` with freqN/freqD/offset    |
| Intra-frame task ordering dependencies       | Yes -- `SequenceGroup` with phase-based waits  |
| TPRM-based runtime task configuration        | Yes -- `SchedulerData` with SDAT binary export |
| Simple timer/callback scheduling             | No -- use OS timer APIs directly               |

**Design intent:** Tasks are minimal executables; the scheduler owns all configuration. Two execution modes (sequential, parallel) share a common base. The MCU variant provides zero-heap scheduling for MCU targets with static task tables.

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point, 10000 cycles.

### Single-Threaded Execution

| Operation             | Median (us) | Calls/s | CV%  |
| --------------------- | ----------- | ------- | ---- |
| Empty tick overhead   | 0.015       | 67.6M   | 4.7% |
| 100 tasks, no work    | 2.260       | 442.5K  | 8.5% |
| 100 tasks, light work | 26.434      | 37.8K   | 1.9% |
| 1000 tasks, execute   | 8.749       | 114.3K  | 1.0% |

### Multi-Threaded Execution

| Operation             | Median (us) | Calls/s | CV%  |
| --------------------- | ----------- | ------- | ---- |
| Empty tick overhead   | 0.047       | 21.3M   | 4.8% |
| 100 tasks, dispatch   | 379.3       | 2.6K    | 4.9% |
| 100 tasks, light work | 381.0       | 2.6K    | 4.9% |
| 1000 tasks, scaling   | 1443.4      | 693     | 1.7% |
| Concurrent stress     | 1467.2      | 682     | 1.1% |

### Sequencing Overhead

| Operation                   | Median (us) | Calls/s | CV%  |
| --------------------------- | ----------- | ------- | ---- |
| `waitForPhase` (fast path)  | 0.009       | 116.3M  | 3.4% |
| `advancePhase`              | 0.024       | 41.8M   | 5.1% |
| `waitForPhase` (spin path)  | 90.281      | 11.1K   | 1.0% |
| Chain4 minimal work         | 261.2       | 3.8K    | 1.3% |
| ThreadPool dispatch latency | 3.372       | 296.6K  | 4.4% |

### McuScheduler Execution

| Operation                     | Median (us) | Calls/s | CV%   |
| ----------------------------- | ----------- | ------- | ----- |
| Empty tick (0 tasks)          | 0.009       | 116.3M  | 5.8%  |
| 8 noop tasks                  | 0.051       | 19.5M   | 0.8%  |
| 32 noop tasks                 | 0.191       | 5.2M    | 37.1% |
| 8 tasks, light work (spin=64) | 0.798       | 1.3M    | 1.3%  |
| 16 tasks, mixed rate groups   | 0.093       | 10.7M   | 3.6%  |
| addTask x32                   | 0.221       | 4.5M    | 2.9%  |
| init (sort 32 tasks)          | 0.322       | 3.1M    | 1.8%  |

### Profiler Analysis (gperftools)

**MCU AllTests (184 samples):**

| Function                      | Self-Time | Type                        |
| ----------------------------- | --------- | --------------------------- |
| `mcuTaskThunk` (user payload) | 43.5%     | CPU-bound (task execution)  |
| `addTask`                     | 20.7%     | CPU-bound (array insertion) |
| `shouldExecute`               | 14.7%     | CPU-bound (modulus check)   |
| `McuScheduler` (constructor)  | 6.5%      | CPU-bound (array zeroing)   |
| `tick`                        | 6.0%      | CPU-bound (iteration loop)  |
| `sortTasksByPriority`         | 2.7%      | CPU-bound (insertion sort)  |

**ST TaskCountScaling/1000 (8410 samples):**

| Function                   | Self-Time | Type                              |
| -------------------------- | --------- | --------------------------------- |
| `SchedulableTask::execute` | 29.9%     | CPU-bound (delegate dispatch)     |
| `executeTasksOnTickSingle` | 20.1%     | CPU-bound (task iteration loop)   |
| `taskThunk`                | 13.3%     | CPU-bound (thunk indirection)     |
| `DelegateU8::operator()`   | 11.3%     | CPU-bound (function pointer call) |
| `TaskEntry::shouldRun`     | 8.3%      | CPU-bound (frequency check)       |

**MT AddTaskWarmCost (14856 samples):**

| Function                      | Self-Time | Type                                 |
| ----------------------------- | --------- | ------------------------------------ |
| `SchedulerBase::addTask`      | 13.5%     | CPU-bound (collision check + insert) |
| `std::vector::empty`          | 5.3%      | CPU-bound (vector bookkeeping)       |
| `std::vector::back`           | 3.3%      | CPU-bound (element access)           |
| `SchedulerBase::addTask::$_1` | 2.6%      | CPU-bound (lambda in addTask)        |

### Memory Footprint

| Component               | Stack                                | Heap                        |
| ----------------------- | ------------------------------------ | --------------------------- |
| `SchedulerSingleThread` | ~512B (base + log ptr)               | Vector storage for entries  |
| `SchedulerMultiThread`  | ~640B (base + pool ptr)              | ThreadPool + vector storage |
| `McuScheduler<32>`      | ~416B (32 \* 12B entries + counters) | 0                           |
| `TaskEntry`             | 64B                                  | 0                           |
| `TaskConfig`            | 12B                                  | 0                           |
| `SequenceGroup`         | 32B + atomic counter                 | 0                           |

---

## 4. Architecture

### Design Philosophy

**Tasks are minimal, scheduler owns configuration.**

| Component               | Responsibility                                            |
| ----------------------- | --------------------------------------------------------- |
| `IScheduler`            | Pure virtual interface (tick, taskCount, fundamentalFreq) |
| `SchedulerBase`         | Schedule geometry, task management, TPRM loading          |
| `SchedulerSingleThread` | Sequential execution, deterministic order                 |
| `SchedulerMultiThread`  | Parallel execution with ThreadPool                        |
| `McuScheduler<N>`       | Static task table, no heap, MCU targets                   |

This separation enables:

- Scheduler-side optimization without touching tasks
- Zero allocations in task hot paths
- Single source of truth for scheduling metadata
- Same IScheduler interface for Linux and MCU targets

### Library Structure

| Directory | Library                 | Purpose                            |
| --------- | ----------------------- | ---------------------------------- |
| `base/`   | `scheduler_base`        | IScheduler interface (header-only) |
| `apex/`   | `system_core_scheduler` | Full implementation (SHARED)       |
| `mcu/`    | `scheduler_mcu`         | MCU implementation (header-only)   |

### File Organization

| File                        | Component               | Purpose                                 |
| --------------------------- | ----------------------- | --------------------------------------- |
| `IScheduler.hpp`            | `IScheduler`            | Pure virtual scheduler interface        |
| `SchedulerBase.hpp`         | `SchedulerBase`         | Abstract scheduler with task management |
| `SchedulerSingleThread.hpp` | `SchedulerSingleThread` | Sequential execution                    |
| `SchedulerMultiThread.hpp`  | `SchedulerMultiThread`  | Parallel execution                      |
| `McuScheduler.hpp`          | `McuScheduler<N>`       | Static-table MCU scheduler              |
| `SchedulerData.hpp`         | `SchedulerTaskEntry`    | Packed 15-byte task config for TPRM     |
| `SchedulerStatus.hpp`       | `Status`                | Typed status codes                      |
| `SchedulerExport.hpp`       | SDAT export             | Binary schedule database export         |
| `TaskConfig.hpp`            | `TaskConfig`            | Scheduling configuration (POD)          |
| `TaskCtxPool.hpp`           | `TaskCtxPool`           | Context pool for parallel execution     |
| `SchedulerWaitPolicy.hpp`   | `WaitPolicy`            | Thread wait strategies                  |

### RT-Safety Summary

| Operation                    | RT-Safe | Notes                          |
| ---------------------------- | ------- | ------------------------------ |
| `task.execute()`             | Yes     | Direct delegate call (~5-10ns) |
| `entry.shouldRun()`          | Yes     | Inline frequency check         |
| `executeTasksOnTickSingle()` | Yes     | Sequential, bounded work       |
| `executeTasksOnTickMulti()`  | Yes     | Dispatch to ThreadPool         |
| `scheduler.addTask()`        | No      | Vector growth possible         |
| `scheduler.init()`           | No      | Log creation, allocations      |
| `waitForPhase()`             | Yes     | Hybrid spin/park wait          |
| `advancePhase()`             | Yes     | Atomic increment + notify      |
| `McuScheduler::tick()`       | Yes     | No heap, O(n)                  |

---

## 5. Key Features

### N/D Frequency Decimation

Tasks run at `N/D` of the fundamental frequency:

```cpp
// 1000Hz fundamental, task at 100Hz (every 10th tick)
scheduler.addTask(task, 100, 1);   // N=100, D=1

// Task at 50Hz (every 20th tick)
scheduler.addTask(task, 50, 1);

// Task at 33.33Hz (every 30th tick)
scheduler.addTask(task, 100, 3);   // N=100, D=3 = 33.33Hz
```

### Zero-Allocation Delegate Pattern

Task callables use `DelegateU8` (function pointer + context) for RT-safe invocation:

```cpp
// Member function binding (zero-cost abstraction)
auto delegate = bindMember<MyClass, &MyClass::process>(&instance);

// Stateless lambda binding
auto delegate = bindLambda([]() -> std::uint8_t { return 0; });

// Free function binding
auto delegate = bindFreeFunction(&myFunction);
```

### Scheduler-Owned Configuration

All task configuration lives in `TaskConfig`:

```cpp
TaskConfig config(
    100,              // freqN: frequency numerator
    1,                // freqD: frequency denominator (>=1)
    0,                // offset: tick offset
    PRIORITY_HIGH,    // priority: logical priority [-128, 127]
    0                 // poolId: thread pool ID (multi-threaded only)
);
```

Thread affinity and scheduling policy are configured at pool level (`PoolConfig`), not per-task.

### Phase-Based Sequencing

For intra-frame task ordering, use `SequenceGroup`:

```cpp
SequenceGroup seq(4);

seq.addTask(taskPre1, 1);  // Phase 1 - runs first
seq.addTask(taskPre2, 1);  // Phase 1 - parallel with pre1
seq.addTask(taskStep, 3);  // Phase 3 - waits for phase 1+2
seq.addTask(taskPost, 4);  // Phase 4 - runs after step

TaskConfig cfg(100, 1, 0);
scheduler.addTask(taskPre1, cfg, &seq);
scheduler.addTask(taskPre2, cfg, &seq);
scheduler.addTask(taskStep, cfg, &seq);
scheduler.addTask(taskPost, cfg, &seq);
```

Phase numbers account for task count: if 2 tasks run at phase 1, the next phase is 3.

### McuScheduler for MCUs

Zero-heap scheduler with compile-time-sized task table:

```cpp
using system_core::scheduler::mcu::McuScheduler;

// 8-task table, uint32_t counter (for 8-bit MCUs)
McuScheduler<8, uint32_t> sched(100);  // 100 Hz

sched.addTask({myTask, &ctx, 1, 1, 0, 0, 1});   // 100 Hz
sched.addTask({slowTask, &ctx, 1, 10, 0, 0, 2}); // 10 Hz
sched.init();

while (running) {
  tickSource.waitForNextTick();
  sched.tick();
}
```

Each `McuTaskEntry` is 12 bytes. `McuScheduler<32>` uses 384 bytes of SRAM.

### Schedule Database Export

Export scheduler state to SDAT binary format for offline analysis:

```cpp
scheduler.init();
auto status = scheduler.exportSchedule("/path/to/db/");
// Creates: /path/to/db/schedule.sdat
```

### Dedicated Component Log

Scheduler creates a dedicated log at `logs/scheduler.log`:

```cpp
SchedulerSingleThread scheduler(sysLog, 1000, logDir);
scheduler.init();  // Creates scheduler.log with schedule layout
```

---

## 6. API Reference

### 6.1 IScheduler

| Method              | Purpose                        |
| ------------------- | ------------------------------ |
| `tick()`            | Execute one scheduler tick     |
| `tickCount()`       | Get current tick count         |
| `taskCount()`       | Get number of registered tasks |
| `fundamentalFreq()` | Get fundamental frequency (Hz) |

### 6.2 SchedulerBase

| Method                                | Purpose                   |
| ------------------------------------- | ------------------------- |
| `addTask(task, config)`               | Add task with TaskConfig  |
| `addTask(task, config, seq)`          | Add task with sequencing  |
| `addTask(task, freqN, freqD, offset)` | Add task with params      |
| `getEntry(task)`                      | Get TaskEntry for a task  |
| `fundamentalFreq()`                   | Get fundamental frequency |
| `schedStatus()`                       | Get typed status          |

### 6.3 Scheduler Implementations

| Class                   | Execution Model                                              |
| ----------------------- | ------------------------------------------------------------ |
| `SchedulerSingleThread` | `executeTasksOnTickSingle(tick)` - sequential, deterministic |
| `SchedulerMultiThread`  | `executeTasksOnTickMulti(tick)` - parallel with ThreadPool   |

### 6.4 TaskConfig

| Field/Method     | Purpose                         |
| ---------------- | ------------------------------- |
| `freqN`, `freqD` | Frequency numerator/denominator |
| `offset`         | Tick offset within period       |
| `priority`       | Logical priority [-128, 127]    |
| `poolId`         | Thread pool ID (0 = default)    |
| `frequency()`    | Computed frequency (N/D)        |

### 6.5 TaskEntry

| Field/Method    | Purpose                                          |
| --------------- | ------------------------------------------------ |
| `task`          | Non-owning pointer to SchedulableTask            |
| `config`        | TaskConfig for this task                         |
| `holdCtr`       | Frequency decimation counter                     |
| `seqCounter`    | Shared sequencing counter (null = no sequencing) |
| `seqPhase`      | Phase to wait for                                |
| `seqMaxPhase`   | Max phase (for counter wrap)                     |
| `shouldRun()`   | Check if task runs this tick                     |
| `isSequenced()` | Check if task has sequencing                     |
| `resetHold()`   | Reset decimation counter                         |

### 6.6 SequenceGroup

| Method                    | Purpose                        |
| ------------------------- | ------------------------------ |
| `SequenceGroup(maxPhase)` | Construct with max phase count |
| `addTask(task, phase)`    | Register task at phase         |
| `getSeqInfo(task)`        | Get SeqInfo for task           |
| `counter()`               | Get shared atomic counter      |
| `maxPhase()`              | Get max phase number           |
| `reset()`                 | Reset counter to phase 1       |

Helper functions:

| Function                          | Purpose                           |
| --------------------------------- | --------------------------------- |
| `waitForPhase(counter, phase)`    | Wait until counter >= phase       |
| `advancePhase(counter, maxPhase)` | Increment counter, notify waiters |

### 6.7 TaskBuilder Helpers

| Function                  | Purpose                           |
| ------------------------- | --------------------------------- |
| `bindMember<T, &T::fn>()` | Zero-cost member function binding |
| `bindLambda()`            | Stateless lambda binding          |
| `bindFreeFunction()`      | C-style function binding          |

### 6.8 McuScheduler

| Method                 | Purpose                    |
| ---------------------- | -------------------------- |
| `McuScheduler(freqHz)` | Construct with frequency   |
| `addTask(entry)`       | Add task to static table   |
| `tick()`               | Execute one scheduler tick |
| `taskCount()`          | Get registered task count  |
| `maxTasks()`           | Get compile-time capacity  |
| `task(idx)`            | Get task entry by index    |
| `clearTasks()`         | Remove all tasks           |

### 6.9 Priority Constants

```cpp
PRIORITY_LOWEST   // -128
PRIORITY_LOW      // -64
PRIORITY_NORMAL   // 0
PRIORITY_HIGH     // 63
PRIORITY_HIGHEST  // 127
```

### 6.10 Status Codes

| Status                      | Meaning                   |
| --------------------------- | ------------------------- |
| `SUCCESS`                   | Operation succeeded       |
| `ERROR_NOT_INITIALIZED`     | Scheduler not initialized |
| `ERROR_ALREADY_INITIALIZED` | Duplicate init() call     |
| `ERROR_NULL_TASK`           | Null task pointer         |
| `ERROR_CAPACITY_EXCEEDED`   | Max tasks reached         |
| `ERROR_DUPLICATE_TASK`      | Task already registered   |

---

## 7. Usage Examples

### 7.1 Single-Threaded Execution

```cpp
SchedulerSingleThread scheduler(sysLog, 1000, logDir);

TaskConfig cfg100Hz(100, 1, 0, PRIORITY_HIGH);
TaskConfig cfg50Hz(50, 1, 0);
scheduler.addTask(sensorTask, cfg100Hz);
scheduler.addTask(logTask, cfg50Hz);

scheduler.init();
scheduler.executeTasksOnTickSingle(tick);
```

### 7.2 Multi-Threaded Execution

```cpp
SchedulerMultiThread scheduler(sysLog, 1000, logDir);

scheduler.addTask(task1, 100, 1);
scheduler.addTask(task2, 100, 1);
scheduler.addTask(task3, 100, 1);

scheduler.init();
scheduler.executeTasksOnTickMulti(tick);
```

### 7.3 Schedule Geometry

```cpp
// Fundamental: 1000Hz, Task: 100Hz
// periodTicks = 1000 / 100 = 10
// Task runs at ticks: 0, 10, 20, 30, ...

// With offset = 5
// Task runs at ticks: 5, 15, 25, 35, ...
scheduler.addTask(task, 100, 1, 5);

// With decimation D=2 (50Hz effective)
// periodTicks = (1000 / 100) * 2 = 20
scheduler.addTask(task, 100, 2);
```

### 7.4 MCU Bare-Metal

```cpp
// AVR/STM32: 8-task, 32-bit counter
McuScheduler<8, uint32_t> sched(100);

void sensorRead(void* ctx) noexcept { /* ... */ }
void commTx(void* ctx) noexcept { /* ... */ }

sched.addTask({sensorRead, nullptr, 1, 1, 0, 10, 1});  // 100 Hz, high priority
sched.addTask({commTx, nullptr, 1, 10, 0, 0, 2});      // 10 Hz, normal priority
sched.init();

while (true) {
  waitForTick();
  sched.tick();
}
```

---

## 8. Requirements

### Build Dependencies

- C++17 compiler (GCC 10+, Clang 12+)
- fmt library (formatting)
- POSIX threads (pthreads)

### Runtime

- Linux (POSIX scheduler semantics)
- CAP_SYS_NICE for real-time policies (optional)

### Optional

- CUDA toolkit for `SchedulableTaskCUDA`

---

## 9. Testing

| Directory    | Type                   | Tests | Runs with `make test` |
| ------------ | ---------------------- | ----- | --------------------- |
| `apex/utst/` | Unit tests             | 61    | Yes                   |
| `base/utst/` | Unit tests             | 6     | Yes                   |
| `mcu/utst/`  | Unit tests             | 18    | Yes                   |
| `apex/ptst/` | Performance benchmarks | 23    | No (manual)           |
| `mcu/ptst/`  | Performance benchmarks | 7     | No (manual)           |

### Test Organization

| Component           | Test File                                | Tests  |
| ------------------- | ---------------------------------------- | ------ |
| SchedulableTask     | `apex/utst/SchedulableTask_uTest.cpp`    | 11     |
| SchedulerData       | `apex/utst/SchedulerData_uTest.cpp`      | 19     |
| SchedulerStatus     | `apex/utst/SchedulerStatus_uTest.cpp`    | 11     |
| TaskBuilder         | `apex/utst/TaskBuilder_uTest.cpp`        | 9      |
| SchedulableTaskCUDA | `apex/utst/SchedulableTaskCUDA_uTest.cu` | 8      |
| SchedulerOverhead   | `apex/utst/SchedulerOverhead_uTest.cpp`  | 3      |
| IScheduler          | `base/utst/IScheduler_uTest.cpp`         | 6      |
| McuScheduler        | `mcu/utst/McuScheduler_uTest.cpp`        | 18     |
| **Total**           |                                          | **85** |

---

## 10. See Also

- `src/system/core/infrastructure/schedulable/` - Task abstractions (SchedulableTask, SchedulableTaskCUDA)
- `src/system/core/infrastructure/system_component/` - SystemComponentBase lifecycle
- `src/system/core/infrastructure/logs/` - SystemLog for logging integration
- `src/system/core/executive/` - ApexExecutive uses scheduler
- `src/utilities/concurrency/` - ThreadPool and Delegate
- `tools/rust/` - `sdat_tool` for SDAT file analysis
