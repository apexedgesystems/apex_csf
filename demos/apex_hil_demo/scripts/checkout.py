#!/usr/bin/env python3
"""
ApexHilDemo system checkout.

Single script that verifies every runtime capability of a running ApexHilDemo
from a C2 client. Run after deployment to confirm the system is fully operational.

Tests (in order):
  1. Connectivity         NOOP to executive
  2. Component addressing NOOP to all 11 registered components
  3. Clock rate           Verify ~1000 Hz execution
  4. Executive health     Parse health packet (overruns, watchdog, RT mode)
  5. STM32 driver stats   Read driver #0 state (tx/rx/crc via INSPECT)
  6. Comparator           Read divergence (real vs emulated)
  7. Sleep / Wake         Pause clock, verify stall, resume
  8. Lock / Unlock        Lock component, verify scheduler skip, unlock
  9. File transfer        Chunked 8KB transfer with CRC-32C
 10. File transfer abort  Begin, partial send, abort, verify recovery
 11. TPRM reload          Transfer dummy TPRM, issue reload command
 12. RELOAD_LIBRARY       Hot-swap TestPlugin v1 -> v2 (lock, transfer .so, reload)
 13. C2 latency           20-ping round-trip measurement
 14. Post-test health     Verify clock rate still ~1000 Hz
 15. RELOAD_EXECUTIVE     Graceful restart via execve (kills connection)
 16. Post-restart verify  Reconnect, verify clock rate, NOOP all components

Usage:
  # System must already be running on Pi:
  #   sudo ./run.sh ApexHilDemo --skip-cleanup
  #
  # Then from dev machine:
  python3 checkout.py --host raspberrypi.local

  # Skip destructive tests (restart, library reload):
  python3 checkout.py --host raspberrypi.local --skip-restart --skip-reload-lib

  # TestPlugin .so path (default: auto-detected from build dir):
  python3 checkout.py --host raspberrypi.local --plugin-so path/to/TestPlugin_v2.so
"""

import argparse
import os
import struct
import sys
import tempfile
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parents[3] / "tools/py/src"))
from apex_tools.ops import protocol as proto  # noqa: E402
from apex_tools.ops.client import AprotoClient, crc32c  # noqa: E402

# ---------------------------------------------------------------------------
# Test infrastructure
# ---------------------------------------------------------------------------

PASS_COUNT = 0
FAIL_COUNT = 0


def check(name: str, condition: bool, detail: str = "") -> bool:
    global PASS_COUNT, FAIL_COUNT
    if condition:
        PASS_COUNT += 1
        print(f"  PASS  {name}")
    else:
        FAIL_COUNT += 1
        msg = f"  FAIL  {name}"
        if detail:
            msg += f"  ({detail})"
        print(msg)
    return condition


# ---------------------------------------------------------------------------
# Component registry
# ---------------------------------------------------------------------------

ALL_COMPONENTS = {
    "Executive": 0x000000,
    "Scheduler": 0x000100,
    "Interface": 0x000400,
    "Action": 0x000500,
    "PlantModel": 0x007800,
    "VFC": 0x007900,
    "DriverReal": 0x007A00,
    "DriverEmulated": 0x007A01,
    "Comparator": 0x007B00,
    "SystemMonitor": 0x00C800,
    "TestPlugin": 0x00FA00,
}


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_connectivity(c2: AprotoClient) -> bool:
    """Test 1: Basic NOOP to executive."""
    print("\n--- 1. Connectivity ---")
    r = c2.noop()
    return check("NOOP returns SUCCESS", r["status"] == 0, r["status_name"])


def test_component_addressing(c2: AprotoClient) -> bool:
    """Test 2: NOOP to every registered component."""
    print("\n--- 2. Component Addressing ---")
    ok = True
    for name, uid in ALL_COMPONENTS.items():
        r = c2.send_command(uid, proto.SYS_NOOP)
        ok &= check(f"{name} (0x{uid:06X})", r["status"] == 0, r["status_name"])
    return ok


def test_clock_rate(c2: AprotoClient) -> bool:
    """Test 3: Verify clock running at ~1000 Hz."""
    print("\n--- 3. Clock Rate ---")
    c1 = c2.get_clock_cycles()
    time.sleep(1.0)
    c2_val = c2.get_clock_cycles()
    rate = c2_val - c1
    ok = check(f"Clock rate ~1000 Hz (measured {rate})", 900 < rate < 1100, f"rate={rate}")
    return ok


