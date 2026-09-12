# rover_mcu — how to run

## Boot modes

`tprm/toml/earth_rover.toml` places the rover: `init_from_grid = 1`
with `init_north_m` / `init_east_m` metres from the anchor
(`anchor_lat_deg` / `anchor_lon_deg`), or `0` for geodetic
`init_lat/lon_deg`. `tprm/toml/rover_controller.toml` sets
`boot_mode`: 0 HOLD (sit), 1 TRAJECTORY (the built-in circle), 2
WAYPOINT (drive to whatever target is commanded). Edit, rebuild the
master (`make debug`), run.

## Drive it from a host shell

The reverse ring accepts APROTO command frames; the branch record
carries a probe (`rover_ring_probe.py`) that sends them and reads the
frame's reserved tail back. With the demo running:

```bash
python3 rover_ring_probe.py start 1      # START_RTS_BY_ID 1 (sequence 1)
python3 rover_ring_probe.py watch 20 1.0 # 20 s of frame bytes, once a second
```

Only one consumer may drain the ring: stop the probe before a
visualizer attaches.

## The sequence catalog (`tprm/toml/rts/`)

Stable ids; one visualizer button per id. Legs wait on the
controller's ARRIVED byte; every sequence brackets itself with
SET_SEQ_STATE so the frame narrates it.

| Id  | Sequence                                                                                                    |
| --- | ----------------------------------------------------------------------------------------------------------- |
| 1   | north 10 m, arrive, HOLD                                                                                    |
| 2   | north 10 m → east 20 m, lamp 1 green steady on arrival                                                      |
| 3   | as 2, then lamp 1 green at 10 Hz for 5 s, off, HOLD                                                         |
| 4   | out and back: 15 m north, 30 m east, lamp 2 red 1 Hz while moving, lamp 1 green 10 Hz 5 s at the far corner |
| 5   | guarded 30 m square, lamp 1 blue steady; the boundary may preempt                                           |
| 6   | manual halt (panic): HALT, both lamps red 5 Hz, seq_state 0x15                                              |
| 7   | resume: RESUME, HOLD, lamps off, seq_state idle                                                             |
| 32  | obstacle halt (lidar centre ray < 15 m): reason 0x11, lamps red 5 Hz                                        |
| 33  | geofence halt (±200 m about the anchor): reason 0x12                                                        |
| 34  | slope halt (terrain slip): reason 0x13                                                                      |

Corners are arcs: the driven plant is a steered vehicle (heading rate
v·tan δ / wheelbase; a 1.5 m wheelbase and 33° lock give a minimum
radius of about 2.3 m; 1.5 m/s² acceleration, 2 m/s² braking), and the
controller steers by pure pursuit to a 3.5 m lookahead with a
trapezoidal speed profile at 3 m/s cruise, so each leg ramps up,
cruises, and brakes onto its target inside 0.2 m without pivoting in
place. Legs must be longer than the turning radius;
`tprm/toml/rover_controller.toml` holds every one of those numbers.

After a boundary halt, RESUME (7) then a target inside the fence
drives the rover home; the watchpoints fire on the predicate's
rising edge only (`minFireCount 0`), so a sustained breach starts
its halt once.

Lamp codes: colour {0 off, 1 red, 2 green, 3 blue, 4 yellow, 5
white}; rate {0 steady, 1 0.5 Hz, 2 1 Hz, 3 2 Hz, 4 5 Hz, 5 10 Hz}.
The producer runs the strobe; the frame carries the live bit.

## Upload a sequence while it runs (mission upload)

A sequence that is not in the boot catalog is compiled, transferred
over the TCP ops interface into the active bank's `rts/` directory,
the engine rescans its catalog, and the sequence starts by id. The
ops interface listens on 127.0.0.1:9000 inside the producer's
container, so run the script from there:

```bash
docker exec -i -w /home/kalex/workspace rover_mcu \
  tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/upload_rts.py \
  demos/apex_horizon_demo/rover_mcu/tprm/upload/rts_008_uploaded_beacon.toml --slot 10 --start
```

`Action_0.log` shows `Catalog scanned: 11 RTS` then `RTS started:
id=8`. The uploaded id lives until the next boot repacks the bank.

## Trace and plots

While a sequence runs the rover samples itself at 20 Hz and the 1 Hz
telemetry task drains `SEQTRACE ...` lines into
`logs/models/GroundVehicle_0.log` (sequence id and leg, grid
north/east, heading, speed, nearest lidar return, slope, lamp bits),
ending with `SEQTRACE end dropped=N`. The branch record's
`seqtrace_to_csv.py` cuts one CSV per take for the plots.

## Logs

Under the filesystem root: `system.log` (executive), `logs/core/Action_0.log`
(sequence starts, step timeouts, catalog scans), `logs/models/GroundVehicle_0.log`
and `RoverController_0.log` (1 Hz lines), `logs/support/ShmRingBridge_0.log`
(`rx_cmds=received/decode_errors/dispatch_errors`).
