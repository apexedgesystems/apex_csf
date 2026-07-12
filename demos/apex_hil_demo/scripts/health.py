#!/usr/bin/env python3
"""
ApexHilDemo health telemetry monitor.

Lightweight read-only health check that queries a running system via APROTO.
No side effects -- safe to run repeatedly during soak tests.

Usage:
  python3 health.py --host raspberrypi.local
  python3 health.py --host raspberrypi.local --watch 30   # repeat every 30s
  python3 health.py --host raspberrypi.local --json        # machine-readable
"""

import argparse
import json
import struct
import sys
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.ops import protocol as proto  # noqa: E402
from apex_tools.ops.client import AprotoClient  # noqa: E402

# Component UIDs for driver/comparator inspection
DRIVER_REAL_UID = 0x007A00
DRIVER_EMU_UID = 0x007A01
COMPARATOR_UID = 0x007B00


def query_health(host: str, port: int, timeout: float) -> dict:
    """Query all health telemetry in a single connection."""
    result = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "host": host,
        "connected": False,
    }

    try:
        with AprotoClient(host, port, timeout=timeout) as c2:
            result["connected"] = True

            # Executive health
            r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
            if r["status"] == 0:
                extra = r.get("extra", b"")
                if len(extra) >= 48:
                    result["executive"] = {
                        "clock_cycles": struct.unpack_from("<Q", extra, 0)[0],
                        "task_cycles": struct.unpack_from("<Q", extra, 8)[0],
                        "frame_overruns": struct.unpack_from("<Q", extra, 16)[0],
                        "watchdog_warnings": struct.unpack_from("<Q", extra, 24)[0],
                        "clock_freq_hz": struct.unpack_from("<H", extra, 32)[0],
                        "rt_mode": extra[34],
                        "flags": extra[35],
                        "commands_processed": struct.unpack_from("<Q", extra, 36)[0],
                    }

            # STM32 driver (real hardware)
            r = c2.inspect(DRIVER_REAL_UID, category=2)
            if r["status"] == 0:
                state = r.get("extra", b"")
                if len(state) >= 28:
                    tx, rx, crcErr, txMiss, rxMiss = struct.unpack_from("<IIIII", state, 0)
                    commLostCount = struct.unpack_from("<I", state, 24)[0]
                    result["stm32_driver"] = {
                        "tx": tx,
                        "rx": rx,
                        "crc_errors": crcErr,
                        "tx_misses": txMiss,
                        "rx_misses": rxMiss,
                        "has_cmd": state[20],
                        "uart_open": state[21],
                        "comm_lost": state[22],
                        "comm_lost_count": commLostCount,
                    }

            # Emulated driver (PTY)
            r = c2.inspect(DRIVER_EMU_UID, category=2)
            if r["status"] == 0:
                state = r.get("extra", b"")
                if len(state) >= 28:
                    tx, rx, crcErr, txMiss, rxMiss = struct.unpack_from("<IIIII", state, 0)
                    commLostCount = struct.unpack_from("<I", state, 24)[0]
                    result["emu_driver"] = {
                        "tx": tx,
                        "rx": rx,
                        "crc_errors": crcErr,
                        "tx_misses": txMiss,
                        "rx_misses": rxMiss,
                    }

            # Comparator
            r = c2.inspect(COMPARATOR_UID, category=2)
            if r["status"] == 0:
                state = r.get("extra", b"")
                if len(state) >= 16:
                    n, div_mean, div_max, warnings = struct.unpack("<Iffi", state[:16])
                    result["comparator"] = {
                        "samples": n,
                        "divergence_mean": round(div_mean, 6),
                        "divergence_max": round(div_max, 6),
                        "warnings": warnings,
                    }

    except OSError as e:
        result["error"] = str(e)

    return result


