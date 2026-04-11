#!/usr/bin/env python3
"""
End-to-end runtime update verification for ApexHilDemo over TCP/SLIP/APROTO.

Tests:
  1. Basic connectivity (NOOP, PING, STATUS)
  2. Executive queries (health, clock cycles, RT mode)
  3. Pause/resume via APROTO
  4. File transfer (small + larger payloads)
  5. TPRM hot-reload with modified parameters
  6. Component lock/unlock
  7. Compound update_tprm (transfer + reload)
  8. Executive reload (execve) + reconnect
  9. Graceful shutdown via APROTO

Usage:
  # Start ApexHilDemo first (in Docker or natively):
  #   ./ApexHilDemo --config ... --shutdown-after 60
  # Then run this script (from repo root):
  #   python3 apps/apex_hil_demo/test/test_c2_runtime_update.py
"""

import struct
import sys
import time

sys.path.insert(0, "tools/py/src")

from apex_tools.ops.client import AprotoClient  # noqa: E402

# HIL demo component fullUids
EXEC_UID = 0x000000
SCHEDULER_UID = 0x000100
PLANT_UID = 0x007800
DRIVER_REAL_UID = 0x007A00
DRIVER_EMU_UID = 0x007A01
COMPARATOR_UID = 0x007B00

HOST = "127.0.0.1"
PORT = 9000
TIMEOUT = 5.0

passed = 0
failed = 0


def check(name: str, result: dict, expect_status: int = 0) -> bool:
    global passed, failed
    status = result.get("status", -1)
    ok = status == expect_status
    tag = "PASS" if ok else "FAIL"
    print(f"  [{tag}] {name}: status={status} ({result.get('status_name', '?')})")
    if ok:
        passed += 1
    else:
        failed += 1
        print(f"         Expected status={expect_status}, got {status}")
        if "extra" in result:
            print(f"         extra={result['extra']}")
    return ok


def make_plant_tprm(
    mass: float = 10.0,
    drag_cd: float = 0.5,
    drag_area: float = 0.1,
    target_alt: float = 100.0,
    ctrl_kp: float = 2.0,
    ctrl_kd: float = 1.5,
    thrust_max: float = 200.0,
    comm_loss_frames: float = 25.0,
) -> bytes:
    """Build a 64-byte HilPlantTunableParams binary."""
    return struct.pack(
        "<8d", mass, drag_cd, drag_area, target_alt, ctrl_kp, ctrl_kd, thrust_max, comm_loss_frames
    )


def make_comparator_tprm(warn_threshold: float = 0.1, reserved: float = 0.0) -> bytes:
    """Build an 8-byte ComparatorTunableParams binary."""
    return struct.pack("<2f", warn_threshold, reserved)


def write_temp_tprm(data: bytes, name: str) -> str:
    """Write TPRM binary to test directory."""
    path = f"apps/apex_hil_demo/test/{name}"
    with open(path, "wb") as f:
        f.write(data)
    return path