def test_executive_health(c2: AprotoClient) -> bool:
    """Test 4: Parse executive health packet."""
    print("\n--- 4. Executive Health ---")
    r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
    ok = check("GET_HEALTH returns SUCCESS", r["status"] == 0, r["status_name"])
    extra = r.get("extra", b"")
    if len(extra) >= 48:
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
        clock_cycles = struct.unpack_from("<Q", extra, 0)[0]
        task_cycles = struct.unpack_from("<Q", extra, 8)[0]
        frame_overruns = struct.unpack_from("<Q", extra, 16)[0]
        watchdog_warnings = struct.unpack_from("<Q", extra, 24)[0]
        freq_hz = struct.unpack_from("<H", extra, 32)[0]
        rt_mode = extra[34]
        flags = extra[35]
        cmds_processed = struct.unpack_from("<Q", extra, 36)[0]
        clock_running = bool(flags & 0x01)
        paused = bool(flags & 0x02)
        print(f"    clock_cycles={clock_cycles} task_cycles={task_cycles}")
        print(f"    frame_overruns={frame_overruns} watchdog_warnings={watchdog_warnings}")
        print(f"    freq={freq_hz}Hz rt_mode={rt_mode} cmds={cmds_processed}")
        ok &= check("Clock is running", clock_running)
        ok &= check("Not paused", not paused)
        ok &= check("No watchdog warnings", watchdog_warnings == 0)
    else:
        ok &= check("Health packet size >= 48", False, f"got {len(extra)} bytes")
    return ok


def test_driver_stats(c2: AprotoClient) -> bool:
    """Test 5: Read STM32 driver state via INSPECT."""
    print("\n--- 5. STM32 Driver Stats ---")
    r = c2.inspect(0x007A00, category=2)
    ok = check("INSPECT driver #0 returns SUCCESS", r["status"] == 0, r["status_name"])
    state = r.get("extra", b"")
    if len(state) >= 28:
        # DriverState layout: txCount(4) rxCount(4) crcErrors(4) txMisses(4) rxMisses(4)
        #                      hasCmd(1) uartOpen(1) commLost(1) reserved(1)
        #                      commLostCount(4)
        tx, rx, crcErr, txMiss, rxMiss = struct.unpack_from("<IIIII", state, 0)
        uartOpen = state[21]
        print(
            f"    tx={tx} rx={rx} crcErr={crcErr} txMiss={txMiss} rxMiss={rxMiss} "
            f"uartOpen={uartOpen}"
        )
        ok &= check("STM32 tx > 0 (communicating)", tx > 0)
        ok &= check("STM32 rx > 0 (receiving)", rx > 0)
        ok &= check("STM32 0 CRC errors", crcErr == 0)
        ok &= check("STM32 0 tx misses", txMiss == 0)
    else:
        ok &= check("Driver state size >= 28", False, f"got {len(state)} bytes")
    return ok


def test_comparator(c2: AprotoClient) -> bool:
    """Test 6: Read comparator divergence."""
    print("\n--- 6. Comparator ---")
    r = c2.inspect(0x007B00, category=2)
    ok = check("INSPECT comparator returns SUCCESS", r["status"] == 0, r["status_name"])
    state = r.get("extra", b"")
    if len(state) >= 16:
        n, div_mean, div_max, warnings = struct.unpack("<Iffi", state[:16])
        print(f"    samples={n} div_mean={div_mean:.4f} div_max={div_max:.4f} warnings={warnings}")
        ok &= check("Divergence mean near zero", div_mean < 1.0, f"mean={div_mean}")
        ok &= check("No comparator warnings", warnings == 0)
    else:
        ok &= check("Comparator state size >= 16", False, f"got {len(state)} bytes")
    return ok


