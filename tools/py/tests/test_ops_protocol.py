"""Unit tests for apex_tools.ops.protocol module.

Tests SLIP framing, APROTO packet construction, and header parsing.
"""

from apex_tools.ops.protocol import (
    build_packet,
    parse_header,
    slip_decode_stream,
    slip_encode,
)

# =============================== SLIP Encoding ================================


def test_slip_encode_empty():
    """SLIP-encode empty payload produces END-END frame."""
    encoded = slip_encode(b"")
    assert encoded[0] == 0xC0
    assert encoded[-1] == 0xC0


def test_slip_encode_no_special_bytes():
    """SLIP-encode payload without special bytes wraps with END markers."""
    encoded = slip_encode(b"\x01\x02\x03")
    assert encoded == b"\xc0\x01\x02\x03\xc0"


def test_slip_encode_escapes_end_byte():
    """SLIP-encode escapes 0xC0 (END) as 0xDB 0xDC."""
    encoded = slip_encode(bytes([0xC0]))
    assert b"\xdb\xdc" in encoded


def test_slip_encode_escapes_esc_byte():
    """SLIP-encode escapes 0xDB (ESC) as 0xDB 0xDD."""
    encoded = slip_encode(bytes([0xDB]))
    assert b"\xdb\xdd" in encoded


# =============================== SLIP Decoding ================================


def test_slip_decode_single_frame():
    """Decode a single SLIP frame from a stream buffer."""
    buf = bytearray(b"\xc0\x01\x02\x03\xc0")
    frames = slip_decode_stream(buf)
    assert len(frames) == 1
    assert frames[0] == b"\x01\x02\x03"


def test_slip_decode_multiple_frames():
    """Decode multiple SLIP frames from a single buffer."""
    buf = bytearray(b"\xc0\x41\xc0\xc0\x42\x43\xc0")
    frames = slip_decode_stream(buf)
    assert len(frames) == 2


def test_slip_decode_escaped_bytes():
    """Decode SLIP frame with escaped END and ESC bytes."""
    encoded = slip_encode(bytes([0xC0, 0xDB, 0x01]))
    buf = bytearray(encoded)
    frames = slip_decode_stream(buf)
    assert len(frames) == 1
    assert frames[0] == bytes([0xC0, 0xDB, 0x01])


def test_slip_roundtrip():
    """Encode then decode preserves original payload."""
    original = bytes(range(256))
    encoded = slip_encode(original)
    frames = slip_decode_stream(bytearray(encoded))
    assert len(frames) == 1
    assert frames[0] == original


# =============================== Packet Building ================================


def test_build_packet_returns_bytes():
    """build_packet returns a bytes object."""
    pkt = build_packet(full_uid=0x000100, opcode=0x0001, seq=1, payload=b"\x01\x02")
    assert isinstance(pkt, bytes)
    assert len(pkt) > 0


def test_build_packet_header_size():
    """APROTO header is 14 bytes minimum."""
    pkt = build_packet(full_uid=0x000000, opcode=0x0000, seq=0, payload=b"")
    assert len(pkt) >= 14


# =============================== Header Parsing ================================


def test_parse_header_valid():
    """parse_header returns dict with expected fields for a valid packet."""
    pkt = build_packet(full_uid=0x000100, opcode=0x0001, seq=1, payload=b"\xAA\xBB")
    hdr = parse_header(pkt)
    assert hdr is not None
    assert hdr["opcode"] == 0x0001


def test_parse_header_too_short():
    """parse_header returns None for undersized data."""
    result = parse_header(b"\x00\x01\x02")
    assert result is None


# ============================ ACK stages ============================


def test_parse_ack_stage_field():
    """The stage byte (offset 5) parses; pre-stage frames read RESULT."""
    import struct

    from apex_tools.ops import protocol as proto

    payload = struct.pack("<HHBB", 0x0642, 77, 0, proto.ACK_STAGE_QUEUED) + b"\x00\x00"
    ack = proto.parse_ack(payload)
    assert ack["cmd_sequence"] == 77
    assert ack["stage"] == proto.ACK_STAGE_QUEUED

    legacy = struct.pack("<HHB", 0x0100, 5, 0) + b"\x00\x00\x00"
    ack = proto.parse_ack(legacy)
    assert ack["stage"] == proto.ACK_STAGE_RESULT


def test_terminal_frame_selection():
    """send_command returns the terminal frame: QUEUED is interim,
    COMPLETION (same sequence) is the outcome; stale sequences are
    discarded."""
    import struct

    from apex_tools.ops import protocol as proto
    from apex_tools.ops.client import AprotoClient

    client = AprotoClient.__new__(AprotoClient)
    client.timeout = 1.0
    client._seq = 42  # _next_seq returns the current value

    def frame(seq, stage, status=0, extra=b""):
        payload = struct.pack("<HHBB", 0x0642, seq, status, stage) + b"\x00\x00" + extra
        return {"payload": payload}

    responses = iter(
        [
            frame(9, proto.ACK_STAGE_COMPLETION),  # stale, prior exchange
            frame(42, proto.ACK_STAGE_QUEUED),
            frame(42, proto.ACK_STAGE_COMPLETION, status=5, extra=b"\x03"),
        ]
    )
    client._send_raw = lambda pkt: None
    client._recv_response = lambda timeout=None: next(responses)

    ack = client.send_command(0x00C900, 0x0642, b"")
    assert ack["status"] == 5
    assert ack["queued"] is True
    assert ack["extra"] == b"\x03"
