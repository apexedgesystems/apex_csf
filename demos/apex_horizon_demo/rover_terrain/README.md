# rover_terrain — kinematic rover over generated terrain, streamed over shm

The second producer demo over the shared-memory ring bridge: a
GroundVehicle drives the Earth htile terrain patch while two
WorldQueryProbes exercise the Earth and Moon environments, and a
ShmRingBridge streams the rover's 256-byte ROVR/1 frame to
`/horizon_rover` at 10 Hz for an out-of-process visualizer. Runs
headless just as happily — with no consumer attached the ring fills
and back-pressures; the sim is unaffected.

## 1. Quick start

```bash
# One-time: generate the terrain + atmosphere artifacts (see
# docs/HOW_TO_RUN.md — the data files are generated, never committed).

# Run (Ctrl+C to stop)
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/bin/ApexRoverTerrainDemo \
  --config demos/apex_horizon_demo/rover_terrain/tprm/master.tprm \
  --fs-root /tmp/rover_fs
```

While it runs, `/dev/shm/horizon_rover` (+ `sem.horizon_rover_wake`)
exists on the host, and any consumer that speaks the wire format can
attach. See [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) for the
no-consumer verification recipe.

## 2. What it composes

| Piece              | Where                                   | Role                                                             |
| ------------------ | --------------------------------------- | ---------------------------------------------------------------- |
| CelestialBody ×2   | src/sim/environment/celestial_body      | Earth (J2 + HTILE + LAYERED USSA76), Moon (J2 + sphere + vacuum) |
| WorldQueryProbe ×2 | ../world_query_probe                    | 1 Hz world-sanity sampling per body                              |
| GroundVehicle      | ../ground_vehicle                       | 10 Hz kinematic rover + lidar over Earth's terrain               |
| ShmRingBridge      | src/system/core/support/shm_ring_bridge | ROVR/1 egress to /horizon_rover                                  |

Scheduler priorities order vehicleStep (50) before bridgeStep (40)
inside each 10 Hz tick, so the bridge always streams the frame written
that tick.

## 3. Configuration (TPRM)

| File (`tprm/toml/`)                    | Component            | Highlights                                                  |
| -------------------------------------- | -------------------- | ----------------------------------------------------------- |
| `executive.toml`                       | executive (0x000000) | 50 Hz clock, RT mode, run until interrupted                 |
| `scheduler.toml`                       | scheduler (0x000100) | 5 tasks: rover 10 Hz, bridge 10 Hz, probes + rover log 1 Hz |
| `earth_body.toml`                      | earth (0xDC00)       | J2 + HTILE patch + LAYERED USSA76 (generated data paths)    |
| `moon_body.toml`                       | moon (0xDC01)        | analytic J2 + sphere + vacuum                               |
| `earth_probe.toml` / `moon_probe.toml` | probes (0xDD00/01)   | survey points per body                                      |
| `earth_rover.toml`                     | rover (0xDE00)       | init pose centered on the patch, 6 deg/s steer, 8-ray lidar |
| `rover_bridge.toml`                    | bridge (0xCB00)      | ROVR/1, `/horizon_rover`, payload 256/4, capacity 16        |

Regenerate after editing any TOML (cfg2bin renames + tprm_pack; see
docs/HOW_TO_RUN.md).

## 4. Wire contract (ROVR/1)

256-byte RoverFrame, frozen: the layout is pinned field-by-field by
static_asserts in the GroundVehicle unit suite, and the consumer side
byte-diffs its contract header against ours at pairing. Capacity 16,
reverse ring 4-byte inert placeholder, 10 Hz.
