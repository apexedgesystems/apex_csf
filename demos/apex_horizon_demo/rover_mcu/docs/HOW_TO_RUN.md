# rover_mcu — how to run

## Boot modes

`tprm/toml/earth_rover.toml` places the rover: `init_from_grid = 1`
with `init_north_m` / `init_east_m` metres from the anchor
(`anchor_lat_deg` / `anchor_lon_deg`), or `0` for geodetic
`init_lat/lon_deg`.

What the rover does at boot is decided by the ATS bank first and the
controller's `boot_mode` second. As shipped, `tprm/toml/ats/ats_010_boot.toml`
is packed into ATS slot 32 (`ats 32` in `tprm/tprm.manifest`) and armed;
the executive starts it once the components are up, and its one step
asks the action engine to START_RTS_BY_ID the sequence named in its
payload -- RTS 1 (north 10 m) as shipped, bytes `[1,0]` of
`commandPayload`. To boot a different sequence, change that payload. To
boot with no sequence, set its `armed` to 0 or remove the `ats 32` row;
the controller then sits in `tprm/toml/rover_controller.toml`'s
`boot_mode`: 0 HOLD (sit), 1 TRAJECTORY (the built-in circle), 2
WAYPOINT (drive to whatever target is commanded). A sequence started by
the ATS sets the mode itself, so `boot_mode` only matters without one.
Edit, rebuild the master (`make debug`), run.

## Drive it from a host shell

The reverse ring accepts APROTO command frames;
`scripts/rover_ring_probe.py` (standard library only) sends them and
reads the frame's reserved tail back. The dev containers share the host
IPC namespace, so with the demo running, from the repository root on
the host:

```bash
P=demos/apex_horizon_demo/rover_mcu/scripts/rover_ring_probe.py
python3 $P start 1       # START_RTS_BY_ID 1 (sequence 1)
python3 $P watch 20 1.0  # 20 frames, once a second: position and tail
python3 $P halt          # HALT the rover; resume releases it
```

`watch` drains the frame ring as its consumer, and only one consumer
may drain it: stop the probe before a visualizer attaches.

## The sequence catalog (`tprm/toml/rts/`)

Stable ids; one visualizer button per id. Legs wait on the
controller's ARRIVED byte; every sequence brackets itself with
SET_SEQ_STATE so the frame narrates it. One sequence runs at a time:
the executive puts every catalog RTS in one exclusion group, so
starting a sequence stops the running one (a halt cancels the tour
it interrupts, a resume replaces the halt, a tour started over
another restarts it). A leg that cannot arrive inside its 60 s
ceiling aborts the tour. While halted the plant accepts only HALT and
RESUME (lamps, mode, targets and sequence state answer EXEC_FAILED),
so the alarm lamps and the halt reason stay on the frame whatever is
started until RESUME.

| Id  | Sequence                                                                                                    |
| --- | ----------------------------------------------------------------------------------------------------------- |
| 1   | north 10 m, arrive, HOLD                                                                                    |
| 2   | north 10 m → east 20 m, lamp 1 green steady on arrival                                                      |
| 3   | as 2, then lamp 1 green at 10 Hz for 5 s, off, HOLD                                                         |
| 4   | out and back: 15 m north, 30 m east, lamp 2 red 1 Hz while moving, lamp 1 green 10 Hz 5 s at the far corner |
| 5   | guarded 30 m square, lamp 1 blue steady; the boundary may preempt                                           |
| 6   | manual halt (panic): both lamps red 5 Hz, then HALT; seq_state 0x15                                         |
| 7   | resume: RESUME, HOLD, lamps off, seq_state idle                                                             |
| 9   | home: absolute target (0, 0), the anchor; brings the rover back from wherever the relative legs left it     |
| 32  | obstacle halt (closest lidar return in the frame < 15 m): reason 0x11, lamps red 5 Hz                       |
| 33  | geofence halt (±200 m about the anchor): reason 0x12                                                        |
| 34  | slope halt (terrain slip): reason 0x13                                                                      |
| 35  | link-lost halt (the board stopped answering): reason 0x14; holds after the link returns until resume (7)    |
| 36  | sensor-lost halt (the board stopped receiving lidar scans): reason 0x16; holds until resume (7)             |

