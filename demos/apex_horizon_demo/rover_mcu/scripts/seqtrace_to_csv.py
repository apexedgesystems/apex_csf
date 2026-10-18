#!/usr/bin/env python3
r"""Cut the rover's SEQTRACE log lines into one CSV per sequence take.

While a sequence runs, the GroundVehicle samples itself at 20 Hz and its
1 Hz telemetry task writes each sample to `logs/models/GroundVehicle_0.log`
as one line

  SEQTRACE seq=S wp=A/B t=.. n=.. e=.. hdg=.. v=.. lidar=.. slope=.. led=L
           lstate=.. lhits=0x.. lnear=.. lscan=.. sweep=0x..

and closes the take with `SEQTRACE end dropped=K` (K samples lost to a full
buffer). `seq` is the frame's state byte: a running sequence id (bit 0x10
clear) or 0x10|reason while halted, so a halt changes it inside one take.
Each take becomes `<out_dir>/seq<S>_take<i>.csv`, named from its first
sample's byte and numbered per byte in start order, with the header

  t_s,seq,wp,wp_total,north_m,east_m,heading_deg,speed_m_s,lidar_nearest_m,
  slope_deg,led_bits,lidar_state,lidar_hit_bits,lidar_frame_m,lidar_scan_seq,
  sweep_hit_bits

(one line in the file). The lidar columns are the frame's picture -- what the
board saw, or the plant's own sweep with no board sensor -- beside the
plant's own sweep.

Take boundaries. The telemetry task drains every pending sample before the
pending end marker, so a take that starts within a second of the previous
one ending can log its first samples ahead of the previous take's marker,
and one marker can stand for two takes (its count is their sum). The
producer restarts the take clock `t` at 0 when a take starts and only
advances it within a take, so the extractor guarantees:

  * an end marker with no take started since the previous marker closes the
    current take and its dropped count is exact;
  * a sample whose `t` is not greater than the previous sample's starts a new
    take, even before an end marker; the earlier rows stay with the earlier
    take;
  * a sample whose state byte is a running id other than the take's running
    id, with no clock restart, also starts a new take (a sequence that hands
    over to another without going idle); halt bytes never split a take.

It does not decide where such a take ends or how a shared dropped count
divides, so every take on either side of one of those splits is written as
`seq<S>_take<i>_uncertain.csv` beside a `.txt` note naming the log lines and
the reason; a shared nonzero dropped count is reported as unknown. With
`--strict` it writes nothing and exits 2 at the first such boundary, naming
the line. A take cut short by the end of the log is still written, with its
dropped count reported as unknown. Standard library only:

    python3 demos/apex_horizon_demo/rover_mcu/scripts/seqtrace_to_csv.py \
      <fs-root>/logs/models/GroundVehicle_0.log out/
"""
import argparse
import pathlib
import re
import sys

NUM = r"([-+]?[\d.]+)"
LINE = re.compile(
    r"SEQTRACE seq=(\d+) wp=(\d+)/(\d+) "
    rf"t={NUM} n={NUM} e={NUM} hdg={NUM} v={NUM} lidar={NUM} slope={NUM} led=(\d+) "
    r"lstate=(\d+) lhits=(0x[0-9a-fA-F]+) lnear=(\d+) lscan=(\d+) sweep=(0x[0-9a-fA-F]+)"
)
END = re.compile(r"SEQTRACE end dropped=(\d+)")
HEADER = (
    "t_s,seq,wp,wp_total,north_m,east_m,heading_deg,speed_m_s,lidar_nearest_m,"
    "slope_deg,led_bits,lidar_state,lidar_hit_bits,lidar_frame_m,lidar_scan_seq,"
    "sweep_hit_bits"
)
HALT_BIT = 0x10


class AmbiguousBoundary(Exception):
    """A take boundary the log does not settle, raised under --strict."""


def row(m: re.Match) -> list[str]:
    """CSV fields in header order from one SEQTRACE match."""
    seq, wp, wpt, t, n, e, hdg, v, lidar, slope, led, lst, lhits, lnear, lscan, sweep = m.groups()
    return [
        t, seq, wp, wpt, n, e, hdg, v, lidar, slope, led,
        lst, str(int(lhits, 16)), lnear, lscan, str(int(sweep, 16)),
    ]  # fmt: skip


