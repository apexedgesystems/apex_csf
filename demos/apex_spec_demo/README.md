# ApexSpecDemo

The **living compatibility suite** for spec-driven component development:
every app component is born from a spec, and together the fleet covers
the component taxonomy, both authoring formats, and the full spec
vocabulary. If this app builds, boots, and passes its checkout, the
codegen chain works -- including the weird cases.

| Component                    | fullUid   | Tier     | Spec  | Proves                                                    |
| ---------------------------- | --------- | -------- | ----- | --------------------------------------------------------- |
| [SpecSensor](sensor/)        | 0xD400    | SW_MODEL | TOML  | Drift physics, mode machine, all four dispatch shapes     |
| [SpecActuator](actuator/)    | 0xD500    | SW_MODEL | proto | Proto authoring, slew physics, bounded string             |
| [SpecBusDriver](bus_driver/) | 0xD600    | DRIVER   | TOML  | Driver-tier dispatch, loopback round-trips                |
| [SpecMatrix](matrix/)        | 0xD700    | SUPPORT  | TOML  | Support-tier dispatch, full type vocabulary in one struct |
| [SpecLimits](limits/)        | 0xD800    | SW_MODEL | TOML  | Every constraint kind at its rail                         |
| [SpecProtoMax](proto_max/)   | 0xD900    | SW_MODEL | proto | Every proto-profile feature (the copyable reference)      |
| [SpecChannel](channel/)      | 0xDA00/01 | SW_MODEL | TOML  | One spec, two instances, per-instance TPRM                |

Data structures, command dispatch, ground dictionaries, protobuf
interfaces ([apex profile](../../tools/rust/docs/proto_profile.md)), and
component skeletons all derive from the specs.

## The three-artifact model

| Artifact                    | Generated         | Owned by     | Regenerated                                  |
| --------------------------- | ----------------- | ------------ | -------------------------------------------- |
| `sensor/.auto/`             | `cdef_gen`        | the spec     | always (`make cdef`, pinned by `check-cdef`) |
| `sensor/inc/SpecSensor.hpp` | `cdef_gen --stub` | **the user** | never (the tool refuses to overwrite)        |
| `.ovr/` overrides           | hand-written      | the user     | never                                        |

- **`.auto/` headers** carry the packed structs (with `static_assert(sizeof)`
  and the layout-hash constant the TPRM v3 prelude is checked against) and
  the generated component base `<C>SpecBase<TDerived, TBase>`: a CRTP mixin
  over the derived component and its tier base that owns the categorized
  data members (ParamBank + State/Output blocks), the hash-enforcing
  `loadTprm` (publishes the first generation, applies on reload), the
  `[[tasks]]`-driven `doInit`, and the command dispatch. Malformed payloads
  and unknown opcodes never reach user logic; `validateParams` /
  `onParamsLoaded` / `onInit` hooks carry user policy.
- **The stub** is generated once as a compilable skeleton -- identity, task
  methods, and hook overrides -- then filled in by hand. Here it carries the
  sensor physics: a measurement drifting from a calibrated reference at a
  tunable rate, with a mode state machine (IDLE / MEASURE / FAULT_INJECT).

## What the spec defines

- **Structs + fields**: tunables, state, output, and command payloads as
  ordered `[[fields.<Struct>]]` arrays (name/type/size/default/doc).
- **Constraints**: legal ranges enforced by the TPRM constraint rails.
- **Commands**: `[[commands]]` entries (name/opcode/request/response/doc)
  covering all four dispatch shapes -- request+response (`Recalibrate`),
  request-only (`SetMode`), response-only (`GetStats`), bare (`Reset`).
- **Tasks**: `[[tasks]]` entries (name/uid/doc) -- identity only; the
  generated `doInit` binds the same-named derived method, while timing
  stays in the scheduler TOML.
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
