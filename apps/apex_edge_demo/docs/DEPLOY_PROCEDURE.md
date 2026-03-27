# ApexEdgeDemo Deploy Procedure

End-to-end build, test, release, and deploy for NVIDIA Thor.

## Prerequisites

- Thor: `kalex@192.168.1.40` (14-core aarch64, NVIDIA Thor GPU, 122 GB RAM)
- SSH key auth configured (`ssh-copy-id kalex@192.168.1.40`)
- Docker Compose environment configured on dev PC
- Rust tools built (`make tools-rust`) for TPRM generation

## Procedure

```bash
# 1. Build native debug (from distclean)
make distclean
make compose-debug

# 2. Run all tests
make compose-testp

# 3. Run GPU tests explicitly (CUDA container)
docker compose run --rm -T dev-cuda bash -c \
  'cd build/native-linux-debug && ctest --test-dir . -L gpu_compute -j4'

# 4. Build release (cross-compile aarch64 + package)
make release APP=ApexEdgeDemo

# 5. Clean target on Thor
ssh kalex@192.168.1.40 'rm -rf ~/ApexEdgeDemo && mkdir ~/ApexEdgeDemo'

# 6. Deploy package (bank_a/ + run.sh)
rsync -a build/release/ApexEdgeDemo/jetson/ kalex@192.168.1.40:~/ApexEdgeDemo/

# 7. Run ApexEdgeDemo (15 seconds, headless)
#    run.sh auto-adds --config bank_a/tprm/master.tprm and --fs-root .
#    CRITICAL: </dev/null prevents stdin CLI reader from getting garbage
ssh kalex@192.168.1.40 'cd ~/ApexEdgeDemo && \
  rm -rf logs tlm db swap_history active_bank bank_b system.log profile.log heartbeat.csv .apex_fs && \
  timeout 45 ./run.sh ApexEdgeDemo --shutdown-after 15 --skip-cleanup </dev/null'

# 8. Verify results
ssh kalex@192.168.1.40 'cd ~/ApexEdgeDemo && \
  echo "=== STATS ===" && \
  grep -E "cycles|overrun|completion|utilization|Runtime" system.log | tail -8 && \
  echo && echo "=== GPU MODELS ===" && \
  for f in logs/models/*.log; do \
    echo "--- $(basename $f) ---"; \
    grep "GPU complete" "$f" | tail -1; \
  done && \
  echo && echo "=== ERRORS ===" && \
  grep -E "ERROR|FATAL" system.log | head -5'

# 9. Long-running soak test (run in background)
ssh kalex@192.168.1.40 'cd ~/ApexEdgeDemo && \
  rm -rf logs tlm db swap_history active_bank bank_b system.log profile.log heartbeat.csv .apex_fs && \
  nohup ./run.sh ApexEdgeDemo --skip-cleanup </dev/null >stdout.log 2>&1 &'

# 10. Stop soak test
ssh kalex@192.168.1.40 'kill $(pgrep ApexEdgeDemo)'

# 11. Analyze soak test results
python3 apps/apex_edge_demo/scripts/analyze_soak.py kalex@192.168.1.40:~/ApexEdgeDemo/
```

## TPRM Configuration

Two TPRM sets are maintained for different targets:

| Set                   | Scheduler          | Use             |
| --------------------- | ------------------ | --------------- |
| `tprm/toml/` (native) | 1 pool, 8 workers  | Dev PC testing  |
| `tprm/toml/thor/`     | 1 pool, 14 workers | Thor deployment |

The native scheduler TPRM is packed into `master.tprm` (used by `make release`).
The Thor scheduler TPRM is packed into `master_thor.tprm` (for Thor-specific tuning).

