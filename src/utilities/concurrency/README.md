# Concurrency

**Namespace:** `apex::concurrency`
**Platform:** Linux (POSIX), cross-platform compatible
**C++ Standard:** C++17

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
- **Latch** - One-shot countdown synchronization (C++20 backport)
- **Barrier** - Reusable multi-phase synchronization (C++20 backport)

### Thread Pools

Two thread pool implementations for different use cases:

- **ThreadPool** - Mutex-based with unbounded queue
  - Condition-variable wake (no busy spin)
  - Growable queue, handles bursts
  - Best for variable workloads
- **ThreadPoolLockFree** - Lock-free with bounded queue
  - LockFreeQueue + Semaphore design (8x faster enqueue)
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

| Queue             | Threading               | Throughput | Latency | Best For                                     |
| ----------------- | ----------------------- | ---------- | ------- | -------------------------------------------- |
| **RingBuffer**    | Single-thread only      | 45M ops/s  | 22ns    | Processing pipelines, event buffers, parsing |
| **SPSCQueue**     | 1 producer, 1 consumer  | 25M ops/s  | 40ns    | Audio/video streaming, sensor data           |
| **LockFreeQueue** | Multi-producer/consumer | 14M ops/s  | 70ns    | Work stealing, task queues, general MPMC     |

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
- **Semaphore::tryAcquire** only - never wait on CV
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

---

## 4. Module Reference

### ThreadPool

**Header:** `ThreadPool.hpp`
**Purpose:** Real-time friendly thread pool for concurrent task execution.

#### Key Types

```cpp
enum class PoolStatus : std::uint8_t {
  SUCCESS = 0,           ///< Operation completed successfully.
  TASK_FAILED = 1,       ///< Task returned non-zero error code.
  POOL_STOPPED = 2,      ///< Pool is shutting down, task rejected.
  EXCEPTION_STD = 254,   ///< Task threw std::exception.
  EXCEPTION_UNKNOWN = 255 ///< Task threw unknown exception type.
};

struct TaskError {
  const char* label;      ///< Task identifier (non-owning).
  std::uint8_t errorCode; ///< Task return code (0 = success).
  PoolStatus status;      ///< Categorized error status.
};

class ThreadPool {
public:
  using TaskFn = DelegateU8;

  explicit ThreadPool(std::size_t numThreads);
  ~ThreadPool() noexcept;

  bool isValid() const noexcept;
  std::size_t workerCount() const noexcept;
  void enqueueTask(const char* label, TaskFn task);
  PoolStatus tryEnqueue(const char* label, TaskFn task);
  bool threadsRunning() const noexcept;
  bool hasErrors() const noexcept;
  void shutdown() noexcept;
  std::queue<TaskError> collectErrors();
};
```

#### RT-Safety

| Method             | RT-Safe | Reason                         |
| ------------------ | ------- | ------------------------------ |
| `ThreadPool()`     | No      | Spawns threads                 |
| `~ThreadPool()`    | No      | Joins threads                  |
| `isValid()`        | Yes     | Checks stored size             |
| `workerCount()`    | Yes     | Returns stored size            |
| `enqueueTask()`    | No      | Mutex lock, queue may allocate |
| `tryEnqueue()`     | No      | Mutex lock, queue may allocate |
| `threadsRunning()` | Yes     | Atomic load                    |
| `hasErrors()`      | Yes     | Atomic load                    |
| `shutdown()`       | No      | Joins threads                  |
| `collectErrors()`  | No      | Mutex lock                     |

---

### ThreadPoolLockFree

**Header:** `ThreadPoolLockFree.hpp`
**Purpose:** High-performance lock-free thread pool with bounded queue and backpressure.

Uses `LockFreeQueue` for task storage and `Semaphore` for worker wakeup. Provides immediate `QUEUE_FULL` feedback when the bounded queue is at capacity.

#### Key Types

```cpp
class ThreadPoolLockFree {
public:
  using TaskFn = DelegateU8;

  explicit ThreadPoolLockFree(std::size_t numThreads, std::size_t queueCapacity = 1024);
  ~ThreadPoolLockFree() noexcept;

  bool isValid() const noexcept;
  std::size_t workerCount() const noexcept;
  std::size_t capacity() const noexcept;
  PoolStatus tryEnqueue(const char* label, TaskFn task) noexcept;
  void enqueueTask(const char* label, TaskFn task) noexcept;
  bool threadsRunning() const noexcept;
  bool hasErrors() const noexcept;
  void shutdown() noexcept;
  std::queue<TaskError> collectErrors();
};
```

#### RT-Safety

