#!/usr/bin/env python3
"""
Demo: Chunked file transfer over APROTO.

Demonstrates:
  1. Connect to running ApexHilDemo
  2. Transfer a test file with progress output
  3. Query FILE_STATUS mid-transfer
  4. Abort a transfer and verify cleanup
  5. Transfer again to verify clean state

Usage:
  python3 demo_file_transfer.py [--host HOST] [--port PORT]
"""

import argparse
import sys
import tempfile

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.ops.client import AprotoClient  # noqa: E402


def progress(sent: int, total: int) -> None:
    pct = sent * 100 // total
    bar = "#" * (pct // 2) + "-" * (50 - pct // 2)
    print(f"\r  [{bar}] {sent}/{total} chunks ({pct}%)", end="", flush=True)
    if sent == total:
        print()


def main() -> int:
    parser = argparse.ArgumentParser(description="Demo: APROTO file transfer")
    parser.add_argument("--host", default="raspberrypi.local")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    with AprotoClient(args.host, args.port) as c2:
        # Connectivity check
        result = c2.noop()
        print(f"NOOP: {result['status_name']}")
        if result["status"] != 0:
            print("ERROR: Cannot reach target")
            return 1

        # Create a test file
        test_data = bytes(range(256)) * 40  # 10KB
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(test_data)
            test_file = f.name
        print(f"Test file: {len(test_data)} bytes")

        # Transfer with progress
        print("Transferring test file...")
        result = c2.send_file(
            test_file, "test/transfer_demo.bin", chunk_size=1024, progress_cb=progress
        )
        print(f"Result: {result['status_name']}")
        if result["status"] != 0:
            print(f"ERROR: Transfer failed: {result}")
            return 1

        # Check transfer status (should be IDLE after successful transfer)
        status = c2.get_transfer_status()
        if "transfer" in status:
            print(f"Transfer state: {status['transfer']['state_name']}")

        # Demonstrate abort: start transfer, abort mid-stream
        print("\nDemonstrating abort...")
        from apex_tools.ops import protocol as proto
        from apex_tools.ops.client import crc32c

        begin_payload = proto.build_file_begin(
            len(test_data),
            1024,
            (len(test_data) + 1023) // 1024,
            crc32c(test_data),
            "test/abort_demo.bin",
        )
        result = c2.send_command(0, proto.FILE_BEGIN, begin_payload)
        print(f"  FILE_BEGIN: {result['status_name']}")

        # Send 2 chunks then abort
        for i in range(2):
            chunk = proto.build_file_chunk(i, test_data[i * 1024 : (i + 1) * 1024])
            c2.send_command(0, proto.FILE_CHUNK, chunk)

        result = c2.abort_transfer()
        print(f"  FILE_ABORT: {result['status_name']}")

        # Verify state is IDLE
        status = c2.get_transfer_status()
        if "transfer" in status:
            print(f"  State after abort: {status['transfer']['state_name']}")

        # Transfer again to verify clean recovery
        print("\nRe-transferring after abort...")
        result = c2.send_file(
            test_file, "test/recovery_demo.bin", chunk_size=2048, progress_cb=progress
        )
        print(f"Result: {result['status_name']}")

        print("\nDone.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
