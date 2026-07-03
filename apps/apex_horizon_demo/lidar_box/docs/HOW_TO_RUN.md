# lidar_box -- how to run and verify

## Prerequisites

- Debug build: `make compose-debug` (from the repo root).
- Packed TPRM archive: `apps/apex_horizon_demo/lidar_box/tprm/master.tprm`
  (committed; regenerate per the README section 3 if you edit the tomls).

## Run

```bash
docker compose run --rm dev-cuda ./build/hosted-x86_64-debug/bin/ApexLidarBoxDemo \
  --config apps/apex_horizon_demo/lidar_box/tprm/master.tprm \
  --fs-root /tmp/lidar_fs
```

Runs until Ctrl+C (`shutdownAfterSeconds = 0`). Component logs land under
`/tmp/lidar_fs` inside the container; the producer and bridge each emit a 1 Hz
telemetry line.

The dev container shares the host IPC namespace (`ipc: host` on the compose
anchor), so while the demo runs the ring is visible on the **host**:

```bash
ls -la /dev/shm | grep lidar
# lidar_box (1152 bytes) + sem.lidar_box_wake
```

On shutdown the producer (Side A) unlinks both, so a clean exit leaves nothing
behind.

## Verify without a consumer

With no consumer attached the producer fills the 8-slot ring and then refuses
further pushes (`pushes_failed_full` counts up in its telemetry) -- the sim is
unaffected. The ring bytes prove the pipe end to end:

```bash
# Header: framework magic+v1, app "LBOX"+v1, payload 48, capacity 8
xxd -l 24 /dev/shm/lidar_box            # Region A @ 0
xxd -s 576 -l 24 /dev/shm/lidar_box     # Region B @ 576 (same stamp, inert)

# Cursors + latest frame (pose, clearances, timestamp)
python3 - <<'EOF'
import struct
b = open('/dev/shm/lidar_box','rb').read(1152)
head = struct.unpack_from('<Q', b, 64)[0]
f = struct.unpack_from('<10f Q', b, 192 + ((head - 1) % 8) * 48)
print(f"frames pushed: {head}")
print(f"pos=({f[0]:+.3f},{f[1]:+.3f},{f[2]:+.3f}) yaw={f[3]:+.3f}")
print(f"clearances +x/-x={f[4]:.3f}/{f[5]:.3f} +y/-y={f[6]:.3f}/{f[7]:.3f} +z/-z={f[8]:.3f}/{f[9]:.3f}")
print(f"sums (expect 8/6/5): {f[4]+f[5]:.3f} {f[6]+f[7]:.3f} {f[8]+f[9]:.3f}")
EOF
```

The opposite-clearance sums equal the box dimensions exactly -- the closed-form
invariant, checked on the wire.

## Verify with a consumer

Any Side B implementation of the wire format attaches to `/lidar_box`, drains
the ring each tick (the producer cursor then advances at ~50/s), and renders
the body + six rays from the streamed values. The reference consumer is the
horizon UE5 scene (its loopback writer exercises the same layout, so attach is
plug-and-play). The `ShmRingBridge` unit suite also contains a hand-rolled
Side B reader (`fullEndToEndPublish`) demonstrating a minimal consumer.

## Tuning

Edit `tprm/toml/lidar_box_producer.toml` (drift amplitudes / frequencies / yaw
rate / lidar noise) or `lidar_box_bridge.toml` (channel identity / path), then
repack (README section 3) and rerun. Setting `sigma_m > 0` makes the rays
noisy; the noise stream is deterministic per seed.

## Troubleshooting

| Symptom                             | Cause / fix                                                                                                         |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `Config file not found`             | Run from the repo root, or pass an absolute `--config` path.                                                        |
| No `/dev/shm/lidar_box` on the host | The compose service must have `ipc: host` (the dev anchor does); rebuild the container if you overrode it.          |
| Consumer refuses to attach          | Identity mismatch: both region headers must read LBOX/v1, payload 48, capacity 8 -- check with the xxd lines above. |
| Cursor stuck at 8                   | Expected with no consumer (ring full, pushes refused). Attach a consumer or just read the slots.                    |
