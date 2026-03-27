# Action Component

**Namespace:** `system_core::action`
**Platform:** Any (no OS dependencies)
**C++ Standard:** C++23

Core infrastructure component wrapping `ActionInterface` as a managed `CoreComponentBase` for executive lifecycle ownership of the runtime action engine.

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

| Component                | Purpose                                        | RT-Safe |
| ------------------------ | ---------------------------------------------- | ------- |
| `ActionComponent`        | Executive-owned wrapper around ActionInterface | Partial |
| `ActionComponentStatus`  | Status enum with `toString()`                  | Yes     |
| `ActionEngineTprm`       | Boot-time TPRM binary struct (2624 bytes)      | N/A     |
| `StandaloneSequenceTprm` | Standalone RTS/ATS file format (1352 bytes)    | N/A     |

---

## 2. When to Use

| Scenario                      | Implementation             | Notes                                           |
| ----------------------------- | -------------------------- | ----------------------------------------------- |
| Executive needs action engine | `ActionComponent`          | Single-instance, auto-configured (no TPRM)      |
| Direct table access           | `iface()`                  | Configure watchpoints, sequences, notifications |
| Standalone action processing  | `ActionInterface` directly | No lifecycle needed (use data library)          |
| Load RTS/ATS from file        | `loadRts()` / `loadAts()`  | Standalone binary files, validated on load      |
| Ground command sequencing     | `handleCommand()` override | Opcodes 0x0500-0x0505 (LOAD/START/STOP)         |

---

## 3. Performance

ActionComponent is a thin wrapper (~25 lines `.cpp`) around `ActionInterface`. All performance-critical logic lives in `processCycle()` from the data library. Measurements below are from `ActionInterface_PTEST`.

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point.

### Pipeline Throughput

| Metric                                 | Median | CV%   | Calls/s |
| -------------------------------------- | ------ | ----- | ------- |
| Empty cycle (no tables armed)          | 44 ns  | 11.6% | 22.7M   |
| 8 watchpoints (steady-state)           | 203 ns | 24.7% | 4.9M    |
| 4 watchpoints (firing)                 | 115 ns | 0.7%  | 8.7M    |
| 4 sequences (waiting)                  | 50 ns  | 14.0% | 20.2M   |
| 8 DATA_WRITE actions                   | 467 ns | 2.0%  | 2.1M    |
| Full pipeline (8wp + 2g + 4seq + 4act) | 380 ns | 1.1%  | 2.6M    |

### Isolated Sub-Pipelines

| Stage                           | Median | CV%  | Calls/s |
| ------------------------------- | ------ | ---- | ------- |
| `evaluateWatchpoints` (8 armed) | 171 ns | 0.8% | 5.9M    |
| `tickSequences` (4 waiting)     | 28 ns  | 1.1% | 36.1M   |
| `processActions` (8 active)     | 266 ns | 1.9% | 3.8M    |

### Profiler Analysis

| Hotspot                | Self-Time | Type                                |
| ---------------------- | --------- | ----------------------------------- |
| `applyDataWrite`       | 13.9%     | CPU-bound (mask application)        |
| `detail::compareTyped` | 8.9%      | CPU-bound (watchpoint comparison)   |
| `evaluateGroup`        | 8.2%      | CPU-bound (AND logic)               |
| `resolveTarget`        | 7.3%      | CPU-bound (delegate + bounds check) |
| `evaluateEdge`         | 6.8%      | CPU-bound (edge detection)          |

### Memory Footprint

| Component              | Stack           | Heap |
| ---------------------- | --------------- | ---- |
| `ActionComponent`      | ~2.5 KB         | 0    |
| `ActionInterface`      | ~2.4 KB         | 0    |
| `DataWatchpoint` (x8)  | 48 bytes each   | 0    |
| `WatchpointGroup` (x4) | 16 bytes each   | 0    |
| `DataSequence` (x4)    | ~480 bytes each | 0    |
| `DataAction` (x16)     | 116 bytes each  | 0    |

---

## 4. Architecture

ActionComponent wraps ActionInterface for executive lifecycle management:

```
Executive
  |
  +-- ActionComponent (CoreComponentBase)
        |-- setResolver()       -> wires registry lookup delegate
        |-- setCommandHandler() -> wires command routing delegate
        |-- init()              -> validates resolver, marks initialized
        |-- tick(cycle)         -> processCycle(iface_, cycle)
        +-- iface()             -> direct table access for configuration
```

The component adds no runtime logic. `tick()` is a single inline call to `processCycle()`. All watchpoint evaluation, group logic, event dispatch, sequence stepping, and action processing are implemented in `ActionInterface` (data library).

### Data Flow Per Tick

```
tick(cycle)
  +-> processCycle(iface, cycle)
       |-> evaluateWatchpoints()  -> collect fired eventIds
       |-> evaluateGroups()       -> collect group eventIds
       |-> dispatchEvents()       -> invoke notifications, start sequences
       |-> tickSequences()        -> queue step actions
       +-> processActions()       -> apply DATA_WRITE, route COMMAND, ARM_CONTROL
```

---

## 5. Key Features

- **Auto-configured.** No TPRM needed. Constructor marks configured immediately.
- **Zero allocations.** All tables are statically sized arrays. No heap activity during tick.
- **Delegate-based wiring.** Resolver and command handler use allocation-free `Delegate<>` function pointers, not virtual dispatch.
- **Thin wrapper.** Component `.cpp` is ~25 lines. All performance-critical logic lives in the data library's header-only `ActionInterface`.
- **Static table sizes.** 8 watchpoints, 4 groups, 4 sequences, 8 notifications, 16 actions.

