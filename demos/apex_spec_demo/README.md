# ApexSpecDemo

A minimal application whose only component is **born from a spec**: the
environment sensor's data structures, command dispatch, ground dictionary,
and component skeleton all derive from one file,
[sensor/apex_data.toml](sensor/apex_data.toml). The demo exists to prove the
spec-driven development path end to end -- if this app builds, boots, and
passes its checkout, the codegen chain works.

## The three-artifact model

| Artifact                    | Generated         | Owned by     | Regenerated                                  |
| --------------------------- | ----------------- | ------------ | -------------------------------------------- |
| `sensor/.auto/`             | `cdef_gen`        | the spec     | always (`make cdef`, pinned by `check-cdef`) |
| `sensor/inc/SpecSensor.hpp` | `cdef_gen --stub` | **the user** | never (the tool refuses to overwrite)        |
| `.ovr/` overrides           | hand-written      | the user     | never                                        |

- **`.auto/` headers** carry the packed structs (with `static_assert(sizeof)`
  and the layout-hash constant the TPRM v3 prelude is checked against) and
  the command-dispatch base `SpecSensorCmdBase<TBase>`: a tier-agnostic mixin
  whose `handleCommand` verifies each spec-declared command's payload size,
  decodes the request struct, invokes the pure-virtual `on<Name>` hook, and
  encodes the response. Malformed payloads and unknown opcodes never reach
  user logic (unknowns fall through to the tier base).
- **The stub** is generated once as a compilable skeleton -- identity,
  ParamBank wired to the generated layout hash, a registered step task, and
  hook overrides -- then filled in by hand. Here it carries the sensor
  physics: a measurement drifting from a calibrated reference at a tunable
  rate, with a mode state machine (IDLE / MEASURE / FAULT_INJECT).

## What the spec defines

- **Structs + fields**: tunables, state, output, and command payloads as
  ordered `[[fields.<Struct>]]` arrays (name/type/size/default/doc).
- **Constraints**: legal ranges enforced by the TPRM constraint rails.
- **Commands**: `[[commands]]` entries (name/opcode/request/response/doc)
  covering all four dispatch shapes -- request+response (`Recalibrate`),
  request-only (`SetMode`), response-only (`GetStats`), bare (`Reset`).
- **Telemetry**: `[[telemetry]]` streams naming the published structs.

From that one file the build derives:

- `sensor/.auto/*.hpp` -- structs + dispatch (`make cdef` / `make check-cdef`)
- `SpecSensor.json` ground dictionary with structs, commands, and telemetry
  (`make apex-data-db`)
- TPRM templates and the packed `master.tprm` with layout-hash-stamped
  payloads (`apex_tprm_ApexSpecDemo`, part of the default build)

## Layout

```
sensor/        spec (apex_data.toml), .auto/ generated headers, inc/ user stub
exec/          SpecExecutive + main (executive, scheduler, system monitor, sensor)
tprm/          manifest + authored TOML parameter sets
scripts/       checkout.py -- live verification of the full spec surface
docs/          HOW_TO_RUN, SPEC_DEMO_DESIGN, RESULTS, DEPLOY_PROCEDURE
app_data.toml  application descriptor for Zenith target generation
release.mk     release-packaging manifest (RPi posix target)
```

- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- build, run, and check out the app
- [docs/SPEC_DEMO_DESIGN.md](docs/SPEC_DEMO_DESIGN.md) -- architecture and the spec-born component
- [docs/RESULTS.md](docs/RESULTS.md) -- baseline checkout results
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- release and Raspberry Pi deploy
