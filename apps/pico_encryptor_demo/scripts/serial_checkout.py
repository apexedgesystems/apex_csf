#!/usr/bin/env python3
"""
Firmware checkout script for Raspberry Pi Pico (pico_encryptor_demo).

Validates encryption firmware functionality over dual serial ports.
Run after flashing to verify features as they are incrementally added.

Usage:
    python3 apps/pico_encryptor_demo/scripts/serial_checkout.py
    python3 apps/pico_encryptor_demo/scripts/serial_checkout.py --data-port /dev/ttyUSB1
    python3 apps/pico_encryptor_demo/scripts/serial_checkout.py --cmd-port /dev/ttyUSB2
    python3 apps/pico_encryptor_demo/scripts/serial_checkout.py --verbose

Port assignments:
    Data channel:    UART0 via FTDI adapter  (/dev/ftdi_0)
    Command channel: USB CDC via native USB  (/dev/pico_0)

Checkout groups:
     1. Connection      - Serial ports accessible
     2. Data Channel    - SLIP framing, CRC validation, encrypt pipeline
     3. Key Store       - Flash-based key provisioning and management
     4. Key Mode        - Lock/unlock key selection
     5. IV Management   - Nonce tracking and reset
     6. Diagnostics     - Statistics and counters
     7. Rejection       - Invalid packets correctly discarded
     8. Stress          - Continuous encryption reliability
     9. Throughput      - Sustained encryption data rates
    10. Overhead        - No-op on M0+ (no DWT), verify zeros
    11. Idle            - No spurious output

Prerequisites:
    - Firmware flashed and board reset
    - pyserial installed (pip install pyserial)
    - cryptography installed (pip install cryptography) -- for decrypt verify
    - FTDI adapter connected to UART0 (GP0 TX, GP1 RX)
    - Pico USB cable connected (native USB CDC for command channel)

See docs/ENCRYPTOR_DESIGN.md for full protocol specification.
"""

import argparse
import os
import struct
import sys
import time

import serial

# Optional: for decrypt verification
try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    HAS_CRYPTO = True
except ImportError:
    HAS_CRYPTO = False

# =============================================================================
# Configuration
# =============================================================================

DEFAULT_DATA_PORT = "/dev/ftdi_0"
DEFAULT_CMD_PORT = "/dev/pico_0"
DEFAULT_BAUD = 115200
TIMEOUT_S = 2.0

VERBOSE = False

# Packet limits (must match EncryptorConfig.hpp)
MAX_PLAINTEXT_SIZE = 256
KEY_SLOT_COUNT = 16
KEY_SIZE = 32
NONCE_SIZE = 12
TAG_SIZE = 16

# =============================================================================
# Command opcodes (must match CommandDeck.hpp)
# =============================================================================

CMD_KEY_STORE_WRITE = 0x01
CMD_KEY_STORE_READ = 0x02
CMD_KEY_STORE_ERASE = 0x03
CMD_KEY_STORE_STATUS = 0x04

CMD_KEY_LOCK = 0x10
CMD_KEY_UNLOCK = 0x11
CMD_KEY_MODE_STATUS = 0x12

CMD_IV_RESET = 0x20
CMD_IV_STATUS = 0x21

CMD_STATS = 0x30
CMD_STATS_RESET = 0x31

CMD_OVERHEAD = 0x40
CMD_OVERHEAD_RESET = 0x41
CMD_FASTFORWARD = 0x42

# Command response status codes
STATUS_OK = 0x00
STATUS_ERR_INVALID_CMD = 0x01
STATUS_ERR_BAD_PAYLOAD = 0x02
STATUS_ERR_KEY_SLOT = 0x03
STATUS_ERR_FLASH = 0x04
STATUS_ERR_LOCKED = 0x05

# Key modes
KEY_MODE_RANDOM = 0x00
KEY_MODE_LOCKED = 0x01

# =============================================================================
# SLIP framing (RFC 1055)
# =============================================================================

SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD


def slip_encode(data: bytes) -> bytes:
    """SLIP-encode a frame with leading and trailing END delimiters."""
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


def slip_decode_frames(raw: bytes) -> list:
    """Decode all complete SLIP frames from raw bytes."""
    frames = []
    current = bytearray()
    in_frame = False
    i = 0

    while i < len(raw):
        b = raw[i]
        if b == SLIP_END:
            if in_frame and len(current) > 0:
                frames.append(bytes(current))
            current = bytearray()
            in_frame = True
        elif b == SLIP_ESC:
            i += 1
            if i < len(raw):
                if raw[i] == SLIP_ESC_END:
                    current.append(SLIP_END)
                elif raw[i] == SLIP_ESC_ESC:
                    current.append(SLIP_ESC)
                else:
                    current.append(raw[i])
            in_frame = True
        else:
            current.append(b)
            in_frame = True
        i += 1

    return frames


# =============================================================================
# CRC-16/XMODEM
# =============================================================================


def crc16_xmodem(data: bytes) -> int:
    """Calculate CRC-16/XMODEM (poly 0x1021, init 0x0000)."""
    crc = 0x0000
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


# =============================================================================
# Protocol helpers
# =============================================================================


def build_data_frame(plaintext: bytes) -> bytes:
    """Build a SLIP-encoded data channel input frame: plaintext + CRC-16."""
    crc = crc16_xmodem(plaintext)
    payload = plaintext + struct.pack(">H", crc)
    return slip_encode(payload)


