# ApexLidarBoxDemo Deploy Procedure

Build, test, package, and pair the producer with a host-side consumer. The
consumer (e.g. a UE5 scene implementing the wire format) runs on the host; the
producer runs in the dev container or as a packaged release on the same
machine, sharing the host IPC namespace.

## Prerequisites

- Docker Compose environment configured (the dev services set `ipc: host`).
- A consumer that implements the LBOX/v1 wire format (optional -- the demo
  verifies headless too; see [RESULTS.md](RESULTS.md)).

## Procedure

```bash
# 1. Build native debug
make compose-debug

# 2. Run the producer unit tests
docker compose run --rm dev-cuda \
  ./build/hosted-x86_64-debug/bin/utests/TestLidarBoxProducer

# 3. (If tomls changed) rebuild + repack the TPRM archive
T=tools/rust/target/debug ; TP=apps/apex_horizon_demo/lidar_box/tprm
$T/cfg2bin --batch $TP/toml/ --output $TP/
$T/tprm_pack pack -e 0x000000:$TP/executive.tprm -e 0x000100:$TP/scheduler.tprm \
  -e 0x00E600:$TP/lidar_box_producer.tprm -e 0x00CB00:$TP/lidar_box_bridge.tprm \
  -o $TP/master.tprm

# 4. Start the producer (runs until Ctrl+C)
docker compose run --rm dev-cuda ./build/hosted-x86_64-debug/bin/ApexLidarBoxDemo \
  --config apps/apex_horizon_demo/lidar_box/tprm/master.tprm \
  --fs-root /tmp/lidar_fs

# 5. Verify the channel from the host (second terminal)
ls -la /dev/shm | grep lidar          # lidar_box + sem.lidar_box_wake
xxd -l 24 /dev/shm/lidar_box          # LBOX/v1 header stamp

# 6. Attach the consumer
#    Point it at /lidar_box (Side B, attach-with-retry). It validates both
#    region headers, drains the ring, and renders the body + six rays.
```

## Release packaging

The app declares a single-executive deployment (`master.tprm` bundled):

```bash
make release APP=ApexLidarBoxDemo
# package lands under build/release/ApexLidarBoxDemo/
```

Run the packaged executive on any Linux host alongside the consumer; no
container is required as long as producer and consumer share `/dev/shm`.

## Notes

- The producer is Side A: it creates and, on clean exit, unlinks the shm region
  and wakeup semaphore. Restarting the producer while a consumer holds the old
  mapping requires the consumer to re-attach (attach-with-retry handles this).
- The demo is egress-only; no return channel is configured (`sink_enabled = 0`).
