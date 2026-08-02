# Logging Library

Real-time logging facility with file-backed persistence, rotation, and two modes: synchronous (blocking) and asynchronous (RT-safe lock-free queue drained by one shared service thread).

**Library:** `system_core_logs`
**Namespace:** `logs`
**Headers:** `inc/SystemLog.hpp`, `inc/LogBase.hpp`, `inc/AsyncLogBackend.hpp`, `inc/LogDrainService.hpp`

---

## 1. Quick Reference

| Component         | Type   | Purpose                                                                          | RT-Safe                                 |
| ----------------- | ------ | -------------------------------------------------------------------------------- | --------------------------------------- |
| `SystemLog`       | Class  | High-level severity-filtered logger with SYNC/ASYNC modes                        | ASYNC: Yes (log calls), SYNC: No        |
| `LogBase`         | Class  | Low-level file management, atomic append, rotation                               | No (file I/O)                           |
| `AsyncLogBackend` | Class  | Lock-free MPMC queue drained by the shared service (dedicated thread fallback)   | Enqueue: Yes, Lifecycle: No             |
| `LogDrainService` | Class  | Process-wide drain thread shared by all async backends (Linux)                   | notify(): Yes, Lifecycle: No            |
| `Level`           | Enum   | DEBUG, INFO, WARNING, ERROR, FATAL                                               | Yes                                     |
| `Status`          | Enum   | OK, ERROR_OPEN, ERROR_SIZE, ERROR_ROTATE_RENAME, ERROR_ROTATE_REOPEN, ERROR_SYNC | Yes                                     |
| `setLevel`        | Method | Set minimum severity threshold                                                   | Yes (atomic store)                      |
| `setVerbosity`    | Method | Set debug verbosity level (0-255)                                                | Yes (atomic store)                      |
| `flush`           | Method | Drain async queue to disk                                                        | No (blocks)                             |
| `rotate`          | Method | Rotate log file with timestamped backup (descriptor stays stable via dup2)       | No (mutex + file I/O)                   |
| `setFatalFlush`   | Method | Opt-in: fatal() drains the queue before returning (durability over RT safety)    | Yes (atomic store); fatal() then blocks |
| `writeErrorCount` | Method | Monotonic count of write() failures (disk full, EIO)                             | Yes (atomic load)                       |
| `droppedCount`    | Method | Monotonic count of entries not accepted (queue full or backend stopped)          | Yes (atomic load)                       |

---

## 2. When to Use

| Scenario                                     | Use This Library?                              |
| -------------------------------------------- | ---------------------------------------------- |
| RT-safe logging from scheduler/model tasks   | Yes -- ASYNC mode                              |
| Boot-time or debugging logging               | Yes -- SYNC mode                               |
| File-backed persistent logging with rotation | Yes -- `LogBase`                               |
| Severity-filtered log output                 | Yes -- `setLevel` + `setVerbosity`             |
| Protocol I/O byte tracing                    | No -- use `ByteTrace` mixin (protocols/common) |
| Structured telemetry export                  | No -- use APROTO telemetry                     |

**Design intent:** Two-mode logging where ASYNC mode never blocks the calling thread and never shares a lock with it. Below-threshold messages skip formatting entirely (~36ns). Producers push to a lock-free MPMC ring and wake the drain side with one non-blocking eventfd write; a single process-wide service thread (LogDrainService) empties every backend's ring, so a fleet of component logs costs one I/O thread, not one per component. Every loss path is counted: queue-full and stopped-backend drops in droppedCount(), write() failures in writeErrorCount().

---

## 3. Performance

### Throughput and Latency

| Mode  | Scenario                             | Median (us) | Calls/s | CV%   |
| ----- | ------------------------------------ | ----------- | ------- | ----- |
| ASYNC | Single-thread enqueue                | 0.367       | 2.72M   | 3.2%  |
| ASYNC | Multi-thread contention              | 0.601       | 1.66M   | 24.1% |
| ASYNC | Sporadic log-to-drained cycle        | 0.783       | 1.28M   | 4.2%  |
| ASYNC | Fleet of 16 backends, sporadic cycle | 0.876       | 1.14M   | 2.1%  |
| ASYNC | Queue-full drop path                 | 0.502       | 1.99M   | 2.8%  |
| SYNC  | Single-thread                        | 0.801       | 1.25M   | 3.4%  |
| SYNC  | Multi-thread contention              | 0.917       | 1.09M   | 14.9% |
| Skip  | Below threshold                      | 0.036       | 27.9M   | 10.8% |
| ASYNC | Payload size sensitivity             | 0.365       | 2.74M   | 8.3%  |

