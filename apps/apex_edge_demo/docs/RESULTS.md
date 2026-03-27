# Edge Compute Demo Results

**Executive:** ApexExecutive at 100 Hz, HARD_PERIOD_COMPLETE mode
**GPU Workload:** 4 concurrent CUDA kernels (convolution, FFT, reduction, compaction)
**Platforms:** NVIDIA Thor (automotive ARM SoC) and RTX 5000 Ada (laptop discrete GPU)
**Runs:** 39-minute Thor soak, 39-minute native soak, both archived

---

## System Under Test

An Apex real-time executive scheduling GPU-accelerated signal processing workloads
alongside system health monitoring. The executive runs at 100 Hz (10 ms frame
budget) and dispatches CUDA kernels via non-blocking async kick/poll tasks.

```
Pool 0 (CPU tasks)                Pool 1 (GPU dispatch)
========================          ==========================
SystemMonitor    @ 1 Hz           ConvFilter.kick    @ 1 Hz
                                  ConvFilter.poll    @ 1 Hz
                                  FFTAnalyzer.kick   @ 2 Hz
                                  FFTAnalyzer.poll   @ 2 Hz
                                  BatchStats.kick    @ 5 Hz
                                  BatchStats.poll    @ 5 Hz
                                  StreamCompact.kick @ 1 Hz
                                  StreamCompact.poll @ 1 Hz
```

The GPU kernels process real data: Gaussian-distributed floats, synthetic images,
multi-channel sinusoidal signals, and uniform random arrays.

---

## GPU Kernels

| Kernel        | Algorithm                                      | Input Size            | Purpose             |
| ------------- | ---------------------------------------------- | --------------------- | ------------------- |
| BatchStats    | Warp-shuffle reduction + atomic histogram      | 1M floats             | Parallel statistics |
| ConvFilter    | Separable 2D convolution, shared memory tiling | 2048x2048 image       | Image processing    |
| FFTAnalyzer   | cuFFT R2C + custom magnitude/peak kernel       | 256 ch x 4096 samples | Spectral analysis   |
| StreamCompact | Threshold + prefix-sum scatter + classify      | 4M floats             | Detection pipeline  |

---

## Results Summary

### Thor (39-minute soak, N=233,046 cycles)

| Metric              | Value   | Significance                                     |
| ------------------- | ------- | ------------------------------------------------ |
| Cycle completion    | 100.00% | Zero missed frames across 39 minutes             |
| Jitter P99          | 2.5 ms  | Sub-3ms timing at 99th percentile                |
| Jitter stddev       | 0.36 ms | 360 microsecond timing precision                 |
| GPU completions     | 20,973  | Sustained throughput across 4 kernels            |
| GPU throttle events | 0       | Automotive thermal design handles sustained load |
| Errors              | 0       | Zero faults across all subsystems                |
| Warnings            | 0       | Clean system log                                 |

### RTX 5000 Ada (39-minute soak, N=234,828 cycles)

| Metric              | Value   | Significance                            |
| ------------------- | ------- | --------------------------------------- |
| Cycle completion    | 100.00% | Same binary, same result                |
| Jitter P99          | 2.9 ms  | Higher due to laptop thermal throttling |
| Jitter stddev       | 0.63 ms | 1.8x Thor due to GPU power limit events |
| GPU completions     | 21,133  | Sustained throughput across 4 kernels   |
| GPU throttle events | 2,346   | Laptop GPU hits thermal + power limits  |
| Errors              | 0       | Zero faults despite throttling          |

---

## Key Findings

### 1. RT determinism is preserved under GPU load

Both platforms achieved 100% cycle completion with zero cycle lag. The executive
never dropped a frame across a combined 467,874 clock cycles. Executive overhead
was 0.1% of the 10 ms frame budget.

This is the core property: **the GPU does not affect the RT loop.** The kick/poll
pattern ensures that GPU tasks complete in microseconds on the CPU side, regardless
of how long the actual kernel takes on the GPU (up to 413 ms for convolution).

### 2. Thor outperforms the laptop on timing precision

| Platform     | P50      | P95      | P99     | Stddev  |
| ------------ | -------- | -------- | ------- | ------- |
| Thor         | +0.12 ms | +0.30 ms | +2.5 ms | 0.36 ms |
| RTX 5000 Ada | +0.11 ms | +1.9 ms  | +2.9 ms | 0.63 ms |

Thor's jitter standard deviation (0.36 ms) is 1.8x tighter than the laptop (0.63 ms).
The laptop's worst-case deviation was 6.1 ms (vs Thor's 4.6 ms), correlating
directly with GPU thermal throttle events that stall the NVIDIA driver.

