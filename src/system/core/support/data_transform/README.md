# DataTransform

**Namespace:** `system_core::support`
**Library:** `system_core_support_data_transform`
**Platform:** Cross-platform
**C++ Standard:** C++23

Command-driven support component for runtime byte-level data mutation via
mask proxies, used for fault injection, operator overrides, and safing.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Common Workflows](#3-common-workflows)
4. [API Reference](#4-api-reference)
5. [Requirements](#5-requirements)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Start

```cpp
#include "src/system/core/support/data_transform/inc/DataTransform.hpp"

system_core::support::DataTransform transform;
transform.setResolver(registryResolverFn, &registry);
executive.registerSupport(&transform);
```

The action engine sequencer triggers mask application at precise cycles via
APPLY_ENTRY and APPLY_ALL commands. Ground operators can also send commands
directly via C2.

### Quick Reference

| Module              | Purpose                                 | RT-Safe |
| ------------------- | --------------------------------------- | ------- |
| `DataTransform`     | Lifecycle wrapper, command dispatch     | Partial |
| `DataTransformData` | Entry table, stats, opcodes, telemetry  | Yes     |
| `FaultCampaignTprm` | TPRM-driven fault campaign with ATS gen | Yes     |

| Question                                          | Module              |
| ------------------------------------------------- | ------------------- |
| How do I inject faults at specific cycles?        | `FaultCampaignTprm` |
| How do I apply byte masks to registered data?     | `DataTransform`     |
| How do I arm/disarm individual transform entries? | `DataTransform`     |
| How do I push zero/high/flip masks?               | `DataTransform`     |

---

## 2. Key Features

### Command-Driven Mask Application

DataTransform is not scheduled. Mask application happens when the action
engine sequencer (or a ground operator) sends APPLY_ENTRY or APPLY_ALL.
This gives cycle-accurate timing inherited from the sequencer with zero
periodic overhead in DataTransform itself.

### ByteMaskProxy Per Entry

Each of the 8 transform entries owns an independent `ByteMaskProxy` from
the data_proxy library. Masks follow the standard AND/XOR rule:

| Pattern | AND  | XOR  | Effect              |
| ------- | ---- | ---- | ------------------- |
| Zero    | 0x00 | 0x00 | Forces byte to 0    |
| High    | 0x00 | 0xFF | Forces byte to 0xFF |
| Flip    | 0xFF | 0xFF | Inverts all bits    |
| Custom  | user | user | Arbitrary AND/XOR   |

### Command Opcodes (12 opcodes, 0x0600 range)

| Opcode             | Code   | Description                                 |
| ------------------ | ------ | ------------------------------------------- |
| `GET_STATS`        | 0x0600 | Return 20-byte health telemetry             |
| `ARM_ENTRY`        | 0x0601 | Arm a transform entry by index              |
| `DISARM_ENTRY`     | 0x0602 | Disarm a transform entry by index           |
| `PUSH_ZERO_MASK`   | 0x0603 | Push zero mask to entry proxy queue         |
| `PUSH_HIGH_MASK`   | 0x0604 | Push high mask to entry proxy queue         |
| `PUSH_FLIP_MASK`   | 0x0605 | Push flip mask to entry proxy queue         |
| `PUSH_CUSTOM_MASK` | 0x0606 | Push custom AND/XOR mask to entry proxy     |
| `CLEAR_MASKS`      | 0x0607 | Clear all masks on an entry                 |
| `CLEAR_ALL`        | 0x0608 | Disarm all entries and clear all masks      |
| `SET_TARGET`       | 0x0609 | Set target (fullUid, category, offset, len) |
| `APPLY_ENTRY`      | 0x060A | Resolve target and apply front mask         |
| `APPLY_ALL`        | 0x060B | Apply front mask on all armed entries       |

### Fault Campaign (TPRM)

A fault campaign defines up to 8 timed mutations loaded from a TPRM binary
file. At boot, DataTransform translates the campaign into an ATS (Absolute
Time Sequence) and loads it into the action engine via `onBusReady()`. Each
fault entry specifies:

- Target (fullUid, category, byte offset, byte length)
- Mask type (ZERO, HIGH, FLIP, CUSTOM)
- Trigger cycle and duration (0 = one-shot)

The ATS approach provides cycle-accurate fault timing through the action
engine's existing sequencer with zero additional runtime overhead.

### 1 Hz Telemetry

A low-frequency telemetry task logs health stats (apply cycles, masks
applied, resolve failures, apply failures, armed count).

---

## 3. Common Workflows

### Fault Injection via Action Engine Sequence

The action engine sequences commands to DataTransform at precise cycles:

```
Step 0 (cycle 500): CMD 0xCA00 ARM_ENTRY [0]
Step 1 (cycle 500): CMD 0xCA00 PUSH_ZERO_MASK [0, 0, 0, 4]
Step 2 (cycle 500): CMD 0xCA00 APPLY_ENTRY [0]
Step 3 (cycle 600): CMD 0xCA00 DISARM_ENTRY [0]
```

### Direct Entry Configuration

```cpp
system_core::support::DataTransform transform;
transform.setResolver(resolverFn, &registry);
transform.setInstanceIndex(0);
transform.init();

// Configure entry 0 to target a specific data block
auto* e = transform.entry(0);
e->target.fullUid = 0x006600;
e->target.category = system_core::data::DataCategory::OUTPUT;
e->target.byteOffset = 0;
e->target.byteLen = 4;
e->proxy.pushZeroMask(0, 4);
e->armed = true;
```

### TPRM Fault Campaign

```cpp
system_core::support::DataTransform transform;
transform.setResolver(resolverFn, &registry);
transform.setClockFrequency(10);  // 10 Hz scheduler
executive.registerSupport(&transform);

// Executive calls loadTprm() during init, which:
//   1. Reads fault_campaign.tprm binary
//   2. Translates entries into ATS binary
//   3. onBusReady() sends LOAD_ATS to action engine
```

---

## 4. API Reference

### DataTransform

**RT-safe:** `handleCommand()` is RT-safe for all opcodes. `init()`, `reset()`,
`loadTprm()`, `onBusReady()` are NOT RT-safe.

| Method                        | RT-Safe | Description                                 |
| ----------------------------- | ------- | ------------------------------------------- |
| `setResolver(fn, ctx)`        | Yes     | Wire registry lookup delegate               |
| `setClockFrequency(hz)`       | Yes     | Set clock frequency for ATS time conversion |
| `handleCommand(op, pay, rsp)` | Yes     | Dispatch ground/sequencer commands          |
| `loadTprm(dir)`               | No      | Load fault campaign from TPRM binary        |
| `onBusReady()`                | No      | Send LOAD_ATS to action engine via bus      |
| `telemetry()`                 | No      | Log health stats (1 Hz task)                |
| `stats()`                     | Yes     | Get diagnostic counters                     |
| `entries()`                   | Yes     | Get transform entry table (8 entries)       |
| `entry(index)`                | Yes     | Get mutable entry by index                  |

### TransformEntry

**RT-safe:** Yes (pure data structure)

| Field    | Type            | Description                  |
| -------- | --------------- | ---------------------------- |
| `target` | `DataTarget`    | Where to apply transforms    |
| `proxy`  | `ByteMaskProxy` | Mask queue for this target   |
| `armed`  | `bool`          | Whether this entry is active |

### TransformStats

**RT-safe:** Yes (pure data structure)

| Field             | Type       | Description                  |
| ----------------- | ---------- | ---------------------------- |
| `applyCycles`     | `uint32_t` | Total apply invocations      |
| `masksApplied`    | `uint32_t` | Successful mask applications |
| `resolveFailures` | `uint32_t` | Target resolution failures   |
| `applyFailures`   | `uint32_t` | Mask application failures    |
| `entriesArmed`    | `uint32_t` | Currently armed entry count  |

### FaultCampaignTprm

**RT-safe:** Yes (292-byte packed POD, loaded once at boot)

| Field        | Type            | Description                   |
| ------------ | --------------- | ----------------------------- |
| `entryCount` | `uint8_t`       | Number of valid entries (0-8) |
| `entries`    | `FaultEntry[8]` | Fault table (36 bytes each)   |

### FaultEntry

| Field              | Type         | Description                         |
| ------------------ | ------------ | ----------------------------------- |
| `targetFullUid`    | `uint32_t`   | Target component fullUid            |
| `targetCategory`   | `uint8_t`    | DataCategory enum value             |
| `targetByteOffset` | `uint16_t`   | Byte offset within data block       |
| `targetByteLen`    | `uint8_t`    | Number of bytes to mutate           |
| `maskType`         | `uint8_t`    | MaskType (ZERO, HIGH, FLIP, CUSTOM) |
| `customAnd`        | `uint8_t[8]` | AND mask (CUSTOM only)              |
| `customXor`        | `uint8_t[8]` | XOR mask (CUSTOM only)              |
| `triggerCycle`     | `uint32_t`   | Cycle at which to inject fault      |
| `durationCycles`   | `uint32_t`   | Hold duration (0 = one-shot)        |

---

## 5. Requirements

| Requirement  | Details                                                                                |
| ------------ | -------------------------------------------------------------------------------------- |
| C++ Standard | C++23                                                                                  |
| Dependencies | `system_core_system_component`, `utilities_data_proxy`, `utilities_concurrency`, `fmt` |
| OS           | None (no OS-specific calls in command path)                                            |
| Component ID | 202 (support component range)                                                          |

---

## 6. Testing

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run DataTransform tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L support
```

### Test Organization

| Test File               | Tests | Coverage                                    |
| ----------------------- | ----- | ------------------------------------------- |
| DataTransform_uTest.cpp | 21    | Commands, mask application, fault campaigns |

---

## 7. See Also

- `src/system/core/infrastructure/system_component/` - SupportComponentBase lifecycle
- `src/utilities/data_proxy/` - ByteMaskProxy for mask operations
- `src/system/core/components/action/` - Action engine (sequences, commands)
- `src/system/core/executive/apex/` - ApexExecutive (registers and schedules support)
