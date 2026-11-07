#!/usr/bin/env python3
r"""Watch the rover producer's pushed telemetry arrive, and check its names.

Connects to the ops interface, reads every packet the producer sends on
its own for a window, and reports what arrived: a count and a rate per
(fullUid, opcode, payload size). Then, for the rover frame (fullUid
0x00DE00, 256 bytes), it decodes the frame's tail two ways -- by the
named fields' offsets in the struct dictionary, and by the frame-byte
offsets 232..253 a consumer reading the ROVR/2 layout uses -- and
reports whether every byte agrees.

Run from inside the producer's container (the ops interface listens on
127.0.0.1:9000 there), with the apex tools venv:

    docker exec -i -w /home/$USER/workspace rover_mcu \\
      tools/py/.venv/bin/python demos/apex_horizon_demo/rover_mcu/scripts/push_probe.py --seconds 5

Arguments:
  --seconds N     window to listen for (default 5)
  --host/--port   ops interface (default 127.0.0.1:9000)
  --db DIR        struct dictionaries (default build/hosted-x86_64-debug/apex_data_db)
  --expect-hz     fail (exit 1) if the rover frame arrives below this rate (default 0: report only)
"""
import argparse
import json
import pathlib
import socket
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[4]
ROVER_UID = 0x00DE00
ROVER_FRAME_BYTES = 256
TAIL_BASE = 232
TAIL_NAMES = [
    "board_link",
    "controller_mode",
    "seq_state",
    "active_waypoint",
    "waypoint_total",
    "led_bits",
    "led1_colour",
    "led1_rate",
    "led2_colour",
    "led2_rate",
    "last_cmd_result",
    "last_cmd_opcode_lo",
    "last_cmd_opcode_hi",
    "board_load_pct",
    "board_tick_lo",
    "board_tick_hi",
    "mast_pan_deg",
    "mast_ext_pct",
    "lidar_state",
    "lidar_hit_bits",
    "lidar_nearest_m",
    "lidar_scan_seq",
]


def tail_offsets(db: pathlib.Path) -> dict:
    """Named tail field -> offset, from the GroundVehicle dictionary."""
    d = json.loads((db / "GroundVehicle.json").read_text())
    fields = d["structs"]["GroundVehicleTelemetry"]["fields"]
    return {f["name"]: f["offset"] for f in fields if f["name"] in TAIL_NAMES}


def main() -> int:
    """Listen for the window, report what arrived, check the tail two ways."""
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--db", default="build/hosted-x86_64-debug/apex_data_db")
    ap.add_argument("--expect-hz", type=float, default=0.0)
    a = ap.parse_args()

    sys.path.insert(0, str(REPO / "tools/py/src"))
    from apex_tools.ops.protocol import parse_header, slip_decode_stream  # noqa: E402

    offsets = tail_offsets(pathlib.Path(a.db))
    missing = [n for n in TAIL_NAMES if n not in offsets]
    if missing:
        print("dictionary lacks named tail fields:", missing, file=sys.stderr)
        return 2

    s = socket.create_connection((a.host, a.port), timeout=2.0)
    s.settimeout(0.2)
    buf = bytearray()
    counts: dict = {}
    frames = 0
    agree = 0
    disagree = []
    first = None
    deadline = time.monotonic() + a.seconds
    while time.monotonic() < deadline:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf.extend(chunk)
        for pkt in slip_decode_stream(buf):
            hdr = parse_header(pkt)
            if hdr is None:
                continue
            key = (hdr["full_uid"], hdr["opcode"], len(hdr["payload"]))
            counts[key] = counts.get(key, 0) + 1
            if first is None:
                first = time.monotonic()
            if hdr["full_uid"] == ROVER_UID and len(hdr["payload"]) == ROVER_FRAME_BYTES:
                frames += 1
                p = hdr["payload"]
                by_name = {n: p[offsets[n]] for n in TAIL_NAMES}
                by_offset = {n: p[TAIL_BASE + i] for i, n in enumerate(TAIL_NAMES)}
                if by_name == by_offset:
                    agree += 1
                elif len(disagree) < 3:
                    disagree.append(
                        {
                            n: (by_name[n], by_offset[n])
                            for n in TAIL_NAMES
                            if by_name[n] != by_offset[n]
                        }
                    )
    s.close()

    window = (time.monotonic() - first) if first else a.seconds
    print(
        f"listened {a.seconds:.1f} s; {sum(counts.values())} packets from "
        f"{len(counts)} (fullUid, opcode, size) keys"
    )
    for (uid, op, size), n in sorted(counts.items()):
        print(f"  0x{uid:06X} opcode 0x{op:04X} {size:4d} B: {n:5d} packets, {n / window:6.1f} Hz")
    print(f"rover frames: {frames}; tail by name == tail by offset in {agree}")
    if disagree:
        print("disagreements (first three):", disagree)
    if frames and not disagree:
        shown = ("board_link", "controller_mode", "seq_state", "lidar_state", "lidar_nearest_m")
        last = {n: by_name[n] for n in shown}
        print("last frame's tail by name:", last)

    rover_hz = frames / window if window > 0 else 0.0
    if a.expect_hz > 0 and rover_hz < a.expect_hz:
        print(
            f"rover frame rate {rover_hz:.1f} Hz below the expected {a.expect_hz:.1f} Hz",
            file=sys.stderr,
        )
        return 1
    return 0 if not disagree else 1


if __name__ == "__main__":
    sys.exit(main())