A 256-entry burst followed by flush() (drain + fsync) costs ~1.4 ms:
flush() is a control-plane checkpoint, never an RT-path call. The fleet
cycle sits within ~12% of the single-backend cycle -- the shared drain
thread erases the wake-a-cold-thread penalty of per-component I/O
threads (formerly 4-5x).

### Profiler Analysis

**SYNC mode:**

| Function                         | Self-Time | Type                           |
| -------------------------------- | --------- | ------------------------------ |
| `__write` (glibc)                | 31.1%     | Syscall-bound (file I/O)       |
| `fmt::detail::buffer::push_back` | 13.6%     | CPU-bound (message formatting) |
| `fmt::detail::copy`              | 8.1%      | CPU-bound (message formatting) |
| `LogBase::appendBytes`           | 1.3%      | CPU-bound (write dispatch)     |

**ASYNC mode:**

| Function                           | Self-Time | Type                           |
| ---------------------------------- | --------- | ------------------------------ |
| `fmt::detail::buffer::push_back`   | 19.3%     | CPU-bound (message formatting) |
| `fmt::detail::copy`                | 11.7%     | CPU-bound (message formatting) |
| `fmt::detail::buffer::try_reserve` | 10.4%     | CPU-bound (buffer management)  |
| `fmt::detail::concat::format`      | 2.1%      | CPU-bound (format dispatch)    |

### Memory Footprint

| Component           | Stack | Heap                                                                         |
| ------------------- | ----- | ---------------------------------------------------------------------------- |
| `SystemLog` (SYNC)  | ~64B  | File descriptor only                                                         |
| `SystemLog` (ASYNC) | ~64B  | ~2.1MB (4096 entries x 520B queue; size it per component via asyncQueueSize) |
| `AsyncLogBackend`   | ~48B  | ring only -- no thread in shared-drain mode                                  |
| `LogDrainService`   | ~1KB  | one thread + one eventfd for the whole process                               |
| `LogBase`           | ~32B  | File descriptor only                                                         |

---

## 4. Design Principles

- **Two modes** -- SYNC for boot/debug, ASYNC for RT operation
- **Lock-free hot path, lock-free wakeup** -- ASYNC enqueue uses an MPMC ring; the drain wake is one non-blocking eventfd write; no lock is shared with the RT caller in either direction
- **One drain thread per process** -- LogDrainService sweeps every registered backend's ring; component logs stop costing a thread each; the dedicated-thread path remains as fallback and for non-Linux builds
- **Skip-path optimization** -- Below-threshold messages cost ~36ns (no formatting)
- **Atomic appends, stable descriptors** -- O_APPEND write safety; rotation repoints the same descriptor via dup2 so lock-free writers never touch a closed or reused fd
- **Counted loss, never silent loss** -- queue-full and stopped-backend drops, and write() failures, are all monotonic counters
- **No exceptions** -- Typed status codes throughout
- **fmt formatting** -- Profiler shows formatting dominates the ASYNC hot path; write syscall dominates SYNC
- **Timestamped rotation** -- Backup files named with YYYYMMDD-HHMMSS suffix
- **Fine-grained verbosity** -- Integer levels 0-255 for debug messages

### Constraints

- The drain service thread is default-priority and outside the scheduler:
  under full CPU saturation it degrades to counted drops (bounded
  staleness), never to RT blocking. Executive-configured thread policy is
  the planned integration hook.
- echoConsole=true does stdio on the calling thread: NOT RT-safe in any
  mode; reserve for boot/diagnostics.
- setFatalFlush(true) makes fatal() block until the queue drains -- the
  deliberate durability trade for supervisors that log FATAL and abort.
- Async backends must not outlive main() (static-destruction ordering
  with the process-wide drain service).
- rotate() in ASYNC mode rotates the sync-path descriptor only; async
  entries continue to the original file. Rotation ownership for async
  logs is tracked work.

---

## 5. API Reference

### SystemLog

