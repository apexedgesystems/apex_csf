"""
SLIP framing and APROTO packet encode/decode for C2 client.

Provides low-level protocol primitives:
  - SLIP encode/decode (RFC 1055)
  - APROTO header build/parse (14-byte binary header)
  - ACK/NAK payload parsing
  - File transfer payload builders

Wire format:
  SLIP_END | APROTO_HEADER (14B) | PAYLOAD (0-65535B) | SLIP_END

APROTO header (little-endian):
  magic:2 version:1 flags:1 fullUid:4 opcode:2 sequence:2 payloadLength:2
"""

import struct
from typing import Optional

# SLIP constants (RFC 1055)
SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD

# APROTO constants
APROTO_MAGIC = 0x5041
APROTO_VERSION = 1
APROTO_HEADER_SIZE = 14
APROTO_HEADER_FMT = "<HBBI HHH"  # magic, version, flags, fullUid, opcode, seq, payloadLen

# Flag bits (match AprotoFlags bitfield: internalOrigin:1, isResponse:1, ackRequested:1, ...)
FLAG_INTERNAL = 0x01
FLAG_RESPONSE = 0x02
FLAG_ACK_REQ = 0x04
FLAG_CRC = 0x08
FLAG_ENCRYPTED = 0x10

# System opcodes (0x0000-0x00FF)
SYS_NOOP = 0x0000
SYS_PING = 0x0001
SYS_GET_STATUS = 0x0002
SYS_RESET = 0x0003
SYS_ACK = 0x00FE
SYS_NAK = 0x00FF

# File transfer opcodes
FILE_BEGIN = 0x0020
FILE_CHUNK = 0x0021
FILE_END = 0x0022
FILE_ABORT = 0x0023
FILE_STATUS = 0x0024

# Executive opcodes
EXEC_GET_HEALTH = 0x0100
EXEC_NOOP = 0x0101
EXEC_GET_CLOCK_FREQ = 0x0102
EXEC_GET_RT_MODE = 0x0103
EXEC_GET_CLOCK_CYCLES = 0x0104
EXEC_CMD_PAUSE = 0x0110
EXEC_CMD_RESUME = 0x0111
EXEC_CMD_SHUTDOWN = 0x0112
EXEC_CMD_FAST_FORWARD = 0x0113
EXEC_CMD_LOCK = 0x0114
EXEC_CMD_UNLOCK = 0x0115
EXEC_CMD_SLEEP = 0x0116
EXEC_CMD_WAKE = 0x0117
EXEC_SET_CLOCK_FREQ = 0x0120
EXEC_SET_VERBOSITY = 0x0121
EXEC_RELOAD_TPRM = 0x0125
EXEC_RELOAD_LIBRARY = 0x0126
EXEC_RELOAD_EXECUTIVE = 0x0127
EXEC_INSPECT = 0x0130

# NAK status codes
NAK_SUCCESS = 0
NAK_UNKNOWN_OPCODE = 1
NAK_INVALID_PAYLOAD = 2
NAK_NO_RESOLVER = 3
NAK_COMPONENT_NOT_FOUND = 4

NAK_NAMES = {
    0: "SUCCESS",
    1: "UNKNOWN_OPCODE",
    2: "INVALID_PAYLOAD",
    3: "NO_RESOLVER",
    4: "COMPONENT_NOT_FOUND",
    5: "LOAD_FAILED",
    6: "EXEC_FAILED",
    10: "TRANSFER_IN_PROGRESS",
    11: "NO_TRANSFER",
    12: "CHUNK_OUT_OF_ORDER",
    13: "CRC_MISMATCH",
    14: "WRITE_FAILED",
    15: "PATH_INVALID",
    17: "DLOPEN_FAILED",
    18: "FACTORY_NOT_FOUND",
    19: "INIT_FAILED",
    20: "NOT_SWAPPABLE",
}


def slip_encode(data: bytes) -> bytes:
    """SLIP-encode data with END delimiters."""
    out = bytearray([SLIP_END])
    for b in data:
        if b == SLIP_END:
            out.extend([SLIP_ESC, SLIP_ESC_END])
        elif b == SLIP_ESC:
            out.extend([SLIP_ESC, SLIP_ESC_ESC])
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