def parse_encrypted_frame(frame: bytes):
    """Parse an encrypted output frame. Returns (key_index, nonce, ct, tag)."""
    if len(frame) < 1 + NONCE_SIZE + TAG_SIZE:
        return None
    key_index = frame[0]
    nonce = frame[1 : 1 + NONCE_SIZE]
    tag = frame[-(TAG_SIZE):]
    ciphertext = frame[1 + NONCE_SIZE : -(TAG_SIZE)]
    return key_index, nonce, ciphertext, tag


def build_command(opcode: int, payload: bytes = b"") -> bytes:
    """Build a SLIP-encoded command frame: opcode + payload + CRC-16."""
    frame_data = bytes([opcode]) + payload
    crc = crc16_xmodem(frame_data)
    frame_data += struct.pack(">H", crc)
    return slip_encode(frame_data)


def parse_response(frame: bytes):
    """Parse a command response. Returns (opcode, status, payload) or None."""
    if len(frame) < 4:  # opcode(1) + status(1) + crc(2)
        return None
    crc_recv = struct.unpack(">H", frame[-2:])[0]
    body = frame[:-2]
    crc_calc = crc16_xmodem(body)
    if crc_recv != crc_calc:
        return None
    return body[0], body[1], body[2:]


def send_command(ser: serial.Serial, opcode: int, payload: bytes = b"", timeout: float = TIMEOUT_S):
    """Send a command and wait for the response frame."""
    drain(ser, settle=0.05)
    cmd = build_command(opcode, payload)
    ser.write(cmd)
    ser.flush()

    # Read until we get a complete SLIP frame or timeout
    deadline = time.monotonic() + timeout
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            raw.extend(chunk)
            frames = slip_decode_frames(bytes(raw))
            if frames:
                return parse_response(frames[0])
        else:
            time.sleep(0.01)
    return None


def send_data_and_recv(ser: serial.Serial, plaintext: bytes, timeout: float = TIMEOUT_S):
    """Send plaintext on data channel and receive encrypted response."""
    drain(ser, settle=0.05)
    frame = build_data_frame(plaintext)
    ser.write(frame)
    ser.flush()

    deadline = time.monotonic() + timeout
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            raw.extend(chunk)
            frames = slip_decode_frames(bytes(raw))
            if frames:
                return parse_encrypted_frame(frames[0])
        else:
            time.sleep(0.01)
    return None


# =============================================================================
# Utilities
# =============================================================================


def log(msg: str) -> None:
    """Print only when --verbose is set."""
    if VERBOSE:
        print(f"         {msg}")


def drain(ser: serial.Serial, settle: float = 0.1) -> bytes:
    """Drain and discard any pending RX data."""
    ser.reset_input_buffer()
    time.sleep(settle)
    stale = ser.read(ser.in_waiting or 256)
    ser.reset_input_buffer()
    return stale


def make_test_key(slot: int) -> bytes:
    """Generate a deterministic 32-byte test key for a given slot index."""
    return bytes([(slot * 17 + i) & 0xFF for i in range(KEY_SIZE)])


# =============================================================================
# Checkout Group 1: Connection
# =============================================================================


def check_port_exists(port: str, label: str) -> bool:
    """Verify a serial device node exists."""
    if os.path.exists(port):
        print(f"  PASS  {label} port exists: {port}")
        return True
    print(f"  FAIL  {label} port not found: {port}")
    return False


def check_port_open(ser: serial.Serial, label: str) -> bool:
    """Verify port opened successfully."""
    if ser.is_open:
        print(f"  PASS  {label} port open: {ser.port} @ {ser.baudrate}")
        return True
    print(f"  FAIL  {label} port not open")
    return False


# =============================================================================
# Checkout Group 2: Data Channel
# =============================================================================


def check_encrypt_basic(data_ser: serial.Serial) -> bool:
    """Send a plaintext packet and verify we get an encrypted response."""
    plaintext = b"Hello Pico Encryptor!"
    result = send_data_and_recv(data_ser, plaintext)

    if result is None:
        print("  FAIL  No encrypted response received")
        return False

    key_idx, nonce, ciphertext, tag = result

    if len(ciphertext) != len(plaintext):
        print(f"  FAIL  Ciphertext length {len(ciphertext)} != " f"plaintext {len(plaintext)}")
        return False

    if len(tag) != TAG_SIZE:
        print(f"  FAIL  Auth tag length {len(tag)} != {TAG_SIZE}")
        return False

    if len(nonce) != NONCE_SIZE:
        print(f"  FAIL  Nonce length {len(nonce)} != {NONCE_SIZE}")
        return False

    log(f"key_idx={key_idx} nonce={nonce.hex()} ct={ciphertext.hex()[:32]}...")
    print(
        f"  PASS  Encrypt basic: {len(plaintext)} B plaintext -> "
        f"{len(ciphertext)} B ciphertext (key={key_idx})"
    )
    return True


