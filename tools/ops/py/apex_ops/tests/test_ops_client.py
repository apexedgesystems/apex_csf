"""Unit tests for apex_ops.client module.

Tests CRC32C computation (software implementation matching hardware CRC32C)
and the client's handling of several response frames in one read.
"""

import socket
import struct

import pytest
from apex_ops import protocol as proto
from apex_ops.client import AprotoClient, crc32c

# =============================== CRC32C ================================


def test_crc32c_empty():
    """CRC32C of empty data returns initial value XORed."""
    result = crc32c(b"")
    assert isinstance(result, int)


def test_crc32c_known_value():
    """CRC32C of known input matches expected checksum."""
    # "123456789" is the standard CRC test vector
    result = crc32c(b"123456789")
    assert result == 0xE3069283


def test_crc32c_deterministic():
    """Same input always produces same CRC."""
    data = b"apex_csf test data"
    assert crc32c(data) == crc32c(data)


def test_crc32c_different_inputs_differ():
    """Different inputs produce different CRCs."""
    assert crc32c(b"aaa") != crc32c(b"bbb")


def test_crc32c_single_byte():
    """CRC32C handles single-byte input."""
    result = crc32c(b"\x00")
    assert isinstance(result, int)
    assert 0 <= result <= 0xFFFFFFFF


# =========================== Response frames ============================


class FakeSocket:
    """Stands in for a connected socket: recv() hands out prepared reads."""

    def __init__(self, reads):
        self._reads = list(reads)
        self.sent = []

    def settimeout(self, _timeout):
        pass

    def connect(self, _address):
        pass

    def sendall(self, data):
        self.sent.append(bytes(data))

    def recv(self, _size):
        if not self._reads:
            raise socket.timeout()
        return self._reads.pop(0)

    def close(self):
        pass


def response_frame(seq: int, stage: int, status: int = 0, extra: bytes = b"") -> bytes:
    """A SLIP-framed ACK for command sequence `seq` at lifecycle `stage`."""
    ack = struct.pack("<HHBBH", 0x0100, seq, status, stage, 0) + extra
    return proto.slip_encode(
        proto.build_packet(0x00D000, proto.SYS_ACK, seq, ack, ack_req=False, is_response=True)
    )


def client_reading(reads) -> AprotoClient:
    client = AprotoClient("unused", timeout=0.2)
    client._sock = FakeSocket(reads)
    return client


def test_completion_in_the_same_read_as_queued_is_returned():
    """A QUEUED frame and its COMPLETION delivered by one read both count."""
    one_read = response_frame(0, proto.ACK_STAGE_QUEUED) + response_frame(
        0, proto.ACK_STAGE_COMPLETION, extra=b"\x2a\x00\x00\x00"
    )
    client = client_reading([one_read])

    ack = client.send_command(0x00D000, 0x0100)

    assert ack["stage"] == proto.ACK_STAGE_COMPLETION
    assert ack["queued"] is True
    assert ack["extra"] == b"\x2a\x00\x00\x00"


def test_frames_left_from_one_command_do_not_answer_the_next():
    """A frame still queued for an earlier sequence is discarded as stale."""
    reads = [
        response_frame(0, proto.ACK_STAGE_RESULT) + response_frame(0, proto.ACK_STAGE_RESULT),
        response_frame(1, proto.ACK_STAGE_RESULT, status=3),
    ]
    client = client_reading(reads)

    assert client.send_command(0x00D000, 0x0100)["status"] == 0
    assert client.send_command(0x00D000, 0x0100)["status"] == 3


def test_queued_without_completion_times_out():
    """A QUEUED frame alone is not an outcome."""
    client = client_reading([response_frame(0, proto.ACK_STAGE_QUEUED)])

    with pytest.raises(TimeoutError):
        client.send_command(0x00D000, 0x0100)


def test_connect_drops_frames_of_the_previous_connection(monkeypatch):
    """Frames decoded before a reconnect belong to the old connection."""
    client = client_reading([])
    client._rx_frames.append(b"left over")
    monkeypatch.setattr(socket, "socket", lambda *_args: FakeSocket([]))

    client.connect()

    assert len(client._rx_frames) == 0
