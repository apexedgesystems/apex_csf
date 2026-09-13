#!/usr/bin/env python3
"""Upload a standalone sequence into a running rover_mcu producer and start it.

The mission-upload path: compile the sequence TOML with cfg2bin, transfer
the payload over the APROTO ops interface into the active bank's rts/
directory, ask the action engine to rescan its catalog (0x0520), then
start the sequence by id (0x0510). The producer keeps running throughout;
the new id exists in the catalog until the next boot repacks the bank.

Run from inside the producer's container (the ops interface listens on
127.0.0.1:9000 there), with the apex tools venv:

    docker exec -i -w /home/kalex/workspace rover_mcu \\
      tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/upload_rts.py \\
      demos/apex_horizon_demo/rover_mcu/tprm/upload/rts_008_uploaded_beacon.toml --slot 11 --start

Arguments:
  toml            the StandaloneSequenceTprm TOML (its sequenceId is the id you start)
  --slot N        bank file name NNN.rts (must not collide with a packed slot; the
                  boot catalog uses 000..010)
  --start         START_RTS_BY_ID after the rescan
  --host/--port   ops interface (default 127.0.0.1:9000)
  --build-dir     build tree holding bin/tools/rust/cfg2bin (default build/hosted-x86_64-debug)
"""
import argparse
import pathlib
import re
import struct
import subprocess
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[4]
SPEC = REPO / "src/system/core/components/action/apex/apex_data.toml"
ACTION_UID = 0x000500
OP_START_RTS_BY_ID = 0x0510
OP_RESCAN_CATALOG = 0x0520


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("toml")
    ap.add_argument("--slot", type=int, required=True)
    ap.add_argument("--start", action="store_true")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--build-dir", default="build/hosted-x86_64-debug")
    ap.add_argument("--bank", default="bank_a")
    a = ap.parse_args()

    sys.path.insert(0, str(REPO / "tools/py/src"))
    from apex_tools.ops.client import AprotoClient  # noqa: E402

    toml = pathlib.Path(a.toml)
    m = re.search(r"sequenceId\s*=\s*\{[^}]*value\s*=\s*(\d+)", toml.read_text())
    if m is None:
        print("no sequenceId in", toml, file=sys.stderr)
        return 2
    seq_id = int(m.group(1))
    out = pathlib.Path(a.build_dir) / "rover_mcu_upload" / f"{a.slot:03d}.rts"
    out.parent.mkdir(parents=True, exist_ok=True)
    cfg2bin = pathlib.Path(a.build_dir) / "bin/tools/rust/cfg2bin"
    subprocess.run(
        [
            str(cfg2bin),
            "--config",
            str(toml),
            "--output",
            str(out),
            "--pin-spec",
            f"{SPEC}:StandaloneSequenceTprm",
        ],
        check=True,
    )
    print(f"compiled {toml.name} -> {out} ({out.stat().st_size} B), sequence id {seq_id}")

    c = AprotoClient(a.host, a.port, timeout=5.0)
    c.connect()
    r = c.send_file(str(out), f"{a.bank}/rts/{a.slot:03d}.rts")
    if r.get("status", 1) != 0:
        print("transfer failed:", r, file=sys.stderr)
        return 1
    print(f"transferred to {a.bank}/rts/{a.slot:03d}.rts ({r['file_end']['bytes_written']} B)")
    r = c.send_command(ACTION_UID, OP_RESCAN_CATALOG)
    print("RESCAN_CATALOG:", r["status_name"])
    if a.start:
        time.sleep(0.5)
        r = c.send_command(ACTION_UID, OP_START_RTS_BY_ID, struct.pack("<H", seq_id))
        print(f"START_RTS_BY_ID {seq_id}:", r["status_name"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