Corners are arcs: the driven plant is a steered vehicle (heading rate
v·tan δ / wheelbase; a 1.5 m wheelbase and 33° lock give a minimum
radius of about 2.3 m; 1.5 m/s² acceleration, 2 m/s² braking), and the
controller steers by pure pursuit along the leg line (the aim point
sits 2.5 m ahead on the line from where the leg started to its
target, so an off-axis start converges onto the line and arrives
aligned with it: the next leg starts square) with a trapezoidal speed
profile at 3 m/s cruise and 0.6 m/s through a corner, with the lock
limited by speed so the heading never turns faster than 15°/s (a 90°
turn takes about 6 s), so each leg ramps up,
cruises, and brakes onto its target inside 0.2 m without pivoting in
place. A leg arrives inside 0.2 m, or once the rover has passed its end within 0.5 m of the target. A rover that is not lined up with a leg (more than 60° off it) with less than about 7 m left, which is two turning radii plus the lookahead, swings out through a keyhole first: it drives to the line 7 m short of the target, turns onto it and drives the rest straight, so a 2 m leg entered facing away takes up to a minute. Every catalog leg is 10 m or longer and never needs the keyhole;
`tprm/toml/rover_controller.toml` holds every one of those numbers.

After a boundary halt, RESUME (7) then home (9) drives the rover
back to the anchor; the watchpoints fire on the predicate's rising
edge only (`minFireCount 0`), so a sustained breach starts its halt
once.

Lamp codes: colour {0 off, 1 red, 2 green, 3 blue, 4 yellow, 5
white}; rate {0 steady, 1 0.5 Hz, 2 1 Hz, 3 2 Hz, 4 5 Hz, 5 10 Hz}.
The producer runs the strobe; the frame carries the live bit.

## Upload a sequence while it runs (mission upload)

A sequence that is not in the boot catalog is compiled, transferred
over the TCP ops interface into the active bank's `rts/` directory,
the engine rescans its catalog, and the sequence starts by id. The
script needs the build tree's `cfg2bin` and the tools venv, so run it
inside the producer's container (started as `rover_mcu`, see
[the console's endpoint](#the-consoles-endpoint-on-the-host)), where
it connects to the ops interface at 127.0.0.1:9000:

```bash
docker exec -i -w /home/$USER/workspace rover_mcu \
  tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/upload_rts.py \
  demos/apex_horizon_demo/rover_mcu/tprm/upload/rts_008_uploaded_beacon.toml --start
```

The slot is the bank's file name. The boot catalog packs 000 to 012,
so the upload needs one past them; left out, the script takes the
lowest slot `tprm.manifest` leaves free (013 here) and names it, and a
`--slot` the manifest packs is refused rather than replacing the
sequence that boots there.

`Action_0.log` shows `Catalog scanned: 14 RTS` then `RTS started:
id=8`; the uploaded id joins the exclusion group like the rest
(starting another sequence over it logs `stopping RTS 8`). The
uploaded id lives until the next boot repacks the bank.

## Pushed telemetry (the console target)

The producer pushes four blocks over the ops interface (TCP/SLIP;
127.0.0.1:9000 inside the container, 127.0.0.1:9010 on the host)
without being asked: the rover's 256-byte ROVR/2 OUTPUT
frame at 20 Hz, the controller's OUTPUT at 10 Hz, and the board
link's and the lidar model's STATE (counters and the board's
heartbeat; those two drivers have no OUTPUT block) at 5 Hz. `tprm/toml/telemetry_manager.toml` is
the table (fullUid, category, opcode, rate divisor against the 20 Hz
collect); a console that decodes a packet by its (fullUid, size)
sees every field of the block by name. The frame's tail bytes 232..253
are named fields of the frame (`board_link`, `seq_state`,
`lidar_nearest_m`, ...), and the lidar fan's rays are per-element
channels (`lidar_range_m[0]`..`[15]`, `lidar_hit[0]`..`[15]`), so the
"Lidar rays" layout in `app_data.toml` plots the eight the demo
sweeps.

### The console's endpoint on the host

Inside its container the interface listens on `0.0.0.0:9000`
(`tprm/toml/interface.toml`), so a client in the container connects to
127.0.0.1:9000; nothing reaches it from the host unless the container
publishes the port. The supported host endpoint is
**127.0.0.1:9010**, host loopback only, so a console on this machine
connects there and nothing off the machine can:

```bash
docker compose run -d --rm --name rover_mcu -p 127.0.0.1:9010:9000 dev-cuda \
  ./build/hosted-x86_64-debug/bin/ApexRoverMcuDemo \
  --config build/hosted-x86_64-debug/demos/apex_horizon_demo/rover_mcu/exec/tprm/master.tprm \
  --fs-root /home/$USER/workspace/build/rover_mcu_fs --skip-cleanup \
  --rt-mode lag-tolerant --rt-max-lag 200
```

This is the quick start's run command, detached, named `rover_mcu` and
with the port published; stop it with `docker stop rover_mcu`. From the
host, `push_probe.py --host 127.0.0.1 --port 9010` then sees the same
four streams, and a command sent to 127.0.0.1:9010 is answered.

To watch the pushes arrive without a console, from inside the
producer's container (127.0.0.1:9000, the script's default):

