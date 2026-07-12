#!/usr/bin/env python3
"""
Soak test analyzer for ApexEdgeDemo.

Parses heartbeat.csv, system.log, and model logs to produce a summary
report with jitter statistics, overrun analysis, and GPU kernel metrics.

Usage:
    python3 analyze_soak.py /path/to/ApexEdgeDemo/
    python3 analyze_soak.py kalex@192.168.1.40:~/ApexEdgeDemo/

When given an SSH target, files are fetched via scp to a temp directory.
"""

import argparse
import re
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class HeartbeatSample:
    timestamp_ns: int = 0
    clock_cycles: int = 0
    task_cycles: int = 0
    frame_overruns: int = 0
    status: str = ""


@dataclass
class KernelStats:
    name: str = ""
    completions: int = 0
    durations_ms: list = field(default_factory=list)
    last_line: str = ""


def parse_heartbeat(path: Path) -> list:
    """Parse heartbeat.csv (log-prefixed format)."""
    samples = []
    PATTERN = re.compile(r"HEARTBEAT - (\d+),(\d+),(\d+),(\d+),(\w+)")
    with open(path) as f:
        for line in f:
            M = PATTERN.search(line)
            if M:
                samples.append(
                    HeartbeatSample(
                        timestamp_ns=int(M.group(1)),
                        clock_cycles=int(M.group(2)),
                        task_cycles=int(M.group(3)),
                        frame_overruns=int(M.group(4)),
                        status=M.group(5),
                    )
                )
    return samples


def parse_system_log(path: Path) -> dict:
    """Extract final stats from system.log."""
    stats = {}
    PATTERNS = {
        "runtime": re.compile(r"Runtime: ([\d.]+) seconds"),
        "clock_cycles": re.compile(r"Clock cycles completed: (\d+)"),
        "task_cycles": re.compile(r"Task execution cycles: (\d+)"),
        "cycle_lag": re.compile(r"Cycle lag: (\d+)"),
        "completion_pct": re.compile(r"([\d.]+)% completion"),
        "frame_overruns": re.compile(r"Frame overruns: (\d+)"),
        "utilization": re.compile(r"Average utilization: ([\d.]+)%"),
    }
    with open(path) as f:
        for line in f:
            for key, pattern in PATTERNS.items():
                M = pattern.search(line)
                if M:
                    stats[key] = M.group(1)
    return stats


def parse_model_logs(logs_dir: Path) -> list:
    """Parse GPU model logs for completion counts and durations."""
    kernels = []
    DURATION_RE = re.compile(r"duration=([\d.]+)ms")
    models_dir = logs_dir / "models"
    if not models_dir.exists():
        return kernels

    for LOG_FILE in sorted(models_dir.glob("*.log")):
        KS = KernelStats(name=LOG_FILE.stem)
        with open(LOG_FILE) as f:
            for line in f:
                if "GPU complete" in line:
                    KS.completions += 1
                    KS.last_line = line.strip()
                    DM = DURATION_RE.search(line)
                    if DM:
                        KS.durations_ms.append(float(DM.group(1)))
        if KS.completions > 0:
            kernels.append(KS)
    return kernels


def parse_sysmon_gpu(logs_dir: Path) -> list:
    """Extract GPU telemetry lines from SystemMonitor log."""
    gpu_lines = []
    SYSMON = logs_dir / "support" / "SystemMonitor_0.log"
    if not SYSMON.exists():
        return gpu_lines
    with open(SYSMON) as f:
        for line in f:
            if "GPU" in line:
                gpu_lines.append(line.strip())
    return gpu_lines


def compute_jitter(samples: list) -> dict:
    """Compute inter-sample timing jitter from heartbeat data."""
    if len(samples) < 3:
        return {}

    # Skip first 2 samples (startup transient)
    DELTAS_NS = []
    for i in range(3, len(samples)):
        DT = samples[i].timestamp_ns - samples[i - 1].timestamp_ns
        if DT > 0:
            DELTAS_NS.append(DT)

    if not DELTAS_NS:
        return {}

    DELTAS_NS.sort()
    N = len(DELTAS_NS)
    MEAN = sum(DELTAS_NS) / N
    VARIANCE = sum((d - MEAN) ** 2 for d in DELTAS_NS) / N
    STDDEV = VARIANCE**0.5

    return {
        "count": N,
        "mean_ms": MEAN / 1e6,
        "stddev_ms": STDDEV / 1e6,
        "min_ms": DELTAS_NS[0] / 1e6,
        "max_ms": DELTAS_NS[-1] / 1e6,
        "p50_ms": DELTAS_NS[N // 2] / 1e6,
        "p95_ms": DELTAS_NS[int(N * 0.95)] / 1e6,
        "p99_ms": DELTAS_NS[int(N * 0.99)] / 1e6,
    }


def compute_overrun_rate(samples: list) -> dict:
    """Compute overrun rate over time windows."""
    if len(samples) < 10:
        return {}

    # Incremental overruns per sample
    INCREMENTAL = []
    for i in range(1, len(samples)):
        DELTA = samples[i].frame_overruns - samples[i - 1].frame_overruns
        CYCLES = samples[i].clock_cycles - samples[i - 1].clock_cycles
        if CYCLES > 0:
            INCREMENTAL.append(DELTA / CYCLES)

    if not INCREMENTAL:
        return {}

    INCREMENTAL.sort()
    N = len(INCREMENTAL)
    return {
        "mean_pct": sum(INCREMENTAL) / N * 100,
        "max_pct": INCREMENTAL[-1] * 100,
        "min_pct": INCREMENTAL[0] * 100,
        "total_overruns": samples[-1].frame_overruns,
        "total_cycles": samples[-1].clock_cycles,
        "overall_pct": samples[-1].frame_overruns / max(samples[-1].clock_cycles, 1) * 100,
    }


def fetch_ssh(target: str, local_dir: Path):
    """Fetch soak test artifacts from SSH target."""
    FILES = [
        "heartbeat.csv",
        "system.log",
        "logs/models/",
        "logs/support/",
    ]
    for F in FILES:
        SRC = f"{target}/{F}"
        if F.endswith("/"):
            DST = local_dir / F
            DST.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                ["scp", "-r", SRC, str(DST.parent)],
                capture_output=True,
                timeout=30,
            )
        else:
            DST = local_dir / F
            DST.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                ["scp", SRC, str(DST)],
                capture_output=True,
                timeout=30,
            )


