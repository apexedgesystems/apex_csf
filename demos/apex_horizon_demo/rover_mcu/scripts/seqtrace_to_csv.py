#!/usr/bin/env python3
r"""Cut the rover's SEQTRACE log lines into one CSV per sequence take.

While a sequence runs, the GroundVehicle samples itself at 20 Hz and its
1 Hz telemetry task writes each sample to `logs/models/GroundVehicle_0.log`
as one line

  SEQTRACE seq=S wp=A/B t=.. n=.. e=.. hdg=.. v=.. lidar=.. slope=.. led=L
           lstate=.. lhits=0x.. lnear=.. lscan=.. sweep=0x..

and closes the take with `SEQTRACE end dropped=K` (K samples lost to a full
buffer). Each take becomes `<out_dir>/seq<S>_take<i>.csv`, numbered per
starting sequence state, with the header

  t_s,seq,wp,wp_total,north_m,east_m,heading_deg,speed_m_s,lidar_nearest_m,
  slope_deg,led_bits,lidar_state,lidar_hit_bits,lidar_frame_m,lidar_scan_seq,
  sweep_hit_bits

(one line in the file). The lidar columns are the frame's picture -- what the
board saw, or the plant's own sweep with no board sensor -- beside the
plant's own sweep. A take cut short by the end of the log is still written,
with its dropped count reported as unknown. Standard library only:

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


def row(m: re.Match) -> list[str]:
    """CSV fields in header order from one SEQTRACE match."""
    seq, wp, wpt, t, n, e, hdg, v, lidar, slope, led, lst, lhits, lnear, lscan, sweep = m.groups()
    return [
        t, seq, wp, wpt, n, e, hdg, v, lidar, slope, led,
        lst, str(int(lhits, 16)), lnear, lscan, str(int(sweep, 16)),
    ]  # fmt: skip


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("log", type=pathlib.Path, help="GroundVehicle_0.log")
    ap.add_argument("out_dir", type=pathlib.Path, help="directory for the per-take CSVs")
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    rows: list[list[str]] = []
    takes: dict[str, int] = {}

    def flush(dropped: str) -> None:
        if not rows:
            return
        seq = rows[0][1]
        takes[seq] = takes.get(seq, 0) + 1
        path = args.out_dir / f"seq{seq}_take{takes[seq]}.csv"
        with path.open("w") as f:
            f.write(HEADER + "\n")
            f.writelines(",".join(r) + "\n" for r in rows)
        print(f"{path}  rows={len(rows)}  dropped={dropped}")
        rows.clear()

    with args.log.open(errors="replace") as src:
        for line in src:
            m = LINE.search(line)
            if m:
                rows.append(row(m))
                continue
            end = END.search(line)
            if end:
                flush(end.group(1))
    flush("unknown")
    return 0


if __name__ == "__main__":
    sys.exit(main())
