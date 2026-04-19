# Action Component

**Namespace:** `system_core::action`, `system_core::data`
**Platform:** Cross-platform (no OS dependencies)
**C++ Standard:** C++23

Runtime action engine for telemetry monitoring, onboard command sequencing,
and event-driven automation. Evaluates watchpoints, fires events, executes
RTS/ATS command sequences, and routes actions to registered components.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Key Features](#3-key-features)
4. [API Reference](#4-api-reference)
5. [Performance](#5-performance)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Reference

| Module               | Purpose                                         | RT-Safe     |
| -------------------- | ----------------------------------------------- | ----------- |
| `ActionComponent`    | Executive-owned lifecycle wrapper               | Partial     |
| `ActionInterface`    | processCycle pipeline (WP/group/seq/action)     | Yes         |
| `DataWatchpoint`     | Telemetry monitoring with computed functions    | Yes         |
| `WatchpointGroup`    | Composite conditions (AND/OR/custom)            | Yes         |
| `DataSequence`       | RTS/ATS command sequence execution              | Yes         |
| `EventNotification`  | Event callbacks and built-in logging            | Yes         |
| `DataAction`         | COMMAND routing and ARM_CONTROL                 | Yes         |
| `SequenceCatalog`    | O(log N) sequence registry with cached binaries | Lookup: Yes |
| `ResourceCatalog`    | WP/group/notification definition registry       | Lookup: Yes |
| `ActionEngineConfig` | Compile-time table sizes                        | N/A         |

### Question-to-Module

| Question                                      | Module                                   |
| --------------------------------------------- | ---------------------------------------- |
| How do I monitor telemetry values?            | `DataWatchpoint`                         |
| How do I compute rate of change or magnitude? | `WatchFunction` (DELTA, RATE, MAGNITUDE) |
| How do I combine multiple conditions?         | `WatchpointGroup`                        |
| How do I run a command sequence on an event?  | `DataSequence` + `SequenceCatalog`       |
| How do I log when a threshold is crossed?     | `EventNotification`                      |
| How do I send a command to another component? | `DataAction` (COMMAND type)              |
| How do I arm/disarm watchpoints at runtime?   | `ARM_CONTROL` action or ground command   |

---

## 2. Design Principles

| Principle          | Implementation                                           |
| ------------------ | -------------------------------------------------------- |
| RT-safe hot path   | processCycle: no allocation, bounded loops, noexcept     |
| Edge-triggered     | Watchpoints fire on false-to-true transition only        |
| Delegate wiring    | No virtual dispatch, Delegate<> function pointers        |
| Configurable scale | All table sizes via ActionEngineConfig                   |
| Cached loading     | Sequence binaries cached at boot, memcpy at trigger      |
| Compute pipeline   | raw bytes -> WatchFunction -> predicate -> edge -> event |

---

## 3. Key Features

### Watchpoint Compute Functions

Pre-process raw telemetry before predicate evaluation:

| Function  | Description                        | Use Case                       |
| --------- | ---------------------------------- | ------------------------------ |
| NONE      | Raw value comparison               | Simple threshold               |
| DELTA     | abs(current - previous)            | Sudden change detection        |
| RATE      | (current - previous) / dt          | Rate of change monitoring      |
| MAGNITUDE | sqrt(sum of squares) across fields | Vector magnitude (accel, gyro) |
| MEAN      | Rolling average over N samples     | Noise filtering                |
| STALE     | Ticks since value last changed     | Stale data detection           |
| CUSTOM    | User delegate returns double       | Arbitrary computation          |

```cpp
// Monitor rate of temperature change > 5 deg/s
auto& wp = iface.watchpoints[0];
wp.target = {sensorUid, DataCategory::OUTPUT, 0, 4};
wp.predicate = WatchPredicate::GT;
wp.dataType = WatchDataType::FLOAT32;
wp.function = WatchFunction::RATE;
wp.eventId = 1;
double threshold = 5.0;
std::memcpy(wp.threshold.data(), &threshold, sizeof(double));
wp.armed = true;
```

### Sequence Catalog

256-entry registry with O(log N) lookup and cached binaries:

- Priority preemption (higher takes slot from lower)
- Blocking relationships (directional)
- Mutual exclusion groups (one-at-a-time)
- Abort events (cleanup on preempt/stop/timeout)
- Chaining by sequence ID
- Hot-add via RESCAN_CATALOG

### Ground Command Interface (22 opcodes)

| Range         | Commands                                                                                     |
| ------------- | -------------------------------------------------------------------------------------------- |
| 0x0500-0x0506 | Slot-based: LOAD/START/STOP RTS/ATS, ABORT_ALL_RTS                                           |
| 0x0510-0x0515 | ID-based: START/STOP_BY_ID, SET_PRIORITY, SET_BLOCKING, SET_ABORT_EVENT, SET_EXCLUSION_GROUP |
| 0x0520-0x0522 | Catalog: RESCAN, GET_CATALOG, GET_STATUS                                                     |
| 0x0530-0x0535 | Resources: ACTIVATE/DEACTIVATE WP, GROUP, NOTIFICATION                                       |

---

## 4. API Reference

### ActionComponent

| Method                                     | RT-Safe | Description                          |
| ------------------------------------------ | ------- | ------------------------------------ |
| `setResolver(fn, ctx)`                     | Yes     | Wire registry lookup delegate        |
| `setCommandHandler(fn, ctx)`               | Yes     | Wire command routing delegate        |
| `tick(cycle)`                              | Yes     | Run one processCycle                 |
| `iface()`                                  | Yes     | Direct table access                  |
| `stats()`                                  | Yes     | Engine statistics                    |
| `loadTprm(dir)`                            | No      | Load boot configuration              |
| `scanCatalog(rtsDir, atsDir)`              | No      | Populate sequence catalog            |
| `startRtsById(id)`                         | Yes     | Catalog lookup + cached load + start |
| `stopRtsById(id)`                          | Yes     | Stop all instances by ID             |
| `loadAtsFromCatalog()`                     | No      | Load ATS entries into slots          |
| `handleCommand(opcode, payload, response)` | Partial | Ground command dispatch              |

### processCycle Pipeline

| Stage                    | Function                | Complexity             |
| ------------------------ | ----------------------- | ---------------------- |
| 1. Evaluate watchpoints  | `evaluateWatchpoints()` | O(WATCHPOINT_COUNT)    |
| 2. Evaluate groups       | `evaluateGroups()`      | O(GROUP_COUNT \* refs) |
| 3. Dispatch events       | `dispatchEvents()`      | O(events \* popcount)  |
| 4. Tick sequences        | `tickSequences()`       | O(SEQUENCE_TABLE_SIZE) |
| 5. Dispatch abort events | `dispatchAbortEvents()` | O(SEQUENCE_TABLE_SIZE) |
| 6. Process actions       | `processActions()`      | O(ACTION_QUEUE_SIZE)   |

### Index Rebuild Functions

| Function                        | When to Call                        | Cost            |
| ------------------------------- | ----------------------------------- | --------------- |
| `rebuildWatchpointIndex(iface)` | After WP activate/deactivate        | 129 ns (32 WPs) |
| `rebuildEventIndex(iface)`      | After notification/sequence changes | O(tables)       |

---

## 5. Performance

Measured on x86_64 (clang-21, debug), 15 repeats. Profiled with gperftools,
callgrind, and perf. Two optimization passes applied.

### Pipeline Throughput

| Scenario                                    | Median  | Calls/s |
| ------------------------------------------- | ------- | ------- |
| Empty cycle                                 | 127 ns  | 7.8M    |
| Scale steady-state (full tables)            | 1.17 us | 856K    |
| Raw cascade fire (32 WP + 16 grp + 16 seq)  | 1.68 us | 595K    |
| Computed cascade (RATE + MAGNITUDE + DELTA) | 2.44 us | 410K    |

### Compute Function Overhead

| Function            | Per-WP Cost | Overhead vs NONE |
| ------------------- | ----------- | ---------------- |
| NONE                | 352 ns      | baseline         |
| DELTA               | 346 ns      | ~0               |
| RATE                | 363 ns      | +11 ns           |
| MAGNITUDE (3-field) | 371 ns      | +19 ns           |
| CUSTOM (delegate)   | 381 ns      | +29 ns           |

### Catalog and Trigger Latency

| Operation                                  | Latency |
| ------------------------------------------ | ------- |
| Catalog lookup (200 entries)               | 16 ns   |
| RTS trigger path (200 catalog, 16 running) | 293 ns  |
| Abort dispatch (8 pending)                 | 983 ns  |
| Activate 32 watchpoints                    | 3.1 us  |
| Deactivate + reactivate 1 WP               | 127 ns  |

### Memory Footprint

| Component                             | Size    |
| ------------------------------------- | ------- |
| ActionInterface (full tables)         | ~102 KB |
| Sequence catalog (256 entries)        | ~360 KB |
| Lookup tables (wpId + event bitmasks) | 3.3 KB  |

---

## 6. Requirements

| Requirement       | Details                                                                                               |
| ----------------- | ----------------------------------------------------------------------------------------------------- |
| C++ Standard      | C++23                                                                                                 |
| Dependencies      | `system_core_system_component`, `utilities_concurrency`, `utilities_helpers`, `utilities_time`, `fmt` |
| OS                | None (no OS-specific calls in hot path)                                                               |
| Resolver delegate | Must be set before `init()`                                                                           |
| Configuration     | All sizes via `ActionEngineConfig` (override with `APEX_ACTION_ENGINE_CUSTOM_CONFIG`)                 |

### Default Table Sizes (POSIX)

| Resource         | Count |
| ---------------- | ----- |
| Watchpoints      | 128   |
| Groups           | 32    |
| Notifications    | 128   |
| RTS slots        | 32    |
| ATS slots        | 16    |
| Action queue     | 64    |
| Sequence catalog | 256   |

---

## 7. Testing

### Test Organization

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 275   | Yes                   |
| `ptst/`   | Performance tests | 17    | No (manual)           |

### Test Targets

| Target                  | Tests | Description                                                                 |
| ----------------------- | ----- | --------------------------------------------------------------------------- |
| `TestSystemCoreAction`  | 275   | Watchpoints, groups, sequences, notifications, catalogs, computed functions |
| `ActionInterface_PTEST` | 17    | Pipeline throughput, cascade fire, catalog lookup, trigger path             |

---

## 8. See Also

- [system_component](../../infrastructure/system_component/) - CoreComponentBase lifecycle, DataTarget
- [concurrency](../../../../utilities/concurrency/) - Delegate for resolver/command/compute wiring
- [time](../../../../utilities/time/) - TimeProviderDelegate for ATS timing
- [scheduler](../scheduler/) - Scheduler that drives tick() each frame
