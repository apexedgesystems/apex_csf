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

One data file: the spec-generated USSA76 layered-atmosphere table
(public-domain U.S. government standard content; generated, never
committed; identity by the header spec_hash logged at load — see
docs/HOW_TO_RUN.md). No terrain artifact — this demo's world contract
is the atmosphere.