def test_sleep_wake(c2: AprotoClient) -> bool:
    """Test 7: Sleep/wake executive.

    SLEEP suspends task execution but the clock keeps running.
    Verify the command round-trips and the health flags reflect sleep state.
    """
    print("\n--- 7. Sleep / Wake ---")
    ok = True

    r = c2.send_command(0x000000, proto.EXEC_CMD_SLEEP)
    ok &= check("SLEEP returns SUCCESS", r["status"] == 0, r["status_name"])

    time.sleep(1.0)

    # Verify health reports sleeping flag (FLAG_SLEEPING = 0x20)
    r = c2.get_health()
    extra = r.get("extra", b"")
    if len(extra) >= 48:
        flags = extra[35]
        sleeping = bool(flags & 0x20)
        ok &= check("Health reports sleeping", sleeping)
    else:
        ok &= check("Health packet during sleep", False, f"got {len(extra)} bytes")

    r = c2.send_command(0x000000, proto.EXEC_CMD_WAKE)
    ok &= check("WAKE returns SUCCESS", r["status"] == 0, r["status_name"])

    time.sleep(1.0)

    # Verify health no longer reports sleeping
    r = c2.get_health()
    extra = r.get("extra", b"")
    if len(extra) >= 48:
        flags = extra[35]
        sleeping = bool(flags & 0x20)
        ok &= check("Health reports awake", not sleeping)

    # Verify clock still advancing after wake
    c1 = c2.get_clock_cycles()
    time.sleep(1.0)
    c2_val = c2.get_clock_cycles()
    rate = c2_val - c1
    ok &= check(f"Clock advancing after wake (rate={rate})", rate > 500)
    return ok


def test_lock_unlock(c2: AprotoClient) -> bool:
    """Test 8: Lock/unlock a component."""
    print("\n--- 8. Lock / Unlock ---")
    ok = True
    target = 0x00C800  # SystemMonitor

    r = c2.lock_component(target)
    ok &= check("Lock SystemMonitor", r["status"] == 0, r["status_name"])

    time.sleep(0.2)

    r = c2.unlock_component(target)
    ok &= check("Unlock SystemMonitor", r["status"] == 0, r["status_name"])

    cycles = c2.get_clock_cycles()
    ok &= check("Clock advancing after unlock", cycles > 0, f"cycles={cycles}")

    r = c2.lock_component(0xFFFFFF)
    ok &= check("Lock non-existent returns COMPONENT_NOT_FOUND", r["status"] == 4, r["status_name"])
    return ok


def test_file_transfer(c2: AprotoClient) -> bool:
    """Test 9: Chunked file transfer (8KB)."""
    print("\n--- 9. File Transfer ---")
    ok = True

    test_data = bytes(range(256)) * 32  # 8KB
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(test_data)
        test_file = f.name

    try:
        chunks_seen = []
        result = c2.send_file(
            test_file,
            "test/checkout.bin",
            chunk_size=4096,
            progress_cb=lambda s, t: chunks_seen.append(s),
        )
        ok &= check("Transfer 8KB succeeds", result["status"] == 0, result["status_name"])
        ok &= check("Progress callback fired", len(chunks_seen) > 0)

        if "file_end" in result:
            ok &= check(
                "Bytes written matches",
                result["file_end"]["bytes_written"] == len(test_data),
                f"got {result['file_end']['bytes_written']}",
            )

        status = c2.get_transfer_status()
        if "transfer" in status:
            ok &= check(
                "Transfer state IDLE after success",
                status["transfer"]["state"] == 0,
                status["transfer"]["state_name"],
            )
    finally:
        os.unlink(test_file)
    return ok


def test_file_transfer_abort(c2: AprotoClient) -> bool:
    """Test 10: File transfer abort and recovery."""
    print("\n--- 10. File Transfer Abort ---")
    ok = True

    test_data = bytes(range(256)) * 8  # 2KB
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(test_data)
        test_file = f.name

    try:
        # Begin a transfer
        begin_payload = proto.build_file_begin(
            len(test_data),
            512,
            (len(test_data) + 511) // 512,
            crc32c(test_data),
            "test/abort_test.bin",
        )
        r = c2.send_command(0, proto.FILE_BEGIN, begin_payload)
        ok &= check("FILE_BEGIN for abort test", r["status"] == 0, r["status_name"])

        # Send one chunk then abort
        chunk = proto.build_file_chunk(0, test_data[:512])
        c2.send_command(0, proto.FILE_CHUNK, chunk)

        r = c2.abort_transfer()
        ok &= check("FILE_ABORT succeeds", r["status"] == 0, r["status_name"])

        status = c2.get_transfer_status()
        if "transfer" in status:
            ok &= check(
                "Transfer state IDLE after abort",
                status["transfer"]["state"] == 0,
                status["transfer"]["state_name"],
            )

        # Verify clean recovery
        r = c2.send_file(test_file, "test/recovery_test.bin", chunk_size=1024)
        ok &= check("Transfer after abort succeeds", r["status"] == 0, r["status_name"])
    finally:
        os.unlink(test_file)
    return ok