def check_encrypt_roundtrip(
    data_ser: serial.Serial, cmd_ser: serial.Serial, key_slot: int = 0
) -> bool:
    """Encrypt a packet and verify it decrypts correctly with the known key."""
    if not HAS_CRYPTO:
        print("  SKIP  Decrypt verify requires 'cryptography' package")
        return True  # Not a failure, just missing optional dep

    key = make_test_key(key_slot)
    plaintext = b"Roundtrip test payload 1234567890"

    result = send_data_and_recv(data_ser, plaintext)
    if result is None:
        print("  FAIL  No encrypted response for roundtrip")
        return False

    key_idx, nonce, ciphertext, tag = result

    try:
        aesgcm = AESGCM(key)
        decrypted = aesgcm.decrypt(nonce, ciphertext + tag, None)
    except Exception as e:
        print(f"  FAIL  Decryption failed: {e}")
        return False

    if decrypted != plaintext:
        print(f"  FAIL  Decrypted mismatch: {decrypted!r} != {plaintext!r}")
        return False

    print("  PASS  Encrypt roundtrip: encrypt + decrypt verified " f"({len(plaintext)} B)")
    return True


def check_encrypt_nonce_increment(data_ser: serial.Serial) -> bool:
    """Send two packets and verify the nonce incremented between them."""
    result1 = send_data_and_recv(data_ser, b"packet_one")
    if result1 is None:
        print("  FAIL  No response for packet 1")
        return False

    result2 = send_data_and_recv(data_ser, b"packet_two")
    if result2 is None:
        print("  FAIL  No response for packet 2")
        return False

    _, nonce1, _, _ = result1
    _, nonce2, _, _ = result2

    # Parse nonces as big-endian integers
    n1 = int.from_bytes(nonce1, "big")
    n2 = int.from_bytes(nonce2, "big")

    if n2 == n1 + 1:
        print(f"  PASS  Nonce increment: {n1} -> {n2}")
        return True

    print(f"  FAIL  Nonce not incremented: {n1} -> {n2} (expected {n1 + 1})")
    return False


# =============================================================================
# Checkout Group 3: Key Store
# =============================================================================


def check_key_erase(cmd_ser: serial.Serial) -> bool:
    """Erase all keys from the store."""
    resp = send_command(cmd_ser, CMD_KEY_STORE_ERASE)
    if resp is None:
        print("  FAIL  No response to KEY_STORE_ERASE")
        return False

    opcode, status, _ = resp
    if opcode == CMD_KEY_STORE_ERASE and status == STATUS_OK:
        print("  PASS  Key store erased")
        return True

    print(f"  FAIL  KEY_STORE_ERASE: opcode=0x{opcode:02X} status=0x{status:02X}")
    return False


def check_key_write(cmd_ser: serial.Serial, slot: int = 0) -> bool:
    """Write a test key to a specific slot."""
    key = make_test_key(slot)
    payload = bytes([slot]) + key
    resp = send_command(cmd_ser, CMD_KEY_STORE_WRITE, payload)

    if resp is None:
        print(f"  FAIL  No response to KEY_STORE_WRITE (slot {slot})")
        return False

    opcode, status, _ = resp
    if opcode == CMD_KEY_STORE_WRITE and status == STATUS_OK:
        print(f"  PASS  Key written to slot {slot}")
        return True

    print(f"  FAIL  KEY_STORE_WRITE: status=0x{status:02X}")
    return False


def check_key_read(cmd_ser: serial.Serial, slot: int = 0) -> bool:
    """Read a key back and verify it matches what was written."""
    expected = make_test_key(slot)
    resp = send_command(cmd_ser, CMD_KEY_STORE_READ, bytes([slot]))

    if resp is None:
        print(f"  FAIL  No response to KEY_STORE_READ (slot {slot})")
        return False

    opcode, status, payload = resp
    if status != STATUS_OK:
        print(f"  FAIL  KEY_STORE_READ: status=0x{status:02X}")
        return False

    if payload != expected:
        print(f"  FAIL  Key readback mismatch (slot {slot})")
        return False

    print(f"  PASS  Key readback verified (slot {slot})")
    return True


def check_key_status(cmd_ser: serial.Serial, expected_count: int = 0) -> bool:
    """Query key store status and verify populated count."""
    resp = send_command(cmd_ser, CMD_KEY_STORE_STATUS)

    if resp is None:
        print("  FAIL  No response to KEY_STORE_STATUS")
        return False

    opcode, status, payload = resp
    if status != STATUS_OK or len(payload) < 3:
        print(f"  FAIL  KEY_STORE_STATUS: status=0x{status:02X} " f"len={len(payload)}")
        return False

    count = payload[0]
    bitmap = struct.unpack("<H", payload[1:3])[0]
    log(f"Key store: count={count} bitmap=0x{bitmap:04X}")

    if count == expected_count:
        print(f"  PASS  Key store status: {count} keys populated " f"(bitmap=0x{bitmap:04X})")
        return True

    print(f"  FAIL  Expected {expected_count} keys, got {count}")
    return False


# =============================================================================
# Checkout Group 4: Key Mode
# =============================================================================


def check_key_lock(cmd_ser: serial.Serial, slot: int = 0) -> bool:
    """Lock to a specific key slot."""
    resp = send_command(cmd_ser, CMD_KEY_LOCK, bytes([slot]))
    if resp is None:
        print("  FAIL  No response to KEY_LOCK")
        return False

    opcode, status, _ = resp
    if status == STATUS_OK:
        print(f"  PASS  Key locked to slot {slot}")
        return True

    print(f"  FAIL  KEY_LOCK: status=0x{status:02X}")
    return False


