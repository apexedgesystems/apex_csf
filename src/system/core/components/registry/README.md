# Registry Library

**Namespace:** `system_core::registry`
**Platform:** Linux (POSIX)
**C++ Standard:** C++23

Unified registry for components, tasks, and data in an Apex application. Provides centralized tracking with RT-safe queries after initialization.

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

| Component        | Purpose                                  | RT-Safe |
| ---------------- | ---------------------------------------- | ------- |
| `ApexRegistry`   | Central database (SystemComponentBase)   | Partial |
| `ComponentEntry` | Component UID, name, task/data links     | Yes     |
| `TaskEntry`      | Task UID, name, pointer                  | Yes     |
| `DataEntry`      | Data category, name, pointer, size       | Yes     |
| `RegistryExport` | RDAT binary export for external analysis | No      |
| `Status`         | Typed status codes                       | Yes     |

### Quick Example

```cpp
#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"

using system_core::registry::ApexRegistry;
using system_core::registry::Status;
using system_core::data::DataCategory;

ApexRegistry registry;
registry.setConfigured(true);
registry.init();

// Registration phase (NOT RT-safe)
registry.registerComponent(0x6600, "PolynomialModel");

PolynomialState state;
registry.registerData(0x6600, DataCategory::STATE, "state",
                      &state, sizeof(state));

// Freeze before run phase
registry.freeze();

// Query phase (RT-safe)
auto* entry = registry.getData(0x6600, DataCategory::STATE);
auto bytes = entry->getBytes();
```

---

## 2. When to Use

| Scenario                                               | Use This Library?                          |
| ------------------------------------------------------ | ------------------------------------------ |
| Centralized component/task/data tracking for executive | Yes -- `ApexRegistry`                      |
| RT-safe metadata queries after initialization          | Yes -- all query methods after `freeze()`  |
| Byte-level data access for telemetry logging           | Yes -- `DataEntry::getBytes()`             |
| Binary export for offline analysis                     | Yes -- `exportDatabase()` with RDAT format |
| Runtime component discovery and routing                | Yes -- `getComponent()`, `getData()`       |
| Internal component-to-component messaging              | No -- use `components/interface/`          |
| Task scheduling                                        | No -- use `components/scheduler/`          |

**Design intent:** Single source of truth for runtime metadata. Two-phase lifecycle: registration (NOT RT-safe, vectors may grow) then freeze + query (RT-safe, fixed storage). All query methods are O(n) linear scans over small fixed-size arrays.

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point, 10000 cycles.

### Lookup Operations

| Operation                     | Median (us) | Calls/s | CV%   |
| ----------------------------- | ----------- | ------- | ----- |
| Component lookup (10 entries) | 0.037       | 27.0M   | 12.4% |
| Component lookup (50 entries) | 0.088       | 11.4M   | 6.2%  |
| Component lookup (miss)       | 0.153       | 6.5M    | 4.1%  |
| Task lookup                   | 0.341       | 2.9M    | 1.8%  |
| Data lookup                   | 0.132       | 7.6M    | 2.6%  |

### Iteration Operations

| Operation              | Median (us) | Calls/s | CV%   |
| ---------------------- | ----------- | ------- | ----- |
| Iterate all components | 0.196       | 5.1M    | 3.5%  |
| Iterate all tasks      | 0.674       | 1.5M    | 29.2% |

### Registration and Utilities

| Operation           | Median (us) | Calls/s | CV%  |
| ------------------- | ----------- | ------- | ---- |
| Register components | 0.636       | 1.6M    | 1.3% |
| `toString(Status)`  | 0.009       | 109.9M  | 7.8% |

### Profiler Analysis (gperftools)

**ComponentLookup_50 (88 samples):**

| Function                           | Self-Time | Type                                    |
| ---------------------------------- | --------- | --------------------------------------- |
| `std::vector::size`                | 38.6%     | CPU-bound (bounds check in linear scan) |
| `ApexRegistry::findComponentIndex` | 26.1%     | CPU-bound (linear search loop)          |
| `std::vector::operator[]`          | 10.2%     | CPU-bound (element access)              |

**TaskLookup (328 samples):**

