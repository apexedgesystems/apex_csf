# lidar_box design

## What it is

A minimal end-to-end producer for the shm spine: a body drifting in a fixed
asymmetric box, a six-axis clearance lidar, and a 50 Hz stream of
`LidarBoxFrame` (pose + clearances) over the `ShmRingBridge` to `/lidar_box`.
A consumer renders the body and draws the six rays from the streamed
clearances; sphere-vs-drone is purely a consumer render choice (the producer is
body-agnostic).

## Division of labor: apex owns the sensor physics

**Render geometry is not sensor physics.** The body meshes live in the
consumer's render engine, but the six wall clearances need no mesh: for an
axis-aligned box they are closed-form from the sensor position and the box
constants (`clr_pos = half - pos`, `clr_neg = half + pos`) -- no ray-march.
The sim therefore computes them (it is the single source of physics truth) and
the consumer renders the streamed values without casting. Because the sensor
data originates sim-side, the demo is egress-only: nothing needs to flow back.

Consequences:

- The sensor is a **generic core model** (`sim/sensors` `BoxClearanceLidar`),
  reusable by any body-in-a-box scenario, with deterministic optional noise.
- The demo app owns only the specifics: the box constants, the trajectory, the
  wire framing, and the channel identity.
- The rays a drone-shaped consumer draws from its rotor arms are a cosmetic
  offset of the one physical mount (the body center). Physically-real per-arm
  clearances would be a deliberate extension (N per-arm beams), not a render
  tweak.

## Component split

```
LidarBoxProducer (SwModel, 0xE600)          ShmRingBridge (SUPPORT, 0xCB00)
  bodyStep @ 50 Hz, priority 50               bridgeStep @ 50 Hz, priority 40
    t += dt                                     resolve (0xE600, OUTPUT) via the
    pos = amp * sin(omega t + phase)              registry-lookup delegate
    yaw += rate * dt (wrapped)                  memcpy 48 B -> next ring slot
    clearances = lidar.measure(pos, box)        release-store cursor, sem_post
    OUTPUT <- LidarBoxFrame
```

The producer publishes a registered OUTPUT block and knows nothing of the
bridge; the bridge's TPRM selects the producer's `(fullUid, OUTPUT)` as its
source. Priority (50 before 40) sequences producer-then-publisher inside each
tick, so the consumer always sees the frame computed that tick.

## Trajectory

A 3D Lissajous figure with incommensurate angular frequencies (0.11 / 0.17 /
0.23 rad/s) so the path never closes and all six clearances animate, plus a
steady yaw (0.35 rad/s, wrapped to +/-pi). Amplitude per axis is
`amp_frac * (half - body_radius)` with `amp_frac` clamped to [0, 1], so the
in-box guarantee (`clearance >= body_radius`) holds by construction -- verified
by a 10-sim-minute sweep in the unit tests. Closed-form in `t`, so the
trajectory is exact at any tick rate.

## Wire contract (LBOX/v1) -- locked

48-byte flat POD frame (floats + one uint64), byte-locked with static_asserts
on size, alignment, and field offsets; box constants shared compile-time. The
channel is `/lidar_box`, capacity 8, framework v1; both region headers carry
the full identity stamp (the consumer validates A and B on attach; Region B is
inert in this egress-only demo but must present valid geometry -- symmetric
48-byte slots). The transport itself is specified in the `ShmRingBridge`
README (section 5).

## Runtime behavior

The per-tick hot path is allocation-free: closed-form trig for the drift, the
clearance measurement, and the bridge's memcpy + cursor publish. With no
consumer attached the ring fills to capacity and further pushes are refused
without blocking the tick -- the sim is unaffected, and a consumer that attaches
later drains the ring and then tracks live at the stream rate. On shutdown the
producer side unlinks the shm region and semaphore, so a clean exit leaves no
kernel objects behind.