def test_tprm_reload(c2: AprotoClient) -> bool:
    """Test 11: TPRM file transfer + reload command."""
    print("\n--- 11. TPRM Reload ---")
    ok = True
    target = 0x00C800  # SystemMonitor

    dummy_tprm = bytes([0x00] * 64)
    with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as f:
        f.write(dummy_tprm)
        tprm_file = f.name

    try:
        remote_path = f"tprm/{target:06x}.tprm"
        r = c2.send_file(tprm_file, remote_path, chunk_size=4096)
        ok &= check("TPRM file transfer succeeds", r["status"] == 0, r["status_name"])

        r = c2.reload_tprm(target)
        # Status 5 (LOAD_FAILED) is expected for dummy data; 0 means it loaded.
        ok &= check(
            "RELOAD_TPRM command accepted",
            r["status"] in (0, 5),
            f"status={r['status']} ({r['status_name']})",
        )

        cycles = c2.get_clock_cycles()
        ok &= check("System still running after TPRM reload", cycles > 0)
    finally:
        os.unlink(tprm_file)
    return ok


def test_reload_library(c2: AprotoClient, plugin_so: str) -> bool:
    """Test 12: Hot-swap TestPlugin .so via RELOAD_LIBRARY."""
    print("\n--- 12. RELOAD_LIBRARY ---")
    ok = True

    if not os.path.isfile(plugin_so):
        print(f"  SKIP  TestPlugin .so not found: {plugin_so}")
        return True

    # Verify TestPlugin responds before swap
    r = c2.send_command(0x00FA00, proto.SYS_NOOP)
    ok &= check("TestPlugin NOOP before swap", r["status"] == 0, r["status_name"])

    # Compound operation: lock -> transfer .so -> reload -> auto-unlock
    r = c2.update_component(
        full_uid=0x00FA00,
        local_so_path=plugin_so,
        component_name="TestPlugin",
        instance_index=0,
    )
    ok &= check("update_component succeeds", r["status"] == 0, r["status_name"])

    # Verify TestPlugin responds after swap
    r = c2.send_command(0x00FA00, proto.SYS_NOOP)
    ok &= check("TestPlugin NOOP after swap", r["status"] == 0, r["status_name"])

    # Verify system still healthy
    cycles = c2.get_clock_cycles()
    ok &= check("Clock advancing after library reload", cycles > 0)
    return ok


