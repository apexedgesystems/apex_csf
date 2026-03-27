#!/usr/bin/env python3
"""
Firmware checkout script for Arduino Uno R3 (arduino_encryptor_demo).

Validates encryption firmware functionality over a single serial port.
Run after flashing to verify features as they are incrementally added.

Usage:
    python3 apps/arduino_encryptor_demo/scripts/serial_checkout.py
    python3 apps/arduino_encryptor_demo/scripts/serial_checkout.py --port /dev/ttyACM1
    python3 apps/arduino_encryptor_demo/scripts/serial_checkout.py --verbose

Port assignment:
    Single UART: USART0 via ATmega16U2 USB-serial bridge (/dev/arduino_1)

    Both data and command channels share one UART. Frames are multiplexed
    with a 1-byte channel prefix inside each SLIP frame:
        0x00 = data channel (encrypt pipeline)
        0x01 = command channel (key management, diagnostics)

Checkout groups:
     1. Connection      - Serial port accessible
     2. Data Channel    - SLIP framing, CRC validation, encrypt pipeline
     3. Key Store       - EEPROM-based key provisioning and management
     4. Key Mode        - Lock/unlock key selection
     5. IV Management   - Nonce tracking and reset
     6. Diagnostics     - Statistics and counters
     7. Rejection       - Invalid packets correctly discarded
     8. Stress          - Continuous encryption reliability
     9. Throughput      - Sustained encryption data rates
    10. Overhead        - Timer0 tick measurement and fast-forward
    11. Idle            - No spurious output

Prerequisites:
    - Firmware flashed and board reset
    - pyserial installed (pip install pyserial)
    - cryptography installed (pip install cryptography) -- for decrypt verify
    - /dev/arduino_1 or /dev/ttyACM1
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

DEFAULT_PORT = "/dev/arduino_1"
DEFAULT_BAUD = 115200
TIMEOUT_S = 2.0

VERBOSE = False

# Packet limits (must match EncryptorConfig.hpp)
MAX_PLAINTEXT_SIZE = 48
KEY_SLOT_COUNT = 4
KEY_SIZE = 32
NONCE_SIZE = 12
TAG_SIZE = 16

# Channel prefix bytes (must match EncryptorConfig.hpp)
CHANNEL_DATA = 0x00
CHANNEL_CMD = 0x01

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
    """Build a SLIP-encoded data channel input frame.

    Wire format: SLIP( channel_data + plaintext + CRC-16 )
    CRC covers plaintext only (channel byte is transport framing).
    """
    crc = crc16_xmodem(plaintext)
    payload = plaintext + struct.pack(">H", crc)
    return slip_encode(bytes([CHANNEL_DATA]) + payload)


def parse_encrypted_frame(data: bytes):
    """Parse encrypted output (channel byte already stripped).

    Returns (key_index, nonce, ciphertext, tag) or None.
    """
    if len(data) < 1 + NONCE_SIZE + TAG_SIZE:
        return None
    key_index = data[0]
    nonce = data[1 : 1 + NONCE_SIZE]
    tag = data[-(TAG_SIZE):]
    ciphertext = data[1 + NONCE_SIZE : -(TAG_SIZE)]
    return key_index, nonce, ciphertext, tag


def build_command(opcode: int, payload: bytes = b"") -> bytes:
    """Build a SLIP-encoded command frame.

    Wire format: SLIP( channel_cmd + opcode + payload + CRC-16 )
    CRC covers opcode + payload (channel byte is transport framing).
    """
    frame_data = bytes([opcode]) + payload
    crc = crc16_xmodem(frame_data)
    frame_data += struct.pack(">H", crc)
    return slip_encode(bytes([CHANNEL_CMD]) + frame_data)


def parse_response(data: bytes):
    """Parse command response (channel byte already stripped).

    Returns (opcode, status, payload) or None.
    """
    if len(data) < 4:  # opcode(1) + status(1) + crc(2)
        return None
    crc_recv = struct.unpack(">H", data[-2:])[0]
    body = data[:-2]
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

    # Read until we get a SLIP frame with CHANNEL_CMD prefix or timeout
    deadline = time.monotonic() + timeout
    raw = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(max(1, ser.in_waiting))
        if chunk:
            raw.extend(chunk)
            frames = slip_decode_frames(bytes(raw))
            for f in frames:
                if len(f) >= 2 and f[0] == CHANNEL_CMD:
                    return parse_response(f[1:])
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
            for f in frames:
                if len(f) >= 2 and f[0] == CHANNEL_DATA:
                    return parse_encrypted_frame(f[1:])
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


def check_port_exists(port: str) -> bool:
    """Verify serial device node exists."""
    if os.path.exists(port):
        print(f"  PASS  Port exists: {port}")
        return True
    print(f"  FAIL  Port not found: {port}")
    return False


def check_port_open(ser: serial.Serial) -> bool:
    """Verify port opened successfully."""
    if ser.is_open:
        print(f"  PASS  Port open: {ser.port} @ {ser.baudrate}")
        return True
    print("  FAIL  Port not open")
    return False


# =============================================================================
# Checkout Group 2: Data Channel
# =============================================================================


def check_encrypt_basic(ser: serial.Serial) -> bool:
    """Send a plaintext packet and verify we get an encrypted response."""
    plaintext = b"Hello Encryptor!"
    result = send_data_and_recv(ser, plaintext)

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


def check_encrypt_roundtrip(ser: serial.Serial, key_slot: int = 0) -> bool:
    """Encrypt a packet and verify it decrypts correctly with the known key."""
    if not HAS_CRYPTO:
        print("  SKIP  Decrypt verify requires 'cryptography' package")
        return True

    key = make_test_key(key_slot)
    plaintext = b"Roundtrip test payload 1234567890"

    result = send_data_and_recv(ser, plaintext)
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


def check_encrypt_nonce_increment(ser: serial.Serial) -> bool:
    """Send two packets and verify the nonce incremented between them."""
    result1 = send_data_and_recv(ser, b"packet_one")
    if result1 is None:
        print("  FAIL  No response for packet 1")
        return False

    result2 = send_data_and_recv(ser, b"packet_two")
    if result2 is None:
        print("  FAIL  No response for packet 2")
        return False

    _, nonce1, _, _ = result1
    _, nonce2, _, _ = result2

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


def check_key_erase(ser: serial.Serial) -> bool:
    """Erase all keys from the store."""
    resp = send_command(ser, CMD_KEY_STORE_ERASE)
    if resp is None:
        print("  FAIL  No response to KEY_STORE_ERASE")
        return False

    opcode, status, _ = resp
    if opcode == CMD_KEY_STORE_ERASE and status == STATUS_OK:
        print("  PASS  Key store erased")
        return True

    print(f"  FAIL  KEY_STORE_ERASE: opcode=0x{opcode:02X} status=0x{status:02X}")
    return False


def check_key_write(ser: serial.Serial, slot: int = 0) -> bool:
    """Write a test key to a specific slot."""
    key = make_test_key(slot)
    payload = bytes([slot]) + key
    resp = send_command(ser, CMD_KEY_STORE_WRITE, payload)

    if resp is None:
        print(f"  FAIL  No response to KEY_STORE_WRITE (slot {slot})")
        return False

    opcode, status, _ = resp
    if opcode == CMD_KEY_STORE_WRITE and status == STATUS_OK:
        print(f"  PASS  Key written to slot {slot}")
        return True

    print(f"  FAIL  KEY_STORE_WRITE: status=0x{status:02X}")
    return False


def check_key_read(ser: serial.Serial, slot: int = 0) -> bool:
    """Read a key back and verify it matches what was written."""
    expected = make_test_key(slot)
    resp = send_command(ser, CMD_KEY_STORE_READ, bytes([slot]))

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


def check_key_status(ser: serial.Serial, expected_count: int = 0) -> bool:
    """Query key store status and verify populated count."""
    resp = send_command(ser, CMD_KEY_STORE_STATUS)

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


def check_key_lock(ser: serial.Serial, slot: int = 0) -> bool:
    """Lock to a specific key slot."""
    resp = send_command(ser, CMD_KEY_LOCK, bytes([slot]))
    if resp is None:
        print("  FAIL  No response to KEY_LOCK")
        return False

    opcode, status, _ = resp
    if status == STATUS_OK:
        print(f"  PASS  Key locked to slot {slot}")
        return True

    print(f"  FAIL  KEY_LOCK: status=0x{status:02X}")
    return False


def check_key_locked_consistent(ser: serial.Serial) -> bool:
    """Verify all packets use the same key when locked."""
    indices = set()
    for i in range(5):
        result = send_data_and_recv(ser, f"locked_test_{i}".encode())
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


def check_key_unlock(ser: serial.Serial) -> bool:
    """Unlock (return to random key selection)."""
    resp = send_command(ser, CMD_KEY_UNLOCK)
    if resp is None:
        print("  FAIL  No response to KEY_UNLOCK")
        return False

    opcode, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  Key unlocked (random mode)")
        return True

    print(f"  FAIL  KEY_UNLOCK: status=0x{status:02X}")
    return False


def check_key_mode_status(ser: serial.Serial, expected_mode: int = KEY_MODE_RANDOM) -> bool:
    """Query key mode and verify."""
    resp = send_command(ser, CMD_KEY_MODE_STATUS)
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


def check_iv_reset(ser: serial.Serial) -> bool:
    """Reset IV counter to zero."""
    resp = send_command(ser, CMD_IV_RESET)
    if resp is None:
        print("  FAIL  No response to IV_RESET")
        return False

    _, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  IV reset to zero")
        return True

    print(f"  FAIL  IV_RESET: status=0x{status:02X}")
    return False


def check_iv_status(ser: serial.Serial) -> bool:
    """Query IV status and verify it returns valid data."""
    resp = send_command(ser, CMD_IV_STATUS)
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


def check_iv_after_encrypt(ser: serial.Serial) -> bool:
    """Reset IV, encrypt a packet, verify IV advanced by 1."""
    send_command(ser, CMD_IV_RESET)
    time.sleep(0.05)

    send_data_and_recv(ser, b"iv_test")
    time.sleep(0.05)

    resp = send_command(ser, CMD_IV_STATUS)
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


def check_stats_reset(ser: serial.Serial) -> bool:
    """Reset statistics counters."""
    resp = send_command(ser, CMD_STATS_RESET)
    if resp is None:
        print("  FAIL  No response to STATS_RESET")
        return False

    _, status, _ = resp
    if status == STATUS_OK:
        print("  PASS  Statistics reset")
        return True

    print(f"  FAIL  STATS_RESET: status=0x{status:02X}")
    return False


def check_stats_query(ser: serial.Serial) -> bool:
    """Query statistics and verify valid format."""
    resp = send_command(ser, CMD_STATS)
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


def check_stats_after_encrypt(ser: serial.Serial) -> bool:
    """Reset stats, encrypt a packet, verify framesOk incremented."""
    send_command(ser, CMD_STATS_RESET)
    time.sleep(0.05)

    plaintext = b"stats_test_payload"
    send_data_and_recv(ser, plaintext)
    time.sleep(0.05)

    resp = send_command(ser, CMD_STATS)
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


def check_reject_bad_crc(ser: serial.Serial) -> bool:
    """Send a packet with wrong CRC and verify it is rejected."""
    send_command(ser, CMD_STATS_RESET)
    time.sleep(0.05)

    # Build data frame with intentionally wrong CRC
    plaintext = b"bad_crc_test"
    bad_crc = 0xDEAD
    payload = plaintext + struct.pack(">H", bad_crc)
    frame = slip_encode(bytes([CHANNEL_DATA]) + payload)

    drain(ser, settle=0.05)
    ser.write(frame)
    ser.flush()
    time.sleep(0.3)

    # Should get no data channel response
    raw = ser.read(ser.in_waiting or 256)
    data_frames = []
    if raw:
        for f in slip_decode_frames(raw):
            if len(f) >= 2 and f[0] == CHANNEL_DATA:
                data_frames.append(f)

    # Check error counter incremented
    resp = send_command(ser, CMD_STATS)
    if resp is None:
        print("  FAIL  No STATS response")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 16:
        print("  FAIL  STATS: bad response")
        return False

    frames_err = struct.unpack("<I", payload[4:8])[0]

    if len(data_frames) == 0 and frames_err >= 1:
        print(f"  PASS  Bad CRC rejected (framesErr={frames_err})")
        return True

    print(
        f"  FAIL  Bad CRC: got {len(data_frames)} data response frames, " f"framesErr={frames_err}"
    )
    return False


def check_reject_too_short(ser: serial.Serial) -> bool:
    """Send a frame shorter than minimum (1 plaintext + 2 CRC = 3 bytes)."""
    send_command(ser, CMD_STATS_RESET)
    time.sleep(0.05)

    # Send only 2 bytes payload (below minimum of 3)
    frame = slip_encode(bytes([CHANNEL_DATA]) + b"\x00\x00")
    drain(ser, settle=0.05)
    ser.write(frame)
    ser.flush()
    time.sleep(0.3)

    raw = ser.read(ser.in_waiting or 256)
    data_frames = []
    if raw:
        for f in slip_decode_frames(raw):
            if len(f) >= 2 and f[0] == CHANNEL_DATA:
                data_frames.append(f)

    resp = send_command(ser, CMD_STATS)
    if resp and resp[1] == STATUS_OK and len(resp[2]) >= 16:
        frames_err = struct.unpack("<I", resp[2][4:8])[0]
        if len(data_frames) == 0 and frames_err >= 1:
            print("  PASS  Too-short packet rejected")
            return True

    print("  FAIL  Too-short packet not properly rejected")
    return False


def check_reject_no_keys(ser: serial.Serial) -> bool:
    """Erase all keys, then try to encrypt. Should be rejected."""
    send_command(ser, CMD_KEY_STORE_ERASE)
    send_command(ser, CMD_STATS_RESET)
    time.sleep(0.1)

    plaintext = b"no_keys_test"
    result = send_data_and_recv(ser, plaintext, timeout=0.5)

    resp = send_command(ser, CMD_STATS)
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


def check_continuous_encrypt(ser: serial.Serial, count: int = 20) -> bool:
    """Send multiple packets continuously and verify all succeed."""
    send_command(ser, CMD_STATS_RESET)
    time.sleep(0.05)

    successes = 0
    for i in range(count):
        plaintext = f"stress_{i:04d}".encode()
        result = send_data_and_recv(ser, plaintext, timeout=1.0)
        if result is not None:
            _, _, ct, _ = result
            if len(ct) == len(plaintext):
                successes += 1
        else:
            log(f"Packet {i}: no response")

    resp = send_command(ser, CMD_STATS)
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
            for f in frames:
                if len(f) >= 2 and f[0] == CHANNEL_DATA:
                    return parse_encrypted_frame(f[1:])
        else:
            time.sleep(0.001)
    return None


def check_throughput(ser, payload_size, count):
    """Measure sequential encrypt throughput at a given payload size.

    Sends packets back-to-back with minimal inter-packet delay.
    Reports achieved packet rate and plaintext data throughput.
    """
    send_command(ser, CMD_STATS_RESET)
    send_command(ser, CMD_IV_RESET)
    time.sleep(0.1)
    drain(ser, settle=0.05)

    successes = 0
    t_start = time.monotonic()

    for i in range(count):
        pt = bytes([((i * 7 + j) & 0xFF) for j in range(payload_size)])
        result = send_recv_fast(ser, pt)
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
# Checkout Group 10: Overhead
# =============================================================================


def check_overhead_query(ser: serial.Serial) -> bool:
    """Query overhead stats and verify valid response with samples."""
    resp = send_command(ser, CMD_OVERHEAD)
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

    if count > 0 and budget > 0:
        util = (last / budget) * 100.0
        print(
            f"  PASS  Overhead query: {count} samples, "
            f"last={last} T0 ticks ({util:.1f}% of {budget})"
        )
        return True

    print(f"  FAIL  Overhead: count={count} budget={budget}")
    return False


def check_overhead_reset(ser: serial.Serial) -> bool:
    """Reset overhead stats and verify counters zeroed."""
    resp = send_command(ser, CMD_OVERHEAD_RESET)
    if resp is None:
        print("  FAIL  No response to OVERHEAD_RESET")
        return False

    _, status, _ = resp
    if status != STATUS_OK:
        print(f"  FAIL  OVERHEAD_RESET: status=0x{status:02X}")
        return False

    time.sleep(0.05)
    resp2 = send_command(ser, CMD_OVERHEAD)
    if resp2 is None:
        print("  FAIL  No response to OVERHEAD after reset")
        return False

    _, status2, payload2 = resp2
    if status2 != STATUS_OK or len(payload2) < 21:
        print("  FAIL  OVERHEAD after reset: bad response")
        return False

    count = struct.unpack("<I", payload2[12:16])[0]
    if count < 500:
        print(f"  PASS  Overhead reset (count={count} after reset)")
        return True

    print(f"  FAIL  Expected low count after reset, got {count}")
    return False


def check_overhead_idle(ser: serial.Serial) -> bool:
    """Let firmware run for 1 second and verify max < budget."""
    send_command(ser, CMD_OVERHEAD_RESET)
    time.sleep(1.0)

    resp = send_command(ser, CMD_OVERHEAD)
    if resp is None:
        print("  FAIL  No OVERHEAD response")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 21:
        print("  FAIL  OVERHEAD: bad response")
        return False

    last, minimum, maximum, count, budget = struct.unpack("<IIIII", payload[:20])

    if budget == 0:
        print("  FAIL  Budget is zero")
        return False

    util = (maximum / budget) * 100.0
    print(
        f"  PASS  Idle overhead: max={maximum} T0 ticks "
        f"({util:.1f}% of {budget}), {count} samples"
    )
    return True


def check_fastforward_on(ser: serial.Serial) -> bool:
    """Enable fast-forward mode and verify the flag is set."""
    resp = send_command(ser, CMD_FASTFORWARD, bytes([0x01]))
    if resp is None:
        print("  FAIL  No response to FASTFORWARD on")
        return False

    _, status, _ = resp
    if status != STATUS_OK:
        print(f"  FAIL  FASTFORWARD on: status=0x{status:02X}")
        return False

    time.sleep(0.05)
    resp2 = send_command(ser, CMD_OVERHEAD)
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


def check_fastforward_overhead(ser: serial.Serial) -> bool:
    """Query overhead while fast-forward is active; report max rate."""
    send_command(ser, CMD_OVERHEAD_RESET)
    time.sleep(1.0)

    resp = send_command(ser, CMD_OVERHEAD)
    if resp is None:
        print("  FAIL  No OVERHEAD response in fast-forward")
        return False

    _, status, payload = resp
    if status != STATUS_OK or len(payload) < 21:
        print("  FAIL  OVERHEAD: bad response")
        return False

    last, minimum, maximum, count, _budget = struct.unpack("<IIIII", payload[:20])

    if count == 0 or maximum == 0:
        print(f"  FAIL  No samples in fast-forward (count={count})")
        return False

    # Timer0 runs at F_CPU/64 = 250 kHz. Max rate = 250000 / last_ticks.
    if last > 0:
        max_rate = 250_000 / last
    else:
        max_rate = 0

    print(
        f"  PASS  Fast-forward overhead: last={last} min={minimum} "
        f"max={maximum} ({count} samples)"
    )
    print(f"         Max achievable rate: ~{max_rate:.0f} Hz " f"(last={last} T0 ticks @ 250 kHz)")
    return True


def check_fastforward_off(ser: serial.Serial) -> bool:
    """Disable fast-forward mode and verify normal operation."""
    resp = send_command(ser, CMD_FASTFORWARD, bytes([0x00]))
    if resp is None:
        print("  FAIL  No response to FASTFORWARD off")
        return False

    _, status, _ = resp
    if status != STATUS_OK:
        print(f"  FAIL  FASTFORWARD off: status=0x{status:02X}")
        return False

    time.sleep(0.05)
    resp2 = send_command(ser, CMD_OVERHEAD)
    if resp2 is None:
        print("  FAIL  No OVERHEAD response after FF off")
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


def check_idle(ser: serial.Serial) -> bool:
    """Verify no unsolicited output on the UART."""
    drain(ser)
    time.sleep(1.0)
    data = ser.read(ser.in_waiting or 256)

    if not data:
        print("  PASS  UART: clean idle")
        return True

    print(f"  FAIL  UART: {len(data)} unsolicited bytes")
    return False


# =============================================================================
# Runner
# =============================================================================


def run_checkout(port: str, baud: int) -> int:
    """Run all checkout groups. Returns 0 on full pass, 1 on any failure."""
    print("Arduino Encryptor Checkout (single UART)")
    print(f"  Port: {port}")
    print("=" * 60)

    results = {}
    group_results = {}
    ser = None

    # --- Group 1: Connection ---
    print("\n--- Connection ---")

    if not check_port_exists(port):
        results["Port exists"] = False
        print("\n  Port not available. All tests will be skipped.")
        group_results["Connection"] = False

        print("\n" + "=" * 60)
        print("Checkout Summary")
        print("=" * 60)
        print("\n  [FAIL] Connection")
        print("        [FAIL] Port exists")
        print("\n0/1 checks passed")
        print("Checkout: FAIL (1 failed)")
        return 1

    results["Port exists"] = True

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=TIMEOUT_S,
            rtscts=False,
            dsrdtr=False,
        )
        ser.rts = False
        results["Port open"] = check_port_open(ser)
    except serial.SerialException as e:
        print(f"  FAIL  Cannot open port: {e}")
        results["Port open"] = False

    group_results["Connection"] = all(v for k, v in results.items() if "port" in k.lower())

    if ser is None or not ser.is_open:
        print("\n  Port not open. Cannot continue.")
        return 1

    # Let firmware settle after port open (DTR toggle may reset board)
    time.sleep(2.0)
    drain(ser, settle=0.5)

    # Define remaining test groups
    test_groups = [
        (
            "Data Channel",
            [
                ("Encrypt basic", check_encrypt_basic),
                ("Nonce increment", check_encrypt_nonce_increment),
            ],
        ),
        (
            "Key Store",
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
            [
                ("Lock to key 0", lambda s: check_key_lock(s, 0)),
                ("Locked mode consistent", check_key_locked_consistent),
                ("Mode status (locked)", lambda s: check_key_mode_status(s, KEY_MODE_LOCKED)),
                ("Unlock", check_key_unlock),
                ("Mode status (random)", lambda s: check_key_mode_status(s, KEY_MODE_RANDOM)),
            ],
        ),
        (
            "IV Management",
            [
                ("IV reset", check_iv_reset),
                ("IV status", check_iv_status),
                ("IV after encrypt", check_iv_after_encrypt),
            ],
        ),
        (
            "Diagnostics",
            [
                ("Stats reset", check_stats_reset),
                ("Stats query", check_stats_query),
                ("Stats after encrypt", check_stats_after_encrypt),
            ],
        ),
        (
            "Rejection",
            [
                ("Bad CRC rejected", check_reject_bad_crc),
                ("Too-short rejected", check_reject_too_short),
                ("No-keys rejected", check_reject_no_keys),
            ],
        ),
        (
            "Stress",
            [
                ("Re-provision key 0", lambda s: check_key_write(s, 0)),
                ("Continuous encrypt (20 packets)", lambda s: check_continuous_encrypt(s, 20)),
                ("Encrypt roundtrip (decrypt verify)", lambda s: check_encrypt_roundtrip(s, 0)),
            ],
        ),
        (
            "Throughput",
            [
                ("Re-provision key 0", lambda s: check_key_write(s, 0)),
                ("Lock to key 0", lambda s: check_key_lock(s, 0)),
                ("Throughput 16B x 50", lambda s: check_throughput(s, 16, 50)),
                ("Throughput 32B x 50", lambda s: check_throughput(s, 32, 50)),
                ("Throughput 48B x 25", lambda s: check_throughput(s, 48, 25)),
            ],
        ),
        (
            "Overhead",
            [
                ("Overhead query", check_overhead_query),
                ("Overhead reset", check_overhead_reset),
                ("Idle overhead (1s)", check_overhead_idle),
                ("Fast-forward on", check_fastforward_on),
                ("Fast-forward overhead", check_fastforward_overhead),
                ("Fast-forward of", check_fastforward_off),
            ],
        ),
        (
            "Idle",
            [
                ("UART idle", check_idle),
            ],
        ),
    ]

    for group_name, checks in test_groups:
        print(f"\n--- {group_name} ---")
        group_pass = True

        for check_name, check_fn in checks:
            try:
                ok = check_fn(ser)
                results[check_name] = ok
                if not ok:
                    group_pass = False
            except Exception as e:
                print(f"  ERROR {check_name}: {e}")
                results[check_name] = False
                group_pass = False

        group_results[group_name] = group_pass

    # Cleanup
    ser.close()

    # Summary
    print("\n" + "=" * 60)
    print("Checkout Summary")
    print("=" * 60)

    all_groups = [("Connection", [])] + [(g, c) for g, c in test_groups]

    for group_name, checks in all_groups:
        group_result = group_results.get(group_name)
        icon = "PASS" if group_result else "FAIL"
        print(f"\n  [{icon}] {group_name}")

        if group_name == "Connection":
            for check_name in ["Port exists", "Port open"]:
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

    print(f"\n{passed}/{total} checks passed")

    if failed == 0:
        print("Checkout: PASS")
        return 0

    print(f"Checkout: FAIL ({failed} failed)")
    return 1


def main():
    global VERBOSE

    parser = argparse.ArgumentParser(
        description="Arduino Encryptor firmware checkout (single UART)"
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port (default: %(default)s)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Baud rate")
    parser.add_argument("--verbose", action="store_true", help="Show detailed output")
    args = parser.parse_args()

    VERBOSE = args.verbose
    return run_checkout(args.port, args.baud)


if __name__ == "__main__":
    sys.exit(main())
