# aircraft_atmo — closed-loop 6DOF transport over shm

The third producer demo over the shared-memory ring bridge: a
four-engine wide-body transport flies full 6DOF at 50 Hz under its
six-loop autopilot, through a layered standard atmosphere with Dryden
turbulence, while a ShmRingBridge carries the ACFT/2 link on
`/horizon_aircraft` — 256-byte AircraftFrames out, APROTO commands in
(turbulence + gust-alleviation toggles). Runs headless just as happily
— with no consumer attached the ring back-pressures, command ingress
stays live, and the sim is unaffected.

## 1. Quick start

```bash
# Stage the package, then run it (zero arguments) through the RT
# compose service -- see docs/HOW_TO_RUN.md for the identity checks
docker compose run --rm dev-cuda \
  cmake --build build/hosted-x86_64-debug --target package_ApexAircraftAtmoDemo
docker compose run --rm cuda-rt \
  ./build/hosted-x86_64-debug/packages/ApexAircraftAtmoDemo/run.sh
```

## 2. Components

| Component          | Source                                  | Role                                       |
| ------------------ | --------------------------------------- | ------------------------------------------ |
| CelestialBody      | src/sim/environment/celestial_body      | J2 gravity + ellipsoid + USSA76 atmosphere |
| Aircraft           | ../aircraft                             | 6DOF transport, ACFT frame, drive toggles  |
| AircraftController | ../aircraft_controller                  | six-loop autopilot, per-loop enables       |
| ShmRingBridge      | src/system/core/support/shm_ring_bridge | ACFT/2 bidirectional link                  |

## 3. TPRM

Sources under `tprm/toml/`; `tprm/tprm.manifest` is the packing
recipe; the master generates at build time (edit a toml, rebuild, no
packed binaries committed). Executive: 50 Hz fundamental, full-schema
block including the thread table. Scheduler: six tasks in Hz —
controller 25 Hz (prio 60) ahead of the aircraft 50 Hz (prio 50)
ahead of the bridge 50 Hz (prio 40); each component's step/telemetry
pair shares its sequencing group so the 50 Hz tasks never race their
own telemetry.

## 4. Wire contract (ACFT/2 — frame layout frozen since v1)

Forward: 256-byte AircraftFrame, frozen — layout pinned field-by-field
by static_asserts in the aircraft unit suite; the consumer byte-diffs
its contract header against ours at pairing. Capacity 16, 50 Hz.

Reverse: 256-byte slots carrying APROTO command frames (14-byte
header + payload). The aircraft (0x00E000) accepts:

| Opcode   | Command                     | Payload           |
| -------- | --------------------------- | ----------------- |
| `0x0100` | SET_TURBULENCE_ENABLE       | 1 byte, 0/1       |
| `0x0101` | SET_GUST_ALLEVIATION_ENABLE | 1 byte, 0/1       |
| `0x0102` | GET_COMMAND_STATE           | none (16-B reply) |

Mode-demonstration machinery (scripted excitations + per-loop damper
switches) is built and unit-tested producer-side; its wire exposure is
the ACFT/2 contract rev.

## 5. The world data

The shared earth world (`worlds.manifest`, bundle uid 0x010100) carries
two entries: the spec-generated USSA76 layered-atmosphere table this
demo binds (public-domain U.S. government standard content; identity
by the header spec_hash logged at load — see docs/HOW_TO_RUN.md) and
the rover demo's terrain tile, which this demo never loads (its world
contract is the atmosphere; terrain fidelity stays ELLIPSOID). The
`world_pin` in `tprm/toml/earth_body.toml` is the bundle's content
hash over both entries; a bundle that hashes differently is refused
at init and the executive refuses the boot.

## 6. Testing

- Unit suites (`make testp`): the wire pins (`AircraftWire_uTest`:
  snapshot offsets 2/3, frame offsets 202/203/204, size 256), the
  closed-loop command-surface suite in `../aircraft_controller/utst`
  (mask adoption and rejection, excitation lifecycle, orchestration
  mirror and recovery counting, boot seed, the two-phase recovery
  recapture), and the aero mode anchors in `src/sim/aerodynamics/utst`
  (short period, Dutch roll, spiral, phugoid closed forms).
- Benchmark (`bin/ptests/ApexHorizonDemoAircraftController_PTEST`):
  per-tick cost of the plant with the mode trace idle and armed, and
  of the 25 Hz closed-loop tick — the perf record for the RT path
  (trace adds under 1 µs to a 10 ms step on the hosted debug preset).
- Live: the criterion takes in docs/HOW_TO_RUN.md (clean air, both
  sides measuring the same take).
