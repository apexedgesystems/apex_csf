# lidar_box

A body drifts through a fixed asymmetric box on a 3D Lissajous path while a
six-axis clearance lidar measures the distance from the body to each wall. The
producer publishes a 48-byte `LidarBoxFrame` (pose + six clearances) that a
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

| Piece               | Where                                    | Role                                                           |
| ------------------- | ---------------------------------------- | -------------------------------------------------------------- |
| `BoxClearanceLidar` | `src/sim/sensors` (generic core)         | closed-form six-wall clearance from the sensor point           |
| `LidarBoxProducer`  | `producer/` (this app)                   | Lissajous drift + yaw, measures clearance, publishes the frame |
| `ShmRingBridge`     | `src/system/core/support` (generic core) | memcpys the OUTPUT block to the shm ring each tick             |
| wire contract       | `producer/inc/LidarBoxTypes.hpp`         | LBOX/v1: the 48-byte frame + box constants, byte-locked        |

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
| `lidar_box_bridge.toml`   | bridge (0xCB00)      | LBOX/v1 identity, `/lidar_box`, payload 48, capacity 8    |

Rebuild the packed archive after editing any toml:

```bash
T=tools/rust/target/debug ; TP=apps/apex_horizon_demo/lidar_box/tprm
$T/cfg2bin --batch $TP/toml/ --output $TP/
$T/tprm_pack pack -e 0x000000:$TP/executive.tprm -e 0x000100:$TP/scheduler.tprm \
  -e 0x00E600:$TP/lidar_box_producer.tprm -e 0x00CB00:$TP/lidar_box_bridge.tprm \
  -o $TP/master.tprm
```

## 4. Wire contract (LBOX/v1)

Locked with the consumer side; both implement it independently (no shared code).

- Channel: `/lidar_box` (+ auto-derived `/lidar_box_wake`), apex = Side A owner.
- Identity: framework v1, `app_magic 0x4C424F58` ("LBOX"), `app_version 1`.
- Ring A slot: `LidarBoxFrame` (48 B) -- pose (pos xyz + yaw) + six clearances +
  monotonic ns timestamp. Ring B: present but inert (egress-only demo).
- Box constants (compile-time, shared): half-extents (4.0, 3.0, 2.5) m, body
  radius inset 0.5 m. The body center stays within +/-(half - radius), so every
  clearance is always >= 0.5 m.

Bump `app_version` on any frame-layout change; the consumer validates identity,
payload size, and capacity on attach. Full transport spec: the `ShmRingBridge`
README (section 5).

## 5. Testing

```bash
# Producer unit tests (wire layout, in-box guarantee, clearance consistency)
docker compose run --rm dev-cuda ./build/hosted-x86_64-debug/bin/utests/TestLidarBoxProducer
```

8 tests: the locked frame layout (sizeof/offsets), a 10-sim-minute in-box
sweep, clearance >= body-radius everywhere, opposite-clearance sums equal the
box size, frame-vs-state consistency, yaw wrap + monotonic timestamps, and the
zero-amplitude pin. The underlying `BoxClearanceLidar` has its own suite in
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
