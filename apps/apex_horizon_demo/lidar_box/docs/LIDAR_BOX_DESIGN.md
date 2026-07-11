# lidar_box design

## What it is

A minimal end-to-end producer for the shm spine: a body drifting in a fixed
asymmetric box, a mounted six-beam lidar, and a 50 Hz stream of
`LidarBoxFrame` (pose + pod distances + the scene block) over the
`ShmRingBridge` to `/lidar_box`. A consumer renders the body and draws the six
rays from the streamed values; sphere-vs-drone is purely a consumer render
choice (the producer is body-agnostic).

## Division of labor: apex owns the sensor physics

**Render geometry is not sensor physics.** The body meshes live in the
consumer's render engine, but the six pod distances need no mesh: for an
axis-aligned box the ray-to-wall distance is closed-form (the slab method --
per axis `t_i = (sign(d_i)*half_i - p_i)/d_i`, take the minimum), and the
mounted reading is that distance minus the mount offset. The sim therefore
computes them (it is the single source of physics truth) and the consumer
renders the streamed values without casting. Because the sensor data
originates sim-side, the demo is egress-only: nothing needs to flow back.

**A mounted lidar turns with its vehicle.** The six pods sit at `mount_radius`
from the body center along the BODY axes; the X/Y pairs yaw with the body and
the Z pair stays vertical. Each streamed value is "pod tip to wall", reaching
0.0 exactly at contact -- so the rendered rays stay pinned to the rotating
body, matching the physical read of a mounted sensor.

**The scene streams in-band.** The box half-extents and mount radius live in
the producer's tunables (the single configuration owner) and ride in every
frame's scene block; the consumer builds the room and standoffs from the
streamed values. The compile-time constants are seed defaults only -- editing
one tprm changes both the physics and (through the stream) the rendered
scene, with the wire version gating any schema change.

Consequences:

- The sensor is a **generic core model** (`sim/sensors` `BoxClearanceLidar`),
  reusable by any body-in-a-box scenario, with deterministic optional noise.
- The demo app owns only the specifics: the scene tunables (box + mount), the
  trajectory, the wire framing, and the channel identity.
- The six pods share one spherical mount radius; the consumer places its
  rendered pods at the streamed offset. Per-arm mounts with distinct offsets
  would be a deliberate extension (N per-arm beams), not a render tweak.

## Component split

```
LidarBoxProducer (SwModel, 0xE600)          ShmRingBridge (SUPPORT, 0xCB00)
  bodyStep @ 50 Hz, priority 50               bridgeStep @ 50 Hz, priority 40
    t += dt                                     resolve (0xE600, OUTPUT) via the
    pos = amp * sin(omega t + phase)              registry-lookup delegate
    yaw += rate * dt (wrapped)                  memcpy 64 B -> next ring slot
    dists = lidar.measureMounted(pos, yaw, mount_r, box)        release-store cursor, sem_post
    OUTPUT <- LidarBoxFrame
```

The producer publishes a registered OUTPUT block and knows nothing of the
bridge; the bridge's TPRM selects the producer's `(fullUid, OUTPUT)` as its
source. Priority (50 before 40) sequences producer-then-publisher inside each
tick, so the consumer always sees the frame computed that tick.

## Trajectory

A 3D Lissajous figure with incommensurate angular frequencies (0.11 / 0.17 /
0.23 rad/s) so the path never closes and all six distances animate, plus a
steady yaw (0.35 rad/s, wrapped to +/-pi). Amplitude per axis is
`amp_frac * (half - mount_radius)` with `amp_frac` clamped to [0, 1], so every
pod-tip distance stays >= 0 by construction (0 exactly at wall contact) --
verified by a 10-sim-minute sweep in the unit tests. Closed-form in `t`, so
the trajectory is exact at any tick rate.

## Wire contract (LBOX/v2) -- locked

64-byte flat POD frame (floats + one uint64), byte-locked with static_asserts
on size, alignment, and field offsets: pose, six mounted pod distances along
the body axes, a monotonic timestamp, and the scene block (box half-extents +
mount radius, streamed from the tunables). The channel is `/lidar_box`,
capacity 8, framework v1, app version 2; both region headers carry the full
identity stamp (the consumer validates A and B on attach; Region B is inert in
this egress-only demo but must present valid geometry -- symmetric 64-byte
slots). The transport itself is specified in the `ShmRingBridge` README
(section 5).

## Runtime behavior

The per-tick hot path is allocation-free: closed-form trig for the drift, the
clearance measurement, and the bridge's memcpy + cursor publish. With no
consumer attached the ring fills to capacity and further pushes are refused
without blocking the tick -- the sim is unaffected, and a consumer that attaches
later drains the ring and then tracks live at the stream rate. On shutdown the
producer side unlinks the shm region and semaphore, so a clean exit leaves no
kernel objects behind.
