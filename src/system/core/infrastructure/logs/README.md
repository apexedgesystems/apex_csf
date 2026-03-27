# Logging Library

Real-time logging facility with file-backed persistence, rotation, and two modes: synchronous (blocking) and asynchronous (RT-safe lock-free queue).

**Library:** `system_core_logs`
**Namespace:** `logs`
**Headers:** `inc/SystemLog.hpp`, `inc/LogBase.hpp`, `inc/AsyncLogBackend.hpp`

---

## 1. Quick Reference

| Component         | Type   | Purpose                                                                          | RT-Safe                          |
| ----------------- | ------ | -------------------------------------------------------------------------------- | -------------------------------- |
| `SystemLog`       | Class  | High-level severity-filtered logger with SYNC/ASYNC modes                        | ASYNC: Yes (log calls), SYNC: No |
| `LogBase`         | Class  | Low-level file management, atomic append, rotation                               | No (file I/O)                    |
| `AsyncLogBackend` | Class  | Lock-free MPMC queue with dedicated I/O thread                                   | Enqueue: Yes, Lifecycle: No      |
| `Level`           | Enum   | DEBUG, INFO, WARNING, ERROR, FATAL                                               | Yes                              |
| `Status`          | Enum   | OK, ERROR_OPEN, ERROR_SIZE, ERROR_ROTATE_RENAME, ERROR_ROTATE_REOPEN, ERROR_SYNC | Yes                              |
| `setLevel`        | Method | Set minimum severity threshold                                                   | Yes (atomic store)               |
| `setVerbosity`    | Method | Set debug verbosity level (0-255)                                                | Yes (atomic store)               |
| `flush`           | Method | Drain async queue to disk                                                        | No (blocks)                      |
| `rotate`          | Method | Rotate log file with timestamped backup                                          | No (mutex + file I/O)            |

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

**Design intent:** Two-mode logging where ASYNC mode never blocks the calling thread. Below-threshold messages skip formatting entirely (~38ns). The async backend uses a lock-free MPMC queue with a dedicated I/O thread, achieving ~0.8us hot-path latency.

---

## 3. Performance

### Throughput and Latency

| Mode  | Scenario                 | Median (us) | Calls/s | CV%   |
| ----- | ------------------------ | ----------- | ------- | ----- |
| SYNC  | Single-thread            | 1.168       | 856.5K  | 5.9%  |
| SYNC  | Multi-thread contention  | 1.390       | 719.6K  | 8.5%  |
| ASYNC | Single-thread            | 0.754       | 1.33M   | 1.2%  |
| ASYNC | Multi-thread contention  | 0.863       | 1.16M   | 18.7% |
| Skip  | Below threshold          | 0.034       | 29.2M   | 2.5%  |
| ASYNC | Payload size sensitivity | 0.760       | 1.32M   | 1.2%  |

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

| Component           | Stack | Heap                               |
| ------------------- | ----- | ---------------------------------- |
| `SystemLog` (SYNC)  | ~64B  | File descriptor only               |
| `SystemLog` (ASYNC) | ~64B  | ~2.1MB (4096 entries x 520B queue) |
| `AsyncLogBackend`   | ~48B  | ~2.1MB (lock-free MPMC queue)      |
| `LogBase`           | ~32B  | File descriptor only               |

---

## 4. Design Principles

- **Two modes** -- SYNC for boot/debug, ASYNC for RT operation
- **Lock-free hot path** -- ASYNC enqueue uses MPMC queue, never blocks caller
- **Skip-path optimization** -- Below-threshold messages cost ~38ns (no formatting)
- **Atomic appends** -- O_APPEND semantics for concurrent write safety
- **No exceptions** -- Typed status codes throughout
- **fmt formatting** -- Profiler shows ~60% of ASYNC time is in fmt; write syscall dominates SYNC
- **Timestamped rotation** -- Backup files named with YYYYMMDD-HHMMSS suffix
- **Fine-grained verbosity** -- Integer levels 0-255 for debug messages

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

// RT-safe: lock-free enqueue (~0.8us)
log.info("SCHEDULER", "task completed");
log.warning("IO", 7, "buffer full");

// Before shutdown: flush remaining entries
log.flush();
```

### Severity Filtering

```cpp
SystemLog log("system.log", SystemLog::Mode::ASYNC);
log.setLevel(SystemLog::Level::WARNING);

// These skip formatting entirely (~38ns)
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