def print_report(base_dir: Path):
    """Generate and print the full soak test report."""
    HEARTBEAT = base_dir / "heartbeat.csv"
    SYSLOG = base_dir / "system.log"
    LOGS_DIR = base_dir / "logs"

    print("=" * 72)
    print("  ApexEdgeDemo Soak Test Report")
    print("=" * 72)
    print()

    # System stats
    if SYSLOG.exists():
        STATS = parse_system_log(SYSLOG)
        print("--- Executive Summary ---")
        print(f"  Runtime:          {STATS.get('runtime', '?')} seconds")
        print(f"  Clock cycles:     {STATS.get('clock_cycles', '?')}")
        print(f"  Task cycles:      {STATS.get('task_cycles', '?')}")
        lag = STATS.get("cycle_lag", "?")
        pct = STATS.get("completion_pct", "?")
        print(f"  Cycle lag:        {lag} ({pct}% completion)")
        print(f"  Frame overruns:   {STATS.get('frame_overruns', '?')}")
        print(f"  Avg utilization:  {STATS.get('utilization', '?')}%")
        print()

    # Heartbeat jitter
    if HEARTBEAT.exists():
        SAMPLES = parse_heartbeat(HEARTBEAT)
        print(f"--- Heartbeat Analysis ({len(SAMPLES)} samples) ---")

        JITTER = compute_jitter(SAMPLES)
        if JITTER:
            print("  Inter-sample timing (target: 1000.0 ms):")
            print(f"    Mean:   {JITTER['mean_ms']:.3f} ms")
            print(f"    Stddev: {JITTER['stddev_ms']:.3f} ms")
            print(f"    Min:    {JITTER['min_ms']:.3f} ms")
            print(f"    Max:    {JITTER['max_ms']:.3f} ms")
            print(f"    P50:    {JITTER['p50_ms']:.3f} ms")
            print(f"    P95:    {JITTER['p95_ms']:.3f} ms")
            print(f"    P99:    {JITTER['p99_ms']:.3f} ms")
            print()

        OVERRUNS = compute_overrun_rate(SAMPLES)
        if OVERRUNS:
            print("  Overrun analysis:")
            total = OVERRUNS["total_overruns"]
            total_cyc = OVERRUNS["total_cycles"]
            overall_pct = OVERRUNS["overall_pct"]
            print(f"    Total:    {total} / {total_cyc} cycles ({overall_pct:.1f}%)")
            print(f"    Mean:     {OVERRUNS['mean_pct']:.1f}% per window")
            print(f"    Peak:     {OVERRUNS['max_pct']:.1f}% per window")
            print()

    # GPU kernel stats
    KERNELS = parse_model_logs(LOGS_DIR)
    if KERNELS:
        print("--- GPU Kernel Performance ---")
        for K in KERNELS:
            print(f"  {K.name}:")
            print(f"    Completions: {K.completions}")
            if K.durations_ms:
                K.durations_ms.sort()
                N = len(K.durations_ms)
                MEAN = sum(K.durations_ms) / N
                p50 = K.durations_ms[N // 2]
                p95 = K.durations_ms[int(N * 0.95)]
                print(
                    f"    Duration:    {MEAN:.1f} ms mean, "
                    f"{K.durations_ms[0]:.1f} ms min, {K.durations_ms[-1]:.1f} ms max"
                )
                print(f"    P50/P95:     {p50:.1f} / {p95:.1f} ms")
        print()

    # GPU telemetry
    GPU_LINES = parse_sysmon_gpu(LOGS_DIR)
    if GPU_LINES:
        print("--- GPU Telemetry (SystemMonitor) ---")
        # Show first and last
        print(f"  First: {GPU_LINES[0]}")
        if len(GPU_LINES) > 1:
            print(f"  Last:  {GPU_LINES[-1]}")
        THROTTLE_COUNT = sum(1 for L in GPU_LINES if "throttl" in L.lower())
        print(f"  Throttle events: {THROTTLE_COUNT}")
        print()

    print("=" * 72)
    print("  End of Report")
    print("=" * 72)


def main():
    PARSER = argparse.ArgumentParser(description="Analyze ApexEdgeDemo soak test")
    PARSER.add_argument("path", help="Local directory or SSH target (user@host:path)")
    ARGS = PARSER.parse_args()

    if ":" in ARGS.path and "@" in ARGS.path:
        # SSH target
        with tempfile.TemporaryDirectory() as TMP:
            TMP_PATH = Path(TMP)
            print(f"Fetching from {ARGS.path}...")
            fetch_ssh(ARGS.path, TMP_PATH)
            print_report(TMP_PATH)
    else:
        print_report(Path(ARGS.path))


if __name__ == "__main__":
    main()
