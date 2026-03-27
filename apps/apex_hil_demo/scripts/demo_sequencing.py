#!/usr/bin/env python3
"""
Demo: RTS/ATS onboard command sequencing.

Demonstrates:
  1. Connect to running ApexHilDemo
  2. Transfer an RTS file to the filesystem (rts/ directory)
  3. Load the RTS into a sequence slot (LOAD_RTS command)
  4. Start the sequence (START_RTS command)
  5. Monitor sequence status
  6. Stop the sequence (STOP_RTS command)

Usage:
  python3 demo_sequencing.py [--host HOST] [--port PORT] [--rts PATH] [--slot N]

Examples:
  # NOOP sweep (connectivity test)
  python3 demo_sequencing.py --rts tprm/rts/rts_001_noop_sweep.rts --slot 0

  # Safe mode entry
  python3 demo_sequencing.py --rts tprm/rts/rts_002_safe_mode.rts --slot 1
"""

import argparse
import struct
import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.c2.client import AprotoClient  # noqa: E402

# ActionComponent opcodes (0x0500 range)
OPCODE_LOAD_RTS = 0x0500
OPCODE_START_RTS = 0x0501
OPCODE_STOP_RTS = 0x0502

# ActionComponent fullUid
ACTION_FULLUID = 0x000500


def progress(sent: int, total: int) -> None:
    if sent == total:
        print(f"  Transferred {total} chunks")


def main() -> int:
    parser = argparse.ArgumentParser(description="Demo: RTS sequencing")
    parser.add_argument("--host", default="raspberrypi.local")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--rts", required=True, help="Path to .rts binary file")
    parser.add_argument("--slot", type=int, default=0, help="Sequence slot (0-7)")
    parser.add_argument("--no-start", action="store_true", help="Load only, don't start")
    args = parser.parse_args()

    rts_path = __import__("pathlib").Path(args.rts)
    if not rts_path.exists():
        print(f"RTS file not found: {rts_path}")
        return 1

    print(f"Connecting to {args.host}:{args.port}...")
    with AprotoClient(args.host, args.port) as c2:
        # Baseline health
        health = c2.get_health()
        print(f"Health: {health['status_name']}")
        cycles = c2.get_clock_cycles()
        print(f"Clock cycles: {cycles}")

        # Step 1: Transfer RTS file to rts/ directory
        dest_filename = rts_path.name
        dest_path = f"rts/{dest_filename}"
        print(f"\n[1] Uploading {rts_path.name} -> {dest_path}...")
        c2.file_send(str(rts_path), dest_path, progress_cb=progress)
        print("  Upload complete.")

        # Step 2: Load RTS into slot
        filename_bytes = dest_path.encode("utf-8") + b"\x00"
        payload = struct.pack("B", args.slot) + filename_bytes
        print(f"\n[2] LOAD_RTS: slot={args.slot}, file={dest_path}...")
        result = c2.send_command(ACTION_FULLUID, OPCODE_LOAD_RTS, payload)
        print(f"  Result: {result['result_name']} ({result['result_code']})")

        if result["result_code"] != 0:
            print("  LOAD_RTS failed. Aborting.")
            return 1

        if args.no_start:
            print("\n--no-start specified. Sequence loaded but not started.")
            return 0

        # Step 3: Start the sequence
        payload = struct.pack("B", args.slot)
        print(f"\n[3] START_RTS: slot={args.slot}...")
        result = c2.send_command(ACTION_FULLUID, OPCODE_START_RTS, payload)
        print(f"  Result: {result['result_name']} ({result['result_code']})")

        if result["result_code"] != 0:
            print("  START_RTS failed.")
            return 1

        print(f"\nSequence started in slot {args.slot}.")
        print("The sequence will execute autonomously.")
        print("Use STOP_RTS to abort if needed:")
        print(
            f"  c2.send_command(0x{ACTION_FULLUID:06X}, 0x{OPCODE_STOP_RTS:04X}, "
            f"struct.pack('B', {args.slot}))"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
