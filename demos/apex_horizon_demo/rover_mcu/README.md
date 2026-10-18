# rover_mcu — the rover with a controller behind a seam, lamps, and a sequence catalog

The rover demo in the form that moves its control loop onto a
microcontroller: the plant (a kinematic rover with a lidar fan over
the shared Earth terrain tile) and the sequence engine run in this
POSIX apex app; the drive command reaches the plant's drive-command
block through one seam, computed either by the host law or by the same
law running on a NUCLEO-F767ZI over its USB serial link. Five A→B tours of rising complexity (10–30 m legs driven as
arcs by a steered plant and a pure-pursuit controller), two lamps as
sequence actions, a manual halt and resume, and a safety
boundary (lidar obstacle, ±200 m geofence, terrain slip, board link lost,
lidar lost) that halts
the rover with a reason the frame names. A ShmRingBridge streams the
256-byte ROVR/2 frame at 100 Hz to `/horizon_rover` for an
out-of-process visualizer and drains its commands back; the demo
runs headless just as happily.

## 1. Quick start

The master packs the shared earth world, whose two data files are
generated and never committed. Provide them once, before configuring
the build, following the
[world-artifact setup](../rover_terrain/docs/HOW_TO_RUN.md#one-time-setup-the-world-artifacts),
at the paths `worlds.manifest` names:

- `demos/apex_horizon_demo/aircraft_atmo/data/earth_ussa76.atm`
- `demos/apex_horizon_demo/rover_terrain/data/alpha_patch_lod4.htile`

Without them everything else builds, but configure leaves the demo's
master.tprm out of the build and names the missing files; add them and
run `make debug` again (it re-runs CMake before building).

```bash
# Build (the master packs the sequence catalog and the shared earth world)
docker compose run --rm dev-cuda make debug

# Run (Ctrl+C to stop); lag-tolerant RT for shared hosts
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/bin/ApexRoverMcuDemo \
  --config build/hosted-x86_64-debug/demos/apex_horizon_demo/rover_mcu/exec/tprm/master.tprm \
  --fs-root /home/$USER/workspace/build/rover_mcu_fs --skip-cleanup \
  --rt-mode lag-tolerant --rt-max-lag 200
```

Boot: `Sequence catalog: 13 entries`, `world bound: ... entries=2`,
`boot sequence: ATS id=10 slot=32 started`, and the rover drives
sequence 1 (north 10 m) from the grid anchor (39.5 N, −105.5 W, the
terrain patch centre). With the ATS disarmed or unpacked the controller
sits in its `boot_mode` instead (HOLD as shipped); see the boot modes in
the run guide. See [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md)
for driving it from a host shell, the sequence catalog, the upload
path, the console endpoint, running the controller on the board, and
the trace. The helper scripts it uses live in [scripts/](scripts/).

## 2. What it composes

| Piece           | Where                                   | Role                                                                          |
| --------------- | --------------------------------------- | ----------------------------------------------------------------------------- |
| CelestialBody   | src/sim/environment/celestial_body      | Earth bound to the shared world (J2, HTILE terrain, LAYERED atmosphere)       |
| GroundVehicle   | ../ground_vehicle                       | 100 Hz steered rover + lidar; lamps; the drive-command seam; trace            |
| RoverController | ../rover_controller                     | 20 Hz HOLD / TRAJECTORY / leg-line pure pursuit; forwards the board's command |
| RoverBoardLink  | ./board_link                            | 20 Hz serial link to the board: state out, command and heartbeat in           |
| RoverLidarModel | ./lidar_model                           | 10 Hz lidar scans on their own wire to the board (a hardware model)           |
| Board firmware  | ./board/firmware                        | The same law on the NUCLEO-F767ZI; lidar scans on USART6; lamp 1; heartbeat   |
| Action engine   | src/system/core/components/action       | Sequence catalog (standalone RTS) + the boundary watchpoints                  |
| ShmRingBridge   | src/system/core/support/shm_ring_bridge | ROVR/2 bidirectional link on /horizon_rover at 100 Hz                         |

Scheduler order inside a tick: board link (priority 70) before the
controller (60) before the plant (50) before the lidar model (45) before
the bridge (40).

## 3. Wire contract (ROVR/2, frame layout unchanged)

The 256-byte `GroundVehicleTelemetry` frame is the ROVR/2 contract
the visualizer pins; this demo writes the reserved tail at offset
232 (byte map and codes in
[ground_vehicle/inc/GroundVehicleCommand.hpp](../ground_vehicle/inc/GroundVehicleCommand.hpp)):
controller mode (HALTED under a plant-level halt), sequence state
(running id, or 0x10|reason for a halt), active/total waypoints, the
lamps' live bits and commanded colour/rate codes, the result and
opcode of the last command, the board link's state, load and tick, and
the lidar picture the rover acts on (state, hit bits, closest return,
scan number: what the board saw, or the plant's own sweep without a
board sensor). Commands on the reverse ring:
HALT / RESUME / SET_THROTTLE, SET_MODE, SET_TARGET_REL / ABS
(single-precision north/east metres), SET_LED (lamp, colour, rate),
SET_SEQ_STATE (sequences only), and START_RTS_BY_ID to the action
engine. Every command is accepted whole or rejected whole; while a
sequence runs, wire targets answer BUSY (sequences drive through
their own twin opcodes).

## 4. Testing

- Unit suites (`make testp`): `TestGroundVehicle` (frame offsets 232..249
  pinned, command bounds and whole-rejection, result stamping, lamp
  strobe frame counts, step-rate scaling, boot seed, trace capture),
  `TestApexHorizonDemoRoverController` (seam, HOLD/TRAJECTORY,
  waypoint legs at demo scale, wire adoption).
- Live: with the demo running, the run guide's walkthroughs exercise
  sequences, lamps, rejections, the geofence and upload against a
  visualizer or the scripts in `scripts/`.