```bash
docker exec -i -w /home/$USER/workspace rover_mcu \
  tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/push_probe.py --seconds 5
```

It connects, counts every packet by (fullUid, opcode, size) for the
window, reports the rates, and checks that the frame's named tail
decodes to the same bytes a consumer reading offsets 232..253 sees.

## Run the controller on the board (NUCLEO-F767ZI)

The rover's controller runs on a NUCLEO-F767ZI connected by its one USB
cable: the ST-Link virtual COM port carries the link (USART3, 115200
8N1) and the programmer. The firmware runs the same guidance law the
host tests pin (`rover_controller/inc/RoverGuidance.hpp`); the plant,
the sequences and the bridge stay in the producer.

Build and flash:

```bash
make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_STM32_BOARD=nucleo_f767zi"
st-flash --connect-under-reset --reset write \
  build/mcu-stm32-relwithdebinfo/firmware/rover_mcu_firmware.bin 0x08000000
```

The firmware sleeps between ticks, so a plain `st-flash` (and
`make compose-stm32-flash`) cannot attach once it runs: connect under
reset. An stm32 build directory holds one board; move
`build/mcu-stm32-relwithdebinfo` aside before building for another. At
boot the three user LEDs light red, green and blue in turn; after that
they show lamp 1 (LD3 red, LD1 green, LD2 blue; yellow is red + green,
white is all three) with its strobe.

`tprm/toml/rover_board_link.toml` selects the drive source:
`enabled = 1` (the default) the board computes the drive and the host
law runs beside it as a shadow; `enabled = 0` the host law drives and
the port is never opened. Plug the board in before starting the
producer: its container sees the host's devices as they were when it
started.

What shows the board is driving:

- `logs/drivers/RoverBoardLink_0.log`: `port open`, `link NEVER -> UP`,
  then a 1 Hz line with frames each way, heartbeats, CRC refusals,
  sequence gaps, and the board's cycle count, step count, tick time and
  load.
- The frame's board bytes: `board_link` (tail @232, 0 NEVER, 1 UP,
  2 LOST), `board_load_pct` (@245), `board_tick` (@246..247) advancing
  at 20 Hz while the board answers.
- `logs/models/RoverController_0.log`: the board's command next to the
  host law's (`host: steer ... thr ...`).

