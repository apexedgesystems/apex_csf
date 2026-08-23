# rover_terrain — how to run and verify

## One-time setup: the world artifacts

The demo consumes two data files with different handling:

- `data/alpha_patch_lod4.htile` — the 1-degree terrain patch
  (39–40 N, 106–105 W), **generated, never committed**. Its content is
  entirely synthetic: the world-generation pipeline's terrain tool
  renders it from a tracked sample spec for a _fictional_ rocky body
  ("alpha" — procedural fBm/warp terrain, made-up physical constants).
  No real-world elevation data feeds it, by policy AND by
  construction: real-world datasets stay in the producer pipeline's
  private sandbox and are never converted into distributable
  artifacts. Generate it with the producer-side world tooling and copy
  it into `data/` (gitignored). The htile header carries a
  `spec_hash` both sides of a pairing log at attach — regenerating
  from the same spec yields a bit-identical artifact, which is the
  file-identity proof for paired runs.
- `data/earth_ussa76.atm` — **generated, never committed**: the
  spec-generated USSA76 layered table (public-domain U.S. government
  standard), produced by the same producer-side world tooling
  (compose-aircraft-world-artifacts) and copied into `data/`.
  Bit-deterministic: 288 bytes, header spec_hash
  0x753549717ab8b8ff, logged at load as the identity proof.

## Run (raw binary — the dev loop)

The TPRM master generates at build time from `tprm/tprm.manifest`
(edit a toml, rebuild, done — no packed binaries are committed):

```bash
TPRM=build/hosted-x86_64-debug/demos/apex_horizon_demo/rover_terrain/exec/tprm
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/bin/ApexRoverTerrainDemo \
  --config $TPRM/master.tprm \
  --fs-root /tmp/rover_fs
```

Runs until Ctrl+C. Logs land under the filesystem root
(`logs/models/GroundVehicle_0.log` has the 1 Hz rover line;
`logs/support/ShmRingBridge_0.log` shows the channel-open result).

## Verify without a consumer

While it runs, the ring is visible on the host (the dev container
shares the host IPC namespace):

```bash
ls -la /dev/shm | grep rover      # horizon_rover + sem.horizon_rover_wake
```

```python
import struct, time
d = open('/dev/shm/horizon_rover', 'rb').read()
print(struct.unpack_from('<IH', d, 0))   # ring-format magic + version
s1 = open('/dev/shm/horizon_rover', 'rb').read(); time.sleep(0.4)
s2 = open('/dev/shm/horizon_rover', 'rb').read()
print('frames flowing:', s1 != s2)
```

With no consumer the ring fills to capacity (16 frames, ~1.6 s after
the first push) and back-pressures; a consumer draining the ring keeps
frames flowing continuously. The GroundVehicle log independently shows
the rover driving: elevation ~1.2 km on the patch, slope a few
degrees, heading advancing at the steer rate.

## Drive the rover from the host (command channel)

The reverse ring accepts APROTO command frames — this emulates the
visualizer's HALT button from a host shell while the demo runs:

```python
import mmap, struct
f = open('/dev/shm/horizon_rover', 'r+b')
m = mmap.mmap(f.fileno(), 0)
B = 192 + 16*256                      # region B (reverse ring) base
PROD, SLOTS = B + 64, B + 192
head = struct.unpack_from('<Q', m, PROD)[0]
frame = struct.pack('<HBBIHHH', 0x5041, 1, 0,  # "AP", ver 1, flags 0
                    0x0000DE00,                # dst: the rover
                    0x0100,                    # HALT (0x0101 RESUME)
                    0, 0)                      # sequence, payload_len
m.seek(SLOTS + (head & 15)*32); m.write(frame)
struct.pack_into('<Q', m, PROD, head + 1)
```

Within a second the GroundVehicle log shows `spd` decaying to zero
with the heading frozen, and the bridge's 1 Hz health line counts the
command (`rx_cmds=1/0/0`). Send `0x0101` to resume. SET_THROTTLE
(`0x0102`) takes one payload byte (percent 0–100): pack it after the
header and set payload_len to 1.

## Troubleshooting

| Symptom                           | Cause                                     | Fix                                                                             |
| --------------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------- |
| CelestialBody init fails on Earth | data files missing                        | run One-time setup                                                              |
| Bridge logs "channel open FAILED" | shm_path empty/not absolute in tprm       | check rover_bridge.toml, repack                                                 |
| Components run with defaults      | master passed via a wrong flag            | use `--config <generated master.tprm>`                                          |
| Ring exists but never changes     | sampled after saturation with no consumer | expected back-pressure; attach a consumer or sample within ~1.6 s of first push |
