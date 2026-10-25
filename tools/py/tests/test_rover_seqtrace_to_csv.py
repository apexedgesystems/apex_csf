"""Take extraction from the rover's SEQTRACE log lines."""

import csv
import importlib.util
import pathlib
import sys
from unittest.mock import patch

import pytest

SCRIPT = (
    pathlib.Path(__file__).resolve().parents[3]
    / "demos/apex_horizon_demo/rover_mcu/scripts/seqtrace_to_csv.py"
)
END = "SEQTRACE end dropped=0\n"


def _module():
    spec = importlib.util.spec_from_file_location("seqtrace_to_csv", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _sample(seq: int, t: float) -> str:
    return (
        f"SEQTRACE seq={seq} wp=1/2 t={t:.2f} n=+1.000 e=+0.000 hdg=0.00 v=1.000 "
        "lidar=10.0 slope=0.00 led=0 lstate=0 lhits=0x00 lnear=255 lscan=0 sweep=0x00\n"
    )


def _run(tmp_path: pathlib.Path, log: str, *flags: str):
    """Run the script's main on one log; return its exit code and {file: [(seq, t)]}."""
    src = tmp_path / "input.log"
    src.write_text(log)
    out = tmp_path / "csv"
    with patch.object(sys, "argv", [str(SCRIPT), *flags, str(src), str(out)]):
        code = _module().main()
    files = {
        p.name: [(r["seq"], r["t_s"]) for r in csv.DictReader(p.open())]
        for p in sorted(out.glob("*.csv"))
    }
    return code, files, out


def _dropped(printed: str) -> dict[str, str]:
    """{file: dropped} from the script's printed summary lines."""
    return {
        pathlib.Path(line.split("  ")[0]).name: line.split("dropped=", 1)[1]
        for line in printed.splitlines()
        if "  rows=" in line
    }


def test_ordinary_takes_split_at_their_end_markers(tmp_path):
    """Two takes each closed by its own marker come out exact and unmarked."""
    log = _sample(1, 0) + _sample(1, 0.05) + END + _sample(2, 0) + _sample(2, 0.05) + END
    code, files, out = _run(tmp_path, log)
    assert code == 0
    assert files == {
        "seq1_take1.csv": [("1", "0.00"), ("1", "0.05")],
        "seq2_take1.csv": [("2", "0.00"), ("2", "0.05")],
    }
    assert not list(out.glob("*.txt"))


def test_new_sequence_before_delayed_end_marker_is_split_and_marked(tmp_path):
    """A different sequence logged before the earlier take's marker gets its own uncertain take."""
    log = _sample(1, 0.9) + _sample(2, 0) + END + _sample(2, 0.1) + END
    code, files, out = _run(tmp_path, log)
    assert code == 0
    assert files == {
        "seq1_take1_uncertain.csv": [("1", "0.90")],
        "seq2_take1_uncertain.csv": [("2", "0.00"), ("2", "0.10")],
    }
    assert (
        "line 2: take clock went from 0.90 to 0.00"
        in (out / "seq1_take1_uncertain.txt").read_text()
    )


def test_same_sequence_restart_before_delayed_end_marker_is_split_and_marked(tmp_path):
    """The same id restarting its clock before the marker starts a second uncertain take."""
    log = _sample(1, 0.9) + _sample(1, 0) + END + _sample(1, 0.1) + END
    code, files, out = _run(tmp_path, log)
    assert code == 0
    assert files == {
        "seq1_take1_uncertain.csv": [("1", "0.90")],
        "seq1_take2_uncertain.csv": [("1", "0.00"), ("1", "0.10")],
    }
    assert "line 2:" in (out / "seq1_take2_uncertain.txt").read_text()


@pytest.mark.parametrize(
    "log",
    [
        _sample(1, 0.9) + _sample(2, 0) + END + _sample(2, 0.1) + END,
        _sample(1, 0.9) + _sample(1, 0) + END + _sample(1, 0.1) + END,
    ],
    ids=["new_sequence", "same_sequence_restart"],
)
def test_strict_refuses_an_ambiguous_boundary(tmp_path, log, capsys):
    """--strict exits 2 naming the line and reason, and writes no CSV."""
    code, files, _ = _run(tmp_path, log, "--strict")
    assert code == 2
    assert files == {}
    assert "line 2: take clock went from 0.90 to 0.00" in capsys.readouterr().err


def test_halt_byte_inside_a_take_does_not_split_it(tmp_path):
    """A halt reason and the RESUME back to the running id stay in one exact take."""
    log = _sample(1, 0) + _sample(0x15, 0.05) + _sample(1, 0.1) + END
    code, files, out = _run(tmp_path, log, "--strict")
    assert code == 0
    assert files == {"seq1_take1.csv": [("1", "0.00"), ("21", "0.05"), ("1", "0.10")]}
    assert not list(out.glob("*.txt"))


def test_running_id_change_without_clock_restart_is_split_and_marked(tmp_path):
    """A sequence handing over to another id mid-take splits there, marked uncertain."""
    log = _sample(1, 0) + _sample(0x11, 0.05) + _sample(32, 0.1) + END
    code, files, _ = _run(tmp_path, log)
    assert code == 0
    assert files == {
        "seq1_take1_uncertain.csv": [("1", "0.00"), ("17", "0.05")],
        "seq32_take1_uncertain.csv": [("32", "0.10")],
    }


def test_later_zero_marker_keeps_an_earlier_shared_drop_count_unknown(tmp_path, capsys):
    """A take that shared a nonzero dropped count stays unknown when a later marker reads 0."""
    # The 64-sample buffer holds A's one row and 63 of B's; B loses seven more before the drain
    # logs marker 7, then ends while C starts before the next drain logs marker 0.
    log = (
        _sample(1, 0)
        + "".join(_sample(2, i * 0.05) for i in range(63))
        + "SEQTRACE end dropped=7\n"
        + _sample(2, 3.5)
        + _sample(3, 0)
        + END
        + _sample(3, 0.05)
        + END
    )
    code, files, _ = _run(tmp_path, log)
    assert code == 0
    assert {name: len(rows) for name, rows in files.items()} == {
        "seq1_take1_uncertain.csv": 1,
        "seq2_take1_uncertain.csv": 64,
        "seq3_take1_uncertain.csv": 2,
    }
    assert _dropped(capsys.readouterr().out) == {
        "seq1_take1_uncertain.csv": "unknown (shared total 7)",
        "seq2_take1_uncertain.csv": "unknown (shared total 7)",
        "seq3_take1_uncertain.csv": "0",
    }


def test_take_cut_short_after_a_shared_zero_marker_is_unknown(tmp_path, capsys):
    """A take with rows after its last end marker, cut off by the end of the log, is unknown."""
    log = _sample(1, 0.9) + _sample(2, 0) + END + _sample(2, 0.1)
    code, files, _ = _run(tmp_path, log)
    assert code == 0
    assert files == {
        "seq1_take1_uncertain.csv": [("1", "0.90")],
        "seq2_take1_uncertain.csv": [("2", "0.00"), ("2", "0.10")],
    }
    assert _dropped(capsys.readouterr().out) == {
        "seq1_take1_uncertain.csv": "0",
        "seq2_take1_uncertain.csv": "unknown",
    }


def test_first_sample_above_t0_shares_the_marker_before_it(tmp_path, capsys):
    """A take whose first sample has t above 0 shares the previous marker's count, uncertain."""
    # The previous take fills the 64-sample buffer and ends; the next starts while it is full,
    # so its first three samples are lost and counted in the previous take's marker.
    log = (
        "".join(_sample(1, i * 0.05) for i in range(64))
        + "SEQTRACE end dropped=3\n"
        + _sample(2, 0.15)
        + _sample(2, 0.2)
        + END
    )
    reason = (
        "line 66: first sample at t=0.15: earlier sample losses may be included in the marker "
        "on line 65"
    )
    code, files, out = _run(tmp_path, log)
    assert code == 0
    assert {name: len(rows) for name, rows in files.items()} == {
        "seq1_take1_uncertain.csv": 64,
        "seq2_take1_uncertain.csv": 2,
    }
    assert _dropped(capsys.readouterr().out) == {
        "seq1_take1_uncertain.csv": "unknown (shared total 3)",
        "seq2_take1_uncertain.csv": "unknown (shared total 3)",
    }
    assert reason in (out / "seq1_take1_uncertain.txt").read_text()
    assert reason in (out / "seq2_take1_uncertain.txt").read_text()

    strict = tmp_path / "strict"
    strict.mkdir()
    code, files, _ = _run(strict, log, "--strict")
    assert code == 2
    assert files == {}
    assert reason in capsys.readouterr().err


@pytest.mark.parametrize(
    "b_rows, c_first_t",
    [([0.0], 0.15), ([0.0, 0.05], 0.05)],
    ids=["running_id_split", "clock_reset_split"],
)
def test_take_started_after_a_shared_marker_shares_it_whichever_rule_started_it(
    tmp_path, capsys, b_rows, c_first_t
):
    """A take starting above t=0 after a shared marker shares it, whichever rule split it."""
    # A fills most of the 64-sample buffer and ends; B logs its first rows while the buffer
    # is full, and three samples are lost into the marker that A and B share. C's first
    # retained row then either exceeds B's last t (a running-id split: C lost its first
    # samples, or kept B's clock through a handover and lost nothing; the log cannot tell)
    # or equals it (a clock-reset split); either way marker 3 may count C's losses, and the
    # later marker 0 must not make C read exact.
    a_rows = 64 - len(b_rows)
    log = (
        "".join(_sample(1, i * 0.05) for i in range(a_rows))
        + "".join(_sample(2, t) for t in b_rows)
        + "SEQTRACE end dropped=3\n"
        + _sample(3, c_first_t)
        + _sample(3, c_first_t + 0.05)
        + END
    )
    reason = (
        f"line 66: first sample at t={c_first_t:.2f}: earlier sample losses may be included in "
        "the marker on line 65"
    )
    code, files, out = _run(tmp_path, log)
    assert code == 0
    assert {name: len(rows) for name, rows in files.items()} == {
        "seq1_take1_uncertain.csv": a_rows,
        "seq2_take1_uncertain.csv": len(b_rows),
        "seq3_take1_uncertain.csv": 2,
    }
    assert _dropped(capsys.readouterr().out) == {
        "seq1_take1_uncertain.csv": "unknown (shared total 3)",
        "seq2_take1_uncertain.csv": "unknown (shared total 3)",
        "seq3_take1_uncertain.csv": "unknown (shared total 3)",
    }
    assert reason in (out / "seq3_take1_uncertain.txt").read_text()