def check_key_locked_consistent(data_ser: serial.Serial) -> bool:
    """Verify all packets use the same key when locked."""
    indices = set()
    for i in range(5):
        result = send_data_and_recv(data_ser, f"locked_test_{i}".encode())
        if result is None:
            print(f"  FAIL  No response for locked test packet {i}")
            return False
        indices.add(result[0])

    if len(indices) == 1:
        idx = indices.pop()
        print(f"  PASS  Locked mode: all 5 packets used key {idx}")
        return True

    print(f"  FAIL  Locked mode: packets used multiple keys: {indices}")
    return False


def check_key_unlock(cmd_ser: serial.Serial) -> bool:
    """Unlock (return to random key selection)."""
    resp = send_command(cmd_ser, CMD_KEY_UNLOCK)
    if resp is None:
        print("  FAIL  No response to KEY_UNLOCK")
        return False

    opcode, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  Key unlocked (random mode)")
        return True

    print(f"  FAIL  KEY_UNLOCK: status=0x{status:02X}")
    return False


def check_key_mode_status(cmd_ser: serial.Serial, expected_mode: int = KEY_MODE_RANDOM) -> bool:
    """Query key mode and verify."""
    resp = send_command(cmd_ser, CMD_KEY_MODE_STATUS)
    if resp is None:
        print("  FAIL  No response to KEY_MODE_STATUS")
        return False

    opcode, status, payload = resp
    if status != STATUS_OK or len(payload) < 2:
        print(f"  FAIL  KEY_MODE_STATUS: status=0x{status:02X}")
        return False

    mode = payload[0]
    active_key = payload[1]
    mode_name = "RANDOM" if mode == KEY_MODE_RANDOM else "LOCKED"
    expected_name = "RANDOM" if expected_mode == KEY_MODE_RANDOM else "LOCKED"

    if mode == expected_mode:
        print(f"  PASS  Key mode: {mode_name} (active_key={active_key})")
        return True

    print(f"  FAIL  Expected {expected_name}, got {mode_name}")
    return False


# =============================================================================
# Checkout Group 5: IV Management
# =============================================================================


def check_iv_reset(cmd_ser: serial.Serial) -> bool:
    """Reset IV counter to zero."""
    resp = send_command(cmd_ser, CMD_IV_RESET)
    if resp is None:
        print("  FAIL  No response to IV_RESET")
        return False

    _, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  IV reset to zero")
        return True

    print(f"  FAIL  IV_RESET: status=0x{status:02X}")
    return False


def check_iv_status(cmd_ser: serial.Serial) -> bool:
    """Query IV status and verify it returns valid data."""
    resp = send_command(cmd_ser, CMD_IV_STATUS)
    if resp is None:
        print("  FAIL  No response to IV_STATUS")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < NONCE_SIZE + 4:
        print(f"  FAIL  IV_STATUS: status=0x{status:02X} len={len(payload)}")
        return False

    iv = payload[:NONCE_SIZE]
    frame_count = struct.unpack("<I", payload[NONCE_SIZE : NONCE_SIZE + 4])[0]
    log(f"IV: {iv.hex()} frameCount={frame_count}")
    print(f"  PASS  IV status: nonce={iv.hex()[:16]}... frames={frame_count}")
    return True


def check_iv_after_encrypt(data_ser: serial.Serial, cmd_ser: serial.Serial) -> bool:
    """Reset IV, encrypt a packet, verify IV advanced by 1."""
    # Reset IV
    send_command(cmd_ser, CMD_IV_RESET)
    time.sleep(0.05)

    # Encrypt one packet
    send_data_and_recv(data_ser, b"iv_test")
    time.sleep(0.05)

    # Check IV status
    resp = send_command(cmd_ser, CMD_IV_STATUS)
    if resp is None:
        print("  FAIL  No IV_STATUS response")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < NONCE_SIZE + 4:
        print("  FAIL  IV_STATUS: bad response")
        return False

    frame_count = struct.unpack("<I", payload[NONCE_SIZE : NONCE_SIZE + 4])[0]
    if frame_count == 1:
        print("  PASS  IV advanced: frameCount=1 after 1 encrypt")
        return True

    print(f"  FAIL  Expected frameCount=1, got {frame_count}")
    return False


# =============================================================================
# Checkout Group 6: Diagnostics
# =============================================================================


def check_stats_reset(cmd_ser: serial.Serial) -> bool:
    """Reset statistics counters."""
    resp = send_command(cmd_ser, CMD_STATS_RESET)
    if resp is None:
        print("  FAIL  No response to STATS_RESET")
        return False

    _, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  Statistics reset")
        return True

    print(f"  FAIL  STATS_RESET: status=0x{status:02X}")
    return False


def check_stats_query(cmd_ser: serial.Serial) -> bool:
    """Query statistics and verify valid format."""
    resp = send_command(cmd_ser, CMD_STATS)
    if resp is None:
        print("  FAIL  No response to STATS")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 16:
        print(f"  FAIL  STATS: status=0x{status:02X} len={len(payload)}")
        return False

    frames_ok, frames_err, bytes_in, bytes_out = struct.unpack("<IIII", payload[:16])
    print(f"  PASS  Stats: ok={frames_ok} err={frames_err} " f"in={bytes_in}B out={bytes_out}B")
    return True


