# Edge Compute Demo - How to Run

## Requirements

**Hardware:**

- NVIDIA GPU with CUDA Compute Capability 8.9+ (Ada Lovelace, Thor, or newer)
- Tested platforms: NVIDIA RTX 5000 Ada (laptop), NVIDIA Thor (automotive SoC)
- Minimum 4 GB GPU memory

**Software:**

- Docker with NVIDIA runtime (`nvidia-container-toolkit`)
- GPU visible in container (`nvidia-smi` works inside `dev-cuda`)
- Rust tools built (`make tools-rust`) for TPRM generation

**Not required:** The demo runs in CPU stub mode when no GPU is detected.
All 4 models fall back to no-op stubs, so the executive and scheduler can be
tested on any Linux machine. GPU kernels require an NVIDIA GPU.

---

## Quick Start (Native Development)

```bash
# 1. Build
make distclean
make compose-debug

# 2. Run tests
make compose-testp

# 3. Run (10 seconds)
docker compose run --rm -T dev-cuda bash -c '
  cd build/native-linux-debug
  rm -rf .apex_fs
  bin/ApexEdgeDemo \
    --config apps/apex_edge_demo/tprm/master.tprm \
    --shutdown-after 10 --skip-cleanup'

# 4. Check results
docker compose run --rm -T dev-cuda bash -c '
  cd build/native-linux-debug
  grep -E "cycles|overrun|completion" .apex_fs/system.log
  tail -3 .apex_fs/logs/models/*.log'

# 5. Analyze (optional)
python3 apps/apex_edge_demo/scripts/analyze_soak.py \
  build/native-linux-debug/.apex_fs/
```

## Thor Deployment

See [DEPLOY_PROCEDURE.md](DEPLOY_PROCEDURE.md) for the full cross-compile,
package, and deploy workflow for NVIDIA Thor.

---

## Expected Output

### System Log

```
Clock cycles completed: ~1000 (for 10s at 100Hz)
Task execution cycles: ~1000
Cycle lag: 0 (100.00% completion)
Frame overruns: <varies>
```

### GPU Model Logs

```
BATCH_STATS    - GPU complete: min=-7.3 max=7.3 var=14.6 duration=72ms
CONV_FILTER    - GPU complete: duration=400ms img=2048x2048 R=3
FFT_ANALYZER   - GPU complete: ch0_peak=100.1Hz duration=223ms
STREAM_COMPACT - GPU complete: compacted=1M/4M selectivity=25% duration=413ms
```

All 4 GPU kernels running concurrently with real data processing.

---

## TPRM Configuration

Two TPRM sets are maintained for different targets:

| File                    | Target          | Scheduler          |
| ----------------------- | --------------- | ------------------ |
| `tprm/master.tprm`      | Native (dev PC) | 1 pool, 8 workers  |
| `tprm/master_thor.tprm` | NVIDIA Thor     | 1 pool, 14 workers |

TOML sources are in `tprm/toml/` (shared) and `tprm/toml/thor/` (Thor overrides).
See [tprm/README.md](../tprm/README.md) for the full TPRM layout and regeneration
commands.

---

## CLI Flags

| Flag                         | Description                                       |
| ---------------------------- | ------------------------------------------------- |
| `--config <path>`            | Path to master.tprm archive (required)            |
| `--shutdown-after <seconds>` | Auto-shutdown after N seconds                     |
| `--skip-cleanup`             | Keep .apex_fs after shutdown (for log inspection) |
| `--fs-root <path>`           | Override filesystem root (default: .apex_fs)      |

---

## Troubleshooting

### GPU allocation failed (stub mode)

```
WARNING: CONV_FILTER[0] - GPU allocation failed; model will run in stub mode
```

**Cause:** CUDA runtime not available or no GPU detected.
**Fix:** Ensure Docker has GPU access (`nvidia-smi` inside container).
Check that the `dev-cuda` service has `deploy.resources.reservations.devices`
configured with `driver: nvidia`.

### Failed to apply RT config

```
WARNING: EDGE_EXECUTIVE[27] - Failed to apply RT config (may require CAP_SYS_NICE)
```

**Cause:** Docker container or user lacks real-time scheduling privileges.
**Fix:** Expected in Docker development. On Thor, run with `sudo` for SCHED_FIFO.
The demo runs correctly without RT priority — only timing precision is affected.

### Frame overruns in debug build

**Cause:** Debug builds disable compiler optimizations. The `generateInput()`
functions use `sinf()` loops over millions of elements which are slow unoptimized.
**Fix:** Use release builds for soak testing, or reduce workload sizes in TPRM.
Frame overruns do not affect GPU kernel execution or data correctness.
