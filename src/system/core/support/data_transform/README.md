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

Mask application happens when the action engine sequencer (or a ground
operator) sends APPLY_ENTRY or APPLY_ALL, at the tick the sequencer
chooses.

### Held Masks

An applied mask is held: the `hold()` task writes it again on every tick
it runs, so a fault written into a block whose owner rewrites it each
cycle outlives that rewrite and a consumer sampling once per cycle (the
action engine's watchpoints) sees it. DISARM_ENTRY, CLEAR_MASKS and
CLEAR_ALL end the hold; without a scheduled hold task an apply is a
single write.

Ownership: one mutex serializes the component's commands with the
hold's writes. A command can run on the I/O thread (the interface's
direct dispatch when a component queue is full or its pool exhausted) or
on the task thread (the queue drain, including after an action-phase
wait timed out) while a worker runs `hold()`, so:

- Every command the component handles runs under the lock (GET_STATS,
  ARM, DISARM, the mask pushes, CLEAR_MASKS, CLEAR_ALL, SET_TARGET,
  APPLY_ENTRY, APPLY_ALL); reset and the 1 Hz telemetry's read take it
  too. An APPLY resolves, writes and publishes one snapshot of the
  entry's target and front mask, so a hold writes exactly what the
  apply wrote; a SET_TARGET, ARM, mask push or CLEAR that arrives during
  the apply runs after it. When DISARM_ENTRY, CLEAR_MASKS or CLEAR_ALL
  returns, no hold write of that entry is in progress and none follows
  until a command applies again.
- A command may wait for another command (its resolution and logging
  included) or for one in-progress hold tick. That is a plain mutex
  wait: no fairness and no duration bound is claimed.
- `hold()` never waits for the lock: it tries it and, when a command
  holds it, skips the whole tick and counts it. Under the lock it reads
  the held copies, resolves each target through the registry and writes
  the mask: no allocation, no logging, work bounded by the eight entries,
  one resolver call each and at most 32 bytes each, given a resolver
  that is bounded, does not block and does not call back into the
  component (the lock is not recursive). It is not syscall-free:
  releasing the lock wakes a command waiting for it. Repeated
  contention skips ticks without bound, so the hold promises no
  delivery rate.
- The hold's counters (written, skipped, resolve and apply failures) are
  atomics the 1 Hz telemetry reads; the hold never writes the statistics
  block GET_STATS returns.
- `entry(index)` is preparation before that entry's first APPLY; after
  it, only commands change what a hold writes.

Ordering against the block's owner is the schedule's: run `hold()` on
the same single worker below the owner's step (dispatch is
priority-ordered and a single worker executes serially, as the action
demo does with the step at 127 and the hold at 100), so the admitted
step runs before the admitted hold in a cycle; a skipped or rejected
invocation is not delivered. A schedule with more workers must sequence
the two with matching admission and phase progress; priority alone
orders nothing across workers.

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

**RT-safe:** `handleCommand()` is NOT RT-safe: every opcode runs under the
component's lock and may wait for another command or an in-progress hold
tick, and APPLY resolves and logs. `hold()` never waits for the lock and
its work is bounded, but it is not syscall-free (releasing the lock wakes
a waiting command). `init()`, `reset()`, `loadTprm()`, `onBusReady()`,
`telemetry()` are NOT RT-safe.

| Method                        | RT-Safe | Description                                                                      |
| ----------------------------- | ------- | -------------------------------------------------------------------------------- |
| `setResolver(fn, ctx)`        | Yes     | Wire registry lookup delegate                                                    |
| `setClockFrequency(hz)`       | Yes     | Set clock frequency for ATS time conversion                                      |
| `handleCommand(op, pay, rsp)` | No      | Dispatch commands under the component's lock                                     |
| `loadTprm(dir)`               | No      | Load fault campaign from TPRM binary                                             |
| `onBusReady()`                | No      | Send LOAD_ATS to action engine via bus                                           |
| `telemetry()`                 | No      | Log health stats (1 Hz task)                                                     |
| `hold()`                      | No      | Write every held mask again; never waits for the lock, bounded, not syscall-free |
| `stats()`                     | Yes     | Get diagnostic counters                                                          |
| `entries()`                   | Yes     | Get transform entry table (8 entries)                                            |
| `entry(index)`                | Yes     | Get mutable entry by index                                                       |

### TransformEntry

**RT-safe:** Yes (pure data structure)

| Field    | Type            | Description                  |
| -------- | --------------- | ---------------------------- |
| `target` | `DataTarget`    | Where to apply transforms    |
| `proxy`  | `ByteMaskProxy` | Mask queue for this target   |
| `armed`  | `bool`          | Whether this entry is active |

### TransformStats

**RT-safe:** Yes (pure data structure; spec-defined in `apex_data.toml`, generated into `.auto/`)

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

| Requirement  | Details                                                                                   |
| ------------ | ----------------------------------------------------------------------------------------- |
| C++ Standard | C++23                                                                                     |
| Dependencies | `system_core_system_component`, `utilities_data_proxy`, `utilities_concurrency`, `fmt`    |
| OS           | `std::mutex` on the command and hold paths; no other OS-specific call in the command path |
| Component ID | 202 (support component range)                                                             |

---

## 6. Testing

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run DataTransform tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/hosted-x86_64-debug -L support
```

### Test Organization

| Test File               | Tests | Coverage                                                |
| ----------------------- | ----- | ------------------------------------------------------- |
| DataTransform_uTest.cpp | 30    | Commands, mask application, fault campaigns, held masks |

---

## 7. See Also

- `src/system/core/infrastructure/system_component/` - SupportComponentBase lifecycle
- `src/utilities/data_proxy/` - ByteMaskProxy for mask operations
- `src/system/core/components/action/` - Action engine (sequences, commands)
- `src/system/core/executive/posix/` - ApexExecutive (registers and schedules support)