| Function                                   | Self-Time | Type                           |
| ------------------------------------------ | --------- | ------------------------------ |
| `__gnu_cxx::__normal_iterator::operator++` | 17.7%     | CPU-bound (iterator advance)   |
| `iterator::operator==`                     | 15.9%     | CPU-bound (end comparison)     |
| `iterator::operator*`                      | 14.6%     | CPU-bound (dereference)        |
| `ApexRegistry::getTask`                    | 14.3%     | CPU-bound (linear iteration)   |
| `iterator::base`                           | 14.0%     | CPU-bound (iterator internals) |

**RegisterComponents (660 samples):**

| Function                           | Self-Time | Type                                 |
| ---------------------------------- | --------- | ------------------------------------ |
| `ApexRegistry::registerComponent`  | 25.2%     | CPU-bound (collision check + insert) |
| `std::vector::size`                | 12.6%     | CPU-bound (bounds check)             |
| `ApexRegistry::findComponentIndex` | 11.7%     | CPU-bound (duplicate detection)      |

### Memory Footprint

| Component        | Stack                       | Heap                       |
| ---------------- | --------------------------- | -------------------------- |
| `ApexRegistry`   | ~2KB (base + entry vectors) | Vector storage for entries |
| `ComponentEntry` | 128B (fixed array indices)  | 0                          |
| `TaskEntry`      | 48B                         | 0                          |
| `DataEntry`      | 64B                         | 0                          |

---

## 4. Architecture

### Design Philosophy

**Single source of truth for runtime metadata.**

| Component        | Responsibility                         |
| ---------------- | -------------------------------------- |
| `ApexRegistry`   | Central database (SystemComponentBase) |
| `ComponentEntry` | Component UID, name, task/data links   |
| `TaskEntry`      | Task UID, name, pointer                |
| `DataEntry`      | Data category, name, pointer, size     |

This separation enables:

- Unified queries across components, tasks, and data
- Byte-level data access for logging
- RT-safe queries after freeze()

### Lifecycle

```
Registration        Freeze              Query
(NOT RT-safe)  -->  (NOT RT-safe)  -->  (RT-safe)

registerComponent()   freeze()           getComponent()
registerTask()                           getData()
registerData()                           getAllTasks()
```

### Library Structure

| File                 | Component        | Purpose                        |
| -------------------- | ---------------- | ------------------------------ |
| `ApexRegistry.hpp`   | `ApexRegistry`   | Central registry class         |
| `RegistryData.hpp`   | `ComponentEntry` | Component with task/data links |
| `RegistryData.hpp`   | `TaskEntry`      | Task metadata                  |
| `RegistryData.hpp`   | `DataEntry`      | Data block with byte access    |
| `RegistryExport.hpp` | RDAT export      | Binary database export         |
| `RegistryStatus.hpp` | `Status`         | Typed status codes             |

### RT-Safety Summary

| Operation             | RT-Safe | Notes                      |
| --------------------- | ------- | -------------------------- |
| `registerComponent()` | No      | Vector growth possible     |
| `registerTask()`      | No      | Vector growth possible     |
| `registerData()`      | No      | Vector growth possible     |
| `freeze()`            | No      | Finalizes registry         |
| `getComponent()`      | Yes     | O(n) scan, after freeze    |
| `getTask()`           | Yes     | O(n) scan, after freeze    |
| `getData()`           | Yes     | O(n) scan, after freeze    |
| `getAllComponents()`  | Yes     | Returns span, after freeze |
| `entry->getBytes()`   | Yes     | O(1), pointer arithmetic   |
| `toString(Status)`    | Yes     | Static string lookup       |

---

## 5. Key Features

### Component Identity

Components are identified by fullUid (32-bit):

```cpp
// fullUid = (componentId << 8) | instanceIndex
// Example: PolynomialModel (componentId=102), instance 0
registry.registerComponent(0x6600, "PolynomialModel");

// Same component type, instance 1
registry.registerComponent(0x6601, "PolynomialModel");
```

### Data Categories

From `DataCategory.hpp`:

| Category        | Purpose                       |
| --------------- | ----------------------------- |
| `STATIC_PARAM`  | Read-only constants           |
| `TUNABLE_PARAM` | Runtime-adjustable parameters |
| `STATE`         | Internal model state          |
| `INPUT`         | External data fed to model    |
| `OUTPUT`        | Data produced by model        |

### Byte-Level Data Access

DataEntry provides span-based access for logging:

