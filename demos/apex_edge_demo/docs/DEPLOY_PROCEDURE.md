# ApexEdgeDemo Deploy Procedure

End-to-end build, test, release, and deploy for NVIDIA Thor.

## Prerequisites

- Thor: `kalex@192.168.1.41` (14-core aarch64, NVIDIA Thor GPU, 122 GB RAM)
- SSH key auth configured (`ssh-copy-id kalex@192.168.1.41`)
- Docker Compose environment configured on dev PC

TPRM masters are packed by the build from `tprm/tprm.manifest` (target
`apex_tprm_ApexEdgeDemo`, part of the default build) into
`build/<preset>/demos/apex_edge_demo/exec/tprm/`; the release package ships
`master_thor.tprm` from the cross-jetson build.

## Procedure

```bash
# 1. Build native debug (from distclean)
make distclean
make compose-debug

# 2. Run all tests
make compose-testp

# 3. Run GPU tests explicitly (CUDA container)
docker compose run --rm -T dev-cuda bash -c \
  'cd build/hosted-x86_64-debug && ctest --test-dir . -L gpu_compute -j4'

# 4. Build release (cross-compile aarch64 + package)
make release APP=ApexEdgeDemo

# 5. Clean target on Thor
ssh kalex@192.168.1.41 'rm -rf ~/ApexEdgeDemo && mkdir ~/ApexEdgeDemo'

# 6. Deploy package (bank_a/ + run.sh)
rsync -a build/release/ApexEdgeDemo/jetson/ kalex@192.168.1.41:~/ApexEdgeDemo/

# 7. Run ApexEdgeDemo (15 seconds, headless)
#    run.sh auto-adds --config bank_a/tprm/master.tprm and --fs-root .
#    CRITICAL: </dev/null prevents stdin CLI reader from getting garbage
ssh kalex@192.168.1.41 'cd ~/ApexEdgeDemo && \
  rm -rf logs tlm db swap_history active_bank bank_b system.log profile.log heartbeat.csv .apex_fs && \
  timeout 45 ./run.sh --shutdown-after 15 --skip-cleanup </dev/null'

# 8. Verify results
ssh kalex@192.168.1.41 'cd ~/ApexEdgeDemo && \
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
ssh kalex@192.168.1.41 'cd ~/ApexEdgeDemo && \
  rm -rf logs tlm db swap_history active_bank bank_b system.log profile.log heartbeat.csv .apex_fs && \
  nohup ./run.sh --skip-cleanup </dev/null >stdout.log 2>&1 &'

# 10. Stop soak test
ssh kalex@192.168.1.41 'kill $(pgrep ApexEdgeDemo)'

# 11. Analyze soak test results
python3 demos/apex_edge_demo/scripts/analyze_soak.py kalex@192.168.1.41:~/ApexEdgeDemo/
```

## TPRM Configuration

Two TPRM sets are maintained for different targets:

| Set                   | Scheduler          | Use             |
| --------------------- | ------------------ | --------------- |
| `tprm/toml/` (native) | 1 pool, 8 workers  | Dev PC testing  |
| `tprm/toml/thor/`     | 1 pool, 14 workers | Thor deployment |

`tprm/tprm.manifest` composes both masters from the same algorithm chain; only
the executive/scheduler/system_monitor timing trio differs. The build packs
them into `build/cross-jetson-release/demos/apex_edge_demo/exec/tprm/`, and
`make release APP=ApexEdgeDemo` stages `master_thor.tprm` into the package as
`bank_a/tprm/master.tprm` (what boots on Thor).

After a TOML edit, rebuild: the `apex_tprm_ApexEdgeDemo` target recompiles the
changed TOML with cfg2bin and repacks both masters automatically. See
[../tprm/README.md](../tprm/README.md) for the manifest layout and fullUid map.

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
  bank_a/tprm/master.tprm     # TPRM config archive (staged master_thor.tprm)
  bank_b/{bin,libs,tprm}/     # inactive bank (created by doInit)
  active_bank                  # marker file
  system.log                   # system log
  heartbeat.csv                # per-cycle heartbeat data
  profile.log                  # profiling data
  logs/core/*.log              # core component logs
  logs/models/*.log            # model logs (GPU completion data)
  logs/support/*.log           # SystemMonitor log
```
