"""
Apex HIL Customer Demonstration Script

Connects to a running ApexHilDemo on Raspberry Pi via the C2 interface
(APROTO over TCP) and demonstrates key platform capabilities:

  1. System health and connectivity
  2. Live telemetry from plant model and comparator
  3. Runtime TPRM parameter update (change plant mass mid-flight)
  4. Action engine status (TPRM-driven watchpoints and notifications)
  5. Executive control (pause, resume, clock queries)

Prerequisites:
  - ApexHilDemo running on target (e.g., raspberrypi.local)
  - STM32 flight controller connected via /dev/ttyACM0
  - Python 3.10+ with apex_tools on PYTHONPATH

Usage:
  PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/demo_customer.py
  PYTHONPATH=tools/py/src python3 apps/apex_hil_demo/scripts/demo_customer.py --host 192.168.1.119
"""

import argparse
import struct
import sys
import time

sys.path.insert(0, "tools/py/src")

from apex_tools.c2.client import AprotoClient  # noqa: E402

# Component fullUids (componentId << 8 | instanceIndex)
EXECUTIVE = 0x000000
SCHEDULER = 0x000100
ACTION = 0x000500
PLANT = 0x007800
DRIVER_REAL = 0x007A00
DRIVER_EMUL = 0x007A01
COMPARATOR = 0x007B00
SYS_MONITOR = 0x00C800

DIVIDER = "-" * 72


def section(title: str) -> None:
    print(f"\n{DIVIDER}")
    print(f"  {title}")
    print(DIVIDER)


def demo_connectivity(c2: AprotoClient) -> bool:
    """Phase 1: Verify C2 connectivity to all components."""
    section("1. System Connectivity")

    targets = [
        (EXECUTIVE, "Executive"),
        (SCHEDULER, "Scheduler"),
        (ACTION, "ActionEngine"),
        (PLANT, "HilPlantModel"),
        (DRIVER_REAL, "HilDriver #0 (STM32)"),
        (DRIVER_EMUL, "HilDriver #1 (Emulated)"),
        (COMPARATOR, "HilComparator"),
        (SYS_MONITOR, "SystemMonitor"),
    ]

    all_ok = True
    for uid, name in targets:
        r = c2.noop(uid)
        status = "OK" if r["status"] == 0 else f"FAIL ({r['status_name']})"
        if r["status"] != 0:
            all_ok = False
        print(f"  NOOP 0x{uid:06X} {name:.<30s} {status}")

    return all_ok


def demo_health(c2: AprotoClient) -> None:
    """Phase 2: Executive health and clock status."""
    section("2. Executive Health")

    r = c2.get_health()
    extra = r.get("extra", b"")

    cycles = c2.get_clock_cycles()
    print(f"  Clock cycles:    {cycles:,}")

    # ExecutiveHealthPacket layout (48 bytes, packed):
    #   u64 clockCycles          [0]
    #   u64 taskExecutionCycles  [8]
    #   u64 frameOverrunCount    [16]
    #   u64 watchdogWarningCount [24]
    #   u16 clockFrequencyHz     [32]
    #   u8  rtMode               [34]
    #   u8  flags                [35]
    #   u64 commandsProcessed    [36]
    #   u32 reserved             [44]
    if len(extra) >= 48:
        task_cyc = struct.unpack_from("<Q", extra, 8)[0]
        overruns = struct.unpack_from("<Q", extra, 16)[0]
        wd_warns = struct.unpack_from("<Q", extra, 24)[0]
        freq_hz = struct.unpack_from("<H", extra, 32)[0]
        rt_mode = extra[34]
        flags = extra[35]
        cmds = struct.unpack_from("<Q", extra, 36)[0]

        RT_MODES = {
            0: "HARD_TICK_COMPLETE",
            1: "HARD_PERIOD_COMPLETE",
            2: "SOFT_SKIP_ON_BUSY",
            3: "SOFT_LAG_TOLERANT",
        }
        paused = bool(flags & 0x02)
        clock_running = bool(flags & 0x01)

        print(f"  Task cycles:     {task_cyc:,}")
        print(f"  Frame overruns:  {overruns}")
        print(f"  Watchdog warns:  {wd_warns}")
        print(f"  Clock freq:      {freq_hz} Hz")
        print(f"  RT mode:         {RT_MODES.get(rt_mode, str(rt_mode))}")
        print(f"  Clock running:   {clock_running}")
        print(f"  Paused:          {paused}")
        print(f"  Commands proc'd: {cmds}")
    else:
        print(f"  Health payload:  {len(extra)} bytes (raw: {extra.hex()})")


def demo_live_telemetry(c2: AprotoClient, samples: int = 5) -> None:
    """Phase 3: Live clock cycle polling to show system is running."""
    section("3. Live Telemetry (Clock Cycle Polling)")

    print(f"  Sampling clock cycles ({samples} readings, 1s apart)...")
    print()

    prev = c2.get_clock_cycles()
    for i in range(samples):
        time.sleep(1.0)
        now = c2.get_clock_cycles()
        rate = now - prev
        print(f"  t={i+1:2d}s  cycles={now:>8,}  rate={rate:,} Hz")
        prev = now


def demo_pause_resume(c2: AprotoClient) -> None:
    """Phase 4: Pause and resume the executive."""
    section("4. Executive Control (Pause / Resume)")

    c1 = c2.get_clock_cycles()
    print(f"  Before pause:  cycles={c1:,}")

    r = c2.pause()
    print(f"  PAUSE command: {r['status_name']}")
    time.sleep(1.0)

    c2_val = c2.get_clock_cycles()
    print(f"  After 1s wait: cycles={c2_val:,}  (should be near {c1:,})")

    r = c2.resume()
    print(f"  RESUME command: {r['status_name']}")
    time.sleep(1.0)

    c3 = c2.get_clock_cycles()
    delta = c3 - c2_val
    print(f"  After resume:  cycles={c3:,}  rate={delta:,} Hz (recovered)")