| Method                  | RT-Safe | Reason                          |
| ----------------------- | ------- | ------------------------------- |
| `ThreadPoolLockFree()`  | No      | Spawns threads, allocates queue |
| `~ThreadPoolLockFree()` | No      | Joins threads                   |
| `isValid()`             | Yes     | Checks stored size              |
| `workerCount()`         | Yes     | Returns stored size             |
| `capacity()`            | Yes     | Returns queue capacity          |
| `tryEnqueue()`          | Yes     | Lock-free queue push            |
| `enqueueTask()`         | Yes     | Lock-free queue push            |
| `threadsRunning()`      | Yes     | Atomic load                     |
| `hasErrors()`           | Yes     | Atomic load                     |
| `shutdown()`            | No      | Joins threads                   |
| `collectErrors()`       | No      | Mutex lock                      |

#### ThreadPool vs ThreadPoolLockFree

| Aspect          | ThreadPool                 | ThreadPoolLockFree           |
| --------------- | -------------------------- | ---------------------------- |
| Queue type      | `std::queue` (unbounded)   | `LockFreeQueue` (bounded)    |
| Enqueue latency | ~1.2uss (mutex lock)       | ~0.15uss (lock-free)         |
| Throughput      | 850K ops/s                 | 6.8M ops/s (8x faster)       |
| Backpressure    | No (queue grows)           | Yes (QUEUE_FULL status)      |
| Memory          | Grows with load            | Fixed at construction        |
| Best for        | Variable workloads, bursts | Predictable load, RT systems |

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
| `capacity()`       | Yes     | Returns stored value |

#### Requirements

- `T` must be move-constructible and move-assignable
- Capacity >= 1 (power-of-two not required)

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
| `acquire()`       | No      | May block on CV           |
| `tryAcquire()`    | Yes     | Lock-free CAS loop        |
| `tryAcquireFor()` | No      | Timed wait                |
| `release()`       | Yes     | Atomic fetch_add + notify |
| `count()`         | Yes     | Atomic load               |

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
**Purpose:** One-shot countdown synchronization barrier (C++20 backport).

#### Key Types

```cpp
class Latch {
public:
  explicit Latch(std::ptrdiff_t expected) noexcept;

  void countDown(std::ptrdiff_t n = 1);
  [[nodiscard]] bool tryWait() const noexcept;
  void wait() const;
  template <class Rep, class Period>
  [[nodiscard]] bool waitFor(const std::chrono::duration<Rep, Period>& timeout) const;
  void arriveAndWait(std::ptrdiff_t n = 1);
  [[nodiscard]] std::ptrdiff_t count() const;
};
```

#### RT-Safety

| Method            | RT-Safe | Reason                |
| ----------------- | ------- | --------------------- |
| `Latch()`         | Yes     | No allocation         |
| `countDown()`     | No      | Mutex lock, CV notify |
| `tryWait()`       | Yes     | Single atomic load    |
| `wait()`          | No      | May block on CV       |
| `waitFor()`       | No      | Timed wait            |
| `arriveAndWait()` | No      | Mutex lock, may block |
| `count()`         | Yes     | Single atomic load    |

---

### Barrier

**Header:** `Barrier.hpp`
**Purpose:** Reusable multi-phase synchronization barrier (C++20 backport).

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

### DelegateU8

**Header:** `Delegate.hpp`
**Purpose:** Allocation-free callable for task dispatch.

```cpp
struct DelegateU8 {
  using Fn = std::uint8_t (*)(void*) noexcept;

  Fn fn{nullptr};     ///< Function pointer.
  void* ctx{nullptr}; ///< Opaque context.

  constexpr std::uint8_t operator()() const noexcept;
  constexpr explicit operator bool() const noexcept;
};
```

**RT-Safety:** Fully RT-safe. No allocation, constexpr-compatible.

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

Single-thread throughput on x86-64 (15 repeats):

### Queues

| Primitive         | Throughput  | Latency  | CV%   |
| ----------------- | ----------- | -------- | ----- |
| **RingBuffer**    | 68.5M ops/s | 15 ns/op | 6.9%  |
| **SPSCQueue**     | 32.1M ops/s | 31 ns/op | 4.5%  |
| **LockFreeQueue** | 16.5M ops/s | 60 ns/op | 14.3% |

RingBuffer is 2.1x faster than SPSCQueue (zero atomics vs relaxed atomics).
SPSCQueue is 1.9x faster than LockFreeQueue (relaxed vs CAS pair).

### Synchronization Primitives

