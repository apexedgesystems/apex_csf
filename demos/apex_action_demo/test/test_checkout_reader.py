"""Unit tests for the action demo checkout's reading of the engine's statistics block.

The checkout is a standalone demo script, loaded by path. Each test feeds its
reader a block laid out like the action engine's registered EngineStats and
drives the running-sequence wait with a fake client and clock.
"""

import importlib.util
import pathlib
import struct

import pytest

CHECKOUT = pathlib.Path(__file__).resolve().parents[1] / "scripts/checkout.py"
COUNTERS = 16  # the cumulative uint32 counters before the gauge


def _load_checkout():
    """Import the checkout script as a module without running its CLI."""
    spec = importlib.util.spec_from_file_location("action_checkout", CHECKOUT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


checkout = _load_checkout()


def current_block(running: int, timeout_seq: int = 0) -> bytes:
    """The 72-byte layout: sixteen counters, the gauge, the timeout context."""
    counters = struct.pack("<16I", *range(1, COUNTERS + 1))
    return counters + struct.pack("<I", running) + struct.pack("<HBB", timeout_seq, 3, 1)


def old_block(timeout_seq: int = 0) -> bytes:
    """The 68-byte layout before the gauge: the timeout context sits at offset 64."""
    counters = struct.pack("<16I", *range(1, COUNTERS + 1))
    return counters + struct.pack("<HBB", timeout_seq, 3, 1)


class FakeClient:
    """Answers INSPECT with the next block of a scripted sequence (the last one repeats)."""

    def __init__(self, blocks):
        self.blocks = list(blocks)
        self.reads = 0

    def inspect(self, fulluid, category):
        assert fulluid == checkout.ALL_COMPONENTS["Action"] and category == 4
        block = self.blocks[min(self.reads, len(self.blocks) - 1)]
        self.reads += 1
        return {"extra": block}


class FakeTime:
    """A clock the sleeps advance, so a wait's deadline is deterministic."""

    def __init__(self):
        self.now = 1000.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


@pytest.fixture
def clock(monkeypatch):
    fake = FakeTime()
    monkeypatch.setattr(checkout, "time", fake)
    return fake


def test_gauge_is_read_only_from_the_layout_that_carries_it():
    stats = checkout.read_action_stats(FakeClient([current_block(2)]))
    assert stats["sequencesRunning"] == 2
    assert stats["blockBytes"] == 72
    assert stats["totalCycles"] == 1 and stats["exclusionStops"] == 16


def test_old_layout_timeout_context_is_not_a_running_count():
    for block in (old_block(), old_block(timeout_seq=7)):
        stats = checkout.read_action_stats(FakeClient([block]))
        assert "sequencesRunning" not in stats
        assert stats["blockBytes"] == 68
        assert stats["exclusionStops"] == 16


def test_shorter_block_has_no_gauge():
    stats = checkout.read_action_stats(FakeClient([current_block(0)[:64]]))
    assert "sequencesRunning" not in stats


def test_wait_ends_at_once_without_the_gauge(clock):
    for block in (old_block(), old_block(timeout_seq=7), current_block(0)[:64]):
        client = FakeClient([block])
        quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 60.0)
        assert (quiescent, running) == (False, None)
        assert client.reads == 1 and elapsed == 0.0
        assert "expected 72" in checkout.quiescence_detail(client, running, elapsed)


def test_wait_needs_two_consecutive_zero_readings(clock):
    client = FakeClient(
        [current_block(1), current_block(0), current_block(1), current_block(0), current_block(0)]
    )
    quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 60.0)
    assert (quiescent, running) == (True, 0)
    assert client.reads == 5
    assert elapsed == pytest.approx(0.8)


def test_wait_fails_on_its_deadline_while_a_sequence_runs(clock):
    client = FakeClient([current_block(1)])
    quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 1.0)
    assert (quiescent, running) == (False, 1)
    assert elapsed >= 1.0
    assert (
        checkout.quiescence_detail(client, running, elapsed) == f"running=1 after {elapsed:.1f} s"
    )
