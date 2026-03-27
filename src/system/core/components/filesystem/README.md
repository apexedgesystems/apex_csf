# Filesystem Library

**Namespace:** `system_core::filesystem`
**Platform:** Linux-only
**C++ Standard:** C++23

Deterministic, no-throw filesystem management with atomic writes, archival, and maintenance utilities for real-time aware deployments.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [Design Principles](#4-design-principles)
5. [API Reference](#5-api-reference)
6. [Usage Examples](#6-usage-examples)
7. [Requirements](#7-requirements)
8. [Testing](#8-testing)
9. [See Also](#9-see-also)

---

## 1. Quick Reference

| Class            | Purpose                                                | RT-Safe |
| ---------------- | ------------------------------------------------------ | ------- |
| `FileSystemBase` | Abstract base with maintenance, atomic write, archival | Partial |
| `ApexFileSystem` | Standard deployment layout (6 subdirectories)          | Partial |
| `NullFileSystem` | No-op filesystem for lite/baremetal targets            | Yes     |
| `Status`         | Typed status codes for all operations                  | Yes     |

### Quick Example

```cpp
#include "ApexFileSystem.hpp"

using system_core::filesystem::ApexFileSystem;
using system_core::filesystem::Status;

// Create filesystem with default layout
ApexFileSystem fs(nullptr, "/tmp/apexfs");
fs.init();  // Creates: tlm/, logs/, db/, bank_a/{libs,tprm,bin,rts,ats}, bank_b/...

// Atomic file write (temp + rename pattern)
std::vector<std::uint8_t> data = {1, 2, 3, 4};
fs.writeFileAtomic(fs.logDir() / "telemetry.bin", data, /*doFsync=*/true);
```

---

## 2. When to Use

| Scenario                                       | Use This Library?                      |
| ---------------------------------------------- | -------------------------------------- |
| Structured filesystem layout for RT deployment | Yes -- `ApexFileSystem`                |
| Crash-safe file writes (atomic temp+rename)    | Yes -- `writeFileAtomic()`             |
| Log rotation by age or size                    | Yes -- `pruneByAge()`, `pruneBySize()` |
| Archival with tar on shutdown                  | Yes -- `cleanup()`                     |
| Custom directory layouts                       | Yes -- derive from `FileSystemBase`    |
| Baremetal targets with no filesystem           | Yes -- `NullFileSystem` (no-op)        |
| General-purpose file I/O                       | No -- use standard library directly    |

**Design intent:** Filesystem component for the executive runtime. All operations return status codes (no exceptions). Atomic writes use temp+rename for crash safety. Maintenance utilities (prune, archive) run in non-RT context.

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point, 10000 cycles.

### Filesystem Operations

| Operation                    | Median (us) | Calls/s | CV%  |
| ---------------------------- | ----------- | ------- | ---- |
| `isUnderRoot` (valid path)   | 2.09        | 478K    | 2.9% |
| `isUnderRoot` (invalid path) | 6.64        | 151K    | 3.5% |
| Directory accessors          | 0.41        | 2.5M    | 1.5% |
| `exists()` check             | 0.89        | 1.1M    | 7.1% |
| Atomic write (64B)           | 35.9        | 27.8K   | 1.9% |
| Atomic write (4KB)           | 36.0        | 27.8K   | 3.2% |
| `toString(Status)`           | 0.017       | 60.2M   | 5.5% |

### Profiler Analysis (gperftools)

**AtomicWrite (36557 samples):**

| Function                   | Self-Time | Type                             |
| -------------------------- | --------- | -------------------------------- |
| `rename` (glibc)           | 41.8%     | Syscall-bound (atomic rename)    |
| `__open64` (glibc)         | 21.0%     | Syscall-bound (temp file create) |
| `__write` (glibc)          | 8.0%      | Syscall-bound (data write)       |
| `fstatat` (glibc)          | 2.1%      | Syscall-bound (path validation)  |
| `__close_nocancel` (glibc) | 2.0%      | Syscall-bound (file close)       |

**IsUnderRoot (2103 samples):**

| Function                      | Self-Time | Type                        |
| ----------------------------- | --------- | --------------------------- |
| `path::iterator::operator*`   | 4.4%      | CPU-bound (std::filesystem) |
| `basic_string::_M_data`       | 4.2%      | CPU-bound (string access)   |
| `FileSystemBase::isUnderRoot` | 2.8%      | CPU-bound (path comparison) |

### Memory Footprint

| Component        | Stack                       | Heap                              |
| ---------------- | --------------------------- | --------------------------------- |
| `ApexFileSystem` | ~1KB (base + paths)         | std::filesystem::path allocations |
| `NullFileSystem` | ~16B                        | 0                                 |
| `FileSystemBase` | ~800B (base + cached paths) | std::filesystem::path allocations |

---

## 4. Design Principles

### RT-Safety

| Operation           | RT-Safe | Notes                                           |
| ------------------- | ------- | ----------------------------------------------- |
| `exists()`          | Yes     | Simple filesystem check                         |
| `isUnderRoot()`     | Yes     | Uses cached canonical path                      |
| `writeFileAtomic()` | Yes     | Bounded syscall pattern                         |
| `statusToString()`  | Yes     | Static string lookup                            |
| Directory accessors | Yes     | Return cached `std::filesystem::path` reference |
| `init()`            | No      | Creates directories                             |
| `cleanup()`         | No      | Shells out to tar, heavy I/O                    |
| `pruneByAge()`      | No      | Directory iteration, file deletion              |
| `pruneBySize()`     | No      | File enumeration, sorting                       |

### No-Throw API

All operations return typed `Status` codes. No exceptions thrown.

```cpp
enum class Status : std::uint8_t {
  SUCCESS = 0,
  ERROR_FS_CREATION_FAIL,
  ERROR_FS_TAR_CREATE_FAIL,
  ERROR_FS_TAR_MOVE_FAIL,
  ERROR_INVALID_FS
};
```

### Atomic Write Pattern

File writes use temp-and-rename for crash safety:

1. Write to `target.tmp`
2. Flush stream
3. Optional `fsync` (configurable via FsyncPolicy)
4. Atomic rename to `target`

### Durability Control

```cpp
enum class FsyncPolicy : std::uint8_t {
  NEVER,     // Skip fsync (fastest, least durable)
  ON_DEMAND, // Fsync when explicitly requested
  ALWAYS     // Fsync every write (slowest, most durable)
};
```

### Dedicated Component Log

`ApexFileSystem` creates a dedicated SYNC-mode log at `logs/filesystem.log` after initialization. SYNC mode with no I/O thread (~4KB vs ~2.1MB for async), appropriate for infrequent filesystem operations.

---

## 5. API Reference

### Classes

| Class            | Purpose                                                |
| ---------------- | ------------------------------------------------------ |
| `FileSystemBase` | Abstract base with maintenance, atomic write, archival |
| `ApexFileSystem` | Standard deployment layout (6 subdirectories)          |
| `NullFileSystem` | No-op filesystem for lite/baremetal targets            |
| `Status`         | Typed status codes for all operations                  |

### ApexFileSystem Directories

| Accessor    | Directory      | Banked | Purpose                 |
| ----------- | -------------- | ------ | ----------------------- |
| `libDir()`  | `bank_X/libs/` | Yes    | Libraries and binaries  |
| `tprmDir()` | `bank_X/tprm/` | Yes    | Tunable parameter files |
| `binDir()`  | `bank_X/bin/`  | Yes    | Executable binaries     |
| `rtsDir()`  | `bank_X/rts/`  | Yes    | RTS sequence files      |
| `atsDir()`  | `bank_X/ats/`  | Yes    | ATS sequence files      |
| `tlmDir()`  | `tlm/`         | No     | Telemetry data          |
| `logDir()`  | `logs/`        | No     | Log files               |
| `dbDir()`   | `db/`          | No     | Database exports        |

### Maintenance Utilities

| Operation         | Method                  | Purpose                                      |
| ----------------- | ----------------------- | -------------------------------------------- |
| Space check       | `checkSpace()`          | Query available space, enforce minimums      |
| Age pruning       | `pruneByAge()`          | Delete files older than threshold            |
| Size pruning      | `pruneBySize()`         | Keep directory under size cap (oldest-first) |
| Selective clear   | `clearContentsExcept()` | Remove all except keep-list                  |
| Archive + cleanup | `cleanup()`             | Create tar archive and clear contents        |

### Customization Hooks

Derived classes can override:

| Hook                | Purpose                               |
| ------------------- | ------------------------------------- |
| `init()`            | Initialize custom directory structure |
| `archivePath()`     | Custom archive location/naming        |
| `preCleanupHook()`  | Execute before archival               |
| `postCleanupHook()` | Execute after archival                |

---

## 6. Usage Examples

### Custom Filesystem Layout

```cpp
#include "FileSystemBase.hpp"

using system_core::filesystem::FileSystemBase;
using system_core::filesystem::Status;

class MissionFileSystem : public FileSystemBase {
public:
  explicit MissionFileSystem(std::shared_ptr<logs::SystemLog> log,
                             std::filesystem::path root) noexcept
      : FileSystemBase(std::move(log), std::move(root), "mission") {}

  std::uint8_t init() noexcept override {
    const std::vector<std::filesystem::path> DIRS{
        root() / "data",
        root() / "config",
        root() / "archives"
    };
    const Status ST = createDirectories(DIRS);
    setInitialized(ST == Status::SUCCESS);
    return static_cast<std::uint8_t>(ST);
  }
};
```

### Periodic Log Rotation

```cpp
constexpr std::uintmax_t MAX_LOG_SIZE = 100 * 1024 * 1024;  // 100 MB
fs.pruneBySize(fs.logDir(), MAX_LOG_SIZE);
```

### Atomic Data Write with Durability

```cpp
fs.setFsyncPolicy(FileSystemBase::FsyncPolicy::ON_DEMAND);

std::vector<std::uint8_t> criticalData = serialize(state);
const Status ST = fs.writeFileAtomic(
    fs.rtsDir() / "state.bin",
    criticalData,
    /*doFsync=*/true  // Ensures data is on disk
);

if (ST != Status::SUCCESS) {
  // Handle error - file was not modified
}
```

### RAII Cleanup on Shutdown

```cpp
ApexFileSystem fs(sysLog, "/tmp/apexfs");
fs.init();
fs.configureShutdownCleanup(true, "/archive/backups");
// On destruction: filesystem archived automatically
```

---

## 7. Requirements

### Build Dependencies

- C++17 compiler (GCC 10+, Clang 12+)
- fmt library (formatting)

### Runtime

- Linux (POSIX filesystem semantics)
- `tar` command for archival (graceful fallback if unavailable)
- Writable filesystem for operations

---

## 8. Testing

| Directory    | Type                   | Tests | Runs with `make test` |
| ------------ | ---------------------- | ----- | --------------------- |
| `apex/utst/` | Unit tests             | 16    | Yes                   |
| `lite/utst/` | Unit tests             | 10    | Yes                   |
| `apex/ptst/` | Performance benchmarks | 7     | No (manual)           |

### Test Organization

| Component      | Test File                  | Tests  |
| -------------- | -------------------------- | ------ |
| FileSystemBase | `FileSystemBase_uTest.cpp` | 11     |
| ApexFileSystem | `ApexFileSystem_uTest.cpp` | 5      |
| NullFileSystem | `NullFileSystem_uTest.cpp` | 10     |
| **Total**      |                            | **26** |

---

## 9. See Also

- `src/system/core/infrastructure/system_component/` - SystemComponentBase lifecycle
- `src/system/core/infrastructure/logs/` - SystemLog for logging integration
- `src/system/core/executive/` - ApexExecutive uses filesystem
