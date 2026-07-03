# ShmRingBridge

**Namespace:** `system_core::support`
**Library:** `system_core_support_shm_ring_bridge`
**Platform:** Linux (POSIX shared memory + named semaphore)
**C++ Standard:** C++23

Optional support component that publishes registered apex data to a
shared-memory ring (and optionally drains commands from a reverse ring)
for out-of-process consumers. The bridge owns the **Side A** (producer)
role of a self-described wire format; that format is **defined by this
component and documented in [section 5](#5-wire-format-specification)** --
this README is the single source of truth.

Any consumer implements the format on its end: a UE5 visualization, a
recorder, a web dashboard, a second simulator. There is no shared code,
header, or library with any consumer -- apex has zero compile- or
link-time dependency on them. The wire format is the only contract.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Common Workflows](#3-common-workflows)
4. [API Reference](#4-api-reference)
5. [Wire Format Specification](#5-wire-format-specification)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Start

```cpp
#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridge.hpp"

system_core::support::ShmRingBridge bridge;

// Wire the registry resolver (same delegate signature as DataTransform).
bridge.setResolver(
    [](void* ctx, std::uint32_t uid, system_core::data::DataCategory cat)
        -> system_core::support::ResolvedSource {
      auto* reg = static_cast<ComponentRegistry*>(ctx);
      // ... lookup logic ...
      return {dataPtr, dataSize};
    },
    &registry);

// Tunables come from TPRM at the standard {fullUid:06x}.tprm path.
executive.registerSupport(&bridge);
```

Once `onBusReady()` runs (after the registry is populated), the bridge
opens its shm region + wakeup semaphore, resolves the configured
source `DataTarget`, and starts memcpying source bytes to the ring
each `bridgeStep` tick. Failures here are non-fatal: the sim keeps
running while `channel_open` stays at 0.

---

## 2. Key Features

### Self-described wire format

- The wire format -- layout, cursor semantics, semaphore naming, and
  version-checking rules -- is defined and documented here, in
  [section 5](#5-wire-format-specification).
- Consumers implement it independently: no shared headers, no shared
  `.so`, no shared types. Same file-format-style decoupling apex applies
  to `.htile`, `.atm`, `.bin` -- the format is the contract, not a shared
  library.

### Source-by-DataTarget addressing

Reuses apex's standard `DataTarget` idiom (also used by `DataTransform`
for fault injection): tunables specify a `source_uid`, `source_category`,
`source_byte_offset`, and `source_byte_len`, and a registry-lookup
delegate (`BridgeResolveDelegate`) resolves them to a live byte pointer.
The bridge memcpys those bytes; it is intentionally agnostic to the
source struct.

This means **any** registered OUTPUT (or any `DataCategory`) can be
published. The producer model doesn't have to know the bridge exists,
and the bridge doesn't have to know the producer's struct type.

### Optional + idle-on-failure

- Anything that prevents channel open (bad path, bad capacity, source
  not yet registered, length mismatch) keeps `channel_open == 0` but
  does NOT fail init. The sim runs, just nothing publishes.
- If no consumer attaches, the producer keeps writing happily; the
  ring's FULL state is observable via `pushes_failed_full` but never
  blocks the bridgeStep tick.
- Side A owns the kernel objects: `closeChannel()` unlinks both the
  shm region and the semaphore so the next process start is clean --
  unless the path has been re-owned by another process (see below), in
  which case the foreign region is left untouched.

### External unlink detection

POSIX lets any process unlink the shm path; after that, Side A's writes land in
an orphaned mapping no consumer can open, with every counter still reading
healthy. The 1 Hz `telemetry` task therefore probes the path by name and
compares the inode identity against the object the bridge created:

- **Unlinked or replaced** -> `region_orphaned = 1` in state + telemetry, one
  loud log line. `bridgeStep` is untouched (no RT cost) and stays non-fatal.
- **`orphan_reclaim = 1`** (tunable) -> if the name is _free_ (ENOENT), the
  bridge reopens the channel on it and clears the flag. A name re-owned by
  another process is never fought over -- flagged only, and shutdown will not
  unlink it.

### Two scheduled tasks

| Task         | Default rate | Purpose                                                                                                                                   |
| ------------ | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `bridgeStep` | configurable | Memcpy source -> ring slot, signal wakeup. RT-safe (~us). Optionally drains 1 command from Ring B + dispatches via `postInternalCommand`. |
| `telemetry`  | 1 Hz         | Log health stats. NOT RT-safe (uses fmt::format).                                                                                         |

The `bridgeStep` rate is set in `tprm/toml/scheduler.toml` per the
standard apex pattern, not in the bridge's tunables.

### Ring B command sink (opt-in)

The bridge ships with a Side A _consumer_ path for Ring B (the
reverse direction of the two-ring layout (section 5)). Opt in by
setting `sink_enabled = 1` in the bridge's tunables; the standard
apex_horizon_demo `earth_aircraft_bridge.toml` does this.

When enabled, `bridgeStep` pops at most one **APROTO application-layer
frame** from Ring B per tick (header + variable payload, slot-bounded
-- no SLIP/COBS), validates magic + version + size, then dispatches
through `internalBus()->postInternalCommand(this.fullUid, frame.fullUid,
frame.opcode, payload)`. The dispatched command lands at the target
component's `handleCommand(opcode, payload, response)` callback on the
next dispatcher tick -- _the same callback that handles TCP/APROTO
commands via `ApexInterface`_. APROTO is treated as a protocol, not a
transport: SHM is just a second transport on the same routing fabric.

State counters (ShmRingBridgeState):

- `cmds_received` -- frames successfully decoded + dispatched
- `cmds_decode_errors` -- bad magic / version / oversized payload
- `cmds_dispatch_errors` -- `postInternalCommand` returned false

Rate-limited to one frame per `bridgeStep` to keep dispatch RT-bounded.
Bursts catch up across ticks; the internal-bus queue absorbs the slack.

See [section 5](#5-wire-format-specification) for the Ring B wire format
and [`ShmRingBridge_uTest.cpp`](utst/ShmRingBridge_uTest.cpp) for end-to-end
unit tests (synthetic Side B writer + mock IInternalBus).

Default `sink_enabled = 0` -- existing apps see no behavior change.

---

## 3. Common Workflows

### Publishing a model's OUTPUT to UE5

1. Pick a wire-format struct (e.g. `RoverFrame` from horizon's
   `demos/rover_terrain/contract/RoverContract.hpp`).
2. Make the producer model's `OUTPUT`-category data byte-identical
   with the wire struct (`static_assert(sizeof == sizeof)`). This
   is the cleanest pattern -- the bridge is pure memcpy.
3. Configure `ShmRingBridgeTunables`:
   - `app_magic` / `app_version` = wire-format ID (e.g.
     `'ROVR'` / 1)
   - `payload_size` = `sizeof(RoverFrame)`
   - `source_uid` = the producer's `fullUid()`
   - `source_category` = `DataCategory::OUTPUT` (4)
   - `source_byte_offset` = 0
   - `source_byte_len` = `sizeof(RoverFrame)` (or 0 for whole block)
   - `shm_path` = absolute POSIX shm name (e.g. `/horizon_rover`)
   - `wakeup_path` = empty (auto-derives `/horizon_rover_wake`)
4. Schedule `bridgeStep` at the desired rate in
   `tprm/toml/scheduler.toml`.
5. Schedule `telemetry` at 1 Hz on a stagger offset.

### Multiple bridges in one executive

One bridge instance per (shm_path, app) pair. To publish two distinct
streams (e.g. rover state + camera frames), instantiate two
`ShmRingBridge` components with different `shm_path` + different
source targets. Each gets its own `fullUid` (the executive auto-
assigns instance indices).

---

## 4. API Reference

### ShmRingBridge

| Method                 | RT-Safe | Description                                            |
| ---------------------- | ------- | ------------------------------------------------------ |
| `setResolver(fn, ctx)` | Yes     | Wire the registry-lookup delegate                      |
| `tunables()`           | Yes     | Mutable `TunableParam<ShmRingBridgeTunables>`          |
| `bridgeState()`        | Yes     | Read-only `ShmRingBridgeState` (counters, flags)       |
| `telemetry()`          | Yes     | Read-only `ShmRingBridgeTlm` (32-byte packed snapshot) |
| `bridgeStep()`         | Yes     | One frame: memcpy source -> slot, sem_post             |
| `telemetryTick()`      | No      | 1 Hz health log (uses fmt)                             |
| `loadTprm(dir)`        | No      | Load tunables from `{tprmDir}/{fullUid:06x}.tprm`      |
| `onBusReady()`         | No      | Open shm + sem, resolve source                         |

### ShmRingBridgeTunables (176 bytes)

| Field                  | Type       | Default | Description                                                                    |
| ---------------------- | ---------- | ------- | ------------------------------------------------------------------------------ |
| `app_magic`            | `uint32_t` | 0       | 4-byte wire-format ID (e.g. 0x524F5652 = "ROVR")                               |
| `app_version`          | `uint16_t` | 1       | Bump on payload layout change                                                  |
| `capacity`             | `uint32_t` | 1024    | Slots per direction; **must be power of two**                                  |
| `payload_size`         | `uint32_t` | 0       | Forward-direction slot size; MUST equal effective source length                |
| `reverse_payload_size` | `uint32_t` | 0       | Reverse-direction slot size; 0 = same as `payload_size` (symmetric).           |
|                        |            |         | Asymmetric demos (e.g. consumer uses a small "EmptyFrame" for the unused       |
|                        |            |         | reverse direction) MUST set this to match `sizeof(consumer's Out)` or the      |
|                        |            |         | consumer's `fstat()` will fail with `PAYLOAD_SIZE_MISMATCH`.                   |
| `source_uid`           | `uint32_t` | 0       | Source component fullUid                                                       |
| `source_category`      | `uint8_t`  | 0       | `DataCategory` enum value (typically OUTPUT=4)                                 |
| `source_byte_offset`   | `uint16_t` | 0       | Start byte within the source data block                                        |
| `source_byte_len`      | `uint16_t` | 0       | Bytes to copy; 0 = whole block                                                 |
| `sink_enabled`         | `uint8_t`  | 0       | 1 = drain Ring B + dispatch (see the command sink)                             |
| `orphan_reclaim`       | `uint8_t`  | 0       | 1 = reopen the channel if the shm path vanishes (never fights a re-owned path) |
| `shm_path[64]`         | char[]     | empty   | Absolute POSIX shm path (must start with '/')                                  |
| `wakeup_path[64]`      | char[]     | empty   | Sem path; empty = derive `shm_path + "_wake"`                                  |
| `label[16]`            | char[]     | empty   | Tag for log lines                                                              |

### ShmRingBridgeState

| Field                | Type       | Description                                 |
| -------------------- | ---------- | ------------------------------------------- |
| `tick_count`         | `uint64_t` | bridgeStep invocations (incl idle no-ops)   |
| `frames_published`   | `uint64_t` | Successful pushes                           |
| `pushes_failed_full` | `uint64_t` | Pushes refused because consumer fell behind |
| `signals_failed`     | `uint64_t` | sem_post failures (should stay 0)           |
| `channel_open`       | `uint8_t`  | 1 once shm + sem are open                   |
| `source_resolved`    | `uint8_t`  | 1 once the source DataTarget resolves       |
| `region_orphaned`    | `uint8_t`  | 1 = shm path unlinked/replaced externally   |

### ShmRingBridgeTlm (32 bytes, packed)

Same fields as `ShmRingBridgeState` minus `tick_count`; refreshed each
`telemetryTick`.

---

## 5. Wire Format Specification

This is the authoritative definition of the format. A consumer that
implements it interoperates with no shared code.

### 5.1 Region layout

The shm object holds two independent SPSC rings back to back:

```
  +-------------------- Ring A (forward: apex -> consumer) --------------------+
  | prelude (192 B) | slot[0] | slot[1] | ... | slot[capacity-1]              |
  +-------------------- Ring B (reverse: consumer -> apex) --------------------+
  | prelude (192 B) | slot[0] | slot[1] | ... | slot[capacity-1]              |
```

Each region is `192 + capacity * payload_size` bytes (Ring B uses
`reverse_payload_size`). `capacity` must be a power of two. The shm object
is `ftruncate`d to the sum of both region sizes.

### 5.2 Per-region prelude (192 bytes)

```
  offset 0    RingHeader   (64 B)
  offset 64   producer cursor: std::atomic<uint64_t> HEAD  (64 B, cache-line)
  offset 128  consumer cursor: std::atomic<uint64_t> TAIL  (64 B, cache-line)
  offset 192  first slot
```

`RingHeader` carries: a fixed 4-byte framework magic `0x48524E42` (a
constant stamp; its byte spelling is historical provenance), framework
version `1`, the app-supplied `app_magic` / `app_version`, `capacity`, and
`payload_size`. A consumer validates magic + framework version + payload
size on attach and rejects a mismatch.

### 5.3 Push / pop semantics

Producer push: relaxed-load HEAD, acquire-load TAIL, write the slot at
`HEAD & (capacity-1)`, then release-store `HEAD + 1`. FULL (HEAD - TAIL ==
capacity) is refused, not blocked. Consumer pop mirrors this from the other
cursor. Ring B carries APROTO application-layer frames (one frame per slot;
the slot boundary is the frame boundary -- no SLIP/COBS).

### 5.4 Side A (producer) lifecycle

- **Open:** unlink-then-create the shm object, `ftruncate` to both regions,
  `mmap`, lay out both region preludes, zero the cursors.
- **Wakeup semaphore:** path defaults to `shm_path + "_wake"` if unset;
  Side A does `sem_unlink` then `sem_open(O_CREAT | O_EXCL, 0600, 0)`, and
  `sem_post`s after each Ring A push so a blocked consumer wakes.
- **Close:** `munmap` + `sem_close` + `shm_unlink` + `sem_unlink`, idempotent
  -- Side A owns the kernel objects so the next process start is clean.

### 5.5 Conformance test

`ShmRingBridge_uTest.cpp::fullEndToEndPublish` attaches a hand-rolled Side B
reader (independent of this code, format-only) and verifies that what apex
writes is what any conformant consumer reads -- header validation, per-frame
payload bytes, and the wakeup count. This is the cross-implementation
conformance test.

---

## 6. Requirements

| Requirement  | Details                                                                             |
| ------------ | ----------------------------------------------------------------------------------- |
| C++ Standard | C++23                                                                               |
| Dependencies | `system_core_system_component`, `utilities_concurrency`, `utilities_helpers`, `fmt` |
| OS           | Linux (POSIX shm + sem)                                                             |
| Component ID | 203 (support component range)                                                       |

### Container deployment

If apex runs inside a docker container and the consumer (e.g. UE5
plugin) runs on the host (or in a different container), the producer's
`/dev/shm` is otherwise isolated and the bridge silently fails to
connect. Add `ipc: host` to the docker-compose service so the container
shares the host's IPC namespace:

```yaml
x-common: &common
  ...
  ipc: host  # share host's /dev/shm so bridge shm crosses the boundary
```

apex_csf's `docker-compose.yml` sets `ipc: host` on the shared service
anchor, so a container-hosted producer can reach a host (or
other-container) consumer out of the box.

---

## 7. Testing

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run ShmRingBridge tests only
docker compose run --rm -T dev-cuda \
  ctest --test-dir build/native-linux-debug -L shm_ring_bridge
```

### Test Organization

| Test File               | Tests | Coverage                                                        |
| ----------------------- | ----- | --------------------------------------------------------------- |
| ShmRingBridge_uTest.cpp | 8     | Identity, default-idle, validation, full E2E publish, FULL ring |

`fullEndToEndPublish` is the headline test: configures real shm + sem,
writes 5 frames, hand-rolled Side B reader (spec-only, no horizon
link) sees correct header + correct frames + correct wakeup count.
This is the cross-implementation conformance test.

---

## 8. See Also

- [section 5](#5-wire-format-specification) - the wire-format spec (this README is the single source of truth)
- `src/system/core/support/data_transform/` - precedent for `DataTarget` + resolver pattern
- `src/system/core/support/system_monitor/` - precedent for SUPPORT layout
- `src/system/core/components/interface/` - the OTHER external transport (TCP+APROTO for cmd/tlm; complementary, not overlapping)