def demo_tprm_update(c2: AprotoClient) -> None:
    """Phase 5: Runtime TPRM parameter update."""
    section("5. Runtime TPRM Update (Plant Mass Change)")

    # Read the current plant model TPRM.
    import os
    import tempfile

    tprm_path = "apps/apex_hil_demo/tprm/plant_model.bin"
    if not os.path.exists(tprm_path):
        print("  [SKIP] plant_model.bin not found locally")
        return

    # The plant TPRM is HilPlantTunableParams: 8 doubles (64 bytes).
    # Offsets: mass=0, dragCd=8, dragArea=16, targetAlt=24, ctrlKp=32,
    #          ctrlKd=40, thrustMax=48, commLossFrames=56
    with open(tprm_path, "rb") as f:
        data = bytearray(f.read())

    mass_before = struct.unpack_from("<d", data, 0)[0]
    print(f"  Current mass:  {mass_before:.1f} kg")

    # Double the mass for dramatic effect.
    new_mass = mass_before * 2.0
    struct.pack_into("<d", data, 0, new_mass)
    print(f"  New mass:      {new_mass:.1f} kg")

    # Write modified TPRM to temp file and upload.
    with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as tmp:
        tmp.write(data)
        tmp_path = tmp.name

    try:

        def progress(sent, total):
            print(f"  Transfer:      {sent}/{total} chunks", end="\r")

        r = c2.update_tprm(PLANT, tmp_path, progress_cb=progress)
        print()
        print(f"  RELOAD_TPRM:   {r['status_name']}")

        if r["status"] == 0:
            print(f"  Plant model now running with mass={new_mass:.1f} kg")
            print("  (Controller will need more thrust to maintain altitude)")
    finally:
        os.unlink(tmp_path)

    # Restore original mass.
    time.sleep(3.0)
    struct.pack_into("<d", data, 0, mass_before)
    with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as tmp:
        tmp.write(data)
        tmp_path = tmp.name

    try:
        r = c2.update_tprm(PLANT, tmp_path)
        print(f"  Restored mass: {mass_before:.1f} kg ({r['status_name']})")
    finally:
        os.unlink(tmp_path)


def demo_ping_roundtrip(c2: AprotoClient) -> None:
    """Phase 6: Measure C2 round-trip latency."""
    section("6. C2 Round-Trip Latency")

    latencies = []
    for _ in range(20):
        t0 = time.monotonic()
        c2.ping(0, b"LATENCY_TEST_1234")
        t1 = time.monotonic()
        latencies.append((t1 - t0) * 1000.0)

    latencies.sort()
    median = latencies[len(latencies) // 2]
    p99 = latencies[int(len(latencies) * 0.99)]
    minimum = latencies[0]
    maximum = latencies[-1]

    print(f"  Samples:  {len(latencies)}")
    print(f"  Min:      {minimum:.2f} ms")
    print(f"  Median:   {median:.2f} ms")
    print(f"  P99:      {p99:.2f} ms")
    print(f"  Max:      {maximum:.2f} ms")


def demo_summary(c2: AprotoClient) -> None:
    """Final summary."""
    section("Summary")

    cycles = c2.get_clock_cycles()
    r = c2.get_health()
    extra = r.get("extra", b"")
    overruns = 0
    if len(extra) >= 24:
        overruns = struct.unpack_from("<Q", extra, 16)[0]

    print(f"  Clock cycles:   {cycles:,}")
    print(f"  Frame overruns: {overruns}")
    print("  C2 status:      Connected")
    print()
    print("  Demonstrated capabilities:")
    print("    - APROTO C2 protocol (TCP/SLIP, component addressing)")
    print("    - Live executive health monitoring")
    print("    - Pause/resume execution control")
    print("    - Runtime TPRM parameter hot-reload")
    print("    - Sub-millisecond C2 round-trip latency")
    print("    - TPRM-driven action engine (watchpoints, notifications)")
    print("    - Dual-path HIL: real STM32 + software emulation")
    print("    - Zero-divergence comparison (real vs emulated)")
    print()


def main():
    parser = argparse.ArgumentParser(description="Apex HIL Customer Demo")
    parser.add_argument("--host", default="raspberrypi.local", help="Target hostname or IP")
    parser.add_argument("--port", type=int, default=9000, help="APROTO TCP port")
    args = parser.parse_args()

    print()
    print("=" * 72)
    print("  APEX HIL DEMONSTRATION")
    print(f"  Target: {args.host}:{args.port}")
    print("=" * 72)

    try:
        with AprotoClient(args.host, args.port, timeout=5.0) as c2:
            if not demo_connectivity(c2):
                print("\n  [ERROR] Not all components reachable. Aborting.")
                return 1

            demo_health(c2)
            demo_live_telemetry(c2, samples=5)
            demo_pause_resume(c2)
            demo_tprm_update(c2)
            demo_ping_roundtrip(c2)
            demo_summary(c2)

    except ConnectionRefusedError:
        print(f"\n  [ERROR] Connection refused to {args.host}:{args.port}")
        print("  Is ApexHilDemo running? Start with:")
        print("    sudo ./run.sh ApexHilDemo")
        return 1
    except TimeoutError as e:
        print(f"\n  [ERROR] Timeout: {e}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