def main():
    global passed, failed

    # Generate test TPRM files with modified parameters
    print("=== Generating test TPRM artifacts ===")

    # Plant: change target altitude from 100m to 50m, increase thrust max
    plant_modified = make_plant_tprm(
        mass=12.0,  # was 10.0
        target_alt=50.0,  # was 100.0
        ctrl_kp=3.0,  # was 2.0
        thrust_max=300.0,  # was 200.0
    )
    plant_path = write_temp_tprm(plant_modified, "plant_modified.bin")
    print(f"  Plant TPRM: {len(plant_modified)} bytes (mass=12, alt=50, kp=3, thrust=300)")

    # Comparator: change warning threshold from 0.1 to 0.5
    comp_modified = make_comparator_tprm(warn_threshold=0.5)
    comp_path = write_temp_tprm(comp_modified, "comparator_modified.bin")
    print(f"  Comparator TPRM: {len(comp_modified)} bytes (threshold=0.5)")

    # Larger test payload (simulate a bigger config file)
    large_payload = bytes(range(256)) * 16  # 4096 bytes
    large_path = write_temp_tprm(large_payload, "large_test.bin")
    print(f"  Large test file: {len(large_payload)} bytes")

    print()

    # =========================================================================
    # Phase 1: Basic connectivity
    # =========================================================================
    print("=== Phase 1: Basic Connectivity ===")
    c2 = AprotoClient(HOST, PORT, timeout=TIMEOUT)
    c2.connect()
    print(f"  Connected to {HOST}:{PORT}")

    check("NOOP", c2.noop())
    r = c2.ping(data=b"RUNTIME_UPDATE_TEST")
    ok = check("PING", r)
    if ok and r.get("extra") == b"RUNTIME_UPDATE_TEST":
        print("         Echo payload matches")
    check("GET_STATUS", c2.get_status())

    # =========================================================================
    # Phase 2: Executive queries
    # =========================================================================
    print()
    print("=== Phase 2: Executive Queries ===")
    check("GET_HEALTH", c2.get_health())
    cycles1 = c2.get_clock_cycles()
    print(f"  Clock cycles: {cycles1}")
    passed += 1 if cycles1 > 0 else 0

    # =========================================================================
    # Phase 3: Pause / Resume
    # =========================================================================
    print()
    print("=== Phase 3: Pause / Resume ===")
    check("PAUSE", c2.pause())
    time.sleep(0.3)
    cycles_paused = c2.get_clock_cycles()
    time.sleep(0.3)
    cycles_still_paused = c2.get_clock_cycles()
    if cycles_paused == cycles_still_paused:
        print(f"  [PASS] System paused at cycle {cycles_paused}")
        passed += 1
    else:
        print(f"  [FAIL] Cycles changed while paused: {cycles_paused} -> {cycles_still_paused}")
        failed += 1

    check("RESUME", c2.resume())
    time.sleep(0.3)
    cycles_resumed = c2.get_clock_cycles()
    if cycles_resumed > cycles_still_paused:
        print(
            f"  [PASS] System resumed, cycles advancing ({cycles_still_paused} -> {cycles_resumed})"
        )
        passed += 1
    else:
        print("  [FAIL] Cycles not advancing after resume")
        failed += 1

    # =========================================================================
    # Phase 4: File transfer
    # =========================================================================
    print()
    print("=== Phase 4: File Transfer ===")

    # Small file
    r = c2.send_file(plant_path, "tprm/007800.tprm")
    ok = check("TRANSFER plant TPRM (64B)", r)
    if ok and "file_end" in r:
        bw = r["file_end"]["bytes_written"]
        if bw == 64:
            print(f"         bytes_written={bw} (correct)")
            passed += 1
        else:
            print(f"         bytes_written={bw} (expected 64)")
            failed += 1

    # Larger file (multi-chunk if chunk_size < 4096)
    r = c2.send_file(large_path, "test/large_upload.bin", chunk_size=1024)
    ok = check("TRANSFER large file (4096B, 4 chunks)", r)
    if ok and "file_end" in r:
        bw = r["file_end"]["bytes_written"]
        if bw == 4096:
            print(f"         bytes_written={bw} (correct)")
            passed += 1
        else:
            print(f"         bytes_written={bw} (expected 4096)")
            failed += 1

    # Transfer status after completion
    r = c2.get_transfer_status()
    check("TRANSFER_STATUS (idle after complete)", r)

    # =========================================================================
    # Phase 5: TPRM hot-reload with modified parameters
    # =========================================================================
    print()
    print("=== Phase 5: TPRM Hot-Reload ===")

    # Plant model: transfer modified TPRM + reload
    r = c2.send_file(plant_path, "tprm/007800.tprm")
    check("TRANSFER modified plant TPRM", r)
    r = c2.reload_tprm(PLANT_UID)
    check("RELOAD plant TPRM (mass=12, alt=50)", r)

    # Comparator: transfer modified TPRM + reload
    r = c2.send_file(comp_path, "tprm/007b00.tprm")
    check("TRANSFER modified comparator TPRM", r)
    r = c2.reload_tprm(COMPARATOR_UID)
    check("RELOAD comparator TPRM (threshold=0.5)", r)

    # =========================================================================
    # Phase 6: Component lock/unlock
    # =========================================================================
    print()
    print("=== Phase 6: Lock / Unlock ===")
    check("LOCK scheduler", c2.lock_component(SCHEDULER_UID))
    check("UNLOCK scheduler", c2.unlock_component(SCHEDULER_UID))

    check("LOCK plant", c2.lock_component(PLANT_UID))
    check("UNLOCK plant", c2.unlock_component(PLANT_UID))

    # =========================================================================
    # Phase 7: Compound update_tprm
    # =========================================================================
    print()
    print("=== Phase 7: Compound update_tprm ===")
    r = c2.update_tprm(COMPARATOR_UID, comp_path)
    check("UPDATE_TPRM comparator (transfer + reload)", r)

    # =========================================================================
    # Phase 8: Post-update health check
    # =========================================================================
    print()
    print("=== Phase 8: Post-Update Health ===")
    check("GET_HEALTH after updates", c2.get_health())
    check("PING after updates", c2.ping(data=b"AFTER_UPDATES"))
    cycles2 = c2.get_clock_cycles()
    if cycles2 > cycles1:
        print(f"  [PASS] System still running (cycles {cycles1} -> {cycles2})")
        passed += 1
    else:
        print("  [FAIL] Cycles not advancing")
        failed += 1

    # =========================================================================
    # Phase 9: Executive reload (execve)
    # =========================================================================
    print()
    print("=== Phase 9: Executive Reload ===")
    r = c2.reload_executive()
    check("RELOAD_EXECUTIVE (execve)", r)
    c2.close()

    # Wait for restart
    time.sleep(3)

    # Reconnect
    try:
        c2 = AprotoClient(HOST, PORT, timeout=TIMEOUT)
        c2.connect()
        print("  [PASS] Reconnected after exec reload")
        passed += 1

        check("NOOP after restart", c2.noop())
        check("PING after restart", c2.ping(data=b"POST_RESTART"))

        cycles3 = c2.get_clock_cycles()
        print(f"  Clock cycles after restart: {cycles3}")

    except Exception as e:
        print(f"  [FAIL] Reconnect failed: {e}")
        failed += 1

    # =========================================================================
    # Phase 10: Shutdown
    # =========================================================================
    print()
    print("=== Phase 10: Graceful Shutdown ===")
    check("SHUTDOWN", c2.request_shutdown())
    c2.close()

    # =========================================================================
    # Summary
    # =========================================================================
    print()
    print(f"{'=' * 50}")
    print(f"  PASSED: {passed}")
    print(f"  FAILED: {failed}")
    print(f"  TOTAL:  {passed + failed}")
    print(f"{'=' * 50}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