If the board stops answering for 500 ms the link reads LOST, the
controller drives zero, and the board-link watchpoint starts sequence 35
(reason 0x14, lamps red 5 Hz), which cancels any running tour. When the
board answers again the link reads UP; resume (7) releases the halt.

## The lidar on its own wire

The rover's lidar reaches the board the way a real sensor would: over
its own serial line. `RoverLidarModel` (0xE400, a hardware model) turns
each plant sweep (10 Hz) into one `LIDAR_SCAN` frame (scan number, a hit
bit and a range per ray, 20 bytes, SLIP + CRC-16) and writes it to a
USB-serial adapter wired to the board's USART6. The board keeps the last
scan and reports what it saw in every `CONTROL_CMD`: the sensor state
(UP, or STALE once scans stop for 500 ms), the scan number, the hit bits
and the closest return. The frame carries that picture at offsets
250..253, and the obstacle and sensor-lost watchpoints read it.

Wiring (adapter jumper at 3.3 V; VCC not connected):

| Adapter | Board | Signal          |
| ------- | ----- | --------------- |
| TXD     | D0    | PG9, USART6_RX  |
| RXD     | D1    | PG14, USART6_TX |
| GND     | GND   | ground          |

`tprm/toml/rover_lidar_model.toml` names the port (`/dev/ttyUSB0`),
the sensor's range (50 m: a ray returning farther reads as no return)
and `enabled`. Plug the adapter in before starting the producer, as with
the board. With `enabled = 0`, or with no board reporting a sensor, the
frame's lidar bytes carry the plant's own sweep (state 0) and the
obstacle halt still works.

| Frame byte | Meaning                                                        |
| ---------- | -------------------------------------------------------------- |
| 250        | lidar state: 0 plant sweep, 1 UP (what the board saw), 2 STALE |
| 251        | hit bits, ray 0 the left edge of the fan                       |
| 252        | closest return, whole metres; 255 none                         |
| 253        | scan number, low byte (moves at 10 Hz while scans flow)        |

Check it from zenith while the rover sits still:

```bash
curl -s "localhost:8080/api/targets/<id>/inspect/0x00DE00?category=4&offset=250&length=4"
```

The SEQTRACE lines carry the frame's lidar fields (`lstate`, `lhits`,
`lnear`, `lscan`) beside the plant's own sweep (`sweep`), so a run plots
the world next to what reached the board.

## Trace and plots

While a sequence runs the rover samples itself at 20 Hz and the 1 Hz
telemetry task drains `SEQTRACE ...` lines into
`logs/models/GroundVehicle_0.log` (sequence id and leg, grid
north/east, heading, speed, nearest lidar return, slope, lamp bits),
ending with `SEQTRACE end dropped=N`. `scripts/seqtrace_to_csv.py`
(standard library only) cuts one CSV per take for plotting. The drain
writes pending samples before the pending end marker, so a take started
within a second of the last one ending can log its first samples ahead of
that take's marker, and one marker can count two takes' drops. The
extractor starts a new take wherever the take clock `t` fails to advance
(the plant restarts it at 0 for each take) or a different running id
appears; halt states never split a take. The log cannot say which take
the next marker closes, so both takes at such a split are written as
`seq<S>_take<i>_uncertain.csv` with a `.txt` note naming the lines and the
reason, and a shared nonzero drop count is reported as unknown. `--strict`
instead writes nothing and exits 2 naming the first such line:

```bash
python3 demos/apex_horizon_demo/rover_mcu/scripts/seqtrace_to_csv.py \
  build/rover_mcu_fs/logs/models/GroundVehicle_0.log build/rover_mcu_trace
```

## Logs

Under the filesystem root: `system.log` (executive), `logs/core/Action_0.log`
(sequence starts, step timeouts, catalog scans), `logs/models/GroundVehicle_0.log`
and `RoverController_0.log` (1 Hz lines), `logs/support/ShmRingBridge_0.log`
(`rx_cmds=received/decode_errors/dispatch_errors`).
