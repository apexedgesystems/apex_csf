# Apex Edge Compute Demo - Design Document

## Purpose

Demonstrate Apex Executive running heavy GPU workloads on NVIDIA Jetson AGX Thor
(SM 89, 12x Cortex-A78AE) while maintaining deterministic real-time scheduling.
The demo proves that GPU saturation does not degrade RT executive timing.

Key customer takeaway: full GPU utilization with zero frame overruns and
sub-millisecond jitter across extended soak tests.

## Hardware Target

| Property     | Value                                           |
| ------------ | ----------------------------------------------- |
| Platform     | NVIDIA Jetson AGX Thor                          |
| CPU          | 12x Arm Cortex-A78AE (3 clusters of 4)          |
| GPU          | Ada (SM 89), tensor cores, FP64, 64-bit atomics |
| CUDA arch    | sm_89 (CUDA_ARCHS="89")                         |
| Sysroot      | /opt/sysroots/jetson-thor/usr                   |
| CMake preset | jetson-aarch64-release                          |

## Architecture Overview

```
                    Apex Executive (100 Hz)
                    ========================
                    CLOCK       @ Core 0  FIFO 90
                    TASK_EXEC   @ Core 1  FIFO 80
                    EXT_IO      @ Core 2  OTHER
                    HOUSEKEEP   @ Core 3  OTHER

    Pool 0 (RT CPU)                    Pool 1 (GPU Dispatch)
    Cores 4-5, FIFO 70                 Cores 6-7, OTHER
    ========================           ========================
    DataIngest     @ 10 Hz             ConvFilter.kick    @ 1 Hz
    Telemetry      @  1 Hz             ConvFilter.poll    @ 1 Hz (offset)
    ActionEngine   @  1 Hz             FFTAnalyzer.kick   @ 2 Hz
                                       FFTAnalyzer.poll   @ 2 Hz (offset)
                                       BatchStats.kick    @ 5 Hz
                                       BatchStats.poll    @ 5 Hz (offset)
                                       StreamCompact.kick @ 1 Hz
                                       StreamCompact.poll @ 1 Hz (offset)

    SystemMonitor  @  1 Hz (Pool 0)    GpuMonitor via NVML in SystemMonitor
    ========================           ========================

    Cores 8-9: Linux kernel, system services, logging
    Cores 10-11: Reserve / future expansion
```

### Kernel Command Line

```
isolcpus=0-7 nohz_full=0-7 rcu_nocbs=0-7
```

Cores 0-7 isolated from Linux scheduler. Cores 8-11 handle system work.

## Thread-to-Core Mapping

### Executive Primary Threads

| Thread         | Core | Policy      | Priority | Purpose                |
| -------------- | ---- | ----------- | -------- | ---------------------- |
| CLOCK          | 0    | SCHED_FIFO  | 90       | 100 Hz tick generation |
| TASK_EXECUTION | 1    | SCHED_FIFO  | 80       | Task dispatcher        |
| EXT_IO         | 2    | SCHED_OTHER | 0        | Network I/O, logging   |
| STARTUP        | 3    | SCHED_OTHER | 0        | Init sequence          |
| SHUTDOWN       | 3    | SCHED_OTHER | 0        | Cleanup sequence       |
| WATCHDOG       | 3    | SCHED_OTHER | 0        | Health watchdog        |

### Scheduler Thread Pools

| Pool   | Workers | Cores | Policy      | Priority | Purpose                              |
| ------ | ------- | ----- | ----------- | -------- | ------------------------------------ |
| Pool 0 | 2       | 4, 5  | SCHED_FIFO  | 70       | RT CPU tasks                         |
| Pool 1 | 2       | 6, 7  | SCHED_OTHER | 0        | GPU dispatch (absorbs driver jitter) |

### Why Pool 1 is SCHED_OTHER

cudaLaunchKernel calls into the NVIDIA driver, which can occasionally block for
50-200 us under load (command queue backpressure, page migration). Isolating GPU
dispatch on a normal-priority pool on dedicated cores ensures:

- Pool 0 RT tasks never compete with driver calls
- Clock thread and RT pool are unaffected by GPU driver jitter
- isComplete() polling (~100 ns) stays on the same pool as launch

## CUDA Kernel Plan

### Design Philosophy

Four kernels of varying weight and duration, all operating on synthetic sensor
data (generated in-process, no external camera needed). Each kernel demonstrates
a different GPU compute pattern relevant to edge applications.

### Kernel Inventory