---

## 6. API Reference

### ActionComponent

```cpp
/// @note RT-safe: Stores pointer pair.
void setResolver(DataResolveDelegate::Fn fn, void* ctx) noexcept;
void setCommandHandler(CommandDelegate::Fn fn, void* ctx) noexcept;

/// @note RT-safe: Bounded by static table sizes.
void tick(std::uint32_t currentCycle) noexcept;

/// @note RT-safe: Direct member access.
ActionInterface& iface() noexcept;
const ActionInterface& iface() const noexcept;
const EngineStats& stats() const noexcept;

/// @note NOT RT-safe: Called once at boot.
std::uint8_t doInit() noexcept;

/// @note NOT RT-safe: Called during reset phase.
void doReset() noexcept;
```

### ActionComponentStatus

```cpp
enum class Status : std::uint8_t {
  SUCCESS,
  ERROR_NO_RESOLVER,
  ERROR_QUEUE_FULL,
  WARN_RESOLVE_FAILURES
};

/// @note RT-safe: Lookup table.
const char* toString(Status s) noexcept;
```

### Component Identity

| Field             | Value    |
| ----------------- | -------- |
| `componentId()`   | 5        |
| `componentName()` | "Action" |
| `label()`         | "ACTION" |

---

## 7. Usage Examples

### Executive Wiring

```cpp
#include "ActionComponent.hpp"

using system_core::action::ActionComponent;

ActionComponent action;

// Wire resolver (maps DataTarget -> byte pointer via registry)
action.setResolver(registryResolverFn, &registry);

// Wire command handler (routes commands to target components)
action.setCommandHandler(busCommandFn, &bus);

// Initialize (validates resolver is set)
auto status = action.init();

// Each scheduler frame
action.tick(currentCycle);
```

### Configuring Watchpoints

```cpp
auto& iface = action.iface();

// Watch altitude > 150.0
auto& wp = iface.watchpoints[0];
wp.target = {0x007800, DataCategory::OUTPUT, 36, 4};
wp.predicate = WatchPredicate::GT;
wp.dataType = WatchDataType::FLOAT32;
wp.eventId = 1;
float threshold = 150.0F;
std::memcpy(wp.threshold.data(), &threshold, sizeof(float));
wp.armed = true;
```

### Querying Statistics

```cpp
const auto& STATS = action.stats();
std::printf("Cycles: %u, WP fired: %u, Actions: %u\n",
            STATS.totalCycles, STATS.watchpointsFired, STATS.actionsApplied);
```

---

## Onboard Command Sequencing

ActionComponent supports RTS (Relative Time Sequence) and ATS (Absolute Time
Sequence) command procedures via standalone binary files loaded at runtime.

### Ground Command Interface

| Opcode | Name      | Payload              | RT-Safe       |
| ------ | --------- | -------------------- | ------------- |
| 0x0500 | LOAD_RTS  | u8 slot, char[] path | No (file I/O) |
| 0x0501 | START_RTS | u8 slot              | Yes           |
| 0x0502 | STOP_RTS  | u8 slot              | Yes           |
| 0x0503 | LOAD_ATS  | u8 slot, char[] path | No (file I/O) |
| 0x0504 | START_ATS | u8 slot              | Yes           |
| 0x0505 | STOP_ATS  | u8 slot              | Yes           |

### Sequence Features

Each step supports: delay, timeout (ABORT/SKIP/GOTO), retry, wait condition
(embedded watchpoint), branching (GOTO_STEP/START_RTS), and COMMAND dispatch
to any registered component via the commandHandler delegate.

### Validation

Sequences are validated on load (`validateSequence()`) and ATS timelines are
checked against the time provider on start (`validateAtsTimeline()`). Catches:
out-of-bounds gotos, unroutable commands, non-monotonic ATS offsets, stale
timestamps in the past.

### Time Provider

ATS timing supports multiple standards via `TimeProviderDelegate` on the
ActionInterface: MONOTONIC, UTC, TAI, GPS, MET. See `src/utilities/time/`.

---

## 8. Requirements

| Requirement       | Details                                                                            |
| ----------------- | ---------------------------------------------------------------------------------- |
| C++ Standard      | C++17                                                                              |
| Dependencies      | `system_core_data`, `system_core_system_component`, `utilities_concurrency`, `fmt` |
| OS                | None (no OS-specific calls)                                                        |
| Resolver delegate | Must be set before `init()`                                                        |

---

## 9. Testing

### Test Organization

| Directory | Type       | Tests | Runs with `make test` |
| --------- | ---------- | ----- | --------------------- |
| `utst/`   | Unit tests | 10    | Yes                   |

### Test Targets

| Target                 | Tests | Description                                        |
| ---------------------- | ----- | -------------------------------------------------- |
| `TestSystemCoreAction` | 10    | Component lifecycle, delegate wiring, tick, status |

---

## 10. See Also

- [data](../../infrastructure/data/) - ActionInterface, DataWatchpoint, DataAction, DataSequence, SequenceValidation primitives
- [system_component](../../infrastructure/system_component/) - CoreComponentBase lifecycle
- [concurrency](../../../../utilities/concurrency/) - Delegate for resolver/command wiring
- [time](../../../../utilities/time/) - TimeProviderDelegate, system clocks, time conversions
- [scheduler](../scheduler/) - Scheduler that drives tick() each frame
