# aircraft_atmo — how to run and verify

## The world data

The demo consumes ONE data file: the spec-generated USSA76 atmosphere
table (`data/earth_ussa76.atm`, gitignored — generated world data
stays out of this repo). Generate it with the producer-side world
tooling (the compose-aircraft-world-artifacts target) and copy it into
`data/`. Regeneration is bit-deterministic from the tracked spec:
expect exactly 288 bytes with header
`spec_hash = 0x753549717ab8b8ff`, and the boot log's
`atmosphere artifact:` line printing that hash is the file-identity
proof paired runs compare.

## Run (packaged deployment — the standard way)

The app declares an apex deployment, so it stages into a
self-contained package that runs with zero arguments. Launch through
the `cuda-rt` compose service: it carries the rtprio rlimit, so the
executive's FIFO thread table applies (clock 90 / tasks 80 —
burst-free cadence under load).

```bash
# Stage the package (once per build)
docker compose run --rm dev-cuda \
  cmake --build build/hosted-x86_64-debug --target package_ApexAircraftAtmoDemo

# Run it (zero arguments; run.sh resolves the executive + bundled TPRM)
docker compose run --rm cuda-rt \
  ./build/hosted-x86_64-debug/packages/ApexAircraftAtmoDemo/run.sh
```

For a long-lived detached producer, add `-d --name <name>` to the
`compose run` and stop it later with `docker rm -f <name>`. A raw
`docker compose run --rm cuda-rt ./build/.../ApexAircraftAtmoDemo
--config <master.tprm> --fs-root <path>` on the unpackaged binary
remains valid for ad-hoc debugging (`dev-cuda` works too when RT
scheduling doesn't matter).

## Verify the boot

Three log greps prove the launch is the binary + world you intended
(guards against a stale binary or wrong config):

```bash
grep -o 'dt=10ms' <fs-root>/logs/models/Aircraft_0.log            # compiled step matches 100 Hz
grep -o 'spec_hash=0x[0-9a-f]*' <fs-root>/logs/models/CelestialBody_0.log
ps -T -o cls,rtprio,comm -p <pid> | grep -E 'exec_clock|exec_tasks' # FF 90 / FF 80 under cuda-rt
```

The heartbeat (`<fs-root>/heartbeat.csv`) should show clock/task
parity within a few ticks.

Runs until Ctrl+C (foreground) or `docker rm -f`. Logs land under the filesystem root:
`logs/models/Aircraft_0.log` has the 1 Hz flight line (pose, airdata,
forces, fuel, gusts); `logs/models/AircraftController_0.log` the
reference-tracking line; `logs/support/ShmRingBridge_0.log` the
channel + command counters.

## Verify without a consumer

The boot smoke-check line pins the atmosphere shape
(`rho_sl=1.2250`, `rho@12192m=0.3016`), and within ~30 s the flight
line shows closed-loop capture: altitude within meters of 12192 m,
airspeed approaching 235.9 m/s, heading turning to 135 deg at the
0.45 rad bank limit. The ring on `/dev/shm/horizon_aircraft`
saturates (16 frames) and back-pressures until a consumer drains.

## Drive the aircraft from the host (command channel)

Emulates the visualizer's toggles from a host shell while the demo
runs — turbulence off:

```python
import mmap, struct
f = open('/dev/shm/horizon_aircraft', 'r+b')
m = mmap.mmap(f.fileno(), 0)
B = 192 + 16*256                       # region B (reverse ring) base
PROD, SLOTS = B + 64, B + 192
head = struct.unpack_from('<Q', m, PROD)[0]
frame = struct.pack('<HBBIHHHB', 0x5041, 1, 0,  # "AP", ver 1, flags 0
                    0x0000E000,                 # dst: the aircraft
                    0x0100,                     # SET_TURBULENCE_ENABLE
                    0, 1, 0)                    # seq, payload_len=1, value=0
m.seek(SLOTS + (head & 15)*256); m.write(frame)
struct.pack_into('<Q', m, PROD, head + 1)
```

Within a second the Aircraft log shows
`cmd: SET_TURBULENCE_ENABLE ON -> OFF`, the gust triple pins to
(+0.00,+0.00,+0.00), and the bridge health line counts the command
(`rx_cmds=1/0/0`). Value 1 re-enables.

## Troubleshooting

| Symptom                           | Cause                               | Fix                                                            |
| --------------------------------- | ----------------------------------- | -------------------------------------------------------------- |
| CelestialBody init fails on Earth | atmosphere table missing            | see The world data above                                       |
| Bridge logs "channel open FAILED" | shm_path empty/not absolute in tprm | check aircraft_bridge.toml, repack                             |
| Components run with defaults      | master passed via a wrong flag      | use `--config <generated master.tprm>`                         |
| Ring exists but never changes     | saturation with no consumer         | expected back-pressure; attach or sample within ~0.3 s of boot |
