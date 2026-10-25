"""Unit tests for the rover demo's shared-memory ring probe.

The probe is a standalone demo script, loaded by path. Each test drives its
reader over an in-memory buffer laid out like the bridge's region A, with a
producer that follows the bridge's admission rule: slot HEAD may be written
only while HEAD - consumer < capacity.
"""

import importlib.util
import pathlib
import struct

import pytest

PROBE = pathlib.Path(__file__).resolve().parents[1] / "scripts/rover_ring_probe.py"
CAP = 16
SIZE = 256


def _load_probe():
    """Import the probe script as a module without running its CLI."""
    spec = importlib.util.spec_from_file_location("rover_ring_probe", PROBE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = _load_probe()


def _frame(number: int) -> bytes:
    """A slot-sized frame filled with its number, tick field set to it."""
    body = bytearray([number & 0xFF]) * SIZE
    struct.pack_into("<Q", body, 8, number)
    return bytes(body)


class _Region(bytearray):
    """Region A buffer that runs a hook just before each slice read (the frame copy)."""

    on_copy = None

    def __getitem__(self, key):
        if isinstance(key, slice) and self.on_copy is not None:
            hook, self.on_copy = self.on_copy, None
            hook()
        return bytearray.__getitem__(self, key)


def _head(buf) -> int:
    """Region A's producer cursor."""
    return struct.unpack_from("<Q", buf, probe.PROD_CURSOR)[0]


def _cons(buf) -> int:
    """Region A's consumer cursor."""
    return struct.unpack_from("<Q", buf, probe.CONS_CURSOR)[0]


def _publish(buf, partial: bool = False) -> bool:
    """One producer step under the bridge's admission rule; partial stops mid-copy."""
    head, tail = _head(buf), _cons(buf)
    if head - tail >= CAP:
        return False
    off = probe.PRELUDE_BYTES + (head & (CAP - 1)) * SIZE
    data = _frame(head + 1)
    if partial:
        bytearray.__setitem__(buf, slice(off, off + SIZE // 2), data[: SIZE // 2])
        return True
    bytearray.__setitem__(buf, slice(off, off + SIZE), data)
    struct.pack_into("<Q", buf, probe.PROD_CURSOR, head + 1)
    return True


def _ring(published: int, consumed: int):
    """A reader whose unread frames are consumed+1..published, behind consumer cursor consumed."""
    buf = _Region(probe.PRELUDE_BYTES + CAP * SIZE)
    struct.pack_into("<Q", buf, probe.PROD_CURSOR, consumed)
    struct.pack_into("<Q", buf, probe.CONS_CURSOR, consumed)
    for _ in range(published - consumed):
        _publish(buf)
    ring = probe.Ring.__new__(probe.Ring)
    ring.m = buf
    ring.a_size, ring.a_cap = SIZE, CAP
    return ring


def _wrap_during_copy(buf):
    """Producer burst that tries to wrap the ring, stopping its last write mid-copy."""

    def burst():
        for _ in range(CAP - 1):
            _publish(buf)
        _publish(buf, partial=True)

    return burst


def test_nothing_published_returns_none():
    """A ring the producer has never written yields no frame and leaves the consumer at zero."""
    ring = _ring(published=0, consumed=0)
    assert ring.latest() is None
    assert _cons(ring.m) == 0


def test_lagging_reader_skips_to_newest_unread_frame():
    """A reader that fell behind returns the newest frame and consumes every older one."""
    ring = _ring(published=10, consumed=3)
    assert ring.latest() == _frame(10)
    assert _cons(ring.m) == 10


def test_full_ring_wrap_during_copy_returns_a_coherent_frame():
    """The producer cannot reuse the slot being copied, so a full ring's newest frame is intact."""
    ring = _ring(published=CAP, consumed=0)
    ring.m.on_copy = _wrap_during_copy(ring.m)
    assert ring.latest() == _frame(CAP)
    assert _cons(ring.m) == CAP


def test_no_unread_frame_returns_none_without_reading_a_released_slot():
    """With head == consumer the reader returns None and never copies the released slot."""
    ring = _ring(published=CAP, consumed=CAP)
    ring.m.on_copy = pytest.fail
    assert ring.latest() is None
    assert _cons(ring.m) == CAP


def test_repeated_reads_return_each_new_frame_once():
    """Back-to-back reads yield a frame, then None, then the next frame once published."""
    ring = _ring(published=5, consumed=0)
    assert ring.latest() == _frame(5)
    assert ring.latest() is None
    _publish(ring.m)
    assert ring.latest() == _frame(6)
    assert _cons(ring.m) == 6


def test_send_writes_the_command_frame_and_advances_the_producer_cursor():
    """A command lands in region B's head slot and region B's producer cursor moves by one."""
    ring = _ring(published=0, consumed=0)
    b_size, b_cap = 32, 4
    ring.b_base = probe.PRELUDE_BYTES + CAP * SIZE
    ring.b_size, ring.b_cap = b_size, b_cap
    ring.m.extend(bytearray(probe.PRELUDE_BYTES + b_cap * b_size))
    ring.send(probe.ROVER_UID, probe.OP_HALT)
    slot = ring.b_base + probe.PRELUDE_BYTES
    magic, version, _, dst, opcode, _, length = struct.unpack_from("<HBBIHHH", ring.m, slot)
    assert (magic, version, dst, opcode, length) == (
        probe.APROTO_MAGIC,
        probe.APROTO_VERSION,
        probe.ROVER_UID,
        probe.OP_HALT,
        0,
    )
    assert struct.unpack_from("<Q", ring.m, ring.b_base + probe.PROD_CURSOR)[0] == 1