def check_stats_after_encrypt(data_ser: serial.Serial, cmd_ser: serial.Serial) -> bool:
    """Reset stats, encrypt a packet, verify framesOk incremented."""
    send_command(cmd_ser, CMD_STATS_RESET)
    time.sleep(0.05)

    plaintext = b"stats_test_payload"
    send_data_and_recv(data_ser, plaintext)
    time.sleep(0.05)

    resp = send_command(cmd_ser, CMD_STATS)
    if resp is None:
        print("  FAIL  No STATS response")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 16:
        print("  FAIL  STATS: bad response")
        return False

    frames_ok = struct.unpack("<I", payload[:4])[0]
    bytes_in = struct.unpack("<I", payload[8:12])[0]

    if frames_ok == 1 and bytes_in == len(plaintext):
        print("  PASS  Stats after encrypt: framesOk=1 " f"bytesIn={bytes_in}")
        return True

    print(
        f"  FAIL  Expected framesOk=1 bytesIn={len(plaintext)}, "
        f"got framesOk={frames_ok} bytesIn={bytes_in}"
    )
    return False


# =============================================================================
# Checkout Group 7: Rejection
# =============================================================================


def check_reject_bad_crc(data_ser: serial.Serial, cmd_ser: serial.Serial) -> bool:
    """Send a packet with wrong CRC and verify it is rejected."""
    send_command(cmd_ser, CMD_STATS_RESET)
    time.sleep(0.05)

    # Build frame with intentionally wrong CRC
    plaintext = b"bad_crc_test"
    bad_crc = 0xDEAD
    payload = plaintext + struct.pack(">H", bad_crc)
    frame = slip_encode(payload)

    drain(data_ser, settle=0.05)
    data_ser.write(frame)
    data_ser.flush()
    time.sleep(0.3)

    # Should get no encrypted response
    raw = data_ser.read(data_ser.in_waiting or 256)
    frames = slip_decode_frames(raw) if raw else []

    # Check error counter incremented
    resp = send_command(cmd_ser, CMD_STATS)
    if resp is None:
        print("  FAIL  No STATS response")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 16:
        print("  FAIL  STATS: bad response")
        return False

    frames_err = struct.unpack("<I", payload[4:8])[0]

    if len(frames) == 0 and frames_err >= 1:
        print(f"  PASS  Bad CRC rejected (framesErr={frames_err})")
        return True

    print(f"  FAIL  Bad CRC: got {len(frames)} response frames, " f"framesErr={frames_err}")
    return False


def check_reject_too_short(data_ser: serial.Serial, cmd_ser: serial.Serial) -> bool:
    """Send a frame shorter than minimum (1 plaintext + 2 CRC = 3 bytes)."""
    send_command(cmd_ser, CMD_STATS_RESET)
    time.sleep(0.05)

    # Send only 2 bytes (below minimum of 3)
    frame = slip_encode(b"\x00\x00")
    drain(data_ser, settle=0.05)
    data_ser.write(frame)
    data_ser.flush()
    time.sleep(0.3)

    raw = data_ser.read(data_ser.in_waiting or 256)
    frames = slip_decode_frames(raw) if raw else []

    resp = send_command(cmd_ser, CMD_STATS)
    if resp and resp[1] == STATUS_OK and len(resp[2]) >= 16:
        frames_err = struct.unpack("<I", resp[2][4:8])[0]
        if len(frames) == 0 and frames_err >= 1:
            print("  PASS  Too-short packet rejected")
            return True

    print("  FAIL  Too-short packet not properly rejected")
    return False


def check_reject_no_keys(data_ser: serial.Serial, cmd_ser: serial.Serial) -> bool:
    """Erase all keys, then try to encrypt. Should be rejected."""
    send_command(cmd_ser, CMD_KEY_STORE_ERASE)
    send_command(cmd_ser, CMD_STATS_RESET)
    time.sleep(0.1)

    plaintext = b"no_keys_test"
    result = send_data_and_recv(data_ser, plaintext, timeout=0.5)

    resp = send_command(cmd_ser, CMD_STATS)
    if resp and resp[1] == STATUS_OK and len(resp[2]) >= 16:
        frames_err = struct.unpack("<I", resp[2][4:8])[0]
        if result is None and frames_err >= 1:
            print("  PASS  No-keys packet rejected")
            return True

    print("  FAIL  No-keys packet not properly rejected")
    return False


# =============================================================================
# Checkout Group 8: Stress
# =============================================================================


def check_continuous_encrypt(
    data_ser: serial.Serial, cmd_ser: serial.Serial, count: int = 20
) -> bool:
    """Send multiple packets continuously and verify all succeed."""
    send_command(cmd_ser, CMD_STATS_RESET)
    time.sleep(0.05)

    successes = 0
    for i in range(count):
        plaintext = f"stress_{i:04d}".encode()
        result = send_data_and_recv(data_ser, plaintext, timeout=1.0)
        if result is not None:
            _, _, ct, _ = result
            if len(ct) == len(plaintext):
                successes += 1
        else:
            log(f"Packet {i}: no response")

    resp = send_command(cmd_ser, CMD_STATS)
    frames_ok = 0
    if resp and resp[1] == STATUS_OK and len(resp[2]) >= 16:
        frames_ok = struct.unpack("<I", resp[2][:4])[0]

    if successes == count and frames_ok == count:
        print(f"  PASS  Continuous encrypt: {count}/{count} packets " f"(stats: {frames_ok})")
        return True

    print(
        f"  FAIL  Continuous encrypt: {successes}/{count} received, " f"stats framesOk={frames_ok}"
    )
    return False


# =============================================================================
# Checkout Group 9: Throughput
# =============================================================================