| #   | Kernel        | Weight | Duration  | Rate | Pattern                                   | Edge Relevance                          |
| --- | ------------- | ------ | --------- | ---- | ----------------------------------------- | --------------------------------------- |
| 1   | ConvFilter    | Heavy  | 2-5 s     | 1 Hz | 2D convolution on large image buffer      | Image preprocessing, denoising          |
| 2   | FFTAnalyzer   | Heavy  | 0.5-1 s   | 2 Hz | Batched 1D FFT + spectral analysis        | Vibration monitoring, signal processing |
| 3   | BatchStats    | Medium | 50-200 ms | 5 Hz | Parallel reduction (min/max/mean/stddev)  | Sensor fusion, anomaly detection        |
| 4   | StreamCompact | Heavy  | 1-3 s     | 1 Hz | Threshold + stream compaction + histogram | Object detection preprocessing          |

Total: 4 kernels across 3 compute patterns (convolution, FFT, reduction/scan).

### Kernel Details

#### 1. ConvFilter (2D Convolution)

Applies configurable NxN convolution kernel to a synthetic grayscale image buffer.

- Input: 2048x2048 float32 image (16 MB default), regenerated each kick
- Kernel: Configurable radius 0-15 (1x1 to 31x31), Gaussian or Box
- Output: Filtered image buffer (same size)
- GPU pattern: Shared memory tiling with halo exchange, constant memory weights
- Tunable via TPRM: imageWidth, imageHeight, kernelRadius, kernelType, gaussianSigma
- Boundary handling: Clamp-to-edge
- Measured: 2048x2048 R=3 Gaussian in ~11 ms (dev GPU)
- Tests: 12 CUDA (identity, uniform, step edge, large image, large radius) + 12 CPU

#### 2. FFTAnalyzer (Batched FFT + Spectral Analysis)

Batched 1D FFT over many sensor channels, then magnitude spectrum + peak detection.

- Input: 256 channels x 4096 samples float32 (~4 MB default), regenerated each kick
- Operation: cuFFT batched R2C transform, then custom kernel for |X[k]| in dB + peak find
- Output: Per-channel ChannelPeak (peakFreqHz, peakMagnitudeDb, peakBin, noiseFloorDb)
- GPU pattern: cuFFT batched plan (one plan, reused), warp-shuffle peak reduction per channel
- Tunable via TPRM: channelCount, samplesPerChannel, peakThresholdDb, sampleRateHz
- Measured: 256 channels x 4096 samples in <1 ms (dev GPU, cuFFT + peak kernel)
- Tests: 6 CUDA (single tone, multi-channel, 256-channel batch) + 11 CPU
- Detects injected tones within 1 frequency bin (~2.4 Hz at 10 kHz / 4096)

#### 3. BatchStats (Parallel Reduction)

Computes statistics over large sensor arrays using parallel reduction.

- Input: 1M float32 values (4 MB default), partitioned into groups of 4096
- Operation: Min, max, mean, variance per group + per-group histogram
- Output: GroupStats struct per group + histogram bins
- GPU pattern: Warp-shuffle reduction (\_\_shfl_down_sync), shared memory cross-warp, atomicAdd histogram
- Tunable via TPRM: elementCount, groupSize, histogramBins, histogramMin/Max
- Measured: 1M elements in ~2 ms (dev GPU)
- Tests: 8 CUDA (single/multi group, histogram, null params, large array) + 13 CPU

#### 4. StreamCompact (Threshold + Compaction)

Threshold filter followed by stream compaction and classification histogram.
Simulates detection pipeline: filter raw data, keep only interesting elements,
classify them.

- Input: 2048x2048 float32 field (16 MB default), synthetic noise + injected features
- Operation: Single-pass warp-ballot compact + separate classify kernel
- Output: Compacted array + per-class histogram counts
- GPU pattern: Warp-level inclusive prefix sum (\_\_shfl_up_sync), atomicAdd for
  global offset reservation, atomic histogram for classification
- Tunable via TPRM: fieldWidth, fieldHeight, threshold, classCount, classMin/Max
- Measured: 4M element compact + classify in <1 ms (dev GPU)
- Tests: 8 CUDA (all/none/half above threshold, classify, large array) + 11 CPU

### Async Execution Model

Each GPU model has two scheduler tasks:

1. **kick** (odd ticks): Check isComplete() from prior run. If complete, harvest
   results, update telemetry, H2D transfer new input, launch kernel,
   recordCompletion(). If NOT complete, log "still running" and return.

2. **poll** (even ticks via offset): Just isComplete() check + telemetry update.
   Lightweight, confirms GPU is still working. Enables mid-cycle status reporting.