def running_id(state: int) -> int | None:
    """The sequence id a state byte names, or None for a halt byte."""
    return state if state != 0 and not state & HALT_BIT else None


class Take:
    """One take's rows, its running id, and what the log leaves unsettled about it."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.rows: list[list[str]] = []
        self.running: int | None = None
        self.last_t = 0.0
        self.dropped = "unknown"
        self.notes: list[str] = []
        self.maybe_closed = False


def extract(lines, strict: bool = False) -> list[Take]:
    """Takes in start order from SEQTRACE log lines; see the module docstring."""
    done: list[Take] = []
    unclosed: list[Take] = []
    current: Take | None = None
    counts: dict[str, int] = {}

    def start(seq: str) -> Take:
        counts[seq] = counts.get(seq, 0) + 1
        take = Take(f"seq{seq}_take{counts[seq]}")
        done.append(take)
        return take

    for lineno, line in enumerate(lines, 1):
        m = LINE.search(line)
        if m:
            fields = row(m)
            t = float(fields[0])
            state = int(fields[1])
            if current is not None and current.maybe_closed:
                if t > current.last_t:
                    current.maybe_closed = False
                else:
                    current = None
            if current is not None:
                rid = running_id(state)
                reason = None
                if t <= current.last_t:
                    reason = f"take clock went from {current.last_t:.2f} to {t:.2f}"
                elif rid is not None and current.running is not None and rid != current.running:
                    reason = f"running id changed from {current.running} to {rid}"
                if reason is not None:
                    note = f"line {lineno}: {reason} before an end marker"
                    if strict:
                        raise AmbiguousBoundary(note)
                    current.notes.append(f"{note}; later rows go to the next take")
                    unclosed.append(current)
                    current = start(fields[1])
                    current.notes.append(f"{note}; this take starts there")
            if current is None:
                current = start(fields[1])
            current.rows.append(fields)
            current.last_t = t
            if running_id(state) is not None:
                current.running = running_id(state)
            continue
        end = END.search(line)
        if not end:
            continue
        count = end.group(1)
        if unclosed:
            shared = [*unclosed, current] if current is not None else unclosed
            for take in shared:
                take.dropped = count if count == "0" else f"unknown (shared total {count})"
                take.notes.append(
                    f"line {lineno}: end marker may close any of "
                    + ", ".join(other.name for other in shared)
                )
            unclosed.clear()
            if current is not None:
                current.maybe_closed = True
        elif current is not None:
            # A take that already shared a nonzero count cannot be given an exact one.
            if not current.dropped.startswith("unknown (shared"):
                current.dropped = count
            if current.maybe_closed:
                current.notes.append(f"line {lineno}: end marker with no rows since the last")
                if count != "0":
                    current.dropped = f"unknown (line {lineno} reported {count})"
            current = None
    return done


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("log", type=pathlib.Path, help="GroundVehicle_0.log")
    ap.add_argument("out_dir", type=pathlib.Path, help="directory for the per-take CSVs")
    ap.add_argument(
        "--strict", action="store_true", help="exit 2 at the first ambiguous take boundary"
    )
    args = ap.parse_args()

    with args.log.open(errors="replace") as src:
        try:
            takes = extract(src, strict=args.strict)
        except AmbiguousBoundary as err:
            print(f"{args.log}: ambiguous take boundary at {err}", file=sys.stderr)
            return 2

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for take in takes:
        stem = take.name + ("_uncertain" if take.notes else "")
        path = args.out_dir / f"{stem}.csv"
        with path.open("w") as f:
            f.write(HEADER + "\n")
            f.writelines(",".join(r) + "\n" for r in take.rows)
        if take.notes:
            (args.out_dir / f"{stem}.txt").write_text("\n".join(take.notes) + "\n")
        print(f"{path}  rows={len(take.rows)}  dropped={take.dropped}")
        for note in take.notes:
            print(f"  uncertain: {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
