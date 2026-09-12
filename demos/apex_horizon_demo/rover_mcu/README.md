# rover_mcu — the rover with a controller behind a seam, lamps, and a sequence catalog

The rover demo in the form that moves its control loop onto a
microcontroller: the plant (a kinematic rover with a lidar fan over
the shared Earth terrain tile) and the sequence engine run in this
POSIX apex app; the drive controller writes the plant's drive-command
block through a seam that a UART driver takes over in the
hardware-in-the-loop form. Five A→B tours of rising complexity, two
lamps as sequence actions, a manual halt and resume, and a safety
boundary (lidar obstacle, ±200 m geofence, terrain slip) that halts
the rover with a reason the frame names. A ShmRingBridge streams the
256-byte ROVR/2 frame at 100 Hz to `/horizon_rover` for an
out-of-process visualizer and drains its commands back; the demo
runs headless just as happily.

## 1. Quick start

```bash
# Build (the master packs the sequence catalog and the shared earth world)
docker compose run --rm dev-cuda make debug

# Run (Ctrl+C to stop); lag-tolerant RT for shared hosts
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/bin/ApexRoverMcuDemo \
  --config build/hosted-x86_64-debug/demos/apex_horizon_demo/rover_mcu/exec/tprm/master.tprm \
  --fs-root /home/kalex/workspace/build/rover_mcu_fs --skip-cleanup \
  --rt-mode lag-tolerant --rt-max-lag 200
```

Boot: `Sequence catalog: 10 entries`, `world bound: ... entries=2`, the
controller HOLDING at the grid anchor (39.5 N, −105.5 W, the terrain
patch centre) heading north. See [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md)
for driving it from a host shell, the sequence catalog, the upload
path, and the trace.

## 2. What it composes

| Piece           | Where                                   | Role                                                                    |
| --------------- | --------------------------------------- | ----------------------------------------------------------------------- |
| CelestialBody   | src/sim/environment/celestial_body      | Earth bound to the shared world (J2, HTILE terrain, LAYERED atmosphere) |
| GroundVehicle   | ../ground_vehicle                       | 100 Hz kinematic rover + lidar; lamps; the drive-command seam; trace    |
| RoverController | ../rover_controller                     | 10 Hz HOLD / TRAJECTORY / WAYPOINT law on a north/east grid             |
| Action engine   | src/system/core/components/action       | Sequence catalog (standalone RTS) + the boundary watchpoints            |
| ShmRingBridge   | src/system/core/support/shm_ring_bridge | ROVR/2 bidirectional link on /horizon_rover at 100 Hz                   |

Scheduler order inside a tick: controller (priority 60) before the
plant (50) before the bridge (40).

## 3. Wire contract (ROVR/2, frame layout unchanged)

The 256-byte `GroundVehicleTelemetry` frame is the ROVR/2 contract
the visualizer pins; this demo writes the reserved tail at offset
232 (byte map and codes in
[ground_vehicle/inc/GroundVehicleCommand.hpp](../ground_vehicle/inc/GroundVehicleCommand.hpp)):
controller mode (HALTED under a plant-level halt), sequence state
(running id, or 0x10|reason for a halt), active/total waypoints, the
lamps' live bits and commanded colour/rate codes, and the result and
opcode of the last command. Commands on the reverse ring:
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
- Live: the joint V&V thread with the visualizer (sequences, lamps,
  rejections, the geofence, upload) is recorded with the branch.