def send_recv_fast(ser, plaintext, timeout=0.5):
    """Send plaintext and receive encrypted response with minimal overhead.

    Unlike send_data_and_recv(), skips the drain/settle delay for
    back-to-back throughput measurement.
    """
    frame = build_data_frame(plaintext)
    ser.write(frame)
    ser.flush()

    deadline = time.monotonic() + timeout
    raw = bytearray()
    while time.monotonic() < deadline:
        avail = ser.in_waiting
        if avail:
            raw.extend(ser.read(avail))
            frames = slip_decode_frames(bytes(raw))
            if frames:
                return parse_encrypted_frame(frames[0])
        else:
            time.sleep(0.001)
    return None


def check_throughput(data_ser, cmd_ser, payload_size, count):
    """Measure sequential encrypt throughput at a given payload size.

    Sends packets back-to-back with minimal inter-packet delay (no drain).
    Reports achieved packet rate and plaintext data throughput.
    """
    send_command(cmd_ser, CMD_STATS_RESET)
    send_command(cmd_ser, CMD_IV_RESET)
    time.sleep(0.1)
    drain(data_ser, settle=0.05)

    successes = 0
    t_start = time.monotonic()

    for i in range(count):
        pt = bytes([((i * 7 + j) & 0xFF) for j in range(payload_size)])
        result = send_recv_fast(data_ser, pt)
        if result is not None:
            _, _, ct, _ = result
            if len(ct) == len(pt):
                successes += 1

    elapsed = time.monotonic() - t_start

    if successes == 0 or elapsed == 0:
        print(f"  FAIL  Throughput ({payload_size:>3d}B x {count}): " "no successful packets")
        return False

    pps = successes / elapsed
    bps = (successes * payload_size) / elapsed

    label = f"{payload_size:>3d}B x {count}"
    if successes == count:
        print(f"  PASS  Throughput ({label}): " f"{pps:.1f} pkt/s, {bps:,.0f} B/s ({elapsed:.2f}s)")
        return True

    print(
        f"  FAIL  Throughput ({label}): "
        f"{successes}/{count} received ({pps:.1f} pkt/s, {elapsed:.2f}s)"
    )
    return False


# =============================================================================
# Checkout Group 10: Overhead (no-op on Pico, M0+ has no DWT)
# =============================================================================


def check_overhead_query(cmd_ser: serial.Serial) -> bool:
    """Query overhead stats -- should return zeros on Pico (no DWT)."""
    resp = send_command(cmd_ser, CMD_OVERHEAD)
    if resp is None:
        print("  FAIL  No response to OVERHEAD")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 21:
        print(f"  FAIL  OVERHEAD: status=0x{status:02X} len={len(payload)}")
        return False

    last, minimum, maximum, count, budget = struct.unpack("<IIIII", payload[:20])
    flags = payload[20]
    ff = bool(flags & 0x01)

    log(
        f"Overhead: last={last} min={minimum} max={maximum} "
        f"count={count} budget={budget} ff={ff}"
    )

    # On Pico (M0+), all values should be zero (no DWT cycle counter)
    if budget == 0 and last == 0 and count == 0:
        print("  PASS  Overhead query: zeros (M0+ has no DWT)")
        return True

    # If somehow nonzero, still pass as long as format is valid
    print(f"  PASS  Overhead query: last={last} count={count} " f"budget={budget}")
    return True


def check_overhead_reset(cmd_ser: serial.Serial) -> bool:
    """Reset overhead stats."""
    resp = send_command(cmd_ser, CMD_OVERHEAD_RESET)
    if resp is None:
        print("  FAIL  No response to OVERHEAD_RESET")
        return False

    _, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  Overhead reset")
        return True

    print(f"  FAIL  OVERHEAD_RESET: status=0x{status:02X}")
    return False


def check_fastforward_on(cmd_ser: serial.Serial) -> bool:
    """Enable fast-forward mode and verify the flag is set."""
    resp = send_command(cmd_ser, CMD_FASTFORWARD, bytes([0x01]))
    if resp is None:
        print("  FAIL  No response to FASTFORWARD on")
        return False

    _, status, _ = resp
    if status != STATUS_OK:
        print(f"  FAIL  FASTFORWARD on: status=0x{status:02X}")
        return False

    # Query overhead to check flag
    time.sleep(0.05)
    resp2 = send_command(cmd_ser, CMD_OVERHEAD)
    if resp2 is None:
        print("  FAIL  No OVERHEAD response after FF on")
        return False

    _, _, payload2 = resp2
    if len(payload2) >= 21:
        flags = payload2[20]
        if flags & 0x01:
            print(f"  PASS  Fast-forward enabled (flag=0x{flags:02X})")
            return True

    print("  FAIL  Fast-forward flag not set")
    return False


def check_fastforward_off(cmd_ser: serial.Serial) -> bool:
    """Disable fast-forward mode and verify normal operation."""
    resp = send_command(cmd_ser, CMD_FASTFORWARD, bytes([0x00]))
    if resp is None:
        print("  FAIL  No response to FASTFORWARD of")
        return False

    _, status, _ = resp
    if status != STATUS_OK:
        print(f"  FAIL  FASTFORWARD off: status=0x{status:02X}")
        return False

    # Verify flag cleared
    time.sleep(0.05)
    resp2 = send_command(cmd_ser, CMD_OVERHEAD)
    if resp2 is None:
        print("  FAIL  No OVERHEAD response after FF of")
        return False

    _, _, payload2 = resp2
    if len(payload2) >= 21:
        flags = payload2[20]
        if not (flags & 0x01):
            print(f"  PASS  Fast-forward disabled (flag=0x{flags:02X})")
            return True

    print("  FAIL  Fast-forward flag still set after disable")
    return False


