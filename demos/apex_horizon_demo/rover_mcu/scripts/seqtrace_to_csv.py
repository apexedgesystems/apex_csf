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
pending end marker, and each marker counts the samples lost since the
previous marker. So a take that starts within a second of the previous one
ending can log its first samples ahead of the previous take's marker, one
marker can stand for two takes (its count is their sum), and a take that
starts while the buffer is still full loses its first samples into the
previous take's count. The producer restarts the take clock `t` at 0 when a
take starts and only advances it within a take, so the extractor
guarantees:

  * an end marker with no take started since the previous marker closes the
    current take; its dropped count is exact unless the next take's first
    sample has `t` above 0;
  * a sample whose `t` is not greater than the previous sample's starts a new
    take, even before an end marker; the earlier rows stay with the earlier
    take;
  * a sample whose state byte is a running id other than the take's running
    id, with no clock restart, also starts a new take (a sequence that hands
    over to another without going idle); halt bytes never split a take;
  * a take whose first sample follows an end marker with `t` above 0 lost
    its earlier samples into that marker's count, which it shares with the
    takes the marker closes.

It does not decide where such a take ends or how a shared dropped count
divides, so every take on either side of one of those boundaries is written
as `seq<S>_take<i>_uncertain.csv` beside a `.txt` note naming the log lines
and the reason, and a take that shares a nonzero count reports its dropped
count as unknown however its later markers read. With `--strict` it writes
nothing and exits 2 at the first such boundary, naming the line. A take
that logs rows after its last end marker and is cut short by the end of the
log is still written, with its dropped count reported as unknown. Standard
library only:

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
        # End markers whose count may include this take's losses, by log line: the count, and why
        # the take may own only part of it, or None when the count is the take's alone.
        self.ends: dict[int, tuple[int, str | None]] = {}
        # Rows logged since its last end marker, whose losses no marker in the log counts yet.
        self.uncounted = False
        self.notes: list[str] = []
        self.maybe_closed = False

    def share(self, line: int, count: int) -> None:
        """Hold the end marker on `line` as a count this take shares with other takes."""
        held = self.ends.get(line)
        if held is None or held[1] is None:
            self.ends[line] = (count, f"shared total {count}")

    def dropped_text(self) -> str:
        """The dropped count, or unknown with the marker counts that leave it open."""
        own = sum(count for count, how in self.ends.values() if how is None)
        # A shared or unattributable zero gives the take nothing; any other such count is never
        # divided, so the total stays unknown whatever later markers report.
        open_counts = [how for count, how in self.ends.values() if count and how]
        if open_counts:
            return f"unknown ({', '.join(([f'at least {own}'] if own else []) + open_counts)})"
        return "unknown" if self.uncounted else str(own)


def extract(lines, strict: bool = False) -> list[Take]:
    """Takes in start order from SEQTRACE log lines; see the module docstring."""
    done: list[Take] = []
    unclosed: list[Take] = []
    current: Take | None = None
    counts: dict[str, int] = {}
    # The latest end marker while no row has followed it: its line, count, and the takes it closes.
    last_end: tuple[int, int, list[Take]] | None = None

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
                # The first sample is taken at t=0 unless the buffer is full, and those losses
                # are in the marker drained just before this row.
                if last_end is not None and t > 0:
                    end_line, end_count, closed = last_end
                    note = (
                        f"line {lineno}: first sample at t={t:.2f}: earlier samples lost and "
                        f"counted in the marker on line {end_line}"
                    )
                    if strict:
                        raise AmbiguousBoundary(note)
                    for take in closed:
                        take.share(end_line, end_count)
                        take.notes.append(f"{note}; that count is shared with {current.name}")
                    current.share(end_line, end_count)
                    current.notes.append(f"{note}; this take starts there")
            current.rows.append(fields)
            current.last_t = t
            current.uncounted = True
            if running_id(state) is not None:
                current.running = running_id(state)
            last_end = None
            continue
        end = END.search(line)
        if not end:
            continue
        count = int(end.group(1))
        if unclosed:
            shared = [*unclosed, current] if current is not None else [*unclosed]
            for take in shared:
                take.share(lineno, count)
                take.uncounted = False
                take.notes.append(
                    f"line {lineno}: end marker (dropped={count}) may close any of "
                    + ", ".join(other.name for other in shared)
                )
            unclosed.clear()
            if current is not None:
                current.maybe_closed = True
            last_end = (lineno, count, shared)
        elif current is not None:
            how = None
            if current.maybe_closed:
                current.notes.append(f"line {lineno}: end marker with no rows since the last")
                how = f"line {lineno} reported {count}"
            current.ends[lineno] = (count, how)
            current.uncounted = False
            last_end = (lineno, count, [current])
            current = None
        else:
            last_end = (lineno, count, [])
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
        print(f"{path}  rows={len(take.rows)}  dropped={take.dropped_text()}")
        for note in take.notes:
            print(f"  uncertain: {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
