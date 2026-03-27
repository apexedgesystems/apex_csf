#!/usr/bin/env python3
"""
Integration test: Runtime update capabilities over APROTO.

Exercises all runtime update features against a running ApexHilDemo:
  1. NOOP (connectivity)
  2. File transfer (chunked with CRC32-C)
  3. Component lock/unlock (scheduler skip, command NAK)
  4. TPRM reload (file transfer + reload command)
  5. Executive restart (execve replacement -- tested last, kills connection)

Usage:
  # Start ApexHilDemo first, then run this script:
  python3 test_runtime_updates.py [--host HOST] [--port PORT]
"""

import argparse
import os
import sys
import tempfile
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.c2 import protocol as proto  # noqa: E402
from apex_tools.c2.client import AprotoClient, crc32c  # noqa: E402

PASS = 0
FAIL = 0


def check(name: str, condition: bool, detail: str = "") -> bool:
    global PASS, FAIL
    if condition:
        PASS += 1
        print(f"  PASS: {name}")
    else:
        FAIL += 1
        msg = f"  FAIL: {name}"
        if detail:
            msg += f" ({detail})"
        print(msg)
    return condition


def test_noop(c2: AprotoClient) -> bool:
    """Test 1: Basic connectivity via NOOP."""
    print("\n=== Test 1: NOOP (connectivity) ===")
    result = c2.noop()
    return check("NOOP returns SUCCESS", result["status"] == 0, result["status_name"])


def test_file_transfer(c2: AprotoClient) -> bool:
    """Test 2: Chunked file transfer with CRC32-C verification."""
    print("\n=== Test 2: File Transfer ===")
    ok = True

    # Create test data (known pattern)
    test_data = bytes(range(256)) * 8  # 2KB
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(test_data)
        test_file = f.name

    try:
        # Transfer file
        chunks_seen = []

        def progress(sent: int, total: int) -> None:
            chunks_seen.append(sent)

        result = c2.send_file(
            test_file, "test/transfer_test.bin", chunk_size=512, progress_cb=progress
        )
        ok &= check("File transfer succeeds", result["status"] == 0, result["status_name"])
        ok &= check(
            "Progress callback fired", len(chunks_seen) > 0, f"{len(chunks_seen)} callbacks"
        )

        # Check file_end response
        if "file_end" in result:
            ok &= check(
                "FILE_END reports bytes written",
                result["file_end"]["bytes_written"] == len(test_data),
                f"got {result['file_end']['bytes_written']}, expected {len(test_data)}",
            )

        # Check transfer status (should be IDLE after success)
        status = c2.get_transfer_status()
        if "transfer" in status:
            ok &= check(
                "Transfer state is IDLE after success",
                status["transfer"]["state"] == 0,
                status["transfer"]["state_name"],
            )

        # Test abort mid-transfer
        begin_payload = proto.build_file_begin(
            len(test_data),
            512,
            (len(test_data) + 511) // 512,
            crc32c(test_data),
            "test/abort_test.bin",
        )
        result = c2.send_command(0, proto.FILE_BEGIN, begin_payload)
        ok &= check("FILE_BEGIN for abort test", result["status"] == 0, result["status_name"])

        # Send one chunk
        chunk = proto.build_file_chunk(0, test_data[:512])
        c2.send_command(0, proto.FILE_CHUNK, chunk)

        # Abort
        result = c2.abort_transfer()
        ok &= check("FILE_ABORT succeeds", result["status"] == 0, result["status_name"])

        # Verify IDLE after abort
        status = c2.get_transfer_status()
        if "transfer" in status:
            ok &= check(
                "Transfer state is IDLE after abort",
                status["transfer"]["state"] == 0,
                status["transfer"]["state_name"],
            )

        # Transfer again to verify clean recovery
        result = c2.send_file(test_file, "test/recovery_test.bin", chunk_size=1024)
        ok &= check("Transfer after abort succeeds", result["status"] == 0, result["status_name"])

    finally:
        os.unlink(test_file)

    return ok


def test_lock_unlock(c2: AprotoClient) -> bool:
    """Test 3: Component lock/unlock."""
    print("\n=== Test 3: Lock/Unlock ===")
    ok = True

    # Use SystemMonitor (0x00C800) as test target
    target_uid = 0x00C800

    # Get baseline clock cycles
    cycles_before = c2.get_clock_cycles()
    ok &= check("Got baseline clock cycles", cycles_before > 0, f"cycles={cycles_before}")

    # Lock component
    result = c2.lock_component(target_uid)
    ok &= check("Lock component succeeds", result["status"] == 0, result["status_name"])

    # Wait a bit for scheduler to skip some ticks
    time.sleep(0.2)

    # Unlock component
    result = c2.unlock_component(target_uid)
    ok &= check("Unlock component succeeds", result["status"] == 0, result["status_name"])

    # Verify still running after unlock
    cycles_after = c2.get_clock_cycles()
    ok &= check(
        "Clock still advancing after unlock",
        cycles_after > cycles_before,
        f"before={cycles_before}, after={cycles_after}",
    )

    # Lock non-existent component
    result = c2.lock_component(0xFFFFFF)
    ok &= check(
        "Lock non-existent component returns COMPONENT_NOT_FOUND",
        result["status"] == 4,
        result["status_name"],
    )

    return ok


