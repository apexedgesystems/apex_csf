#!/usr/bin/env python3
"""
Demo: TPRM hot-reload over APROTO.

Demonstrates:
  1. Connect to running ApexHilDemo
  2. Get executive health (baseline)
  3. Transfer new TPRM file for a component
  4. Issue RELOAD_TPRM command
  5. Verify parameters took effect

Usage:
  python3 demo_tprm_reload.py [--host HOST] [--port PORT] [--tprm PATH] [--uid UID]
"""

import argparse
import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.c2.client import AprotoClient  # noqa: E402


def progress(sent: int, total: int) -> None:
    if sent == total:
        print(f"  Transferred {total} chunks")


def main() -> int:
    parser = argparse.ArgumentParser(description="Demo: TPRM hot-reload")
    parser.add_argument("--host", default="raspberrypi.local")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--tprm", required=True, help="Path to new .tprm file")
    parser.add_argument(
        "--uid",
        type=lambda x: int(x, 0),
        required=True,
        help="Component fullUid (hex, e.g. 0x007800)",
    )
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    with AprotoClient(args.host, args.port) as c2:
        # Baseline health
        health = c2.get_health()
        print(f"Health check: {health['status_name']}")

        # Get clock cycles (baseline)
        cycles = c2.get_clock_cycles()
        print(f"Clock cycles: {cycles}")

        # Transfer + reload
        print(f"\nUpdating TPRM for component 0x{args.uid:06X}...")
        result = c2.update_tprm(args.uid, args.tprm, progress_cb=progress)
        print(f"RELOAD_TPRM result: {result['status_name']}")

        if result["status"] != 0:
            print(f"ERROR: Reload failed: {result}")
            return 1

        # Verify component is still responding
        cycles_after = c2.get_clock_cycles()
        print(f"Clock cycles after reload: {cycles_after}")
        print(f"  (advanced {cycles_after - cycles} cycles during update)")

        print("\nTPRM reload complete.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