def print_human(r: dict) -> None:
    """Print health data in human-readable format."""
    print(f"[{r['timestamp']}] {r['host']}", end="")
    if not r["connected"]:
        print(f"  CONNECTION FAILED: {r.get('error', 'unknown')}")
        return
    print()

    if "executive" in r:
        ex = r["executive"]
        uptime_s = ex["clock_cycles"] / max(ex["clock_freq_hz"], 1)
        uptime_m = uptime_s / 60
        flags = ex["flags"]
        state = "RUNNING" if (flags & 0x01) else "STOPPED"
        if flags & 0x02:
            state = "PAUSED"
        if flags & 0x20:
            state = "SLEEPING"
        print(
            f"  Executive: {state}  uptime={uptime_m:.1f}min  "
            f"cycles={ex['clock_cycles']}  freq={ex['clock_freq_hz']}Hz"
        )
        print(
            f"             overruns={ex['frame_overruns']}  "
            f"watchdog_warns={ex['watchdog_warnings']}  "
            f"cmds={ex['commands_processed']}"
        )

    if "stm32_driver" in r:
        d = r["stm32_driver"]
        rx_ratio = d["rx"] / max(d["tx"], 1) * 100
        comm = "LOST" if d["comm_lost"] else "OK"
        print(
            f"  STM32:     tx={d['tx']}  rx={d['rx']}  "
            f"ratio={rx_ratio:.0f}%  "
            f"crcErr={d['crc_errors']}  "
            f"txMiss={d['tx_misses']}  rxMiss={d['rx_misses']}  "
            f"uart={'OK' if d['uart_open'] else 'CLOSED'}  "
            f"comm={comm}  commLostEvents={d['comm_lost_count']}"
        )

    if "emu_driver" in r:
        d = r["emu_driver"]
        print(
            f"  Emulated:  tx={d['tx']}  rx={d['rx']}  "
            f"crcErr={d['crc_errors']}  "
            f"txMiss={d['tx_misses']}  rxMiss={d['rx_misses']}"
        )

    if "comparator" in r:
        c = r["comparator"]
        print(
            f"  Compare:   samples={c['samples']}  "
            f"div_mean={c['divergence_mean']:.6f}  "
            f"div_max={c['divergence_max']:.6f}  "
            f"warnings={c['warnings']}"
        )

    # Summary verdict
    problems = []
    if "stm32_driver" in r:
        d = r["stm32_driver"]
        if d["comm_lost"]:
            problems.append("COMM LOST (STM32 not responding)")
        if d["comm_lost_count"] > 0:
            problems.append(f"Comm loss events: {d['comm_lost_count']}")
        if d["crc_errors"] > 0:
            problems.append(f"CRC errors: {d['crc_errors']}")
        if d["tx_misses"] > 0:
            problems.append(f"TX misses: {d['tx_misses']}")
        if not d["uart_open"]:
            problems.append("UART closed")
    if "executive" in r:
        if r["executive"]["watchdog_warnings"] > 0:
            problems.append(f"Watchdog warnings: {r['executive']['watchdog_warnings']}")
    if "comparator" in r:
        if r["comparator"]["warnings"] > 0:
            problems.append(f"Comparator warnings: {r['comparator']['warnings']}")

    if problems:
        print(f"  VERDICT:   DEGRADED -- {'; '.join(problems)}")
    else:
        print("  VERDICT:   HEALTHY")


def main():
    parser = argparse.ArgumentParser(description="ApexHilDemo health monitor")
    parser.add_argument("--host", default="raspberrypi.local", help="Target hostname")
    parser.add_argument("--port", type=int, default=9000, help="APROTO TCP port")
    parser.add_argument("--timeout", type=float, default=5.0, help="Connection timeout (seconds)")
    parser.add_argument("--watch", type=int, default=0, help="Repeat every N seconds (0 = once)")
    parser.add_argument("--json", action="store_true", help="JSON output")
    args = parser.parse_args()

    try:
        while True:
            result = query_health(args.host, args.port, args.timeout)
            if args.json:
                print(json.dumps(result))
            else:
                print_human(result)

            if args.watch <= 0:
                break
            time.sleep(args.watch)

    except KeyboardInterrupt:
        pass

    # Exit 1 if not connected or has errors
    if not result.get("connected"):
        sys.exit(1)


if __name__ == "__main__":
    main()
