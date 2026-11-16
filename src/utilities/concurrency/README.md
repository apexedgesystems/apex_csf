# Concurrency

**Namespace:** `apex::concurrency`
**Platform:** Linux (POSIX), cross-platform compatible
**C++ Standard:** C++20 for the built library (`Semaphore` parks on `std::atomic::wait`); declared in `lib.manifest` as `posix_cpp 20 23`. The build compiles the manifest's probe (`probe/Probe.cpp`, the API in use) and one generated TU per public header, each header first and alone, at C++20 and C++23 with `-Werror`. The `mcu/` subset keeps its own C++17 contract.

Thread-safe concurrency primitives optimized for real-time and embedded systems.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Module Selection Guide](#3-module-selection-guide)
4. [Module Reference](#4-module-reference)
5. [Common Patterns](#5-common-patterns)
6. [Performance](#6-performance)
7. [Real-Time Considerations](#7-real-time-considerations)
8. [Building](#8-building)
9. [Testing](#9-testing)
10. [See Also](#10-see-also)

---

## 1. Quick Start

```cpp
#include "src/utilities/concurrency/inc/ThreadPool.hpp"
#include "src/utilities/concurrency/inc/ThreadPoolLockFree.hpp"
#include "src/utilities/concurrency/inc/LockFreeQueue.hpp"
#include "src/utilities/concurrency/inc/SPSCQueue.hpp"
#include "src/utilities/concurrency/inc/RingBuffer.hpp"
#include "src/utilities/concurrency/inc/Semaphore.hpp"
#include "src/utilities/concurrency/inc/SpinLock.hpp"
#include "src/utilities/concurrency/inc/Latch.hpp"
#include "src/utilities/concurrency/inc/Barrier.hpp"

using namespace apex::concurrency;

// Create a thread pool with 4 workers
ThreadPool pool(4);

// Define a task using DelegateU8
auto task = DelegateU8{
  [](void* ctx) noexcept -> std::uint8_t {
    auto* value = static_cast<int*>(ctx);
    *value *= 2;
    return 0; // success
  },
  &myValue
};

// Enqueue the task
pool.enqueueTask("double-value", task);

// Lock-free queue for inter-thread communication
LockFreeQueue<int> queue(1024);
queue.tryPush(42);

int result;
if (queue.tryPop(result)) {
  // result == 42
}
```

---

## 2. Key Features

### Zero-Allocation Task Dispatch

- `DelegateU8` callable wrapper avoids heap allocation
- Function pointer + context pattern for RT-safe dispatch
- No `std::function` overhead or type erasure

### Lock-Free Queues

- **LockFreeQueue** - Bounded MPMC queue (Vyukov algorithm)
- **SPSCQueue** - Optimized single-producer/single-consumer queue
- Non-blocking `tryPush`/`tryPop` API

### Synchronization Primitives

- **Semaphore** - Counting semaphore with timed acquire
- **SpinLock** - Busy-wait lock with platform-optimized PAUSE hints
- **Latch** - One-shot countdown synchronization (`std::latch`'s shape)
- **Barrier** - Reusable multi-phase synchronization (`std::barrier`'s phase semantics)

### Thread Pools

Two thread pool implementations for different use cases:

- **ThreadPool** - Mutex-based with unbounded queue
  - Condition-variable wake (no busy spin)
  - Growable queue, handles bursts
  - Best for variable workloads
- **ThreadPoolLockFree** - Lock-free with bounded queue
  - LockFreeQueue with futex-parked workers (lock-free enqueue)
  - Bounded queue with QUEUE_FULL feedback
  - Best for predictable workloads, backpressure-aware systems

### False Sharing Prevention

- `cache::AlignCl<T>` wrapper for 64-byte alignment
- Hot atomics isolated to separate cache lines

---

## 3. Module Selection Guide

Choose the right module based on your use case, threading model, and target hardware.

### Queue Selection

| Question                                        | Answer | Use               |
| ----------------------------------------------- | ------ | ----------------- |
| Is only one thread accessing the queue?         | Yes    | **RingBuffer**    |
| Is there exactly one producer and one consumer? | Yes    | **SPSCQueue**     |
| Are there multiple producers and/or consumers?  | Yes    | **LockFreeQueue** |

### Queue Comparison

| Queue             | Threading               | Best For                                     |
| ----------------- | ----------------------- | -------------------------------------------- |
| **RingBuffer**    | Single-thread only      | Processing pipelines, event buffers, parsing |
| **SPSCQueue**     | 1 producer, 1 consumer  | Audio/video streaming, sensor data           |
| **LockFreeQueue** | Multi-producer/consumer | Work stealing, task queues, general MPMC     |

Measured costs are in [Performance](#6-performance).

### Synchronization Selection

| Need                                    | Use                    | Why                       |
| --------------------------------------- | ---------------------- | ------------------------- |
| Protect short critical section (<1uss)  | **SpinLock**           | Lower latency than mutex  |
| Protect longer critical section (>1uss) | `std::mutex`           | Don't waste CPU spinning  |
| Limit concurrent resource access        | **Semaphore**          | Counting permits          |
| Wait for N threads to complete          | **Latch**              | One-shot countdown        |
| Sync threads at phase boundaries        | **Barrier**            | Reusable synchronization  |
| Dispatch tasks, handle bursts           | **ThreadPool**         | CV-based, unbounded queue |
| Dispatch tasks, need backpressure       | **ThreadPoolLockFree** | Lock-free, bounded queue  |

### Hardware Considerations

#### High-Performance Workstations (Many Cores, Fast Memory)

```
Scenario: AMD Ryzen 9 / Intel Core i9, 16+ cores, DDR5
```

- **Use LockFreeQueue** for MPMC - CAS contention is low with fast caches
- **Use ThreadPool** with many workers - CV wake latency hidden by parallelism
- **Prefer SPSCQueue** over RingBuffer when producer/consumer are separate threads
- SpinLock works well - short spins complete before cache miss

#### Embedded / Constrained Systems (Few Cores, Limited Memory)

```
Scenario: ARM Cortex-A53/A72, 4 cores, limited cache
```

- **Use RingBuffer** when single-threaded - avoid atomic overhead entirely
- **Avoid LockFreeQueue** - CAS loops expensive on slower cores
- **SPSCQueue for inter-core communication** - minimal atomic overhead
- **Prefer Semaphore over SpinLock** - don't burn CPU spinning
- **Pre-allocate all queues at startup** - avoid runtime allocation

#### RT-Critical Paths

```
Scenario: Hard real-time control loops, audio processing
```

- **RingBuffer for single-thread data buffering** - zero synchronization
- **SPSCQueue for producer/consumer** - predictable single-atomic ops
- **SpinLock::tryLock** only - never block in RT path
- **Semaphore::tryAcquire** only - never block in `acquire()`
- **Avoid ThreadPool::enqueueTask** - mutex contention

#### Multi-Tenant / Cloud (Variable Load, Shared Resources)

```
Scenario: Containerized services, variable CPU allocation
```

- **LockFreeQueue for work distribution** - handles variable contention
- **ThreadPool for background tasks** - CV-based saves CPU quota
- **Avoid SpinLock** - spinning wastes shared CPU time
- **Use Semaphore for rate limiting** - control concurrent access

### Application Examples

| Application              | Recommended Modules        | Rationale                                         |
| ------------------------ | -------------------------- | ------------------------------------------------- |
| Audio DSP pipeline       | RingBuffer + SpinLock      | Single-thread processing, short critical sections |
| Video encoder            | SPSCQueue per stage        | Pipeline stages are producer/consumer pairs       |
| Game engine job system   | LockFreeQueue + ThreadPool | Many workers, dynamic task submission             |
| Network packet processor | SPSCQueue + Latch          | Per-core queues, barrier at epoch boundaries      |
| Database connection pool | Semaphore                  | Limit concurrent connections                      |
| Map-reduce framework     | Barrier + LockFreeQueue    | Phase sync + work distribution                    |
| Sensor data logger       | RingBuffer                 | Single thread reads and writes                    |
| Message broker           | LockFreeQueue              | Multiple publishers and subscribers               |

### Sanitizer-Enforced RT Contracts

`SpinLock`'s methods are tagged with the `APEX_NONBLOCKING` shim
(`compat_rt_attrs.hpp`), which expands to `[[clang::nonblocking]]` under Clang
and to nothing elsewhere. A `-fsanitize=realtime` build (`make rtsan`) then
verifies at runtime that nothing reached from those methods allocates, locks, or
makes a blocking syscall -- so a regression that sneaks a blocking call into a
spin path fails a test rather than a deadline. The annotation is opt-in per
primitive; more RT paths gain it as their `T`-independent nonblocking-ness is
established.

---

## 4. Module Reference

### ThreadPool

**Header:** `ThreadPool.hpp`
**Purpose:** Real-time friendly thread pool for concurrent task execution.

#### Key Types

```cpp
enum class PoolStatus : std::uint8_t {
  SUCCESS = 0,      ///< Operation completed successfully.
  TASK_FAILED = 1,  ///< Task returned non-zero error code.
  POOL_STOPPED = 2, ///< Pool is shutting down, task rejected.
  QUEUE_FULL = 3    ///< Queue is full, task rejected (lock-free pool only).
};

struct TaskError {
  const char* label;      ///< Task identifier (non-owning).
  std::uint8_t errorCode; ///< Task return code (0 = success).
  PoolStatus status;      ///< Categorized error status.
};

class ThreadPool : public PoolBase {
public:
  using TaskFn = DelegateU8;

  explicit ThreadPool(std::size_t numThreads, const PoolConfig& config = {});
  ~ThreadPool() noexcept;

  void enqueueTask(const char* label, TaskFn task);
  PoolStatus tryEnqueue(const char* label, TaskFn task);
  void shutdown() noexcept;

  // From PoolBase, the same for both pools:
  bool isValid() const noexcept;
  std::size_t workerCount() const noexcept;
  std::size_t workerConfigFailures() const noexcept;
  bool threadsRunning() const noexcept;
  bool hasErrors() const noexcept;
  std::queue<TaskError> collectErrors();
};
```

#### Start-up

The constructor returns once every worker has applied its configuration,
or with an invalid pool (`isValid()` false, `workerCount()` 0) when a worker
could not start: the workers that had started are joined first, and the
thread-start failure is reported through invalid state.
`workerConfigFailures()` counts the workers for which a requested setting
(name, affinity, policy with priority) did not apply; the settings are
applied one by one and a failure leaves the others as they are, so a counted
worker may run with part of its configuration, and the count does not say
which setting failed. The constructing thread's signal mask is as it was
when the constructor returns; the workers block SIGINT, SIGTERM and SIGPIPE.

#### RT-Safety

| Method                   | RT-Safe | Reason                         |
| ------------------------ | ------- | ------------------------------ |
| `ThreadPool()`           | No      | Spawns threads                 |
| `~ThreadPool()`          | No      | Joins threads                  |
| `isValid()`              | Yes     | Checks stored size             |
| `workerCount()`          | Yes     | Returns stored size            |
| `workerConfigFailures()` | Yes     | Atomic load                    |
| `enqueueTask()`          | No      | Mutex lock, queue may allocate |
| `tryEnqueue()`           | No      | Mutex lock, queue may allocate |
| `threadsRunning()`       | Yes     | Two atomic loads per worker    |
| `hasErrors()`            | Yes     | Atomic load                    |
| `shutdown()`             | No      | Joins threads                  |
| `collectErrors()`        | No      | Mutex lock                     |

---

### ThreadPoolLockFree

**Header:** `ThreadPoolLockFree.hpp`
**Purpose:** High-performance lock-free thread pool with bounded queue and backpressure.

Uses `LockFreeQueue` for task storage; a worker that finds the ring empty parks on a futex until a producer publishes. Provides immediate `QUEUE_FULL` feedback when the bounded queue is at capacity.

#### Key Types

```cpp
class ThreadPoolLockFree : public PoolBase {
public:
  using TaskFn = DelegateU8;

  explicit ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity = 1024,
                              const PoolConfig& config = {});
  ~ThreadPoolLockFree() noexcept;

  std::size_t capacity() const noexcept;
  PoolStatus tryEnqueue(const char* label, TaskFn task) noexcept;
  void enqueueTask(const char* label, TaskFn task) noexcept;
  void shutdown() noexcept;

  // From PoolBase: isValid(), workerCount(), workerConfigFailures(),
  // threadsRunning(), hasErrors(), collectErrors().
};
```

Start-up is as for `ThreadPool` above: the constructor waits for the
workers' configuration, returns an invalid pool instead of throwing when a
worker cannot start, and `workerConfigFailures()` counts the workers with a
setting that did not apply.

#### Shutdown

`shutdown()` has two stages. It closes admission, so `tryEnqueue()` returns
`POOL_STOPPED`; then it parks until the producers admitted before the close
have published, marks the pool quiescent and wakes the workers. A worker
exits only when it finds the ring empty after seeing that mark, so every
task reported as `SUCCESS` has run when `shutdown()` returns.

#### Worker wake

A producer advances a wake generation after it publishes and issues a futex
wake only while a worker is parked or about to park. A worker drains the
ring until a pop fails, reads the generation, announces itself idle, looks
at the ring once more and parks only if the generation has not moved. A pop
can fail while a producer ahead of a published task is still publishing its
own cell; that producer's signal brings a worker back to drain both. The
generation is not a count of tasks: nothing is stored per task, so a worker
that wakes to an empty ring parks again after two failed pops however many
tasks it delivered before.

The word a worker waits on carries the quiescent mark in a bit of its own,
beside a 31-bit publication counter. `shutdown()` sets the mark once and the
counter never touches it, so the word after shutdown differs from every
value a worker saved before it, a full turn of the counter included.

Before shutdown, a worker delayed across a full turn of the counter (2^31
advances) parks against a counter that moved, which is harmless while some
other worker's view is current. One is, while the ring's capacity plus the
`tryEnqueue()` calls in progress stay below 2^31: the ring is capped at 2^30
cells (`MAX_QUEUE_CAPACITY`), so the condition is fewer than 2^30 producer
calls in progress at once.

#### RT-Safety

| Method                   | RT-Safe | Reason                                                                                            |
| ------------------------ | ------- | ------------------------------------------------------------------------------------------------- |
| `ThreadPoolLockFree()`   | No      | Spawns threads, allocates queue                                                                   |
| `~ThreadPoolLockFree()`  | No      | Joins threads                                                                                     |
| `isValid()`              | Yes     | Checks stored size                                                                                |
| `workerCount()`          | Yes     | Returns stored size                                                                               |
| `capacity()`             | Yes     | Returns queue capacity                                                                            |
| `workerConfigFailures()` | Yes     | Atomic load                                                                                       |
| `tryEnqueue()`           | Depends | No allocation or pool mutex; may issue a futex wake for an idle worker; no per-caller retry bound |
| `enqueueTask()`          | Depends | As `tryEnqueue()`                                                                                 |
| `threadsRunning()`       | Yes     | Two atomic loads per worker                                                                       |
| `hasErrors()`            | Yes     | Atomic load                                                                                       |
| `shutdown()`             | No      | Parks on producers, joins threads                                                                 |
| `collectErrors()`        | No      | Mutex lock                                                                                        |

#### ThreadPool vs ThreadPoolLockFree

| Aspect       | ThreadPool                 | ThreadPoolLockFree           |
| ------------ | -------------------------- | ---------------------------- |
| Queue type   | `std::queue` (unbounded)   | `LockFreeQueue` (bounded)    |
| Enqueue path | Mutex lock                 | No lock; may wake a worker   |
| Backpressure | No (queue grows)           | Yes (QUEUE_FULL status)      |
| Memory       | Grows with load            | Fixed at construction        |
| Best for     | Variable workloads, bursts | Predictable load, RT systems |

The relative cost of the two enqueue paths depends on the task body, the
placement of producer and workers and the host; the module's `ptst/`
fixtures measure it for a given build. No figure here is a budget.

---

### LockFreeQueue

**Header:** `LockFreeQueue.hpp`
**Purpose:** Bounded lock-free MPMC queue using Vyukov's algorithm.

#### Key Types

```cpp
template <class T>
class LockFreeQueue {
public:
  explicit LockFreeQueue(std::size_t capacity) noexcept;
  ~LockFreeQueue();

  bool tryPush(const T& value) noexcept;
  bool tryPush(T&& value) noexcept;
  bool tryPop(T& out) noexcept;
  std::size_t sizeApprox() const noexcept;
  std::size_t capacity() const noexcept;
};
```

#### RT-Safety

| Method             | RT-Safe | Reason               |
| ------------------ | ------- | -------------------- |
| `LockFreeQueue()`  | No      | Allocates buffer     |
| `~LockFreeQueue()` | No      | Deallocates buffer   |
| `tryPush()`        | Yes     | Lock-free CAS loop   |
| `tryPop()`         | Yes     | Lock-free CAS loop   |
| `sizeApprox()`     | Yes     | Atomic loads         |
| `capacity()`       | Yes     | Returns stored value |

#### Requirements

- `T` must be move-constructible and move-assignable
- Capacity >= 2 (rounded up to a power of two); the cells are 64-byte aligned, one per cache line
- `sizeApprox()` may be read from any thread and never exceeds `capacity()`. It is the
  distance between the two cursors, which counts pushes and pops in flight: the element count
  when the queue is quiescent, and no promise that a pop succeeds

---

### SPSCQueue

**Header:** `SPSCQueue.hpp`
**Purpose:** Optimized single-producer/single-consumer lock-free queue.

#### Key Types

```cpp
template <class T>
class SPSCQueue {
public:
  explicit SPSCQueue(std::size_t capacity) noexcept;
  ~SPSCQueue();

  bool tryPush(const T& value) noexcept;
  bool tryPush(T&& value) noexcept;
  bool tryPop(T& out) noexcept;
  bool empty() const noexcept;
  std::size_t sizeApprox() const noexcept;
  std::size_t capacity() const noexcept;
};
```

#### RT-Safety

| Method         | RT-Safe | Reason                 |
| -------------- | ------- | ---------------------- |
| `SPSCQueue()`  | No      | Allocates buffer       |
| `~SPSCQueue()` | No      | Deallocates buffer     |
| `tryPush()`    | Yes     | Single-producer atomic |
| `tryPop()`     | Yes     | Single-consumer atomic |
| `empty()`      | Yes     | Atomic loads           |
| `sizeApprox()` | Yes     | Atomic loads           |
| `capacity()`   | Yes     | Returns stored value   |

#### Requirements

- Single producer thread, single consumer thread only
- `T` must be move-constructible and move-assignable
- Elements are stored at `alignof(T)`, over-aligned types included
- `sizeApprox()` may be read from any thread and never exceeds `capacity()`

Each side keeps its last view of the other side's index beside its own and
reads the real one only when that view says the queue is full (producer) or
empty (consumer). With a backlog in the queue neither side touches the
other's cache line.

---

### RingBuffer

**Header:** `RingBuffer.hpp`
**Purpose:** High-performance single-threaded bounded queue with zero atomic overhead.

#### Key Types

```cpp
template <class T>
class RingBuffer {
public:
  explicit RingBuffer(std::size_t capacity) noexcept;
  ~RingBuffer();

  bool tryPush(const T& value) noexcept;
  bool tryPush(T&& value) noexcept;
  bool tryPop(T& out) noexcept;
  bool tryPeek(T& out) const noexcept;
  bool empty() const noexcept;
  bool full() const noexcept;
  std::size_t size() const noexcept;
  std::size_t capacity() const noexcept;
  void clear() noexcept;
};
```

#### RT-Safety

| Method          | RT-Safe | Reason               |
| --------------- | ------- | -------------------- |
| `RingBuffer()`  | No      | Allocates buffer     |
| `~RingBuffer()` | No      | Deallocates buffer   |
| `tryPush()`     | Yes     | No atomics, O(1)     |
| `tryPop()`      | Yes     | No atomics, O(1)     |
| `tryPeek()`     | Yes     | No atomics, O(1)     |
| `empty()`       | Yes     | Simple comparison    |
| `full()`        | Yes     | Simple comparison    |
| `size()`        | Yes     | Simple arithmetic    |
| `capacity()`    | Yes     | Returns stored value |
| `clear()`       | Yes     | Resets pointers      |

#### Requirements

- **NOT thread-safe** - Single-thread access only
- `T` must be move-constructible and move-assignable
- Elements are stored at `alignof(T)`, over-aligned types included
- Capacity rounded up to power-of-two internally

#### When to Use

- Single-threaded data processing pipelines
- Event buffering within a single thread
- Parsing/decoding buffers
- Any queue where only one thread accesses the data

---

### Semaphore

**Header:** `Semaphore.hpp`
**Purpose:** Counting semaphore for resource limiting and signaling.

#### Key Types

```cpp
class Semaphore {
public:
  explicit Semaphore(std::size_t initialCount = 0) noexcept;

  void acquire();
  [[nodiscard]] bool tryAcquire();
  template <class Rep, class Period>
  [[nodiscard]] bool tryAcquireFor(const std::chrono::duration<Rep, Period>& timeout);
  void release(std::size_t count = 1);
  [[nodiscard]] std::size_t count() const;
};
```

#### RT-Safety

| Method            | RT-Safe | Reason                    |
| ----------------- | ------- | ------------------------- |
| `Semaphore()`     | Yes     | No allocation             |
| `acquire()`       | No      | May block (futex wait)    |
| `tryAcquire()`    | Yes     | Lock-free CAS loop        |
| `tryAcquireFor()` | No      | Timed wait                |
| `release()`       | Yes     | Atomic fetch_add + notify |
| `count()`         | Yes     | Atomic load               |

#### Requirements

- The count is 32 bits and the caller keeps it in range: the initial count and
  each `release()` argument are at most `UINT32_MAX`
- No release may take the permits outstanding above `UINT32_MAX`, counting
  releases made at the same time on other threads; `count()` followed by
  `release()` is not an atomic check
- Nothing is checked or clamped: a value or total above the bound wraps the count

---

### SpinLock

**Header:** `SpinLock.hpp`
**Purpose:** Busy-wait lock with platform-optimized PAUSE hints.

#### Key Types

```cpp
class SpinLock {
public:
  constexpr SpinLock() noexcept = default;

  void lock() noexcept;
  [[nodiscard]] bool tryLock() noexcept;
  void unlock() noexcept;
  [[nodiscard]] bool isLocked() const noexcept;
};
```

#### RT-Safety

| Method       | RT-Safe | Reason                               |
| ------------ | ------- | ------------------------------------ |
| `SpinLock()` | Yes     | No allocation                        |
| `lock()`     | Yes\*   | Busy-wait, bounded if contention low |
| `tryLock()`  | Yes     | Single atomic operation              |
| `unlock()`   | Yes     | Atomic clear                         |
| `isLocked()` | Yes     | Atomic load                          |

\*Note: `lock()` spins indefinitely under high contention.

#### Platform Optimizations

- **x86/x64**: Uses `_mm_pause()` intrinsic
- **ARM64**: Uses `yield` instruction
- **Other**: Falls back to spin-only loop

---

### Latch

**Header:** `Latch.hpp`
**Purpose:** One-shot countdown synchronization with `std::latch`'s shape.

#### Key Types

```cpp
class Latch {
public:
  explicit Latch(std::size_t count) noexcept;

  void countDown(std::size_t n = 1);
  [[nodiscard]] bool tryWait() const noexcept;
  void wait();
  template <class Rep, class Period>
  [[nodiscard]] bool waitFor(const std::chrono::duration<Rep, Period>& timeout);
  void arriveAndWait(std::size_t n = 1);
  [[nodiscard]] std::size_t count() const noexcept;
};
```

`wait()`, `waitFor()` and a true `tryWait()` return after taking the mutex
the final `countDown()` notifies under, so the final `countDown()` is done
with the latch when they return. A waiter that is the last to use the latch
may then destroy it; destruction is not ordered after other waiters or
later callers, which must be known to be finished.

#### RT-Safety

| Method            | RT-Safe | Reason                                                                    |
| ----------------- | ------- | ------------------------------------------------------------------------- |
| `Latch()`         | Yes     | No allocation                                                             |
| `countDown()`     | No      | Mutex lock, CV notify                                                     |
| `tryWait()`       | No\*    | One atomic load while the count is nonzero; a true result takes the mutex |
| `wait()`          | No      | May block on CV                                                           |
| `waitFor()`       | No      | Mutex lock, timed wait                                                    |
| `arriveAndWait()` | No      | Mutex lock, may block                                                     |
| `count()`         | Yes     | Single atomic load                                                        |

\*Note: the not-ready path is one atomic load. The mutex a true result takes
is shared with every other call on the latch, and acquiring it may block for
as long as any holder is scheduled out; a true `tryWait()` is not RT-safe.

---

### Barrier

**Header:** `Barrier.hpp`
**Purpose:** Reusable multi-phase synchronization barrier with `std::barrier`'s phase semantics.

#### Key Types

```cpp
class Barrier {
public:
  explicit Barrier(std::size_t expected) noexcept;

  void arriveAndWait();
  template <class Rep, class Period>
  [[nodiscard]] bool arriveAndWaitFor(const std::chrono::duration<Rep, Period>& timeout);
  void arriveAndDrop();
  [[nodiscard]] std::size_t expected() const;
  [[nodiscard]] std::size_t generation() const;
};
```

Phase semantics follow `std::barrier`: a phase of E participants
completes after E arrivals whether they wait or drop; `arriveAndDrop()`
lowers the participant count from the next phase on, and `expected()`
reports that next-phase count. A timed-out `arriveAndWaitFor()` withdraws
its arrival, so the barrier stays usable and a retry is one new arrival.

#### RT-Safety

| Method               | RT-Safe | Reason                |
| -------------------- | ------- | --------------------- |
| `Barrier()`          | Yes     | No allocation         |
| `arriveAndWait()`    | No      | Mutex lock, may block |
| `arriveAndWaitFor()` | No      | Timed wait            |
| `arriveAndDrop()`    | No      | Mutex lock            |
| `expected()`         | Yes     | Atomic load           |
| `generation()`       | Yes     | Atomic load           |

---

### Shared parts

Three headers hold what the pools and the queues have in common. They are
installed with the library and included by the headers above; a user of a
pool or a queue does not include them directly.

| Header            | Holds                                                                                                                              |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `PoolBase.hpp`    | `PoolStatus`, `TaskError`, `PoolConfig` and `PoolBase`: worker start-up and joining, the in-flight tally, the error queue, queries |
| `RingIndex.hpp`   | `ringSlots()` (slot count for a requested capacity) and `ringOccupancy()` (bounded distance between two free-running cursors)      |
| `RingStorage.hpp` | `RingStorage<Slot>`: the queues' slot array at the slot type's alignment, slots built up front and destroyed with it               |

`threadsRunning()` sums every worker's finished count and then every
started count, and reports a task in flight when the sums differ. A task
that runs through the moment between the two sweeps is always reported,
however work hands over between workers during the call; a task queued and
not yet taken by a worker is not in flight.

---

### DelegateU8 / Delegate<Ret, Args...>

**Header:** `Delegate.hpp` (forwarder; the types live in `mcu/inc/Delegate.hpp`)
**Purpose:** Allocation-free callables for task dispatch and providers.

```cpp
struct DelegateU8 {
  using Fn = uint8_t (*)(void*) noexcept;

  Fn fn{nullptr};     ///< Function pointer.
  void* ctx{nullptr}; ///< Opaque context.

  constexpr uint8_t operator()() const noexcept;
  constexpr explicit operator bool() const noexcept;
};

template <typename Ret, typename... Args> struct Delegate; // Ret(void*, Args...)
// + void specialization and makeDelegate(fn, ctx)
```

**RT-Safety:** Fully RT-safe. No allocation, constexpr-compatible.

**MCU subset:** the delegate types are the concurrency library's freestanding
core, shipped as the `utilities_concurrency_mcu` interface library
(`BAREMETAL`) so firmware callbacks and MCU-clean libraries (math substrate,
frames edge providers) use the same callable shape as the hosted components
without pulling the POSIX concurrency lib. The hosted lib depends on the
subset, and the historical include path keeps working via the forwarder.

---

### AlignCl

**Header:** `Cache.hpp`
**Purpose:** Cacheline-alignment wrapper to prevent false sharing.

```cpp
namespace cache {

constexpr std::size_t CACHE_LINE_SIZE = 64;

template <class T>
struct alignas(CACHE_LINE_SIZE) AlignCl {
  T value;
};

} // namespace cache
```

**RT-Safety:** Fully RT-safe. Compile-time alignment only.

---

## 5. Common Patterns

### Task with Context

```cpp
struct WorkItem {
  int input;
  int output;
};

std::uint8_t processWork(void* ctx) noexcept {
  auto* item = static_cast<WorkItem*>(ctx);
  item->output = item->input * 2;
  return 0;
}

WorkItem work{42, 0};
pool.enqueueTask("process", DelegateU8{processWork, &work});
```

### Producer-Consumer with SPSCQueue

```cpp
SPSCQueue<Message> queue(256);

// Producer thread (one only)
void producer() {
  Message msg{...};
  while (!queue.tryPush(std::move(msg))) {
    // Queue full, retry or handle backpressure
  }
}

// Consumer thread (one only)
void consumer() {
  Message msg;
  while (running) {
    if (queue.tryPop(msg)) {
      process(msg);
    }
  }
}
```

### Resource Pool with Semaphore

```cpp
Semaphore pool(4); // 4 resources available

void useResource() {
  pool.acquire();       // Wait for available resource
  // ... use resource ...
  pool.release();       // Return resource to pool
}

// With timeout
if (pool.tryAcquireFor(std::chrono::milliseconds(100))) {
  // Got resource within timeout
  pool.release();
} else {
  // Timeout, handle gracefully
}
```

### Worker Coordination with Latch

```cpp
Latch startGate(1);   // Signal for workers to start
Latch doneGate(4);    // Wait for 4 workers to finish

void worker() {
  startGate.wait();   // Wait for start signal
  // ... do work ...
  doneGate.countDown(); // Signal completion
}

// Main thread
for (int i = 0; i < 4; ++i) {
  std::thread(worker).detach();
}
startGate.countDown();  // Release workers
doneGate.wait();        // Wait for all to finish
```

### Phased Computation with Barrier

```cpp
Barrier barrier(4); // 4 threads

void worker(int id, std::vector<int>& data) {
  for (int phase = 0; phase < NUM_PHASES; ++phase) {
    // Phase 1: Update local portion
    data[id] = compute(phase);

    barrier.arriveAndWait(); // Sync: all threads updated

    // Phase 2: Read neighbors' data
    int sum = data[(id + 1) % 4] + data[(id + 3) % 4];

    barrier.arriveAndWait(); // Sync before next phase
  }
}
```

### Protecting Shared State with SpinLock

```cpp
SpinLock lock;
int counter = 0;

void incrementCounter() {
  std::lock_guard<SpinLock> guard(lock);
  ++counter;
}
```

### Lock-Free Pool with Backpressure

```cpp
ThreadPoolLockFree pool(4, 256); // 4 workers, 256 task queue capacity
std::atomic<int> counter{0};

auto task = DelegateU8{
  [](void* ctx) noexcept -> std::uint8_t {
    auto* c = static_cast<std::atomic<int>*>(ctx);
    c->fetch_add(1, std::memory_order_relaxed);
    return 0;
  },
  &counter
};

// Enqueue with backpressure handling
for (int i = 0; i < TASK_COUNT; ++i) {
  PoolStatus status = pool.tryEnqueue("work", task);
  if (status == PoolStatus::QUEUE_FULL) {
    // Queue full - apply backpressure
    // Option 1: Retry after brief wait
    std::this_thread::yield();
    --i;
    // Option 2: Drop task and count
    // Option 3: Block until space available
  }
}

pool.shutdown();
```

### Error Collection

```cpp
ThreadPool pool(4);

// ... enqueue tasks ...

// After tasks complete
auto errors = pool.collectErrors();
while (!errors.empty()) {
  const auto& ERR = errors.front();
  fmt::print("Task '{}' failed with code {}\n", ERR.label, ERR.errorCode);
  errors.pop();
}
```

### Graceful Shutdown

```cpp
ThreadPool pool(4);

// ... enqueue tasks ...

// Wait for completion and shutdown
pool.shutdown(); // Drains queue, joins workers
```

---

## 6. Performance

Figures from the module's benchmark on a Jetson AGX Thor (14 aarch64 cores,
Linux 6.8, release build), cores pinned and clocks locked, as of
[#191](https://github.com/apexedgesystems/apex_csf/pull/191). Each is the
median of four runs of 15 repeats; CV is the variation within a run. They
describe that host and build. No figure here is a budget.

### One thread

| Operation                               | Time    | Rate   | CV   |
| --------------------------------------- | ------- | ------ | ---- |
| `RingBuffer` push and pop               | 3.0 ns  | 330M/s | 0.6% |
| `SPSCQueue` push and pop                | 8.5 ns  | 117M/s | 0.1% |
| `LockFreeQueue` push and pop            | 26.7 ns | 37M/s  | 0.1% |
| `SpinLock` lock and unlock              | 10.9 ns | 92M/s  | 0.1% |
| `Semaphore` tryAcquire and release      | 23 ns   | 43M/s  | 0.4% |
| `Latch::tryWait()` on a triggered latch | 8.6 ns  | 117M/s | 0.1% |
| `Barrier::generation()`                 | 2.3 ns  | 433M/s | 0.1% |

`Latch::tryWait()` on a triggered latch takes the mutex; on a latch that is
still counting it is one atomic load.

### Between threads

| Operation                                                | Time   | CV   |
| -------------------------------------------------------- | ------ | ---- |
| `SPSCQueue`, one item from producer to consumer          | 12 ns  | 3.6% |
| `LockFreeQueue` push and pop, four threads contending    | 277 ns | 3.4% |
| `SpinLock` lock and unlock, four threads contending      | 92 ns  | 4.5% |
| `Semaphore` with four permits, four threads contending   | 105 ns | 3.3% |
| `Latch`, a round of four threads arriving and released   | 8.9 us | 0.8% |
| `Barrier`, a phase of four threads arriving and released | 6.5 us | 3.2% |

### Task dispatch

| Measure                                                   | ThreadPool | ThreadPoolLockFree |
| --------------------------------------------------------- | ---------- | ------------------ |
| Enqueue, every worker busy                                | 31 ns      | 37 ns              |
| Enqueue to the task's first instruction, worker asleep    | 4.6 us     | 4.5 us             |
| Worker time per queued task, one worker                   | 27 ns      | 15 ns              |
| Wall time per queued task, four workers sharing the queue | 212 ns     | 81 ns              |

The tasks do nothing, so the worker rows are the pool's own cost per task.
Four workers draining empty tasks contend for the queue and move fewer
tasks per second than one; with tasks that do work, the queue is a small
share of each. The mutex pool's enqueue is the cheaper of the two while it
does not allocate or wait for the lock; the lock-free pool's enqueue never
does either.

### Footprint

| Object               | Size                                                                     |
| -------------------- | ------------------------------------------------------------------------ |
| `RingBuffer<T>`      | 40 bytes and one `T` per slot                                            |
| `SPSCQueue<T>`       | Three cache lines (192 bytes) and one `T` per slot                       |
| `LockFreeQueue<T>`   | Three cache lines and one 64-byte cell per slot (for `T` up to 56 bytes) |
| `ThreadPool`         | 512 bytes, 64 bytes per worker, and its `std::queue` as it grows         |
| `ThreadPoolLockFree` | 576 bytes, 64 bytes per worker, and 64 bytes per slot of its ring        |
| `SpinLock`           | 1 byte                                                                   |
| `Semaphore`          | 8 bytes                                                                  |
| `Latch`, `Barrier`   | 96 and 112 bytes (a mutex, a condition variable and their counts)        |

Slot counts are the requested capacity rounded up to a power of two;
`RingBuffer` keeps one slot empty. Object sizes are for x86-64 and aarch64
Linux with glibc.

---

## 7. Real-Time Considerations

### RT-Safe Components

- **RingBuffer::tryPush/tryPop/tryPeek** - No atomics, O(1) operations
- **LockFreeQueue::tryPush/tryPop** - Lock-free, bounded operations
- **SPSCQueue::tryPush/tryPop** - Lock-free, single producer/consumer
- **SpinLock::tryLock/unlock** - Atomic operations only
- **Semaphore::tryAcquire/release/count** - Lock-free CAS and atomic ops
- **Latch::count** - Atomic load only; `tryWait()` is one load while the count is nonzero and takes the mutex once it is zero
- **Barrier::expected/generation** - Atomic load only
- **DelegateU8** - No allocation, direct function call
- **cache::AlignCl** - Compile-time alignment only
- **ThreadPool / ThreadPoolLockFree::hasErrors** - Single atomic load
- **ThreadPool / ThreadPoolLockFree::threadsRunning** - Two atomic loads per worker, no lock

### RT-Dependent Components

- **ThreadPoolLockFree::tryEnqueue/enqueueTask** - No allocation and no pool mutex: three atomic read-modify-writes (enter admission, advance the wake generation, leave) around a lock-free queue push. Waking an idle worker issues a futex syscall, and the push has no per-caller retry bound

### NOT RT-Safe Components

- **ThreadPool construction/destruction** - Thread creation/joining
- **ThreadPool::enqueueTask/tryEnqueue** - Mutex lock, std::queue allocation
- **ThreadPoolLockFree construction/destruction** - Thread creation, queue allocation
- **ThreadPoolLockFree::collectErrors** - Mutex lock
- **RingBuffer/LockFreeQueue/SPSCQueue construction/destruction** - Buffer allocation
- **Semaphore::acquire/tryAcquireFor** - May block
- **Latch::countDown/wait/arriveAndWait** - Mutex lock, may block
- **Barrier::arriveAndWait/arriveAndDrop** - Mutex lock, may block

### Recommendations

1. **Pre-allocate at startup** - Create pools and queues before RT loop
2. **Use RingBuffer for single-threaded RT paths** - Zero synchronization overhead
3. **Use SPSCQueue for dedicated producer/consumer** - Minimal atomic overhead
4. **Use ThreadPoolLockFree for RT task dispatch** - No lock or allocation on enqueue, bounded queue; see its RT-safety table for the wake syscall
5. **Use lock-free tryPush/tryPop for RT paths** - Avoid blocking operations
6. **SpinLock for very short critical sections** - Only when contention is low
7. **Size queues appropriately** - Prevent allocation during operation
8. **Profile under load** - Measure latency distributions

---

## 8. Building

```bash
# Build the library
docker compose run --rm -T dev-cuda make debug

# The library is built as part of the utilities module
```

---

## 9. Testing

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run concurrency tests
docker compose run --rm -T dev-cuda ctest --test-dir build/hosted-x86_64-debug -L concurrency
```

### Test Organization

| Module               | Test File                      | Coverage                                            |
| -------------------- | ------------------------------ | --------------------------------------------------- |
| Both pools           | `PoolContract_uTest.cpp`       | Lifecycle, task execution, errors, worker identity  |
| PoolBase             | `PoolBase_uTest.cpp`           | In-flight sweep, one tally per worker, error record |
| ThreadPoolLockFree   | `ThreadPoolLockFree_uTest.cpp` | Capacity, producers, admission, wake and shutdown   |
| ThreadConfig         | `ThreadConfig_uTest.cpp`       | Affinity, name, refused settings, timer slack       |
| LockFreeQueue        | `LockFreeQueue_uTest.cpp`      | Push/pop, capacity, MPMC integrity, index wrap      |
| SPSCQueue            | `SPSCQueue_uTest.cpp`          | Push/pop, capacity, index wrap, size, alignment     |
| RingBuffer           | `RingBuffer_uTest.cpp`         | Push/pop, peek, clear, wraparound, alignment        |
| ringOccupancy        | `RingIndex_uTest.cpp`          | Cursor distance across index wrap, split snapshots  |
| RingStorage          | `RingStorage_uTest.cpp`        | Slot construction and destruction order, alignment  |
| Semaphore            | `Semaphore_uTest.cpp`          | Acquire/release, timeout, wake-all, count bound     |
| SpinLock             | `SpinLock_uTest.cpp`           | Lock/unlock, mutual exclusion, std::lock_guard      |
| Latch                | `Latch_uTest.cpp`              | Countdown, wait, arrive-and-wait, lifetime          |
| Barrier              | `Barrier_uTest.cpp`            | Phases, generations, drop, blocking until the last  |
| DelegateU8, Delegate | `Delegate_uTest.cpp`           | Construction, invocation, null and void forms       |
| AlignCl              | `Cache_uTest.cpp`              | Alignment verification                              |

Threaded cases share three helpers under `utst/support/`: bounded waits
(`Waiting.hpp`), a thread owner that ends a case in bounded time (`Crew.hpp`)
and thread identity and sleep checks (`ThreadProbe.hpp`).

---

## 10. See Also

- **Benchmarking Library** ([Vernier](https://github.com/apexedgesystems/vernier)) - Performance measurement
- **Diagnostics Library** (`utilities/diagnostics`) - System monitoring
- **Helpers Library** (`utilities/helpers`) - Utility functions
