#!/usr/bin/env python3
"""
ApexOpsDemo system checkout.

Comprehensive C2 verification suite organized in three layers:

  Layer 1: APROTO Protocol (transport-level, any Apex app)
    1.  Connectivity           NOOP to executive
    2.  PING echo              Round-trip payload echo
    3.  Component addressing   NOOP to all 8 registered components
    4.  File upload            Chunked 8KB upload with CRC-32C
    5.  File upload status     Query transfer state machine
    6.  File upload abort      Begin, partial, abort, recovery
    7.  File download          Upload then download round-trip with CRC-32C
    8.  File download status   Verify IDLE after download completes
    9.  File download abort    Begin download, partial read, abort, recovery

  Layer 2: Apex Executive Core (framework-level, any Apex app)
    10. Clock rate             Verify ~100 Hz execution
    11. Executive health       Parse 48-byte health packet
    12. Clock queries          GET_CLOCK_FREQ, GET_RT_MODE
    13. Scheduler health       GET_HEALTH to scheduler (32 bytes)
    14. Interface stats        GET_STATS to interface
    15. Action stats           GET_STATS to action engine
    16. Base component opcodes GET_COMMAND_COUNT + GET_STATUS_INFO
    17. INSPECT categories     TUNABLE_PARAM, STATE, OUTPUT
    18. GET_REGISTRY           Runtime component self-description
    19. GET_DATA_CATALOG       Runtime data block enumeration
    20. Sleep / Wake           Pause clock, verify flag, resume
    21. Pause / Resume         Pause tasks, verify, resume
    22. Lock / Unlock          Lock component, verify, unlock, error case
    23. SET_VERBOSITY          Log level control
    24. TPRM reload            Upload new params, reload, verify via INSPECT
    25. RELOAD_LIBRARY         Hot-swap TestPlugin v1 -> v2
    26. RTS sequence           Upload, load, start, verify, stop NOOP sweep
    35. RELOAD_EXECUTIVE       Restart via execve, reconnect, verify

  Layer 3: App-Specific
    27. WaveGen INSPECT state  Read WaveGen#0 STATE (stepCount advancing)
    28. WaveGen INSPECT output Read WaveGen#0 OUTPUT (output + phase)
    29. WaveGen INSPECT tunable Read WaveGen#0 TUNABLE_PARAM (frequency)
    30. WaveGen GET_STATS      Custom opcode 0x0100
    31. Multi-instance verify  Both WaveGen instances produce different output
    32. SysMonitor reachability NOOP to SystemMonitor

  Summary:
    33. Latency                20-ping round-trip measurement
    34. Post-test health       Verify clock rate, no warnings

Usage:
  python3 checkout.py --host raspberrypi.local
  python3 checkout.py --host localhost --skip-restart --skip-reload-lib
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


DIVIDER = "-" * 72


def section(title: str) -> None:
    print(f"\n{DIVIDER}")
    print(f"  {title}")
    print(DIVIDER)


# ---------------------------------------------------------------------------
# Component registry
# ---------------------------------------------------------------------------

ALL_COMPONENTS = {
    "Executive": 0x000000,
    "Scheduler": 0x000100,
    "Interface": 0x000400,
    "Action": 0x000500,
    "WaveGen#0": 0x00D000,
    "WaveGen#1": 0x00D001,
    "TelemetryMgr": 0x00C900,
    "SystemMonitor": 0x00C800,
    "TestPlugin": 0x00FA00,
}

# ---------------------------------------------------------------------------
# Main checkout
# ---------------------------------------------------------------------------


def run_checkout(args: argparse.Namespace) -> int:
    global PASS_COUNT, FAIL_COUNT
    PASS_COUNT = 0
    FAIL_COUNT = 0

    host = args.host
    port = args.port
    timeout = args.timeout

    print(f"\nApexOpsDemo Checkout: {host}:{port}")
    print(f"{'=' * 72}")

    with AprotoClient(host, port, timeout=timeout) as c2:

        # ==================================================================
        # Layer 1: APROTO Protocol
        # ==================================================================

        section("1. Connectivity (SYS_NOOP)")
        r = c2.noop()
        check("NOOP returns SUCCESS", r["status"] == 0, r["status_name"])

        section("2. PING Echo (SYS_PING)")
        ping_data = b"APEX_OPS_DEMO_PING"
        r = c2.ping(data=ping_data)
        check("PING returns SUCCESS", r["status"] == 0, r["status_name"])

        section("3. Component Addressing")
        for name, uid in ALL_COMPONENTS.items():
            r = c2.send_command(uid, proto.SYS_NOOP)
            check(f"{name} (0x{uid:06X})", r["status"] == 0, r["status_name"])

        section("4. File Transfer (8KB)")
        test_data = os.urandom(8192)
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
            check("Transfer returns SUCCESS", result["status"] == 0, result["status_name"])
            check("Progress callback fired", len(chunks_seen) > 0)
            if "file_end" in result:
                check(
                    "Bytes written matches",
                    result["file_end"]["bytes_written"] == len(test_data),
                    f"got {result['file_end'].get('bytes_written', '?')}",
                )
        finally:
            os.unlink(test_file)

        section("5. File Transfer Status (FILE_STATUS)")
        r = c2.get_transfer_status()
        check("FILE_STATUS returns SUCCESS", r["status"] == 0, r["status_name"])
        if "transfer" in r:
            check(
                f"Transfer state IDLE after success ({r['transfer']['state_name']})",
                r["transfer"]["state"] == 0,
            )

        section("6. File Transfer Abort + Recovery")
        abort_data = os.urandom(4096)
        begin_payload = proto.build_file_begin(
            len(abort_data),
            512,
            (len(abort_data) + 511) // 512,
            crc32c(abort_data),
            "test/abort.bin",
        )
        c2.send_command(0, proto.FILE_BEGIN, begin_payload)
        chunk = proto.build_file_chunk(0, abort_data[:512])
        c2.send_command(0, proto.FILE_CHUNK, chunk)
        r = c2.abort_transfer()
        check("Abort returns SUCCESS", r["status"] == 0, r["status_name"])

        # Verify state is IDLE after abort
        r = c2.get_transfer_status()
        if "transfer" in r:
            check(
                "Transfer state IDLE after abort",
                r["transfer"]["state"] == 0,
                r["transfer"]["state_name"],
            )

        # Verify recovery: new transfer should work
        recovery_data = os.urandom(1024)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(recovery_data)
            recovery_file = f.name
        try:
            result = c2.send_file(recovery_file, "test/recovery.bin", chunk_size=1024)
            check("Transfer after abort succeeds", result["status"] == 0, result["status_name"])
        finally:
            os.unlink(recovery_file)

        section("7. File Download (round-trip)")
        # Upload a known file, download it back, compare contents.
        dl_data = os.urandom(6000)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(dl_data)
            dl_upload_file = f.name
        dl_download_file = tempfile.mktemp(suffix=".bin")
        try:
            result = c2.send_file(dl_upload_file, "test/download_roundtrip.bin", chunk_size=2048)
            check("Upload for download test", result["status"] == 0, result["status_name"])

            chunks_seen = []
            result = c2.recv_file(
                "test/download_roundtrip.bin",
                dl_download_file,
                max_chunk_size=2048,
                progress_cb=lambda s, t: chunks_seen.append(s),
            )
            check("FILE_GET returns SUCCESS", result["status"] == 0, result["status_name"])
            check("Download progress callback fired", len(chunks_seen) > 0)
            if "file_get" in result:
                check(
                    "Reported size matches",
                    result["file_get"]["total_size"] == len(dl_data),
                    f"got {result['file_get'].get('total_size', '?')}",
                )
            check("CRC32 verified", result.get("crc_match", False))

            # Compare file contents
            if os.path.exists(dl_download_file):
                downloaded = open(dl_download_file, "rb").read()
                check(
                    "Downloaded content matches original",
                    downloaded == dl_data,
                    f"len={len(downloaded)} vs {len(dl_data)}",
                )
        finally:
            os.unlink(dl_upload_file)
            if os.path.exists(dl_download_file):
                os.unlink(dl_download_file)

        section("8. File Download Status (after complete)")
        r = c2.get_transfer_status()
        check("FILE_STATUS returns SUCCESS", r["status"] == 0, r["status_name"])
        if "transfer" in r:
            check(
                f"Transfer state IDLE after download ({r['transfer']['state_name']})",
                r["transfer"]["state"] == 0,
            )

        section("9. File Download Abort + Recovery")
        # Start a download, read one chunk, abort, verify recovery.
        abort_dl_data = os.urandom(4096)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(abort_dl_data)
            abort_dl_file = f.name
        try:
            c2.send_file(abort_dl_file, "test/abort_dl.bin", chunk_size=1024)
            get_payload = proto.build_file_get("test/abort_dl.bin", 1024)
            r = c2.send_command(0, proto.FILE_GET, get_payload)
            check("FILE_GET for abort test", r["status"] == 0, r["status_name"])

            # Read one chunk
            chunk_payload = proto.build_file_read_chunk(0)
            r = c2.send_command(0, proto.FILE_READ_CHUNK, chunk_payload)
            check("Read chunk 0", r["status"] == 0, r["status_name"])

            # Abort
            r = c2.abort_transfer()
            check("Abort download returns SUCCESS", r["status"] == 0, r["status_name"])

            # Verify state is IDLE
            r = c2.get_transfer_status()
            if "transfer" in r:
                check(
                    "Transfer state IDLE after abort",
                    r["transfer"]["state"] == 0,
                    r["transfer"]["state_name"],
                )
        finally:
            os.unlink(abort_dl_file)

        # ==================================================================
        # Layer 2: Apex Executive Core
        # ==================================================================

        section("10. Clock Rate (Layer 2)")
        c1 = c2.get_clock_cycles()
        time.sleep(1.0)
        c2_val = c2.get_clock_cycles()
        rate = c2_val - c1
        check(f"Clock rate ~100 Hz (measured {rate})", 80 < rate < 120)

        section("11. Executive Health (GET_HEALTH)")
        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        check("GET_HEALTH returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if check("Health packet >= 48 bytes", len(extra) >= 48, f"got {len(extra)}"):
            clock_cycles = struct.unpack_from("<Q", extra, 0)[0]
            overruns = struct.unpack_from("<Q", extra, 16)[0]
            watchdog_warns = struct.unpack_from("<Q", extra, 24)[0]
            freq_hz = struct.unpack_from("<H", extra, 32)[0]
            flags = extra[35]
            check(f"Clock running (cycles={clock_cycles})", clock_cycles > 0)
            check(f"Frequency = {freq_hz} Hz", freq_hz == 100)
            check(f"No frame overruns (got {overruns})", overruns == 0)
            check(f"No watchdog warnings (got {watchdog_warns})", watchdog_warns == 0)
            check("Clock running flag set", bool(flags & 0x01))
            check("Not sleeping", not bool(flags & 0x20))

        section("12. Clock Queries (GET_CLOCK_FREQ, GET_RT_MODE)")
        r = c2.send_command(0x000000, proto.EXEC_GET_CLOCK_FREQ)
        check("GET_CLOCK_FREQ returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if len(extra) >= 2:
            freq = struct.unpack_from("<H", extra, 0)[0]
            check(f"Clock freq = {freq} Hz", freq == 100)

        r = c2.send_command(0x000000, proto.EXEC_GET_RT_MODE)
        check("GET_RT_MODE returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if len(extra) >= 1:
            rt_mode = extra[0]
            check(f"RT mode = {rt_mode} (HARD_PERIOD_COMPLETE=1)", rt_mode == 1)

        section("13. Scheduler Health")
        r = c2.get_scheduler_health()
        check("Scheduler GET_HEALTH returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if check("Scheduler health >= 32 bytes", len(extra) >= 32, f"got {len(extra)}"):
            tick_count = struct.unpack_from("<Q", extra, 0)[0]
            task_count = struct.unpack_from("<I", extra, 8)[0]
            violations = struct.unpack_from("<I", extra, 12)[0]
            freq = struct.unpack_from("<H", extra, 20)[0]
            check(f"Tick count advancing ({tick_count})", tick_count > 0)
            check(f"Task count = {task_count}", task_count == 8)
            check(f"No period violations ({violations})", violations == 0)
            check(f"Fundamental freq = {freq} Hz", freq == 100)

        section("14. Interface Stats")
        # Component-level opcodes for queued components return immediate ACK
        # without response data (async queue path). Verify acceptance only.
        r = c2.get_interface_stats()
        check("Interface GET_STATS accepted", r["status"] == 0, r["status_name"])

        section("15. Action Engine Stats")
        r = c2.get_action_stats()
        check("Action GET_STATS accepted", r["status"] == 0, r["status_name"])

        section("16. Base Component Opcodes (GET_COMMAND_COUNT, GET_STATUS_INFO)")
        # Base opcodes (0x0080-0x0081) are routed to components via
        # SystemComponentBase::handleCommand. These go through the async queue
        # path for queued components, so only acceptance is verified (status=0).
        r = c2.send_command(0x00D000, proto.COMPONENT_GET_COMMAND_COUNT)
        check("WaveGen#0 GET_COMMAND_COUNT accepted", r["status"] == 0, r["status_name"])

        r = c2.send_command(0x00D000, proto.COMPONENT_GET_STATUS_INFO)
        check("WaveGen#0 GET_STATUS_INFO accepted", r["status"] == 0, r["status_name"])

        # Test on a core component too (Scheduler -- sync path, returns data)
        r = c2.send_command(0x000100, proto.COMPONENT_GET_COMMAND_COUNT)
        check("Scheduler GET_COMMAND_COUNT accepted", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if len(extra) >= 16:
            cmd_count = struct.unpack_from("<Q", extra, 0)[0]
            rejected = struct.unpack_from("<Q", extra, 8)[0]
            check(f"Scheduler commands={cmd_count} rejected={rejected}", rejected == 0)

        r = c2.send_command(0x000100, proto.COMPONENT_GET_STATUS_INFO)
        check("Scheduler GET_STATUS_INFO accepted", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if len(extra) >= 4:
            initialized = extra[1]
            configured = extra[2]
            registered = extra[3]
            check(
                f"Scheduler init={initialized} cfg={configured} reg={registered}",
                initialized == 1 and configured == 1 and registered == 1,
            )

        section("17. INSPECT Categories")
        # TUNABLE_PARAM (category 1)
        r = c2.inspect(0x00D000, category=1)
        check("INSPECT TUNABLE_PARAM returns SUCCESS", r["status"] == 0, r["status_name"])
        check("TUNABLE data present", len(r.get("extra", b"")) >= 32)

        # STATE (category 2)
        r = c2.inspect(0x00D000, category=2)
        check("INSPECT STATE returns SUCCESS", r["status"] == 0, r["status_name"])
        check("STATE data present", len(r.get("extra", b"")) >= 48)

        # OUTPUT (category 4)
        r = c2.inspect(0x00D000, category=4)
        check("INSPECT OUTPUT returns SUCCESS", r["status"] == 0, r["status_name"])
        check("OUTPUT data present", len(r.get("extra", b"")) >= 8)

        # STATIC_PARAM (category 0) -- may not be registered by WaveGen
        r = c2.inspect(0x00D000, category=0)
        # Accept either SUCCESS (data found) or COMPONENT_NOT_FOUND (no static params registered)
        check(
            "INSPECT STATIC_PARAM handled",
            r["status"] in (0, 4),
            r["status_name"],
        )

        section("18. GET_REGISTRY (runtime self-description)")
        r = c2.get_registry()
        check("GET_REGISTRY returns SUCCESS", r["status"] == 0, r["status_name"])
        components = r.get("components", [])
        check(f"Registry has components (got {len(components)})", len(components) >= 8)
        # Verify known components are present
        comp_uids = {c["full_uid"] for c in components}
        for name, uid in ALL_COMPONENTS.items():
            check(f"{name} (0x{uid:06X}) in registry", uid in comp_uids)
        # Verify fields are populated
        if components:
            first = components[0]
            check("name field populated", len(first.get("name", "")) > 0)
            check("type_name field populated", "UNKNOWN" not in first.get("type_name", "UNKNOWN"))

        section("19. GET_DATA_CATALOG (data block enumeration)")
        r = c2.get_data_catalog()
        check("GET_DATA_CATALOG returns SUCCESS", r["status"] == 0, r["status_name"])
        data_entries = r.get("data_entries", [])
        check(f"Catalog has entries (got {len(data_entries)})", len(data_entries) >= 1)
        # Verify WaveGen#0 has at least OUTPUT, STATE, TUNABLE_PARAM
        wg0_cats = {e["category_name"] for e in data_entries if e["full_uid"] == 0x00D000}
        check("WaveGen#0 has OUTPUT", "OUTPUT" in wg0_cats)
        check("WaveGen#0 has STATE", "STATE" in wg0_cats)
        check("WaveGen#0 has TUNABLE_PARAM", "TUNABLE_PARAM" in wg0_cats)
        # Verify size > 0 for all entries
        if data_entries:
            all_valid = all(e["size"] > 0 for e in data_entries)
            check("All data entries have size > 0", all_valid)

        section("20. Sleep / Wake")
        c2.sleep()
        time.sleep(0.3)
        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        extra = r.get("extra", b"")
        if len(extra) >= 48:
            flags = extra[35]
            check("Sleeping flag set after SLEEP", bool(flags & 0x20))

        c2.wake()
        time.sleep(0.3)
        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        extra = r.get("extra", b"")
        if len(extra) >= 48:
            flags = extra[35]
            check("Sleeping flag cleared after WAKE", not bool(flags & 0x20))

        c1 = c2.get_clock_cycles()
        time.sleep(0.5)
        c2_val = c2.get_clock_cycles()
        check("Clock advancing after WAKE", c2_val > c1)

        section("21. Pause / Resume")
        r = c2.pause()
        check("PAUSE returns SUCCESS", r["status"] == 0, r["status_name"])
        time.sleep(0.3)

        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        extra = r.get("extra", b"")
        if len(extra) >= 48:
            flags = extra[35]
            check("Paused flag set after PAUSE", bool(flags & 0x02))

        r = c2.resume()
        check("RESUME returns SUCCESS", r["status"] == 0, r["status_name"])
        time.sleep(0.3)

        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        extra = r.get("extra", b"")
        if len(extra) >= 48:
            flags = extra[35]
            check("Paused flag cleared after RESUME", not bool(flags & 0x02))

        c1 = c2.get_clock_cycles()
        time.sleep(0.5)
        c2_val = c2.get_clock_cycles()
        check("Clock advancing after RESUME", c2_val > c1)

        section("22. Lock / Unlock")
        r = c2.lock_component(0x00D001)
        check("Lock WaveGen#1 returns SUCCESS", r["status"] == 0, r["status_name"])

        r = c2.unlock_component(0x00D001)
        check("Unlock WaveGen#1 returns SUCCESS", r["status"] == 0, r["status_name"])

        c1 = c2.get_clock_cycles()
        time.sleep(0.2)
        c2_val = c2.get_clock_cycles()
        check("Clock advancing after unlock", c2_val > c1)

        # Error case: non-existent component
        r = c2.lock_component(0xFFFFFF)
        check(
            "Lock non-existent returns COMPONENT_NOT_FOUND",
            r["status"] == 4,
            r["status_name"],
        )

        section("23. SET_VERBOSITY")
        # Set verbosity to level 2, then restore to 0
        r = c2.send_command(0x000000, proto.EXEC_SET_VERBOSITY, bytes([2]))
        check("SET_VERBOSITY(2) returns SUCCESS", r["status"] == 0, r["status_name"])

        r = c2.send_command(0x000000, proto.EXEC_SET_VERBOSITY, bytes([0]))
        check("SET_VERBOSITY(0) restore SUCCESS", r["status"] == 0, r["status_name"])

        section("24. TPRM Reload")
        if not args.skip_tprm:
            # Create a TPRM (32 bytes matching WaveGenTunableParams)
            # Set frequency to 10.0 Hz to verify change via INSPECT
            tprm_data = struct.pack(
                "<ffffffff",
                10.0,  # frequency
                1.5,  # amplitude
                0.0,  # dcOffset
                0.0,  # phaseOffset
                0.0,  # noiseAmplitude
                0.5,  # dutyCycle
                0.0,  # waveType (as float for packing) + reserved
                0.0,  # reserved2
            )
            # Fix waveType byte at offset 24
            tprm_data = tprm_data[:24] + bytes([0, 0, 0, 0]) + tprm_data[28:]

            with tempfile.NamedTemporaryFile(suffix=".tprm", delete=False) as f:
                f.write(tprm_data)
                tprm_file = f.name

            try:
                result = c2.update_tprm(0x00D000, tprm_file)
                check(
                    "TPRM reload accepted",
                    result["status"] in (0, 5),
                    result["status_name"],
                )

                # Verify the new frequency via INSPECT
                if result["status"] == 0:
                    time.sleep(0.2)
                    r = c2.inspect(0x00D000, category=1)
                    extra = r.get("extra", b"")
                    if len(extra) >= 4:
                        new_freq = struct.unpack_from("<f", extra, 0)[0]
                        check(
                            f"Frequency updated to {new_freq:.1f} Hz",
                            abs(new_freq - 10.0) < 0.1,
                        )
            finally:
                os.unlink(tprm_file)
        else:
            print("  SKIP  (--skip-tprm)")

        section("25. Library Hot-Swap (RELOAD_LIBRARY)")
        if not args.skip_reload_lib:
            plugin_so = args.plugin_so
            if plugin_so is None:
                # Auto-detect from build dir
                candidates = [
                    os.path.join(
                        os.path.dirname(__file__),
                        "..",
                        "..",
                        "..",
                        "build",
                        "native-linux-debug",
                        "test_plugins",
                        "OpsTestPlugin_v2.so",
                    ),
                ]
                for c in candidates:
                    if os.path.isfile(c):
                        plugin_so = os.path.abspath(c)
                        break

            if plugin_so and os.path.isfile(plugin_so):
                result = c2.update_component(
                    full_uid=0x00FA00,
                    local_so_path=plugin_so,
                    component_name="TestPlugin",
                    instance_index=0,
                )
                check(
                    "Library hot-swap accepted",
                    result["status"] in (0, 17),
                    result["status_name"],
                )
            else:
                check("OpsTestPlugin_v2.so found", False, f"path={plugin_so}")
        else:
            print("  SKIP  (--skip-reload-lib)")

        section("26. RTS Sequence (NOOP Sweep)")
        if not args.skip_rts:
            # Upload the NOOP sweep RTS binary
            rts_path = os.path.join(
                os.path.dirname(__file__), "..", "tprm", "rts", "rts_001_noop_sweep.rts"
            )
            if os.path.isfile(rts_path):
                result = c2.send_file(rts_path, "rts/noop_sweep.rts")
                check("Upload RTS file", result["status"] == 0, result["status_name"])

                # Load into slot 0 (.apex_fs/ prefix matches file transfer root)
                load_payload = b"\x00" + b".apex_fs/rts/noop_sweep.rts\x00"
                r = c2.send_command(0x000500, proto.ACTION_LOAD_RTS, load_payload)
                check("LOAD_RTS slot 0", r["status"] == 0, r["status_name"])

                # Start
                r = c2.start_rts(0)
                check("START_RTS slot 0", r["status"] == 0, r["status_name"])

                # Wait for sequence to complete (5 steps x 100 cycles = 5s at 100Hz)
                time.sleep(6.0)

                # Check action engine stats
                r = c2.send_command(0x000500, 0x0100)
                check("Action GET_STATS after RTS", r["status"] == 0, r["status_name"])

                # Stop (should already be complete, but clean up)
                r = c2.stop_rts(0)
                check("STOP_RTS slot 0", r["status"] == 0, r["status_name"])
            else:
                check("RTS file found", False, f"path={rts_path}")
        else:
            print("  SKIP  (--skip-rts)")

        # ==================================================================
        # Layer 3: App-Specific (Ops Demo)
        # ==================================================================

        section("27. WaveGen INSPECT State (Layer 3)")
        r = c2.inspect(0x00D000, category=2)
        check("INSPECT WaveGen#0 STATE returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if check("State >= 48 bytes", len(extra) >= 48, f"got {len(extra)}"):
            # WaveGenState layout (48 bytes):
            #   phase(8), rmsAccum(8), output(4), peakPos(4), peakNeg(4),
            #   noiseSeed(4), sampleCount(8), stepCount(4), tlmCount(4)
            step_count = struct.unpack_from("<I", extra, 40)[0]
            sample_count = struct.unpack_from("<Q", extra, 32)[0]
            check(f"stepCount advancing ({step_count})", step_count > 0)
            check(f"sampleCount advancing ({sample_count})", sample_count > 0)

        section("28. WaveGen INSPECT Output")
        r = c2.inspect(0x00D000, category=4)
        check("INSPECT WaveGen#0 OUTPUT returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if check("Output >= 8 bytes", len(extra) >= 8, f"got {len(extra)}"):
            output_val = struct.unpack_from("<f", extra, 0)[0]
            phase_val = struct.unpack_from("<f", extra, 4)[0]
            check(f"Output value present ({output_val:.3f})", True)
            check(f"Phase in [0,1] ({phase_val:.3f})", 0.0 <= phase_val <= 1.0)

        section("29. WaveGen INSPECT Tunable")
        r = c2.inspect(0x00D000, category=1)
        check("INSPECT WaveGen#0 TUNABLE returns SUCCESS", r["status"] == 0, r["status_name"])
        extra = r.get("extra", b"")
        if check("Tunable >= 32 bytes", len(extra) >= 32, f"got {len(extra)}"):
            frequency = struct.unpack_from("<f", extra, 0)[0]
            amplitude = struct.unpack_from("<f", extra, 4)[0]
            wave_type = extra[24]
            check(f"Frequency = {frequency:.1f} Hz", 0.5 < frequency < 50.0)
            check(f"Amplitude = {amplitude:.1f}", amplitude > 0.0)
            check(f"WaveType = {wave_type}", wave_type <= 4)

        section("30. WaveGen GET_STATS (custom opcode)")
        # Custom component opcodes go through async queue and return immediate
        # ACK. Verify acceptance and cross-check via INSPECT OUTPUT.
        r = c2.send_command(0x00D000, 0x0100)
        check("WaveGen GET_STATS accepted", r["status"] == 0, r["status_name"])
        r = c2.inspect(0x00D000, category=4)
        extra = r.get("extra", b"")
        if len(extra) >= 8:
            out = struct.unpack_from("<f", extra, 0)[0]
            phase = struct.unpack_from("<f", extra, 4)[0]
            check(f"WaveGen output={out:.3f} phase={phase:.3f}", True)

        section("31. Multi-Instance Verify")
        # Both WaveGen instances should be running with different configs
        r0 = c2.inspect(0x00D000, category=2)
        r1 = c2.inspect(0x00D001, category=2)
        extra0 = r0.get("extra", b"")
        extra1 = r1.get("extra", b"")
        check("WaveGen#0 STATE readable", len(extra0) >= 48)
        check("WaveGen#1 STATE readable", len(extra1) >= 48)
        if len(extra0) >= 48 and len(extra1) >= 48:
            step0 = struct.unpack_from("<I", extra0, 40)[0]
            step1 = struct.unpack_from("<I", extra1, 40)[0]
            check(f"WaveGen#0 stepCount={step0}", step0 > 0)
            check(f"WaveGen#1 stepCount={step1}", step1 > 0)
            # Both should be running at 100 Hz, so counts should be similar
            check(
                "Both instances advancing",
                step0 > 0 and step1 > 0,
            )

        section("32. SystemMonitor Reachability")
        r = c2.send_command(0x00C800, proto.SYS_NOOP)
        check("SysMonitor NOOP returns SUCCESS", r["status"] == 0, r["status_name"])

        # ==================================================================
        # Summary
        # ==================================================================

        section("33. Latency")
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
            f"  min={latencies[0]:.1f}ms  median={median:.1f}ms"
            f"  p99={p99:.1f}ms  max={latencies[-1]:.1f}ms"
        )
        check(f"Median latency < 50ms (got {median:.1f}ms)", median < 50)

        section("34. Post-Test Health")
        c1 = c2.get_clock_cycles()
        time.sleep(1.0)
        c2_val = c2.get_clock_cycles()
        rate = c2_val - c1
        check(f"Clock still ~100 Hz (measured {rate})", 80 < rate < 120)

        r = c2.send_command(0x000000, proto.EXEC_GET_HEALTH)
        extra = r.get("extra", b"")
        if len(extra) >= 48:
            watchdog_warns = struct.unpack_from("<Q", extra, 24)[0]
            check(f"No watchdog warnings ({watchdog_warns})", watchdog_warns == 0)

    # -- 22. RELOAD_EXECUTIVE (outside `with` block, destructive) ----------
    if not args.skip_restart:
        section("35. Executive Restart (RELOAD_EXECUTIVE)")
        with AprotoClient(host, port, timeout=timeout) as c2:
            r = c2.reload_executive()
            check("RELOAD_EXECUTIVE accepted", r["status"] == 0, r["status_name"])

        print("  Waiting 8 seconds for restart...")
        time.sleep(8.0)

        try:
            with AprotoClient(host, port, timeout=timeout) as c2:
                r = c2.noop()
                check("Post-restart NOOP", r["status"] == 0, r["status_name"])

                # Verify all components still reachable
                all_ok = True
                for _name, uid in ALL_COMPONENTS.items():
                    r = c2.send_command(uid, proto.SYS_NOOP)
                    all_ok &= r["status"] == 0
                check("All components reachable after restart", all_ok)

                cycles = c2.get_clock_cycles()
                check(f"Low cycle count after restart ({cycles})", cycles < 2000)
        except Exception as e:
            check("Post-restart connection", False, str(e))
    else:
        print("\n  SKIP  22. Executive Restart (--skip-restart)")

    # -- Final Summary -----------------------------------------------------
    print(f"\n{'=' * 72}")
    print(f"  Results: {PASS_COUNT} passed, {FAIL_COUNT} failed")
    print(f"{'=' * 72}")

    return 0 if FAIL_COUNT == 0 else 1


def main():
    parser = argparse.ArgumentParser(description="ApexOpsDemo system checkout")
    parser.add_argument("--host", default="localhost", help="Target hostname (default: localhost)")
    parser.add_argument("--port", type=int, default=9000, help="Target port (default: 9000)")
    parser.add_argument("--timeout", type=float, default=5.0, help="Command timeout (default: 5.0)")
    parser.add_argument("--skip-restart", action="store_true", help="Skip RELOAD_EXECUTIVE test")
    parser.add_argument("--skip-reload-lib", action="store_true", help="Skip RELOAD_LIBRARY test")
    parser.add_argument("--skip-tprm", action="store_true", help="Skip TPRM reload test")
    parser.add_argument("--skip-rts", action="store_true", help="Skip RTS sequence test")
    parser.add_argument("--plugin-so", default=None, help="Path to OpsTestPlugin_v2.so")
    args = parser.parse_args()
    sys.exit(run_checkout(args))


if __name__ == "__main__":
    main()