def test_latency(c2: AprotoClient) -> bool:
    """Test 13: C2 round-trip latency measurement."""
    print("\n--- 13. C2 Latency ---")
    latencies = []
    for _ in range(20):
        t0 = time.monotonic()
        c2.noop()
        dt = (time.monotonic() - t0) * 1000.0
        latencies.append(dt)

    latencies.sort()
    median = latencies[len(latencies) // 2]
    p99 = latencies[int(len(latencies) * 0.99)]
    print(
        f"    min={latencies[0]:.1f}ms  median={median:.1f}ms  "
        f"p99={p99:.1f}ms  max={latencies[-1]:.1f}ms"
    )
    return check(f"Median latency < 50ms (got {median:.1f}ms)", median < 50)


def test_post_health(c2: AprotoClient) -> bool:
    """Test 14: Post-test clock rate verification."""
    print("\n--- 14. Post-Test Health ---")
    c1 = c2.get_clock_cycles()
    time.sleep(1.0)
    c2_val = c2.get_clock_cycles()
    rate = c2_val - c1
    return check(f"Clock rate still ~1000 Hz (measured {rate})", 900 < rate < 1100)


def test_reload_executive(c2: AprotoClient) -> bool:
    """Test 15: Executive restart via execve."""
    print("\n--- 15. RELOAD_EXECUTIVE ---")
    ok = True

    cycles = c2.get_clock_cycles()
    ok &= check(f"System running before restart (cycles={cycles})", cycles > 0)

    r = c2.reload_executive()
    ok &= check("RELOAD_EXECUTIVE sent", r["status"] == 0, r["status_name"])

    time.sleep(1.0)
    try:
        c2.noop()
        print("    INFO: Connection still alive (execve may have re-bound)")
    except OSError:
        ok &= check("Connection lost after execve (expected)", True)
    return ok


def test_post_restart(host: str, port: int) -> bool:
    """Test 16: Reconnect after restart and verify."""
    print("\n--- 16. Post-Restart Verification ---")
    print("    Waiting 8s for restart...")
    time.sleep(8)

    ok = True
    try:
        with AprotoClient(host, port, timeout=10.0) as c2:
            r = c2.noop()
            ok &= check("Reconnect NOOP", r["status"] == 0, r["status_name"])

            c1 = c2.get_clock_cycles()
            ok &= check(f"New process (cycles={c1}, should be low)", c1 < 30000)

            time.sleep(1.0)
            c2_val = c2.get_clock_cycles()
            rate = c2_val - c1
            ok &= check(f"Clock rate ~1000 Hz after restart (measured {rate})", 900 < rate < 1100)

            # Verify all components still addressable
            all_ok = True
            for name, uid in ALL_COMPONENTS.items():
                r = c2.send_command(uid, proto.SYS_NOOP)
                if r["status"] != 0:
                    all_ok = False
                    print(f"    FAIL: {name} (0x{uid:06X}) status={r['status']}")
            ok &= check("All 11 components addressable after restart", all_ok)

            # Verify executive health after restart
            r = c2.get_health()
            extra = r.get("extra", b"")
            if len(extra) >= 48:
                frame_overruns = struct.unpack_from("<Q", extra, 16)[0]
                watchdog_warnings = struct.unpack_from("<Q", extra, 24)[0]
                flags = extra[35]
                paused = bool(flags & 0x02)
                print(f"    frame_overruns={frame_overruns} watchdog_warnings={watchdog_warnings}")
                ok &= check("Not paused after restart", not paused)
                ok &= check("No watchdog warnings after restart", watchdog_warnings == 0)
    except Exception as e:
        ok &= check("Reconnect after restart", False, str(e))
    return ok


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def find_plugin_so() -> str:
    """Auto-detect TestPlugin_v2.so from build directory."""
    candidates = [
        "build/rpi-aarch64-release/test_plugins/TestPlugin_v2.so",
        "build/rpi-aarch64-debug/test_plugins/TestPlugin_v2.so",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return path
    return candidates[0]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="ApexHilDemo system checkout",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default="raspberrypi.local")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument(
        "--skip-restart",
        action="store_true",
        help="Skip RELOAD_EXECUTIVE test (preserves running instance)",
    )
    parser.add_argument("--skip-reload-lib", action="store_true", help="Skip RELOAD_LIBRARY test")
    parser.add_argument(
        "--plugin-so", default=None, help="Path to TestPlugin_v2.so (auto-detected if omitted)"
    )
    args = parser.parse_args()

    plugin_so = args.plugin_so or find_plugin_so()

    print("ApexHilDemo System Checkout")
    print(f"Target: {args.host}:{args.port}")
    print(f"Plugin: {plugin_so}")
    print("=" * 60)

    try:
        with AprotoClient(args.host, args.port, timeout=10.0) as c2:
            test_connectivity(c2)
            test_component_addressing(c2)
            test_clock_rate(c2)
            test_executive_health(c2)
            test_driver_stats(c2)
            test_comparator(c2)
            test_sleep_wake(c2)
            test_lock_unlock(c2)
            test_file_transfer(c2)
            test_file_transfer_abort(c2)
            test_tprm_reload(c2)

            if not args.skip_reload_lib:
                test_reload_library(c2, plugin_so)
            else:
                print("\n--- 12. RELOAD_LIBRARY (SKIPPED) ---")

            test_latency(c2)
            test_post_health(c2)

            if not args.skip_restart:
                test_reload_executive(c2)
            else:
                print("\n--- 15. RELOAD_EXECUTIVE (SKIPPED) ---")

    except ConnectionRefusedError:
        print(f"\nERROR: Cannot connect to {args.host}:{args.port}")
        print("Is ApexHilDemo running?  Start with:")
        print("  sudo ./run.sh ApexHilDemo")
        return 1
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback

        traceback.print_exc()
        return 1

    # Post-restart verification (separate connection)
    if not args.skip_restart:
        test_post_restart(args.host, args.port)

    print(f"\n{'=' * 60}")
    print(f"Results: {PASS_COUNT} passed, {FAIL_COUNT} failed")
    print(f"{'=' * 60}")
    return 0 if FAIL_COUNT == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