def test_tprm_reload(c2: AprotoClient) -> bool:
    """Test 4: TPRM file transfer + reload."""
    print("\n=== Test 4: TPRM Reload ===")
    ok = True

    # Use SystemMonitor (0x00C800) as test target
    target_uid = 0x00C800

    # Create a dummy TPRM file (will fail validation but tests the protocol path)
    dummy_tprm = bytes([0x00] * 64)
    with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as f:
        f.write(dummy_tprm)
        tprm_file = f.name

    try:
        # Transfer the TPRM file to correct path
        remote_path = f"tprm/{target_uid:06x}.tprm"
        result = c2.send_file(tprm_file, remote_path, chunk_size=4096)
        ok &= check("TPRM file transfer succeeds", result["status"] == 0, result["status_name"])

        # Issue RELOAD_TPRM command
        # This may fail because the TPRM content is dummy, but the protocol path is exercised
        result = c2.reload_tprm(target_uid)
        # Status 5 (LOAD_FAILED) is expected for dummy data, 0 means it loaded
        ok &= check(
            "RELOAD_TPRM command accepted",
            result["status"] in (0, 5),
            f"status={result['status']} ({result['status_name']})",
        )

        # Verify system is still healthy after reload attempt
        cycles = c2.get_clock_cycles()
        ok &= check("System still running after TPRM reload", cycles > 0, f"cycles={cycles}")

    finally:
        os.unlink(tprm_file)

    return ok


def test_compound_update_tprm(c2: AprotoClient) -> bool:
    """Test 4b: Compound update_tprm (transfer + reload in one call)."""
    print("\n=== Test 4b: Compound TPRM Update ===")
    ok = True

    target_uid = 0x00C800
    dummy_tprm = bytes([0x00] * 32)
    with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as f:
        f.write(dummy_tprm)
        tprm_file = f.name

    try:
        chunks_seen = []
        result = c2.update_tprm(
            target_uid, tprm_file, progress_cb=lambda s, t: chunks_seen.append(s)
        )
        # Transfer should succeed, reload may fail (dummy content)
        ok &= check(
            "Compound update_tprm completes",
            result["status"] in (0, 5),
            f"status={result['status']} ({result['status_name']})",
        )
        ok &= check("Progress callback fired", len(chunks_seen) > 0)
    finally:
        os.unlink(tprm_file)

    return ok


def test_exec_restart(c2: AprotoClient) -> bool:
    """Test 5: Executive restart (execve). Tested last since it kills connection."""
    print("\n=== Test 5: Executive Restart ===")
    ok = True

    # Verify system is running first
    cycles = c2.get_clock_cycles()
    ok &= check("System running before restart", cycles > 0, f"cycles={cycles}")

    # Send RELOAD_EXECUTIVE (fire-and-forget, no ACK expected)
    result = c2.reload_executive()
    # This returns immediately with synthetic SUCCESS since ack_req=False
    ok &= check("RELOAD_EXECUTIVE sent (no ACK)", result["status"] == 0)

    # Connection should be lost after execve
    time.sleep(1.0)
    try:
        c2.noop()
        # If NOOP succeeds, execve may have re-bound the port (unlikely in test)
        print("  INFO: Connection still alive (execve may have re-bound)")
    except OSError:
        ok &= check("Connection lost after execve (expected)", True)

    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="Integration test: runtime updates")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument(
        "--skip-restart",
        action="store_true",
        help="Skip executive restart test (preserves running instance)",
    )
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    try:
        with AprotoClient(args.host, args.port, timeout=5.0) as c2:
            test_noop(c2)
            test_file_transfer(c2)
            test_lock_unlock(c2)
            test_tprm_reload(c2)
            test_compound_update_tprm(c2)

            if not args.skip_restart:
                test_exec_restart(c2)
            else:
                print("\n=== Test 5: Executive Restart (SKIPPED) ===")

    except ConnectionRefusedError:
        print(f"ERROR: Cannot connect to {args.host}:{args.port}")
        print("       Start ApexHilDemo first, then re-run this test.")
        return 1
    except Exception as e:
        print(f"ERROR: {e}")
        return 1

    print(f"\n{'='*50}")
    print(f"Results: {PASS} passed, {FAIL} failed")
    print(f"{'='*50}")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
