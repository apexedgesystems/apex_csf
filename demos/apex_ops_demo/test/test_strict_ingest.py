#!/usr/bin/env python3
"""Strict-ingest scenario suite for the ops demo.

Exercises the boot ingest policy end to end against a built tree:

  1. STRICT (default) refuses a corrupt payload: exit 1, offender and
     check named, zero task cycles.
  2. STRICT refuses a missing non-optional payload; LENIENT warns and
     runs the same master.
  3. A corrupt master with a staged fallback bank self-heals: flip,
     re-exec, RUNNING ON FALLBACK BANK, vehicle runs.
  4. No bank passes: SAFE/HOLD -- vehicle reachable, RESUME refused
     with INGEST_HELD, repaired entirely over the wire (upload to the
     fallback bank + RELOAD_EXECUTIVE), then running.

Run from the repo root inside the dev container:

  PYTHONPATH=tools/py/src python3 demos/apex_ops_demo/test/test_strict_ingest.py

Requires build/hosted-x86_64-debug (binary + generated TPRM tree).
"""

import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

BUILD = "build/hosted-x86_64-debug"
BIN = f"{BUILD}/bin/ApexOpsDemo"
GEN = f"{BUILD}/demos/apex_ops_demo/exec/tprm"
SYSMON_UID = 0x00C800
INGEST_HELD = 12

sys.path.insert(0, "tools/py/src")
from apex_tools.ops.client import AprotoClient  # noqa: E402

PASS = 0
FAIL = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global PASS, FAIL
    tag = "PASS" if ok else "FAIL"
    print(f"  {tag}  {name}" + (f"  ({detail})" if detail and not ok else ""))
    PASS += ok
    FAIL += not ok


def corrupt_master(dest: str) -> None:
    """Copy the master with one flipped byte inside the sysmon body."""
    d = bytearray(open(f"{GEN}/master.tprm", "rb").read())
    for i in range(len(d) - 20):
        if d[i : i + 4] == b"APV3" and struct.unpack_from("<I", d, i + 8)[0] == SYSMON_UID:
            d[i + 25] ^= 0xFF
            break
    open(dest, "wb").write(bytes(d))


def pack_without_sysmon(dest: str) -> None:
    args = [f"{BUILD}/bin/tools/rust/tprm_pack", "pack", "-o", dest]
    keep = {
        0x000000: "toml_executive_toml.tprm",
        0x000100: "toml_scheduler_toml.tprm",
        0x000400: "toml_interface_toml.tprm",
        0x000500: "toml_action_toml.tprm",
        0x00C900: "toml_telemetry_manager_toml.tprm",
        0x00D000: "toml_wave_gen_0_toml.tprm",
        0x00D001: "toml_wave_gen_1_toml.tprm",
    }
    for uid, fn in keep.items():
        args += ["-e", f"0x{uid:06X}:{GEN}/payloads/{fn}"]
    subprocess.run(args, check=True, capture_output=True)


def boot(master: str, fs_root: str, extra=(), timeout=25, shutdown_after=8):
    """Run a bounded boot; return (exit_code, system_log_text)."""
    cmd = [
        BIN,
        "--config",
        master,
        "--fs-root",
        fs_root,
        "--shutdown-after",
        str(shutdown_after),
        "--skip-cleanup",
        *extra,
    ]
    r = subprocess.run(cmd, capture_output=True, timeout=timeout)
    log_path = os.path.join(fs_root, "system.log")
    log = open(log_path).read() if os.path.exists(log_path) else ""
    return r.returncode, log


def boot_bg(master: str, fs_root: str, extra=(), shutdown_after=300):
    cmd = [
        BIN,
        "--config",
        master,
        "--fs-root",
        fs_root,
        "--shutdown-after",
        str(shutdown_after),
        "--skip-cleanup",
        *extra,
    ]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(6)
    return p


def capture_good_payloads(work: str) -> str:
    fs = os.path.join(work, "capture_fs")
    boot(f"{GEN}/master.tprm", fs, shutdown_after=3, timeout=20)
    good = os.path.join(work, "good")
    os.makedirs(good, exist_ok=True)
    for f in glob.glob(os.path.join(fs, "bank_a/tprm/*.tprm")):
        shutil.copy(f, good)
    shutil.rmtree(fs)
    return good


def main() -> int:
    work = tempfile.mkdtemp(prefix="strict_ingest_")
    corrupt = os.path.join(work, "master_corrupt.tprm")
    corrupt_master(corrupt)
    no_sysmon = os.path.join(work, "master_no_sysmon.tprm")
    pack_without_sysmon(no_sysmon)
    good = capture_good_payloads(work)

    print("== 1. STRICT holds a corrupt payload (no fallback staged)")
    code, log = boot(corrupt, os.path.join(work, "fs1"))
    check("held run exits nonzero", code != 0, f"exit={code}")
    check("offender named", "SYS_MON (0x00C800): payload rejected" in log)
    check("SAFE HOLD entered", "SAFE HOLD" in log)
    check("zero cycles dispatched", "Clock stopped after 0 cycles" in log)

    print("== 2. Missing non-optional payload: STRICT holds, LENIENT runs")
    code, log = boot(no_sysmon, os.path.join(work, "fs2"))
    check("held run exits nonzero", code != 0, f"exit={code}")
    check("missing named", "no payload provided" in log)
    code, log = boot(no_sysmon, os.path.join(work, "fs3"), extra=("--ingest-policy", "lenient"))
    check("LENIENT exit zero", code == 0, f"exit={code}")
    check("LENIENT runs", "Task execution started" in log)
    check("LENIENT warns", "LENIENT policy, running defaults" in log)

    print("== 3. Corrupt master + staged bank: self-healing fallback")
    fs4 = os.path.join(work, "fs4")
    os.makedirs(os.path.join(fs4, "bank_b/tprm"), exist_ok=True)
    for f in glob.glob(os.path.join(good, "*.tprm")):
        shutil.copy(f, os.path.join(fs4, "bank_b/tprm"))
    code, log = boot(corrupt, fs4, timeout=40, shutdown_after=8)
    check("fallback announced", "RUNNING ON FALLBACK BANK B" in log)
    check("extraction skipped", "skipping master extraction" in log)
    check("vehicle ran", "Task execution started" in log)
    check("marker consumed", not os.path.exists(os.path.join(fs4, ".ingest_fallback")))

    print("== 4. SAFE/HOLD + over-the-wire repair")
    fs5 = os.path.join(work, "fs5")
    p = boot_bg(corrupt, fs5)
    log = open(os.path.join(fs5, "system.log")).read()
    check("SAFE HOLD announced", "SAFE HOLD" in log)
    c = AprotoClient("localhost")
    c.connect()
    check("NOOP in hold", c.send_command(0, 0x0101)["status"] == 0)
    check("RESUME refused INGEST_HELD", c.send_command(0, 0x0111)["status"] == INGEST_HELD)
    for f in sorted(glob.glob(os.path.join(good, "*.tprm"))):
        c.send_file(f, "bank_b/tprm/" + os.path.basename(f))
    check("RELOAD from hold acked", c.reload_executive()["status"] == 0)
    time.sleep(14)
    log = open(os.path.join(fs5, "system.log")).read()
    check("repaired via fallback", "RUNNING ON FALLBACK BANK B" in log)
    c2 = AprotoClient("localhost")
    c2.connect()
    check("repaired vehicle serves", c2.send_command(0, 0x0101)["status"] == 0)
    c2.close()
    p.terminate()

    print(f"\n  Results: {PASS} passed, {FAIL} failed")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