This two-task pattern means the scheduler never blocks on GPU work. The kick task
takes ~10-50 us on the CPU side regardless of GPU kernel duration.

### Why These Four

| Customer Question                           | Kernel That Answers It |
| ------------------------------------------- | ---------------------- |
| "Can it handle image processing?"           | ConvFilter             |
| "What about signal processing / vibration?" | FFTAnalyzer            |
| "Can it do real-time sensor fusion?"        | BatchStats             |
| "Detection / classification pipeline?"      | StreamCompact          |

Together they saturate the GPU with diverse workloads while the RT executive
runs undisturbed.

## Simulation Library: src/sim/gpu_compute/

New simulation domain under src/sim/ for GPU compute workload models.

```
src/sim/gpu_compute/
    CMakeLists.txt              # Domain CMakeLists (interface library)
    README.md                   # Library documentation
    conv_filter/
        CMakeLists.txt
        inc/
            ConvFilterModel.hpp     # SwModelBase: kick/poll tasks
            ConvFilterData.hpp      # TPRM struct + state
            ConvFilterKernel.cuh    # CUDA kernel API
        src/
            ConvFilterKernel.cu     # Kernel implementation
        utst/
            CMakeLists.txt
            ConvFilterKernel_uTest.cu
            ConvFilterModel_uTest.cpp
    fft_analyzer/
        CMakeLists.txt
        inc/
            FFTAnalyzerModel.hpp
            FFTAnalyzerData.hpp
            FFTAnalyzerKernel.cuh
        src/
            FFTAnalyzerKernel.cu
        utst/
            CMakeLists.txt
            FFTAnalyzerKernel_uTest.cu
            FFTAnalyzerModel_uTest.cpp
    batch_stats/
        CMakeLists.txt
        inc/
            BatchStatsModel.hpp
            BatchStatsData.hpp
            BatchStatsKernel.cuh
        src/
            BatchStatsKernel.cu
        utst/
            CMakeLists.txt
            BatchStatsKernel_uTest.cu
            BatchStatsModel_uTest.cpp
    stream_compact/
        CMakeLists.txt
        inc/
            StreamCompactModel.hpp
            StreamCompactData.hpp
            StreamCompactKernel.cuh
        src/
            StreamCompactKernel.cu
        utst/
            CMakeLists.txt
            StreamCompactKernel_uTest.cu
            StreamCompactModel_uTest.cpp
```

Each model follows the standard pattern:

- Model class inherits SwModelBase
- TPRM-loadable data struct for runtime tuning
- Kernel in separate .cuh/.cu with COMPAT_CUDA_AVAILABLE guards
- SchedulableTaskCUDA for async execution
- CPU fallback stubs when CUDA unavailable

### Component IDs

| Component          | ID  | fullUid (instance 0) |
| ------------------ | --- | -------------------- |
| ConvFilterModel    | 130 | 0x8200               |
| FFTAnalyzerModel   | 131 | 0x8300               |
| BatchStatsModel    | 132 | 0x8400               |
| StreamCompactModel | 133 | 0x8500               |

Range 130-139 reserved for gpu_compute models.

## App Structure: apps/apex_edge_demo/

```
apps/apex_edge_demo/
    CMakeLists.txt
    release.mk                  # Release manifest
    docs/
        EDGE_DESIGN.md          # This document
        HOW_TO_RUN.md           # Run commands, deploy, soak test procedure
        DEPLOY_PROCEDURE.md     # Thor deployment steps
        RESULTS.md              # Soak test results and analysis
    exec/
        CMakeLists.txt
        inc/
            EdgeExecutive.hpp   # ApexExecutive subclass
        src/
            main.cpp
            EdgeExecutive.cpp
    scripts/
        analyze_soak.py         # Post-run log analysis
    tprm/
        toml/                   # Source TOML configs
            executive.toml      # Thread-to-core layout
            scheduler.toml      # Pool config, task entries
            system_monitor.toml # GPU domain enabled
            conv_filter.toml
            fft_analyzer.toml
            batch_stats.toml
            stream_compact.toml
        master.tprm             # Packed archive (native)
        master_thor.tprm        # Packed archive (Thor)
        README.md
```

### Component Inventory