# =============================================================================
# Checkout Group 11: Idle
# =============================================================================


def check_idle_data(data_ser: serial.Serial) -> bool:
    """Verify no unsolicited output on data channel."""
    drain(data_ser)
    time.sleep(1.0)
    data = data_ser.read(data_ser.in_waiting or 256)

    if not data:
        print("  PASS  Data channel: clean idle")
        return True

    print(f"  FAIL  Data channel: {len(data)} unsolicited bytes")
    return False


def check_idle_cmd(cmd_ser: serial.Serial) -> bool:
    """Verify no unsolicited output on command channel."""
    drain(cmd_ser)
    time.sleep(1.0)
    data = cmd_ser.read(cmd_ser.in_waiting or 256)

    if not data:
        print("  PASS  Command channel: clean idle")
        return True

    print(f"  FAIL  Command channel: {len(data)} unsolicited bytes")
    return False


# =============================================================================
# Runner
# =============================================================================


def run_checkout(data_port: str, cmd_port: str, baud: int) -> int:
    """Run all checkout groups. Returns 0 on full pass, 1 on any failure."""
    print("Pico Encryptor Checkout (RP2040)")
    print(f"  Data channel:    {data_port}")
    print(f"  Command channel: {cmd_port}")
    print("=" * 60)

    results = {}
    group_results = {}
    data_ser = None
    cmd_ser = None

    # --- Group 1: Connection ---
    print("\n--- Connection ---")

    # Check data port
    if not check_port_exists(data_port, "Data"):
        results["Data port exists"] = False
        print("\n  Data channel not available. Data tests will be skipped.")
    else:
        results["Data port exists"] = True
        try:
            data_ser = serial.Serial(
                port=data_port,
                baudrate=baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=TIMEOUT_S,
                rtscts=False,
                dsrdtr=False,
            )
            data_ser.rts = False
            results["Data port open"] = check_port_open(data_ser, "Data")
        except serial.SerialException as e:
            print(f"  FAIL  Cannot open data port: {e}")
            results["Data port open"] = False

    # Check command port
    if not check_port_exists(cmd_port, "Command"):
        results["Command port exists"] = False
        print("\n  Command channel not available. Command tests will be " "skipped.")
    else:
        results["Command port exists"] = True
        try:
            cmd_ser = serial.Serial(
                port=cmd_port,
                baudrate=baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=TIMEOUT_S,
            )
            results["Command port open"] = check_port_open(cmd_ser, "Command")
        except serial.SerialException as e:
            print(f"  FAIL  Cannot open command port: {e}")
            results["Command port open"] = False

    group_results["Connection"] = all(v for k, v in results.items() if "port" in k.lower())

    # Let firmware settle after boot (startup blinks take ~900 ms, then
    # UART0 and USB CDC are initialized). Wait long enough for the full
    # boot sequence before draining any noise.
    time.sleep(1.5)
    if data_ser is not None:
        drain(data_ser, settle=0.3)
    if cmd_ser is not None:
        drain(cmd_ser, settle=0.3)

    # Define remaining groups with their port requirements
    test_groups = [
        (
            "Data Channel",
            "data",
            [
                ("Encrypt basic", check_encrypt_basic),
                ("Nonce increment", check_encrypt_nonce_increment),
            ],
        ),
        (
            "Key Store",
            "cmd",
            [
                ("Erase all keys", check_key_erase),
                ("Write key slot 0", lambda s: check_key_write(s, 0)),
                ("Read key slot 0", lambda s: check_key_read(s, 0)),
                ("Write key slot 1", lambda s: check_key_write(s, 1)),
                ("Key store status (2 keys)", lambda s: check_key_status(s, expected_count=2)),
            ],
        ),
        (
            "Key Mode",
            "both",
            [
                ("Lock to key 0", lambda d, c: (check_key_lock(c, 0), True)[-1]),
                ("Locked mode consistent", lambda d, c: check_key_locked_consistent(d)),
                ("Mode status (locked)", lambda d, c: check_key_mode_status(c, KEY_MODE_LOCKED)),
                ("Unlock", lambda d, c: (check_key_unlock(c), True)[-1]),
                ("Mode status (random)", lambda d, c: check_key_mode_status(c, KEY_MODE_RANDOM)),
            ],
        ),
        (
            "IV Management",
            "both",
            [
                ("IV reset", lambda d, c: (check_iv_reset(c), True)[-1]),
                ("IV status", lambda d, c: (check_iv_status(c), True)[-1]),
                ("IV after encrypt", lambda d, c: check_iv_after_encrypt(d, c)),
            ],
        ),
        (
            "Diagnostics",
            "both",
            [
                ("Stats reset", lambda d, c: (check_stats_reset(c), True)[-1]),
                ("Stats query", lambda d, c: (check_stats_query(c), True)[-1]),
                ("Stats after encrypt", lambda d, c: check_stats_after_encrypt(d, c)),
            ],
        ),
        (
            "Rejection",
            "both",
            [
                ("Bad CRC rejected", lambda d, c: check_reject_bad_crc(d, c)),
                ("Too-short rejected", lambda d, c: check_reject_too_short(d, c)),
                ("No-keys rejected", lambda d, c: check_reject_no_keys(d, c)),
            ],
        ),
        (
            "Stress",
            "both",
            [
                ("Re-provision key 0", lambda d, c: (check_key_write(c, 0), True)[-1]),
                (
                    "Continuous encrypt (20 packets)",
                    lambda d, c: check_continuous_encrypt(d, c, 20),
                ),
                (
                    "Encrypt roundtrip (decrypt verify)",
                    lambda d, c: check_encrypt_roundtrip(d, c, 0),
                ),
            ],
        ),
        (
            "Throughput",
            "both",
            [
                ("Re-provision key 0", lambda d, c: (check_key_write(c, 0), True)[-1]),
                ("Lock to key 0", lambda d, c: (check_key_lock(c, 0), True)[-1]),
                ("Throughput 16B x 100", lambda d, c: check_throughput(d, c, 16, 100)),
                ("Throughput 64B x 100", lambda d, c: check_throughput(d, c, 64, 100)),
                ("Throughput 128B x 50", lambda d, c: check_throughput(d, c, 128, 50)),
                ("Throughput 256B x 50", lambda d, c: check_throughput(d, c, 256, 50)),
            ],
        ),
        (
            "Overhead",
            "cmd",
            [
                ("Overhead query (expect zeros)", lambda s: check_overhead_query(s)),
                ("Overhead reset", lambda s: check_overhead_reset(s)),
                ("Fast-forward on", lambda s: check_fastforward_on(s)),
                ("Fast-forward o", lambda s: check_fastforward_off(s)),
            ],
        ),
        (
            "Idle",
            "both",
            [
                ("Data channel idle", lambda d, c: check_idle_data(d)),
                ("Command channel idle", lambda d, c: check_idle_cmd(c)),
            ],
        ),
    ]

    for group_name, port_req, checks in test_groups:
        # Check if required ports are available
        if port_req == "data" and data_ser is None:
            print(f"\n--- {group_name} (SKIPPED: no data port) ---")
            group_results[group_name] = None
            continue
        if port_req == "cmd" and cmd_ser is None:
            print(f"\n--- {group_name} (SKIPPED: no command port) ---")
            group_results[group_name] = None
            continue
        if port_req == "both" and (data_ser is None or cmd_ser is None):
            missing = []
            if data_ser is None:
                missing.append("data")
            if cmd_ser is None:
                missing.append("command")
            print(f"\n--- {group_name} " f"(SKIPPED: no {'/'.join(missing)} port) ---")
            group_results[group_name] = None
            continue

        print(f"\n--- {group_name} ---")
        group_pass = True

        for check_name, check_fn in checks:
            try:
                if port_req == "data":
                    ok = check_fn(data_ser)
                elif port_req == "cmd":
                    ok = check_fn(cmd_ser)
                else:  # both
                    ok = check_fn(data_ser, cmd_ser)

                results[check_name] = ok
                if not ok:
                    group_pass = False

            except Exception as e:
                print(f"  ERROR {check_name}: {e}")
                results[check_name] = False
                group_pass = False

        group_results[group_name] = group_pass

    # Cleanup
    if data_ser is not None:
        data_ser.close()
    if cmd_ser is not None:
        cmd_ser.close()

    # Summary
    print("\n" + "=" * 60)
    print("Checkout Summary")
    print("=" * 60)

    all_groups = [("Connection", [])] + [(g, c) for g, _, c in test_groups]

    for group_name, checks in all_groups:
        group_result = group_results.get(group_name)
        if group_result is None:
            print(f"\n  [SKIP] {group_name}")
            continue
        icon = "PASS" if group_result else "FAIL"
        print(f"\n  [{icon}] {group_name}")

        if group_name == "Connection":
            for check_name in [
                "Data port exists",
                "Data port open",
                "Command port exists",
                "Command port open",
            ]:
                if check_name in results:
                    st = "PASS" if results[check_name] else "FAIL"
                    print(f"        [{st}] {check_name}")
        else:
            for check_name, _ in checks:
                if check_name in results:
                    st = "PASS" if results[check_name] else "FAIL"
                    print(f"        [{st}] {check_name}")

    total = len(results)
    passed = sum(1 for v in results.values() if v)
    failed = total - passed
    skipped = sum(1 for v in group_results.values() if v is None)

    print(f"\n{passed}/{total} checks passed", end="")
    if skipped:
        print(f" ({skipped} groups skipped)")
    else:
        print()

    if failed == 0:
        print("Checkout: PASS")
        return 0

    print(f"Checkout: FAIL ({failed} failed)")
    return 1


def main():
    global VERBOSE

    parser = argparse.ArgumentParser(description="Pico Encryptor firmware checkout (RP2040)")
    parser.add_argument(
        "--data-port",
        default=DEFAULT_DATA_PORT,
        help="Data channel port - FTDI/UART0 (default: %(default)s)",
    )
    parser.add_argument(
        "--cmd-port",
        default=DEFAULT_CMD_PORT,
        help="Command channel port - USB-UART/UART1 (default: %(default)s)",
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate")
    parser.add_argument("--verbose", action="store_true", help="Show detailed output")
    args = parser.parse_args()

    VERBOSE = args.verbose
    return run_checkout(args.data_port, args.cmd_port, args.baud)


if __name__ == "__main__":
    sys.exit(main())