This demonstrates that **automotive-grade thermal design directly impacts RT
timing quality** — an advantage of purpose-built edge compute platforms over
desktop/laptop GPUs.

### 3. GPU kernel performance is consistent across platforms

| Kernel        | Thor (iGPU) | RTX 5000 Ada | Ratio |
| ------------- | ----------- | ------------ | ----- |
| BatchStats    | 66 ms       | 72 ms        | 1.09x |
| FFTAnalyzer   | 224 ms      | 223 ms       | 1.00x |
| ConvFilter    | 413 ms      | 397 ms       | 0.96x |
| StreamCompact | 415 ms      | 415 ms       | 1.00x |

The discrete RTX 5000 Ada is marginally faster on individual kernels (4-8% for
compute-heavy workloads) but the difference is within noise for most kernels.
The integrated Thor GPU matches discrete laptop GPU performance for these
workload sizes.

### 4. Same binary, different platforms, same guarantee

The ApexEdgeDemo binary is identical on both platforms. Only the TPRM configuration
differs:

| Parameter        | Native (x86)    | Thor (ARM)                         |
| ---------------- | --------------- | ---------------------------------- |
| Worker threads   | 8               | 14                                 |
| Scheduler policy | SCHED_OTHER     | SCHED_OTHER (SCHED_FIFO with sudo) |
| GPU monitoring   | NVML via Docker | NVML native                        |
| Binary           | `x86_64` ELF    | `aarch64` ELF (cross-compiled)     |
| TPRM             | `master.tprm`   | `master_thor.tprm`                 |

No code changes, no #ifdefs, no platform-specific branches. The RT safety
guarantee is architectural, not platform-dependent.

### 5. GPU telemetry catches real thermal events

SystemMonitor correctly detected 2,346 GPU throttle events on the laptop platform
(thermal + power limit) while reporting zero events on Thor. This demonstrates
that the monitoring subsystem provides actionable telemetry for thermal management
and capacity planning.

---

## Reproducing These Results

### Prerequisites

- Docker with NVIDIA runtime (`nvidia-container-toolkit`)
- CUDA-capable GPU (SM 8.9+ for Ada, SM 11.0 for Thor)
- For Thor: SSH access to `kalex@192.168.1.40`

### Native Run (any CUDA-capable Linux workstation)

```bash
# Build
make distclean
make compose-debug
make compose-testp

# Run (10 minutes)
docker compose run --rm -T dev-cuda bash -c '
  cd build/native-linux-debug
  rm -rf .apex_fs
  bin/ApexEdgeDemo \
    --config /home/kalex/workspace/apps/apex_edge_demo/tprm/master.tprm \
    --shutdown-after 600 --skip-cleanup'

# Analyze
python3 apps/apex_edge_demo/scripts/analyze_soak.py \
  build/native-linux-debug/.apex_fs/
```

### Thor Run

See [DEPLOY_PROCEDURE.md](DEPLOY_PROCEDURE.md) for the full build-test-release-deploy
workflow.

```bash
# Build + deploy (abbreviated)
make release APP=ApexEdgeDemo
rsync -a build/release/ApexEdgeDemo/jetson/ kalex@192.168.1.40:~/ApexEdgeDemo/

# Run on Thor (runs until killed)
ssh kalex@192.168.1.40 'cd ~/ApexEdgeDemo && \
  sudo rm -rf logs tlm db swap_history system.log heartbeat.csv .apex_fs && \
  sudo ./run.sh ApexEdgeDemo --skip-cleanup </dev/null'

# Analyze
python3 apps/apex_edge_demo/scripts/analyze_soak.py \
  kalex@192.168.1.40:~/ApexEdgeDemo
```

---

## Archived Runs

Stored as compressed tarballs in `docs/results/`:

```
apps/apex_edge_demo/docs/results/
  thor_soak.tar.gz      NVIDIA Thor, 39 min, 233K cycles (baseline)
  native_soak.tar.gz    RTX 5000 Ada, 39 min, 235K cycles (cross-platform check)
```

Extract with `tar xzf <name>.tar.gz`. Each archive contains:

- `system.log` — Executive lifecycle and final statistics
- `heartbeat.csv` — Per-second timing samples for jitter analysis
- `logs/models/*.log` — GPU kernel completion logs with per-invocation durations
- `logs/support/*.log` — SystemMonitor telemetry (CPU, memory, GPU via NVML)
- `logs/core/*.log` — Scheduler, registry, filesystem component logs
