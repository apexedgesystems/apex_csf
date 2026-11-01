"""Unit tests for the action demo checkout's wait on the action component's running count.

The checkout is a standalone demo script, loaded by path. Each test answers its
GET_RUNNING query with a scripted reply and drives the running-sequence wait
with a fake client and clock.
"""

import importlib.util
import pathlib

import pytest

CHECKOUT = pathlib.Path(__file__).resolve().parents[1] / "scripts/checkout.py"


def _load_checkout():
    """Import the checkout script as a module without running its CLI."""
    spec = importlib.util.spec_from_file_location("action_checkout", CHECKOUT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


checkout = _load_checkout()
proto = checkout.proto


def reply(running: int) -> dict:
    """A SUCCESS reply carrying the one-byte count."""
    return {"status": 0, "status_name": "SUCCESS", "extra": bytes([running])}


REJECTED = {"status": 2, "status_name": "UNKNOWN_OPCODE", "extra": b""}
EMPTY = {"status": 0, "status_name": "SUCCESS", "extra": b""}


class FakeClient:
    """Answers GET_RUNNING with the next reply of a scripted sequence (the last one repeats)."""

    def __init__(self, replies):
        self.replies = list(replies)
        self.reads = 0

    def send_command(self, full_uid, opcode, payload=b""):
        assert (full_uid, opcode, payload) == (proto.FULLUID_ACTION, proto.ACTION_GET_RUNNING, b"")
        answer = self.replies[min(self.reads, len(self.replies) - 1)]
        self.reads += 1
        return answer


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


def test_count_is_the_reply_byte():
    assert checkout.read_running_sequences(FakeClient([reply(2)])) == 2
    assert checkout.read_running_sequences(FakeClient([reply(0)])) == 0


def test_rejected_or_empty_reply_has_no_count():
    assert checkout.read_running_sequences(FakeClient([REJECTED])) is None
    assert checkout.read_running_sequences(FakeClient([EMPTY])) is None


def test_wait_ends_at_once_without_a_count(clock):
    for answer in (REJECTED, EMPTY):
        client = FakeClient([answer])
        quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 60.0)
        assert (quiescent, running) == (False, None)
        assert client.reads == 1 and elapsed == 0.0
        detail = checkout.quiescence_detail(client, running, elapsed)
        assert detail.startswith("running count unavailable: GET_RUNNING " + answer["status_name"])
        assert detail.endswith("0-byte reply")


def test_wait_ends_on_the_first_zero_reading(clock):
    client = FakeClient([reply(1), reply(1), reply(0), reply(1)])
    quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 60.0)
    assert (quiescent, running) == (True, 0)
    assert client.reads == 3
    assert elapsed == pytest.approx(0.4)


def test_wait_fails_on_its_deadline_while_a_sequence_runs(clock):
    client = FakeClient([reply(1)])
    quiescent, running, elapsed = checkout.wait_for_no_running_sequence(client, 1.0)
    assert (quiescent, running) == (False, 1)
    assert elapsed >= 1.0
    assert (
        checkout.quiescence_detail(client, running, elapsed) == f"running=1 after {elapsed:.1f} s"
    )
