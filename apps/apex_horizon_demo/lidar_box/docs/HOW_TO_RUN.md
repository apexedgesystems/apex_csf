# lidar_box -- how to run and verify

## Prerequisites

- Debug build: `make compose-debug` (from the repo root).
- Packed TPRM archive: `apps/apex_horizon_demo/lidar_box/tprm/master.tprm`
  (committed; regenerate per the README section 3 if you edit the tomls).

## Run (packaged deployment -- the standard way)

The app declares an apex deployment, so it stages into a self-contained package
(`bank_a/{bin,libs,tprm}` + the generic launcher) and runs with **zero
arguments** -- `run.sh` resolves the executive, uses the deployment directory as
the filesystem root, and wires `--config bank_a/tprm/master.tprm` itself:

```bash
# Stage the package (once per build)
docker compose run --rm dev-cuda \
  cmake --build build/hosted-x86_64-debug --target package_ApexLidarBoxDemo

# Run it (Ctrl+C to stop)
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/packages/ApexLidarBoxDemo/run.sh
```

Binaries self-locate their shared libraries via an `$ORIGIN/../libs` RPATH, so
the package also runs on any Linux host outside the container (producer and
consumer just need a shared `/dev/shm`). Logs, banks, and telemetry land inside
the deployment directory.

## Run (raw binary -- the dev loop)

```bash
docker compose run --rm dev-cuda ./build/hosted-x86_64-debug/bin/ApexLidarBoxDemo \
  --config apps/apex_horizon_demo/lidar_box/tprm/master.tprm \
  --fs-root /tmp/lidar_fs
```

Both forms run until Ctrl+C (`shutdownAfterSeconds = 0`). The producer and
bridge each emit a 1 Hz telemetry line to the component logs under the
filesystem root.

The dev container shares the host IPC namespace (`ipc: host` on the compose
anchor), so while the demo runs the ring is visible on the **host**:

```bash
ls -la /dev/shm | grep lidar
# lidar_box (1408 bytes) + sem.lidar_box_wake
```

On shutdown the producer (Side A) unlinks both, so a clean exit leaves nothing
behind.

## Verify without a consumer

With no consumer attached the producer fills the 8-slot ring and then refuses
further pushes (`pushes_failed_full` counts up in its telemetry) -- the sim is
unaffected. The ring bytes prove the pipe end to end:

```bash
# Header: framework magic+v1, app "LBOX"+v2, payload 64 (0x40), capacity 8
xxd -l 24 /dev/shm/lidar_box            # Region A @ 0
xxd -s 704 -l 24 /dev/shm/lidar_box     # Region B @ 704 (same stamp, inert)

# Cursors + latest frame (pose, mounted distances, scene block)
python3 - <<'EOF'
import struct
b = open('/dev/shm/lidar_box','rb').read(1408)
head = struct.unpack_from('<Q', b, 64)[0]
f = struct.unpack_from('<10f Q 4f', b, 192 + ((head - 1) % 8) * 64)
print(f"frames pushed: {head}")
print(f"pos=({f[0]:+.3f},{f[1]:+.3f},{f[2]:+.3f}) yaw={f[3]:+.3f}")
print(f"dist bx={f[4]:.3f}/{f[5]:.3f} by={f[6]:.3f}/{f[7]:.3f} bz={f[8]:.3f}/{f[9]:.3f}")
print(f"scene: box=({f[11]:.1f},{f[12]:.1f},{f[13]:.1f}) mount={f[14]:.2f}")
print(f"Z-pair sum: {f[8]+f[9]:.3f} (expect 2*half_z - 2*mount = {2*f[13]-2*f[14]:.3f})")
EOF
```

The Z-pair sum equals the constant chord (2*half_z - 2*mount_radius) and the
scene block carries the tunable-owned box -- the v2 invariants, checked on the
wire. The X/Y pairs are yaw-dependent by design (the pods turn with the body).

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
| Consumer refuses to attach          | Identity mismatch: both region headers must read LBOX/v2, payload 64, capacity 8 -- check with the xxd lines above. |
| Cursor stuck at 8                   | Expected with no consumer (ring full, pushes refused). Attach a consumer or just read the slots.                    |
| Consumer shows APP_ID_MISMATCH      | Producer/consumer wire versions differ (v1 vs v2). Both sides must be on LBOX/v2.                                   |
