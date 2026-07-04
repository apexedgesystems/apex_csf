# lidar_box

A body drifts through a fixed asymmetric box on a 3D Lissajous path while a
mounted six-beam lidar ranges from its body-fixed sensor pods to the walls --
the X/Y pod pairs yaw with the body. The producer publishes a 64-byte
`LidarBoxFrame` (pose + six pod distances + the streamed scene block) that a
`ShmRingBridge` streams to the `/lidar_box` shared-memory ring at 50 Hz for an
out-of-process visualizer to render (body + six rays).

The simplest end-to-end proof of the shm spine: **apex computes state + sensor,
a consumer renders it** -- two components, one ring, no ingress.

---

## 1. Quick Start

```bash
# Build (from the repo root)
make compose-debug

# Stage the deployment package, then run it -- zero arguments (Ctrl+C to stop)
docker compose run --rm dev-cuda \
  cmake --build build/hosted-x86_64-debug --target package_ApexLidarBoxDemo
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/packages/ApexLidarBoxDemo/run.sh
```

(For the raw-binary dev loop, see [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md).)

While it runs, `/dev/shm/lidar_box` (+ `sem.lidar_box_wake`) exists on the host
-- the dev container shares the host IPC namespace -- and any consumer that
speaks the wire format can attach. See
[docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) for a no-consumer verification recipe.

## 2. What it demonstrates

| Piece               | Where                                    | Role                                                                                  |
| ------------------- | ---------------------------------------- | ------------------------------------------------------------------------------------- |
| `BoxClearanceLidar` | `src/sim/sensors` (generic core)         | closed-form wall ranging: world-axis clearance + mounted body-fixed beams (slab form) |
| `LidarBoxProducer`  | `producer/` (this app)                   | Lissajous drift + yaw, ranges the mounted pods, publishes the frame                   |
| `ShmRingBridge`     | `src/system/core/support` (generic core) | memcpys the OUTPUT block to the shm ring each tick                                    |
| wire contract       | `producer/inc/LidarBoxTypes.hpp`         | LBOX/v2: the 64-byte frame (incl. scene block), byte-locked                           |

The producer never touches the bridge: it registers a `DataCategory::OUTPUT`
block and the bridge's TPRM selects that `(fullUid, category)` as its source.
Scheduler priorities order `bodyStep` (50) before `bridgeStep` (40) inside each
50 Hz tick, so the bridge always streams the frame written that tick.

## 3. Configuration (TPRM)

| File (`tprm/toml/`)       | Component            | Highlights                                                |
| ------------------------- | -------------------- | --------------------------------------------------------- |
| `executive.toml`          | executive (0x000000) | 50 Hz clock, RT thread pinning, run-until-interrupted     |
| `scheduler.toml`          | scheduler (0x000100) | 4 tasks: bodyStep/bridgeStep @ 50 Hz, telemetries @ 1 Hz  |
| `lidar_box_producer.toml` | producer (0xE600)    | drift amplitudes/frequencies, yaw rate, lidar noise sigma |
| `lidar_box_bridge.toml`   | bridge (0xCB00)      | LBOX/v2 identity, `/lidar_box`, payload 64, capacity 8    |

Rebuild the packed archive after editing any toml:

```bash
T=tools/rust/target/debug ; TP=apps/apex_horizon_demo/lidar_box/tprm
$T/cfg2bin --batch $TP/toml/ --output $TP/
$T/tprm_pack pack -e 0x000000:$TP/executive.tprm -e 0x000100:$TP/scheduler.tprm \
  -e 0x00E600:$TP/lidar_box_producer.tprm -e 0x00CB00:$TP/lidar_box_bridge.tprm \
  -o $TP/master.tprm
```

## 4. Wire contract (LBOX/v2)

Locked with the consumer side; both implement it independently (no shared code).

- Channel: `/lidar_box` (+ auto-derived `/lidar_box_wake`), apex = Side A owner.
- Identity: framework v1, `app_magic 0x4C424F58` ("LBOX"), `app_version 2`.
- Ring A slot: `LidarBoxFrame` (64 B) -- pose (pos xyz + yaw), six mounted pod
  distances along the BODY axes (X/Y pairs yaw with the body; each reading is
  pod tip -> wall, hitting 0.0 at contact), a monotonic ns timestamp, and the
  scene block (box half-extents + mount radius). Ring B: present but inert.
- The scene is owned by the producer's tunables and STREAMED in every frame --
  the consumer builds the room and standoffs from the streamed values. The
  compile-time constants are seed defaults only. The body center stays within
  +/-(half - mount_radius), so every pod distance is >= 0.

Bump `app_version` on any frame-layout change; the consumer validates identity,
payload size, and capacity on attach. Full transport spec: the `ShmRingBridge`
README (section 5).

## 5. Testing

```bash
# Producer unit tests (wire layout, in-box guarantee, mounted-beam invariants)
docker compose run --rm dev-cuda ./build/hosted-x86_64-debug/bin/utests/TestLidarBoxProducer
```

13 tests: the locked v2 frame layout (sizeof/offsets incl. the scene block), a
10-sim-minute in-box sweep, pod distances >= 0 everywhere, the constant Z-pair
chord, X/Y beams cross-checked against independent slab math, the streamed
scene block, frame-vs-state consistency, yaw wrap + monotonic timestamps, and
the amplitude clamps. The underlying `BoxClearanceLidar` has its own suite in
`src/sim/sensors`.

## 6. See Also

- [docs/HOW_TO_RUN.md](docs/HOW_TO_RUN.md) -- build, run, verify (with and
  without a consumer)
- [docs/LIDAR_BOX_DESIGN.md](docs/LIDAR_BOX_DESIGN.md) -- design + the
  producer/bridge/contract split
- [docs/RESULTS.md](docs/RESULTS.md) -- first-light checkout results (what a
  good run looks like)
- [docs/DEPLOY_PROCEDURE.md](docs/DEPLOY_PROCEDURE.md) -- packaging + pairing
  with a host-side consumer
- `src/system/core/support/shm_ring_bridge/README.md` -- the wire-format spec
- `src/sim/sensors/README.md` -- the clearance-lidar model