| Primitive     | Throughput   | Latency  | CV%   |
| ------------- | ------------ | -------- | ----- |
| **Latch**     | 101.0M ops/s | 10 ns/op | 7.1%  |
| **Barrier**   | 106.4M ops/s | 9 ns/op  | 7.9%  |
| **SpinLock**  | 61.7M ops/s  | 16 ns/op | 12.9% |
| **Semaphore** | 42.9M ops/s  | 23 ns/op | 7.9%  |

Latch.tryWait and Barrier.generation are single atomic loads (fastest path).
SpinLock is an atomic exchange. Semaphore uses atomic CAS.

### Task Dispatch

| Primitive              | Throughput | Latency   |
| ---------------------- | ---------- | --------- |
| **ThreadPool**         | 428K ops/s | 2.3 us/op |
| **ThreadPoolLockFree** | ~6x faster | -         |

ThreadPool is futex-bound (mutex wake/wait). ThreadPoolLockFree replaces
the mutex with lock-free queue + semaphore signaling.

---

## 7. Real-Time Considerations

### RT-Safe Components

- **RingBuffer::tryPush/tryPop/tryPeek** - No atomics, O(1) operations
- **LockFreeQueue::tryPush/tryPop** - Lock-free, bounded operations
- **SPSCQueue::tryPush/tryPop** - Lock-free, single producer/consumer
- **SpinLock::tryLock/unlock** - Atomic operations only
- **Semaphore::tryAcquire/release/count** - Lock-free CAS and atomic ops
- **Latch::tryWait/count** - Atomic load only
- **Barrier::expected/generation** - Atomic load only
- **DelegateU8** - No allocation, direct function call
- **cache::AlignCl** - Compile-time alignment only
- **ThreadPool::threadsRunning/hasErrors** - Single atomic load
- **ThreadPoolLockFree::tryEnqueue/enqueueTask** - Lock-free queue push
- **ThreadPoolLockFree::threadsRunning/hasErrors** - Single atomic load

### NOT RT-Safe Components

- **ThreadPool construction/destruction** - Thread creation/joining
- **ThreadPool::enqueueTask/tryEnqueue** - Mutex lock, std::queue allocation
- **ThreadPoolLockFree construction/destruction** - Thread creation, queue allocation
- **ThreadPoolLockFree::collectErrors** - Mutex lock
- **RingBuffer/LockFreeQueue/SPSCQueue construction/destruction** - Buffer allocation
- **Semaphore::acquire/tryAcquireFor** - May block on CV
- **Latch::countDown/wait/arriveAndWait** - Mutex lock, may block
- **Barrier::arriveAndWait/arriveAndDrop** - Mutex lock, may block

### Recommendations

1. **Pre-allocate at startup** - Create pools and queues before RT loop
2. **Use RingBuffer for single-threaded RT paths** - Zero synchronization overhead
3. **Use SPSCQueue for dedicated producer/consumer** - Minimal atomic overhead
4. **Use ThreadPoolLockFree for RT task dispatch** - Lock-free enqueue, bounded queue
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
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L concurrency
```

### Test Organization

| Module             | Test File                      | Coverage                                        |
| ------------------ | ------------------------------ | ----------------------------------------------- |
| ThreadPool         | `ThreadPool_uTest.cpp`         | Lifecycle, task execution, error handling       |
| ThreadPoolLockFree | `ThreadPoolLockFree_uTest.cpp` | Lifecycle, lock-free enqueue, bounded queue     |
| LockFreeQueue      | `LockFreeQueue_uTest.cpp`      | Push/pop, capacity, MPMC correctness            |
| SPSCQueue          | `SPSCQueue_uTest.cpp`          | Push/pop, SPSC correctness, stress tests        |
| RingBuffer         | `RingBuffer_uTest.cpp`         | Push/pop, peek, clear, wraparound, stress tests |
| Semaphore          | `Semaphore_uTest.cpp`          | Acquire/release, timeout, concurrency           |
| SpinLock           | `SpinLock_uTest.cpp`           | Lock/unlock, contention, std::lock_guard        |
| Latch              | `Latch_uTest.cpp`              | Countdown, wait, multi-thread coordination      |
| Barrier            | `Barrier_uTest.cpp`            | Phases, generations, multi-thread sync          |
| DelegateU8         | `Delegate_uTest.cpp`           | Construction, invocation                        |
| AlignCl            | `Cache_uTest.cpp`              | Alignment verification                          |

---

## 10. See Also

- **Benchmarking Library** ([Vernier](https://github.com/apexedgesystems/vernier)) - Performance measurement
- **Diagnostics Library** (`utilities/diagnostics`) - System monitoring
- **Helpers Library** (`utilities/helpers`) - Utility functions
