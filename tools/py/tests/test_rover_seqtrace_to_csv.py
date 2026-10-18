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
