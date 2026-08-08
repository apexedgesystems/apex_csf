# Spec Demo Application Design

## Purpose

ApexSpecDemo is the acceptance vehicle for spec-driven component
development: its only app component is born entirely from
`sensor/apex_data.toml`, so a successful build, boot, and checkout
proves the whole codegen chain -- generated structs, generated command
dispatch, the once-only stub, the ground dictionary, and the
layout-hash-stamped TPRM -- working together in a running executive.
No hardware dependencies; pure SIL on any POSIX host or Raspberry Pi.

## Architecture

```
+---------------------------------------------------------------+
|                     ApexSpecDemo (POSIX)                      |
|                                                               |
|  +----------------------+       +----------------------+     |
|  |  SpecSensor          |       |  SystemMonitor       |     |
|  |  (0xD400, spec-born) |       |  (0xC800)            |     |
|  |  50 Hz step          |       |  1 Hz                |     |
|  +----------------------+       +----------------------+     |
|                                                               |
|  +----------------------+                                     |
|  |  ApexInterface       |                                     |
|  |  TCP:9000            |                                     |
|  +----------------------+                                     |
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
SystemMonitor.tlm     @  1 Hz (priority -128, offset 25)
```

Executive clock: 100 Hz, HARD_PERIOD_COMPLETE.

## Extension path

Phase 3 adds a spec-defined DRIVER-tier component beside the sensor,
exercising the same generation chain through the driver base; the
fleet adoption sweep then follows this app's shape for the existing
demos' fixed-layout components.
