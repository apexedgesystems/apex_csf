#!/usr/bin/env python3
r"""Upload a standalone sequence into a running rover_mcu producer and start it.

The mission-upload path: compile the sequence TOML with cfg2bin, transfer
the payload over the APROTO ops interface into the active bank's rts/
directory, ask the action engine to rescan its catalog (0x0520), then
start the sequence by id (0x0510). The producer keeps running throughout;
the new id exists in the catalog until the next boot repacks the bank.

Run from inside the producer's container (the ops interface listens on
127.0.0.1:9000 there), with the apex tools venv:

    docker exec -i -w /home/$USER/workspace rover_mcu \\
      tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/upload_rts.py \\
      demos/apex_horizon_demo/rover_mcu/tprm/upload/rts_008_uploaded_beacon.toml --slot 13 --start

The slot is the bank file name, and writing a packed slot replaces the
sequence that boots there rather than adding one. Which slots are packed
comes from tprm.manifest, so the check moves with the catalog: pass
--slot and an occupied one is refused with the sequence it would have
replaced named, or leave it out and the lowest free slot is used.

Arguments:
  toml            the StandaloneSequenceTprm TOML (its sequenceId is the id you start)
  --slot N        bank file name NNN.rts; refused if the manifest packs it.
                  Default: the lowest slot the manifest leaves free.
  --start         START_RTS_BY_ID after the rescan
  --host/--port   ops interface (default 127.0.0.1:9000)
  --build-dir     build tree holding bin/tools/build/cfg2bin (default build/hosted-x86_64-debug)
"""
import argparse
import pathlib
import re
import struct
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
SPEC = REPO / "src/system/core/components/action/apex/apex_data.toml"
MANIFEST = pathlib.Path(__file__).resolve().parents[1] / "tprm/tprm.manifest"
ACTION_UID = 0x000500
OP_START_RTS_BY_ID = 0x0510
OP_RESCAN_CATALOG = 0x0520


def running_id_expressible(seq_id: int) -> bool:
    """Whether the frame can carry this id as a running sequence.

    The frame's sequence-state byte stamps a running sequence's id, and a
    halt stamps 0x10 with its reason, so a running id is a byte with bit
    0x10 clear: 1..15, 32..47, 64..79 and so on. The plant refuses the
    rest; this applies the same rule before anything is compiled.
    """
    return 1 <= seq_id <= 255 and (seq_id & 0x10) == 0


def packed_slots(manifest: pathlib.Path) -> dict[int, str]:
    """Slot -> sequence file, for every `rts N` row the manifest packs."""
    slots: dict[int, str] = {}
    if not manifest.is_file():
        return slots
    for line in manifest.read_text().splitlines():
        m = re.match(r"\s*rts\s+(\d+)\s+(\S+)", line)
        if m is not None:
            slots[int(m.group(1))] = pathlib.Path(m.group(2)).name
    return slots


def _status_text(result: dict) -> str:
    """A terminal command result as `NAME (code N)` for a failure line."""
    return f"{result.get('status_name', 'UNKNOWN')} (code {result.get('status', '?')})"


def main() -> int:
    """Compile the sequence, refuse a packed slot, transfer, rescan, start.

    Returns nonzero when any step fails; a failed rescan stops before START.
    """
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("toml")
    ap.add_argument("--slot", type=int, default=None)
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
    if not running_id_expressible(seq_id):
        print(
            f"sequence id {seq_id} cannot be a running sequence on the frame: a "
            f"running id is one byte with bit 0x10 clear (1..15, 32..47, 64..79, "
            f"...), since a halt stamps that bit with its reason on the same byte. "
            f"Renumber it.",
            file=sys.stderr,
        )
        return 2

    packed = packed_slots(MANIFEST)
    if a.slot is None:
        slot = next(n for n in range(1000) if n not in packed)
        print(f"slot {slot} (the lowest the manifest leaves free; packed: " f"{sorted(packed)})")
    else:
        slot = a.slot
        if slot in packed:
            print(
                f"slot {slot} is packed with {packed[slot]}: uploading there would "
                f"replace it instead of adding a sequence. Free slots start at "
                f"{next(n for n in range(1000) if n not in packed)}.",
                file=sys.stderr,
            )
            return 2

    out = pathlib.Path(a.build_dir) / "rover_mcu_upload" / f"{slot:03d}.rts"
    out.parent.mkdir(parents=True, exist_ok=True)
    cfg2bin = pathlib.Path(a.build_dir) / "bin/tools/build/cfg2bin"
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
    r = c.send_file(str(out), f"{a.bank}/rts/{slot:03d}.rts")
    if r.get("status", 1) != 0:
        print("transfer failed:", r, file=sys.stderr)
        return 1
    print(f"transferred to {a.bank}/rts/{slot:03d}.rts ({r['file_end']['bytes_written']} B)")
    # send_command returns the terminal RESULT/COMPLETION, so a successful
    # rescan status means the catalog already holds the new id when START goes out.
    r = c.send_command(ACTION_UID, OP_RESCAN_CATALOG)
    if r.get("status", 1) != 0:
        print(f"RESCAN_CATALOG failed: {_status_text(r)}", file=sys.stderr)
        return 1
    print("RESCAN_CATALOG:", r["status_name"])
    if a.start:
        r = c.send_command(ACTION_UID, OP_START_RTS_BY_ID, struct.pack("<H", seq_id))
        if r.get("status", 1) != 0:
            print(f"START_RTS_BY_ID {seq_id} failed: {_status_text(r)}", file=sys.stderr)
            return 1
        print(f"START_RTS_BY_ID {seq_id}:", r["status_name"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
