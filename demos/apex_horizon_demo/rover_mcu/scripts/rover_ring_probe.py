#!/usr/bin/env python3
r"""Command the rover over its shared-memory ring, and watch the frame.

The producer's ShmRingBridge maps one POSIX shared-memory region,
`/dev/shm/horizon_rover`: region A carries the 256-byte ROVR/2 frame out,
region B carries 32-byte APROTO command frames in. Each region opens with
a 64-byte header (payload size at byte 16, capacity at byte 20), a
producer cursor at byte 64 and a consumer cursor at byte 128; slots start
at byte 192. The geometry is read from the headers, not assumed.

  start ID      push START_RTS_BY_ID (opcode 0x0510, u16 id) to the action
                engine (fullUid 0x000500)
  halt          push HALT (0x0100) to the rover (fullUid 0x00DE00)
  resume        push RESUME (0x0101) to the rover
  watch N [S]   print the newest frame N times, every S seconds (default 1):
                grid position about the anchor, heading, speed and the
                frame tail (mode, sequence state, waypoints, lamps, last
                command result and opcode)

`watch` acts as the frame ring's consumer: it advances region A's consumer
cursor so the producer keeps publishing. Only one consumer may drain the
ring, so stop the probe before a visualizer attaches. The dev containers
share the host IPC namespace, so the probe runs on the host or inside the
container alike, with the standard library only:

    python3 demos/apex_horizon_demo/rover_mcu/scripts/rover_ring_probe.py start 1
    python3 demos/apex_horizon_demo/rover_mcu/scripts/rover_ring_probe.py watch 20 1.0

Grid north/east are a flat-earth approximation about `--anchor-lat` /
`--anchor-lon` (default the shipped anchor, 39.5 N, 105.5 W).
"""
import argparse
import math
import mmap
import struct
import sys
import time

SHM_PATH = "/dev/shm/horizon_rover"
PRELUDE_BYTES = 192
PROD_CURSOR = 64
CONS_CURSOR = 128
HDR_PAYLOAD_SIZE = 16
HDR_CAPACITY = 20

APROTO_MAGIC = 0x5041
APROTO_VERSION = 1
ACTION_UID = 0x000500
ROVER_UID = 0x00DE00
OP_START_RTS_BY_ID = 0x0510
OP_HALT = 0x0100
OP_RESUME = 0x0101

TAIL = 232
EARTH_RADIUS_M = 6378137.0


class Ring:
    """The two regions of the bridge's shared-memory channel."""

    def __init__(self, path: str) -> None:
        self._file = open(path, "r+b")
        self.m = mmap.mmap(self._file.fileno(), 0)
        self.a_size, self.a_cap = self._geometry(0)
        self.b_base = PRELUDE_BYTES + self.a_cap * self.a_size
        self.b_size, self.b_cap = self._geometry(self.b_base)

    def _geometry(self, base: int) -> tuple[int, int]:
        size, cap = struct.unpack_from("<II", self.m, base + HDR_PAYLOAD_SIZE)
        if size == 0 or cap == 0 or cap & (cap - 1):
            raise SystemExit(f"{SHM_PATH}: bad ring header at {base} (size {size}, capacity {cap})")
        return size, cap

    def send(self, dst: int, opcode: int, payload: bytes = b"") -> None:
        """Push one APROTO command frame onto region B."""
        frame = struct.pack(
            "<HBBIHHH", APROTO_MAGIC, APROTO_VERSION, 0, dst, opcode, 0, len(payload)
        )
        frame += payload
        if len(frame) > self.b_size:
            raise SystemExit(f"command frame {len(frame)} B exceeds the {self.b_size} B slot")
        head = struct.unpack_from("<Q", self.m, self.b_base + PROD_CURSOR)[0]
        tail = struct.unpack_from("<Q", self.m, self.b_base + CONS_CURSOR)[0]
        if head - tail >= self.b_cap:
            raise SystemExit("command ring full: the producer is not draining it")
        slot = self.b_base + PRELUDE_BYTES + (head & (self.b_cap - 1)) * self.b_size
        self.m[slot : slot + len(frame)] = frame
        struct.pack_into("<Q", self.m, self.b_base + PROD_CURSOR, head + 1)

    def latest(self) -> bytes | None:
        """Newest frame in region A, consuming everything published so far."""
        head = struct.unpack_from("<Q", self.m, PROD_CURSOR)[0]
        if head == 0:
            return None
        struct.pack_into("<Q", self.m, CONS_CURSOR, head)
        off = PRELUDE_BYTES + ((head - 1) & (self.a_cap - 1)) * self.a_size
        return bytes(self.m[off : off + self.a_size])


def describe(fr: bytes, anchor_lat: float, anchor_lon: float) -> str:
    """One line from a ROVR/2 frame."""
    tick = struct.unpack_from("<Q", fr, 8)[0]
    lat, lon = struct.unpack_from("<dd", fr, 16)
    hdg, spd = struct.unpack_from("<dd", fr, 40)
    north = math.radians(lat - anchor_lat) * EARTH_RADIUS_M
    east = math.radians(lon - anchor_lon) * EARTH_RADIUS_M * math.cos(math.radians(anchor_lat))
    t = fr[TAIL : TAIL + 13]
    return (
        f"tick={tick} N={north:+7.2f} E={east:+7.2f} hdg={hdg:6.1f} spd={spd:4.2f} "
        f"mode={t[1]} seq={t[2]:#04x} wp={t[3]}/{t[4]} "
        f"led={t[5]:02b} c{t[6]}r{t[7]} c{t[8]}r{t[9]} res={t[10]} op=0x{t[11] | (t[12] << 8):04x}"
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--anchor-lat", type=float, default=39.5)
    ap.add_argument("--anchor-lon", type=float, default=-105.5)
    sub = ap.add_subparsers(dest="cmd", required=True)
    st = sub.add_parser("start")
    st.add_argument("id", type=int)
    sub.add_parser("halt")
    sub.add_parser("resume")
    w = sub.add_parser("watch")
    w.add_argument("count", type=int)
    w.add_argument("every", type=float, nargs="?", default=1.0)
    args = ap.parse_args()

    ring = Ring(SHM_PATH)
    if args.cmd == "start":
        ring.send(ACTION_UID, OP_START_RTS_BY_ID, struct.pack("<H", args.id))
        print(f"sent START_RTS_BY_ID {args.id}")
    elif args.cmd == "halt":
        ring.send(ROVER_UID, OP_HALT)
        print("sent HALT")
    elif args.cmd == "resume":
        ring.send(ROVER_UID, OP_RESUME)
        print("sent RESUME")
    else:
        for i in range(args.count):
            fr = ring.latest()
            line = (
                "no frame published yet"
                if fr is None
                else describe(fr, args.anchor_lat, args.anchor_lon)
            )
            print(f"t+{i * args.every:5.1f}s {line}")
            time.sleep(args.every)
    return 0


if __name__ == "__main__":
    sys.exit(main())