def slip_decode_stream(buf: bytearray) -> list[bytes]:
    """Extract complete SLIP frames from buffer, removing consumed bytes.

    Returns list of decoded frames. Modifies buf in-place to remove consumed data.
    """
    frames = []
    while True:
        # Find frame boundaries.
        start = -1
        end = -1
        for i, b in enumerate(buf):
            if b == SLIP_END:
                if start < 0:
                    start = i
                else:
                    end = i
                    break

        if start < 0 or end < 0:
            break

        # Decode frame between delimiters.
        raw = buf[start + 1 : end]
        del buf[: end + 1]

        if len(raw) == 0:
            continue

        decoded = bytearray()
        i = 0
        while i < len(raw):
            if raw[i] == SLIP_ESC:
                i += 1
                if i < len(raw):
                    if raw[i] == SLIP_ESC_END:
                        decoded.append(SLIP_END)
                    elif raw[i] == SLIP_ESC_ESC:
                        decoded.append(SLIP_ESC)
                    else:
                        decoded.append(raw[i])
            else:
                decoded.append(raw[i])
            i += 1

        if len(decoded) > 0:
            frames.append(bytes(decoded))

    return frames


def build_packet(
    full_uid: int,
    opcode: int,
    seq: int,
    payload: bytes = b"",
    ack_req: bool = True,
    is_response: bool = False,
) -> bytes:
    """Build a complete APROTO packet (header + payload)."""
    flags = 0
    if is_response:
        flags |= FLAG_RESPONSE
    if ack_req:
        flags |= FLAG_ACK_REQ

    header = struct.pack(
        APROTO_HEADER_FMT,
        APROTO_MAGIC,
        APROTO_VERSION,
        flags,
        full_uid,
        opcode,
        seq,
        len(payload),
    )
    return header + payload


def parse_header(data: bytes) -> Optional[dict]:
    """Parse APROTO header from raw bytes. Returns None on invalid data."""
    if len(data) < APROTO_HEADER_SIZE:
        return None

    magic, version, flags, full_uid, opcode, seq, payload_len = struct.unpack(
        APROTO_HEADER_FMT, data[:APROTO_HEADER_SIZE]
    )

    if magic != APROTO_MAGIC:
        return None

    return {
        "magic": magic,
        "version": version,
        "flags": flags,
        "full_uid": full_uid,
        "opcode": opcode,
        "sequence": seq,
        "payload_length": payload_len,
        "is_response": bool(flags & FLAG_RESPONSE),
        "ack_requested": bool(flags & FLAG_ACK_REQ),
        "has_crc": bool(flags & FLAG_CRC),
        "payload": data[APROTO_HEADER_SIZE : APROTO_HEADER_SIZE + payload_len],
    }


def parse_ack(payload: bytes) -> dict:
    """Parse ACK/NAK payload (8+ bytes: cmdOpcode:2, cmdSequence:2, status:1, reserved:3)."""
    if len(payload) < 8:
        return {"error": f"ACK payload too short ({len(payload)} bytes, need 8)"}

    cmd_opcode, cmd_seq, status = struct.unpack("<HHB", payload[:5])
    extra = payload[8:] if len(payload) > 8 else b""

    return {
        "cmd_opcode": cmd_opcode,
        "cmd_sequence": cmd_seq,
        "status": status,
        "status_name": NAK_NAMES.get(status, f"UNKNOWN({status})"),
        "extra": extra,
    }


def build_file_begin(
    total_size: int,
    chunk_size: int,
    total_chunks: int,
    crc32: int,
    dest_path: str,
) -> bytes:
    """Build FILE_BEGIN payload (76 bytes)."""
    path_bytes = dest_path.encode("utf-8")[:63]
    path_padded = path_bytes + b"\x00" * (64 - len(path_bytes))
    return struct.pack("<IHHI", total_size, chunk_size, total_chunks, crc32) + path_padded


def build_file_chunk(chunk_index: int, data: bytes) -> bytes:
    """Build FILE_CHUNK payload (2-byte index + data)."""
    return struct.pack("<H", chunk_index) + data


def parse_file_end_response(data: bytes) -> dict:
    """Parse FILE_END response (8 bytes)."""
    if len(data) < 8:
        return {"error": "Response too short"}
    status, r1, r2, r3, bytes_written = struct.unpack("<BBBBI", data[:8])
    return {"status": status, "bytes_written": bytes_written}


def parse_file_status_response(data: bytes) -> dict:
    """Parse FILE_STATUS response (8 bytes)."""
    if len(data) < 8:
        return {"error": "Response too short"}
    state, reserved, chunks_received, bytes_received = struct.unpack("<BBHI", data[:8])
    state_names = {0: "IDLE", 1: "RECEIVING", 2: "COMPLETE", 3: "ERROR"}
    return {
        "state": state,
        "state_name": state_names.get(state, f"UNKNOWN({state})"),
        "chunks_received": chunks_received,
        "bytes_received": bytes_received,
    }