```bash
# Regenerate native master.tprm (after TOML edits)
./build/native-linux-debug/bin/tools/rust/cfg2bin \
  --batch apps/apex_edge_demo/tprm/toml \
  --output apps/apex_edge_demo/tprm

./build/native-linux-debug/bin/tools/rust/tprm_pack pack \
  -e 0x000000:apps/apex_edge_demo/tprm/executive.tprm \
  -e 0x000100:apps/apex_edge_demo/tprm/scheduler.tprm \
  -e 0x000500:apps/apex_edge_demo/tprm/action.tprm \
  -e 0x008200:apps/apex_edge_demo/tprm/conv_filter.tprm \
  -e 0x008300:apps/apex_edge_demo/tprm/fft_analyzer.tprm \
  -e 0x008400:apps/apex_edge_demo/tprm/batch_stats.tprm \
  -e 0x008500:apps/apex_edge_demo/tprm/stream_compact.tprm \
  -e 0x00C800:apps/apex_edge_demo/tprm/system_monitor.tprm \
  -o apps/apex_edge_demo/tprm/master.tprm

# Regenerate Thor master_thor.tprm
./build/native-linux-debug/bin/tools/rust/cfg2bin \
  --batch apps/apex_edge_demo/tprm/toml/thor \
  --output apps/apex_edge_demo/tprm/thor

./build/native-linux-debug/bin/tools/rust/tprm_pack pack \
  -e 0x000000:apps/apex_edge_demo/tprm/thor/executive.tprm \
  -e 0x000100:apps/apex_edge_demo/tprm/thor/scheduler.tprm \
  -e 0x000500:apps/apex_edge_demo/tprm/action.tprm \
  -e 0x008200:apps/apex_edge_demo/tprm/conv_filter.tprm \
  -e 0x008300:apps/apex_edge_demo/tprm/fft_analyzer.tprm \
  -e 0x008400:apps/apex_edge_demo/tprm/batch_stats.tprm \
  -e 0x008500:apps/apex_edge_demo/tprm/stream_compact.tprm \
  -e 0x00C800:apps/apex_edge_demo/tprm/thor/system_monitor.tprm \
  -o apps/apex_edge_demo/tprm/master_thor.tprm
```

## Key Notes

- **No sudo required:** Thor runs without RT scheduling (SCHED_OTHER).
  GPU dispatch is non-blocking so RT priority is not needed for correctness.
  When sudo is available, the scheduler will apply SCHED_FIFO per TPRM config.

- **CUDA 13.0 on Thor:** The Thor has driver 580.00 (CUDA 13.0). The cross-compile
  uses CUDA 13.1 but the runtime is forward-compatible. SM 110 fatbin is included
  in the binary via the `CUDA_ARCHS="89;110"` toolchain default.

- **Always use `</dev/null`** when starting headless. The executive's stdin
  CLI reader interprets 'p' as PAUSE, 'q' as QUIT.

- **Frame overruns expected:** Debug builds have overruns from unoptimized
  `sinf()` input generation. Release builds have overruns from `generateInput()`
  running on the CPU. On Thor with SCHED_FIFO and 2-pool config, RT tasks on
  Pool 0 will have zero overruns.

## Expected Output (Thor, 15 seconds)

```
Clock cycles completed: ~3000-4000
Cycle lag: 0 (100.00% completion)
Frame overruns: ~1300 (single pool, no RT priority)
Average utilization: 0.2%

BATCH_STATS  - GPU complete: min=-7.4 max=7.5 var=14.6 duration=75ms
CONV_FILTER  - GPU complete: duration=410ms img=2048x2048 R=3
FFT_ANALYZER - GPU complete: ch0_peak=100.1Hz duration=223ms
STREAM_COMPACT - GPU complete: compacted=1M/4M selectivity=25% duration=413ms
```

## Filesystem After Deploy + Run

```
~/ApexEdgeDemo/
  run.sh                       # launch script
  bank_a/bin/ApexEdgeDemo      # executive binary (aarch64)
  bank_a/libs/*.so*            # shared libraries (53 project libs)
  bank_a/tprm/master.tprm     # TPRM config archive
  bank_b/{bin,libs,tprm}/     # inactive bank (created by doInit)
  active_bank                  # marker file
  system.log                   # system log
  heartbeat.csv                # per-cycle heartbeat data
  profile.log                  # profiling data
  logs/core/*.log              # core component logs
  logs/models/*.log            # model logs (GPU completion data)
  logs/support/*.log           # SystemMonitor log
```
