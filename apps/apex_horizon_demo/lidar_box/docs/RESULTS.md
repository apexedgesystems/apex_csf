# lidar_box Checkout Results

**Executive:** ApexLidarBoxDemo at 50 Hz
**Platform:** x86-64 dev container (`ipc: host`), Linux 6.8
**Run:** First-light baseline, 2026-07-03

---

## System Under Test

ApexLidarBoxDemo running headless (no consumer attached): the LidarBoxProducer
drifts the body on its Lissajous path and measures the six wall clearances; the
ShmRingBridge publishes the 48-byte frame to `/lidar_box` at the LBOX/v1
identity.

```
Pool 0
========================
LidarBoxProducer.bodyStep   @ 50 Hz (priority  50)
ShmRingBridge.bridgeStep    @ 50 Hz (priority  40)
LidarBoxProducer.telemetry  @  1 Hz (priority -64, offset 3)
ShmRingBridge.telemetry     @  1 Hz (priority -64, offset 7)
```

---

## Checkout Results

| Test | Result | Detail |
|------|--------|--------|
| 1. Unit tests | 8/8 PASS | wire layout, in-box sweep, clearance floor, sums, consistency, wrap, timestamps, pin |
| 2. Channel creation | PASS | `/dev/shm/lidar_box` (1152 B) + `sem.lidar_box_wake` |
| 3. Host visibility | PASS | ring inspectable from the host (`ipc: host`) |
| 4. Region A header | PASS | framework `0x48524E42`/v1, LBOX/v1, payload 48, capacity 8 |
| 5. Region B header | PASS | identical valid stamp (inert region, consumer validates both) |
| 6. Frame physics | PASS | opposite-clearance sums exactly 8.0 / 6.0 / 5.0 m |
| 7. Pose bounds | PASS | body center within +/-(half - radius) |
| 8. Timestamps | PASS | monotonic ns, advancing |
| 9. No-consumer behavior | PASS | ring fills to 8, pushes refused non-blocking, sim unaffected |
| 10. Clean shutdown | PASS | shm + semaphore unlinked on exit |
| 11. Live consumer pairing | PASS | independent Side B consumer attached to `/lidar_box`; 49.6 fps sustained, drained frame-for-frame (+248/+248 over 5 s), 0-slot lag; physics exact on the consumed stream |

Raw inspection transcript:
[results/first_light_shm_verification.txt](results/first_light_shm_verification.txt)

## Interpretation

The full producer path -- trajectory, clearance physics, OUTPUT registration,
bridge resolution, ring write, wakeup signal -- is exercised and byte-verified
against the locked wire contract without any consumer present. A conformant
consumer that attaches will validate both region headers, drain the ring, and
track the stream at ~50 frames/s.
