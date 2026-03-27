#!/usr/bin/env python3
"""
Demo: Component lock/unlock for runtime updates.

Demonstrates:
  1. Connect to running ApexHilDemo
  2. Lock a component (scheduler skips its tasks, interface NAKs its commands)
  3. Verify locked status via executive health
  4. Unlock component
  5. Verify component resumes normal operation

Usage:
  python3 demo_lock_unlock.py [--host HOST] [--port PORT] [--uid UID]
"""

import argparse
import sys
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.c2.client import AprotoClient  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description="Demo: Component lock/unlock")
    parser.add_argument("--host", default="raspberrypi.local")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument(
        "--uid",
        type=lambda x: int(x, 0),
        default=0x00C800,
        help="Component fullUid (hex, default: SystemMonitor 0x00C800)",
    )
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    with AprotoClient(args.host, args.port) as c2:
        # Connectivity
        result = c2.noop()
        print(f"NOOP: {result['status_name']}")

        print(f"\nTarget component: 0x{args.uid:06X}")

        # Get baseline clock cycles
        cycles_before = c2.get_clock_cycles()
        print(f"Clock cycles: {cycles_before}")

        # Lock component
        print(f"\nLocking component 0x{args.uid:06X}...")
        result = c2.lock_component(args.uid)
        print(f"  LOCK result: {result['status_name']}")
        if result["status"] != 0:
            print("  ERROR: Lock failed (component may not exist)")
            return 1

        # While locked, try sending a command to the component
        print("  Sending command to locked component...")
        result = c2.command(args.uid, 0x0080)  # GET_COMMAND_COUNT
        print(f"  Command result: {result['status_name']} (expected: may NAK if locked)")

        # Wait a moment to demonstrate scheduler skip
        print("  Waiting 1 second (component tasks should be skipped)...")
        time.sleep(1.0)

        cycles_during = c2.get_clock_cycles()
        print(f"  Clock cycles advanced: {cycles_during - cycles_before}")

        # Unlock
        print(f"\nUnlocking component 0x{args.uid:06X}...")
        result = c2.unlock_component(args.uid)
        print(f"  UNLOCK result: {result['status_name']}")

        # Verify resumed
        time.sleep(0.5)
        cycles_after = c2.get_clock_cycles()
        print(f"  Clock cycles after unlock: {cycles_after}")

        # Command should work again
        result = c2.command(args.uid, 0x0080)  # GET_COMMAND_COUNT
        print(f"  Command after unlock: {result['status_name']}")

        print("\nLock/unlock demo complete.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
