# Spec Demo Application Design

## Purpose

ApexSpecDemo is the living compatibility suite for spec-driven
component development: every app component is born from a spec, and
the fleet deliberately covers the taxonomy (SW_MODEL, DRIVER,
SUPPORT), both authoring formats (inline `[[fields]]` TOML and the
apex proto profile), the full type vocabulary (every width, floats,
bool, arrays, bounded strings, string arrays, byte buffers), every
constraint kind, and multi-instance per-instance configuration. A
successful build, boot, and checkout proves the whole codegen chain
-- generated structs, dispatch, once-only stubs, dictionaries,
emitted protobuf interfaces, and layout-hash-stamped TPRMs --
working together in a running executive, weird cases included. No
hardware dependencies; pure SIL on any POSIX host or Raspberry Pi.
The component table lives in the README; per-component design notes
below cover the two physics components in depth.

## Architecture

```
+---------------------------------------------------------------+
|                     ApexSpecDemo (POSIX)                      |
|                                                               |
|  +----------------------+       +----------------------+     |
|  |  SpecSensor          |       |  SpecActuator        |     |
|  |  (0xD400, TOML spec) |       |  (0xD500, proto spec)|     |
|  |  50 Hz step          |       |  50 Hz step          |     |
|  +----------------------+       +----------------------+     |
|                                                               |
|  +----------------------+       +----------------------+     |
|  |  ApexInterface       |       |  SystemMonitor       |     |
|  |  TCP:9000            |       |  (0xC800), 1 Hz      |     |
|  +----------------------+       +----------------------+     |
+---------------------------------------------------------------+
         ^                                    |
         |          TCP + SLIP + APROTO      |
         +------------------------------------+
         |
   Zenith / checkout.py / AprotoClient
```

## The spec-born component

Everything about SpecSensor's data and command surface lives in
`sensor/apex_data.toml`; the three artifact classes divide ownership:

- **`sensor/.auto/`** (generated, always regenerable, check-cdef
  pinned): six packed structs with `static_assert(sizeof)` and
  layout-hash constants, plus `SpecSensorCmdBase_auto.hpp` -- the
  dispatch mixin over the tier base that size-verifies, decodes,
  invokes the `on<Name>` hook, and encodes the response. Malformed
  payloads return `ERROR_PARAM` and unknown opcodes fall through to
  the tier base, both before any user code.
- **`sensor/inc/SpecSensor.hpp`** (generated once by
  `cdef_gen --stub`, user-owned): carries the hand-written part --
  the model physics and handler logic.

### Model physics (the hand-written part)

A measurement drifts away from a calibrated reference at a tunable
rate with tunable LCG noise:

```
value = referenceValue + drift + noise,   drift += driftRate * dt
```

A mode state machine gives every spec command an observable effect:

- **IDLE**: holds the last value (sampling frozen).
- **MEASURE**: samples at 50 Hz.
- **FAULT_INJECT**: adds a +50 bias spike so downstream monitoring
  has something to catch. Reachable only from MEASURE -- the guard
  gives the checkout a rejection path with a counted effect.

### Command surface (from the spec)

| Command     | Opcode | Shape              | Effect                                    |
| ----------- | ------ | ------------------ | ----------------------------------------- |
| SetMode     | 0x0200 | request-only       | Mode transition (guarded)                 |
| Recalibrate | 0x0201 | request + response | New reference via ParamBank, drift zeroed |
| GetStats    | 0x0202 | response-only      | Stats snapshot                            |
| Reset       | 0x0203 | bare               | Counters zeroed, mode to tunable default  |

The four commands deliberately cover all four dispatch shapes the
generator emits.

### Data blocks (from the spec)

- TUNABLE_PARAM (20B): sampleRateHz, referenceValue, driftRate,
  noiseAmplitude, mode -- constraint ranges declared in the spec,
  enforced by the TPRM rails, hash-checked by the ParamBank at load.
- STATE (20B): elapsed, drift, samples, rejects, mode.
- OUTPUT (8B): value, sequence.

## The proto-born component

SpecActuator's layouts are authored the standard protobuf way --
proto3 messages with apex custom options carrying the fixed-layout
metadata (width/count/default as `google.protobuf.FieldOptions`
extensions; see tools/rust/docs/proto_profile.md). The manifest
references the file (`proto_spec`) and keeps commands, telemetry, and
constraints TOML-side; ingest resolves the messages to the same
ordered field lists the sensor's `[[fields]]` arrays produce, so
everything downstream is identical. Both components also emit their
canonical interface as `.auto/<Component>.proto` -- the protobuf
interface comes for free regardless of authoring format.

### Actuator model (the hand-written part)

A position slews toward a commanded target at a tunable rate limit
and settles inside a hold band:

```
position += clamp(target - position, -rateLimit*dt, +rateLimit*dt)
```

- **Move(position)**: sets the target; out-of-range (|p| > 1000)
  rejects and counts (`rejects`).
- **Halt**: target becomes the current position (stop where we are).
- **GetPosition**: position + settling state snapshot.

### Actuator command surface (from the spec)

| Command     | Opcode | Shape         | Effect                       |
| ----------- | ------ | ------------- | ---------------------------- |
| Move        | 0x0210 | request-only  | New target; slew begins      |
| Halt        | 0x0211 | bare          | Target frozen at position    |
| GetPosition | 0x0212 | response-only | Position + settling snapshot |

## Observability model

All registered components get interface command queues, so custom
opcodes are ACKed as "accepted" and processed at drain -- response
payloads do not come back over the wire. The checkout therefore
verifies command _effects_ through INSPECT of the three data blocks;
that is also why the state struct carries `samples`/`rejects`
counters. Wire-level response readback is a framework follow-on
(tracked separately), not a demo concern.

## Scheduling

```
Pool 0
========================
SpecSensor.step       @ 50 Hz (priority 127)
SpecActuator.step     @ 50 Hz (priority 126)
SystemMonitor.tlm     @  1 Hz (priority -128, offset 25)
```

Executive clock: 100 Hz, HARD_PERIOD_COMPLETE.

## Extension path

Phase 3 adds a spec-defined DRIVER-tier component, exercising the
same generation chain through the driver base; the fleet adoption
sweep then follows this app's shape for the existing demos'
fixed-layout components.