```cpp
class SystemLog : public LogBase {
public:
  enum class Mode : std::uint8_t { SYNC = 0, ASYNC };
  enum class Level : std::uint8_t { DEBUG = 0, INFO, WARNING, ERROR, FATAL };

  /// @note NOT RT-safe: Opens file. Defaults to SYNC mode.
  explicit SystemLog(const std::string& logPath) noexcept;

  /// @note NOT RT-safe: Opens file, may start I/O thread (ASYNC mode).
  explicit SystemLog(const std::string& logPath, Mode mode,
                     std::size_t asyncQueueSize = 4096) noexcept;

  /// @note RT-safe (ASYNC), NOT RT-safe (SYNC): Formats and logs message.
  Status info(std::string_view src, std::string_view msg,
              bool echoConsole = false) noexcept;
  Status warning(std::string_view src, std::uint8_t ec, std::string_view msg,
                 bool echoConsole = false) noexcept;
  Status error(std::string_view src, std::uint8_t ec, std::string_view msg,
               bool echoConsole = false) noexcept;
  Status fatal(std::string_view src, std::uint8_t ec, std::string_view msg,
               bool echoConsole = false) noexcept;
  Status debug(std::string_view src, std::string_view msg,
               std::uint8_t level = 0) noexcept;

  /// @note RT-safe: Atomic store.
  void setLevel(Level lvl) noexcept;
  Level level() const noexcept;

  /// @note RT-safe: Atomic store.
  void setVerbosity(std::uint8_t level) noexcept;
  std::uint8_t verbosity() const noexcept;

  /// @note NOT RT-safe: Drains queue or calls fsync.
  Status flush() noexcept;

  /// @note RT-safe: Atomic load.
  Mode mode() const noexcept;
  bool isAsync() const noexcept;
};
```

### LogBase

```cpp
class LogBase {
public:
  /// @note NOT RT-safe: Opens file.
  explicit LogBase(const std::string& logPath) noexcept;

  /// @note RT-safe: Lock-free single syscall via O_APPEND.
  Status write(const std::string& msg) noexcept;

  /// @note NOT RT-safe: Blocks on fsync.
  Status flush() noexcept;

  /// @note NOT RT-safe: Acquires mutex.
  Status size(std::size_t& outBytes) noexcept;
  std::string fpath() noexcept;

  /// @note NOT RT-safe: Acquires mutex, file I/O.
  Status rotate(std::size_t maxSize) noexcept;

  /// @note RT-safe: Returns status of last open attempt.
  Status lastOpenStatus() const noexcept;
};
```

### AsyncLogBackend

```cpp
class AsyncLogBackend {
public:
  /// @note NOT RT-safe: Allocates queue, starts I/O thread.
  explicit AsyncLogBackend(LogBase& base, std::size_t capacity) noexcept;

  /// @note RT-safe: Lock-free MPMC enqueue.
  bool enqueue(std::string_view data) noexcept;

  /// @note NOT RT-safe: Blocks until queue drains and fsync completes.
  void flush() noexcept;
};
```

### Status Codes

```cpp
enum class Status : std::uint8_t {
  OK = 0,
  ERROR_OPEN,
  ERROR_SIZE,
  ERROR_ROTATE_RENAME,
  ERROR_ROTATE_REOPEN,
  ERROR_SYNC
};
```

---

## 6. Usage Examples

### RT-Safe Async Logging

```cpp
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

using logs::SystemLog;

SystemLog log("system.log", SystemLog::Mode::ASYNC);
log.setLevel(SystemLog::Level::INFO);

// RT-safe: ~0.37us enqueue; drained by the shared service thread
log.info("SCHEDULER", "task completed");  // lock-free enqueue + eventfd wake
log.warning("IO", 7, "buffer full");

// Before shutdown: flush remaining entries
log.flush();
```

### Severity Filtering

```cpp
SystemLog log("system.log", SystemLog::Mode::ASYNC);
log.setLevel(SystemLog::Level::WARNING);

// These skip formatting entirely (~36ns)
log.debug("SCHEDULER", "verbose trace", 5);
log.info("SCHEDULER", "status update");

// These are formatted and queued
log.warning("SCHEDULER", 1, "deadline approaching");
```

### Debug Verbosity

```cpp
log.setLevel(SystemLog::Level::DEBUG);
log.setVerbosity(3);

log.debug("TRACE", "critical path", 0);   // Logged (0 <= 3)
log.debug("TRACE", "very verbose", 5);    // Skipped (5 > 3)
```

### Log Rotation

```cpp
using logs::LogBase;

LogBase log("system.log");
constexpr std::size_t MAX_SIZE = 10 * 1024 * 1024;
log.rotate(MAX_SIZE);  // Creates backup: system.log.20241227-143052
```

---

## 7. Testing

### Test Organization

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 26    | Yes                   |
| `ptst/`   | Performance tests | 6     | No (manual)           |

### Test Requirements

- All tests are platform-agnostic (uses tmpfile for log output)
- Tests verify severity filtering, formatting, rotation, status codes
- Tests verify async queue lifecycle, enqueue/dequeue, flush semantics
- Tests verify below-threshold skip path

---

## 8. See Also

- `src/utilities/concurrency/` -- LockFreeQueue used by AsyncLogBackend
- `src/system/core/infrastructure/protocols/common/` -- ByteTrace for protocol I/O tracing