```cpp
auto* entry = registry.getData(0x6600, DataCategory::STATE);

// All bytes
auto allBytes = entry->getBytes();

// Specific field (offset=4, length=8)
auto fieldBytes = entry->getBytes(4, 8);

// Mutable access (use with caution)
auto mutBytes = entry->getMutableBytes();
mutBytes[0] = 0x42;
```

### Freeze Semantics

Registration methods may allocate; query methods are RT-safe after freeze:

```cpp
// Registration phase - vectors may grow
registry.registerComponent(0x6600, "Model");
registry.registerData(0x6600, DataCategory::STATE, "state", &s, sizeof(s));

// Freeze locks the registry
registry.freeze();

// After freeze:
// - registerComponent() returns ERROR_ALREADY_FROZEN
// - registerData() returns ERROR_ALREADY_FROZEN
// - Query methods are RT-safe (fixed storage)
```

### Component Linkage

Each component tracks its tasks and data:

```cpp
auto* comp = registry.getComponent(0x6600);

// Get component's tasks
TaskEntry* tasks[16];
size_t count = registry.getTasksForComponent(0x6600, tasks, 16);

// Get component's data
DataEntry* data[16];
size_t count = registry.getDataForComponent(0x6600, data, 16);
```

### Database Export

Export registry contents to RDAT binary format for external analysis:

```cpp
registry.freeze();
auto status = registry.exportDatabase("/path/to/db/");
// Creates: /path/to/db/registry.rdat
```

RDAT format:

| Section      | Size          | Content                                |
| ------------ | ------------- | -------------------------------------- |
| Header       | 16 bytes      | Magic "RDAT", version, counts          |
| Components   | 24 bytes each | fullUid, name offset, task/data ranges |
| Tasks        | 16 bytes each | fullUid, taskUid, name offset          |
| Data         | 24 bytes each | fullUid, category, name offset, size   |
| String Table | Variable      | Null-terminated names                  |

### Capacity Limits

```cpp
static constexpr size_t MAX_COMPONENTS = 64;
static constexpr size_t MAX_TASKS = 256;
static constexpr size_t MAX_DATA_ENTRIES = 512;
static constexpr size_t MAX_TASKS_PER_COMPONENT = 16;
static constexpr size_t MAX_DATA_PER_COMPONENT = 16;
```

---

## 6. API Reference

### 6.1 Status Codes

| Status                      | Meaning                           |
| --------------------------- | --------------------------------- |
| `SUCCESS`                   | Operation succeeded               |
| `ERROR_ALREADY_FROZEN`      | Registration after freeze()       |
| `ERROR_NOT_FROZEN`          | Query before freeze() (future)    |
| `ERROR_NULL_POINTER`        | Null data or name pointer         |
| `ERROR_DUPLICATE_COMPONENT` | Component fullUid exists          |
| `ERROR_DUPLICATE_TASK`      | Task fullUid+taskUid exists       |
| `ERROR_DUPLICATE_DATA`      | Data fullUid+category+name exists |
| `ERROR_COMPONENT_NOT_FOUND` | Unknown component fullUid         |
| `ERROR_CAPACITY_EXCEEDED`   | Max entries reached               |
| `ERROR_ZERO_SIZE`           | Zero-size data registration       |
| `WARN_EMPTY_NAME`           | Empty name string (not error)     |

### 6.2 ApexRegistry

| Method                                    | Purpose                           |
| ----------------------------------------- | --------------------------------- |
| `registerComponent(fullUid, name)`        | Register a component              |
| `registerTask(fullUid, taskUid, name, t)` | Register a task                   |
| `registerData(fullUid, cat, name, p, sz)` | Register a data block             |
| `freeze()`                                | Lock registry for queries         |
| `isFrozen()`                              | Check if frozen                   |
| `getComponent(fullUid)`                   | Get component by UID              |
| `getTask(fullUid, taskUid)`               | Get task by UIDs                  |
| `getData(fullUid, category)`              | Get data by UID + category        |
| `getData(fullUid, category, name)`        | Get data by UID + category + name |
| `getAllComponents()`                      | Get span of all components        |
| `getAllTasks()`                           | Get span of all tasks             |
| `getAllData()`                            | Get span of all data              |
| `getTasksForComponent(uid, out, max)`     | Get component's tasks             |
| `getDataForComponent(uid, out, max)`      | Get component's data              |
| `totalDataSize()`                         | Sum of all data sizes             |
| `reset()`                                 | Clear all entries, unfreeze       |
| `exportDatabase(path)`                    | Export to RDAT binary format      |