| Component          | Type      | ID  | Tasks                  | Pool |
| ------------------ | --------- | --- | ---------------------- | ---- |
| EdgeExecutive      | Executive | 0   | (primary threads)      | -    |
| Scheduler          | System    | 1   | -                      | -    |
| SystemMonitor      | Support   | 200 | telemetry@1Hz          | 0    |
| ConvFilterModel    | SwModel   | 130 | kick@1Hz+2Hz, poll@1Hz | 0    |
| FFTAnalyzerModel   | SwModel   | 131 | kick@2Hz, poll@2Hz     | 0    |
| BatchStatsModel    | SwModel   | 132 | kick@5Hz, poll@5Hz     | 0    |
| StreamCompactModel | SwModel   | 133 | kick@1Hz+2Hz, poll@1Hz | 0    |

Total: 7 active components, 11 scheduler tasks.

For native testing, all tasks run on Pool 0 (single pool). For Thor
deployment, GPU tasks move to Pool 1 with SCHED_OTHER on cores 6-7.

## Verification Strategy

### What We Measure

| Metric                 | Source                                      | Success Criteria            |
| ---------------------- | ------------------------------------------- | --------------------------- |
| Frame overruns         | Executive log                               | 0 over 24h soak             |
| Clock jitter           | Executive profiling (profilingSampleEveryN) | < 100 us p99                |
| Pool 0 task jitter     | Scheduler timing                            | < 500 us p99                |
| GPU utilization        | SystemMonitor (NVML)                        | > 85% sustained             |
| GPU temperature        | SystemMonitor (NVML)                        | < 85 C sustained            |
| GPU memory             | SystemMonitor (NVML)                        | Stable (no leaks)           |
| CPU per-core load      | SystemMonitor (/proc/stat)                  | Cores 4-5 low, 6-7 moderate |
| Kernel completion time | Model telemetry logs                        | Within expected bounds      |
| RT throttle events     | SystemMonitor                               | 0                           |

### How We Verify

#### 1. SystemMonitor with GPU Domain Enabled

The existing SystemMonitor already supports GPU metrics via NVML. For Thor,
the system_monitor.toml enables the GPU domain:

```
gpuEnabled = 1
gpuDeviceIndex = 0
gpuTempWarnC = 80
gpuTempCritC = 90
gpuUtilWarnPercent = 98
gpuUtilCritPercent = 100
gpuMemWarnPercent = 90
gpuMemCritPercent = 98
```

This gives us per-second GPU temperature, utilization %, memory %, and throttle
detection without any additional code.

#### 2. EdgeTelemetry Support Component

Custom support component that aggregates GPU model-specific metrics:

- Per-kernel: last duration, average duration, completion count, timeout count
- Per-kernel: isComplete() poll latency histogram
- Aggregate: total GPU dispatches, total GPU completions, in-flight count
- Logs summary every 1 Hz to component log

This is the "proof" layer - shows kernel durations, confirms work is real.

#### 3. Executive Profiling

Set profilingSampleEveryN in executive.toml to sample clock jitter. The
executive already logs cycle timing statistics. With GPU load active, this
proves the RT loop is unaffected.

#### 4. Soak Test Procedure

```bash
# 1. Deploy to Thor (same pattern as Pi deploy)
scp -r deploy/ user@thor-host:~/apex/

# 2. Run soak test (24 hours)
ssh user@thor-host 'cd ~/apex && sudo ./bin/ApexEdgeDemo \
  --config tprm/master.tprm \
  --shutdown-after 86400'

# 3. Collect results
scp -r user@thor-host:~/apex/bin/.apex_fs/ ./test_runs/soak_24h/

# 4. Analyze
grep "Frame overruns" test_runs/soak_24h/system.log      # Must be 0
grep "GPU util" test_runs/soak_24h/system_monitor.log     # Should show >85%
grep "WARN\|CRIT" test_runs/soak_24h/system_monitor.log   # Review threshold hits
grep "kernel_complete" test_runs/soak_24h/conv_filter.log  # Verify work done
```

#### 5. Quick Validation (5-minute smoke test)

```bash
# Build
make compose-debug

# Run short test
docker compose run --rm -T dev-cuda bash -c '
  rm -rf build/native-linux-debug/bin/.apex_fs
  timeout 320 ./build/native-linux-debug/bin/ApexEdgeDemo \
    --config apps/apex_edge_demo/tprm/master.tprm \
    --shutdown-after 300
'

# Check results
grep "Frame overruns" build/native-linux-debug/bin/.apex_fs/logs/system.log
grep "GPU" build/native-linux-debug/bin/.apex_fs/logs/system_monitor.log
```

## Test Coverage

89 unit tests across the 4 GPU compute models (CUDA kernel correctness + CPU
model behavior). Full project test suite passes with `make compose-testp`.