### 6.3 DataEntry

| Field/Method        | Purpose                             |
| ------------------- | ----------------------------------- |
| `fullUid`           | Owner component's full UID          |
| `category`          | DataCategory (STATE, TUNABLE, etc.) |
| `name`              | Human-readable name                 |
| `dataPtr`           | Pointer to actual data (not owned)  |
| `size`              | Size in bytes                       |
| `getBytes()`        | All bytes as span                   |
| `getBytes(o, l)`    | Bytes at offset o, length l         |
| `getMutableBytes()` | Mutable byte access                 |
| `isValid()`         | Check if entry has data             |

### 6.4 TaskEntry

| Field/Method | Purpose                    |
| ------------ | -------------------------- |
| `fullUid`    | Owner component's full UID |
| `taskUid`    | Task ID within component   |
| `name`       | Human-readable name        |
| `task`       | Pointer to SchedulableTask |
| `isValid()`  | Check if entry has task    |

### 6.5 ComponentEntry

| Field/Method    | Purpose                       |
| --------------- | ----------------------------- |
| `fullUid`       | Component's full UID          |
| `name`          | Component name                |
| `taskIndices[]` | Indices into tasks array      |
| `taskCount`     | Number of linked tasks        |
| `dataIndices[]` | Indices into data array       |
| `dataCount`     | Number of linked data entries |
| `isValid()`     | Check if entry has name       |

---

## 7. Usage Examples

### 7.1 Executive Integration

```cpp
// Executive registers components during model registration
void ApexExecutive::registerModel(SimModelBase* model) {
  auto fullUid = model->fullUid();
  registry_.registerComponent(fullUid, model->componentName());
}

// Models register their data during init
void PolynomialModel::doInit() {
  registry_.registerData(fullUid(), DataCategory::STATE,
                         "state", &state_, sizeof(state_));
  registry_.registerData(fullUid(), DataCategory::TUNABLE_PARAM,
                         "tunables", &tunables_, sizeof(tunables_));
}
```

### 7.2 Data Logging

```cpp
// Log all STATE data every frame
for (const auto& entry : registry.getAllData()) {
  if (entry.category == DataCategory::STATE) {
    auto bytes = entry.getBytes();
    logger.logBytes(entry.fullUid, entry.name, bytes);
  }
}
```

### 7.3 Field-Level Access

```cpp
// Access specific fields by offset
struct Vec3 { double x, y, z; };

registry.registerData(0x6600, DataCategory::STATE, "position",
                      &pos, sizeof(pos));

auto* entry = registry.getData(0x6600, DataCategory::STATE, "position");

// Get just the z component (offset=16, size=8)
auto zBytes = entry->getBytes(16, 8);
```

---

## 8. Requirements

### Build Dependencies

- C++17 compiler (GCC 10+, Clang 12+)
- fmt library (formatting)
- system_core_system_component (SystemComponentBase, DataCategory)

### Runtime

- Linux (POSIX)

---

## 9. Testing

| Directory    | Type                   | Tests | Runs with `make test` |
| ------------ | ---------------------- | ----- | --------------------- |
| `apex/utst/` | Unit tests             | 80    | Yes                   |
| `apex/ptst/` | Performance benchmarks | 9     | No (manual)           |

### Test Organization

| Component      | Test File                  | Tests  |
| -------------- | -------------------------- | ------ |
| ApexRegistry   | `ApexRegistry_uTest.cpp`   | 55     |
| RegistryData   | `RegistryData_uTest.cpp`   | 16     |
| RegistryStatus | `RegistryStatus_uTest.cpp` | 9      |
| **Total**      |                            | **80** |

---

## 10. See Also

- `src/system/core/infrastructure/data/` - DataCategory enum and ModelData templates
- `src/system/core/infrastructure/system_component/` - SystemComponentBase lifecycle
- `src/system/core/executive/` - ApexExecutive integration
- `src/system/core/components/scheduler/` - Task scheduling
- `src/system/core/infrastructure/schedulable/` - SchedulableTask
- `tools/rust/` - `rdat_tool` for RDAT file analysis and SQLite export
