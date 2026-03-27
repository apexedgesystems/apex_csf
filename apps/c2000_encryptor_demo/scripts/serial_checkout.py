#!/usr/bin/env python3
"""
Firmware checkout script for LAUNCHXL-F280049C (c2000_encryptor_demo).

Validates encryption and CAN loopback functionality over single serial port.
Run after flashing to verify firmware is operational.

Usage:
    python3 apps/c2000_encryptor_demo/scripts/serial_checkout.py
    python3 apps/c2000_encryptor_demo/scripts/serial_checkout.py --port /dev/ttyACM0
    python3 apps/c2000_encryptor_demo/scripts/serial_checkout.py --verbose

Port assignment:
    SCI-A via XDS110 backchannel (/dev/c2000_0)

Checkout groups:
     1. Connection     - Serial port accessible, echo test
     2. Encryption     - AES-256-GCM encrypt + host-side verify
     3. Nonce Tracking - Nonce increments per packet
     4. CAN Loopback   - Internal CAN TX/RX with data integrity
     5. CAN Status     - CAN controller state report
     6. Rejection      - Invalid lengths correctly rejected
     7. Idle           - No spurious output

Prerequisites:
    - Firmware built: make release APP=c2000_encryptor_demo
    - Firmware flashed: make compose-c2000-flash C2000_FIRMWARE=c2000_encryptor_demo \
        C2000_CCXML=apps/c2000_encryptor_demo/LAUNCHXL_F280049C.ccxml
    - pyserial: pip install pyserial
    - cryptography: pip install cryptography

See .claude_docs/LESSONS_LEARNED.md for board-specific notes.
"""

import argparse
import sys
import time

import serial

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    HAS_CRYPTO = True
except ImportError:
    HAS_CRYPTO = False

# =============================================================================
# Configuration
# =============================================================================

DEFAULT_PORT = "/dev/c2000_0"
DEFAULT_BAUD = 115200
TIMEOUT_S = 10.0

TEST_KEY = bytes(range(0x00, 0x20))

VERBOSE = False

# =============================================================================
# Helpers
# =============================================================================

passed = 0
failed = 0
skipped = 0


def result(name, ok, detail=""):
    global passed, failed
    tag = "\033[32mPASS\033[0m" if ok else "\033[31mFAIL\033[0m"
    print(f"  [{tag}] {name}" + (f"  ({detail})" if detail else ""))
    if ok:
        passed += 1
    else:
        failed += 1


def skip(name, reason):
    global skipped
    print(f"  [\033[33mSKIP\033[0m] {name}  ({reason})")
    skipped += 1


def send_and_recv(ser, data, expected_len, timeout=TIMEOUT_S):
    """Send data, wait for expected_len bytes response."""
    ser.reset_input_buffer()
    ser.write(data)
    resp = b""
    deadline = time.time() + timeout
    while len(resp) < expected_len and time.time() < deadline:
        avail = ser.in_waiting
        if avail:
            resp += ser.read(min(avail, expected_len - len(resp)))
        else:
            time.sleep(0.01)
    return resp


# =============================================================================
# Checkout Groups
# =============================================================================


def group_connection(ser):
    print("\n1. Connection")

    # Echo test
    resp = send_and_recv(ser, b"\x00", 4)
    result("Echo test (0x00 -> OK)", resp == b"OK\r\n", repr(resp) if VERBOSE else "")


def group_encryption(ser):
    print("\n2. Encryption")

    if not HAS_CRYPTO:
        skip("AES-256-GCM verify", "cryptography not installed")
        return

    aesgcm = AESGCM(TEST_KEY)

    # Small packet (4 bytes)
    msg = b"test"
    resp = send_and_recv(ser, bytes([len(msg)]) + msg, 1 + len(msg) + 16 + 12)
    if len(resp) >= 1 + len(msg) + 16 + 12:
        ct = resp[1 : 1 + len(msg)]
        tag = resp[1 + len(msg) : 1 + len(msg) + 16]
        nonce = resp[1 + len(msg) + 16 : 1 + len(msg) + 16 + 12]
        try:
            pt = aesgcm.decrypt(nonce, ct + tag, None)
            result("Encrypt 4B", pt == msg, f"nonce={nonce.hex()}" if VERBOSE else "")
        except Exception as e:
            result("Encrypt 4B", False, str(e))
    else:
        result("Encrypt 4B", False, f"short response: {len(resp)} bytes")

    # Medium packet (8 bytes = half AES block)
    msg = b"AES test"
    resp = send_and_recv(ser, bytes([len(msg)]) + msg, 1 + len(msg) + 16 + 12)
    if len(resp) >= 1 + len(msg) + 16 + 12:
        ct = resp[1 : 1 + len(msg)]
        tag = resp[1 + len(msg) : 1 + len(msg) + 16]
        nonce = resp[1 + len(msg) + 16 : 1 + len(msg) + 16 + 12]
        try:
            pt = aesgcm.decrypt(nonce, ct + tag, None)
            result("Encrypt 8B text", pt == msg)
        except Exception as e:
            result("Encrypt 8B text", False, str(e))
    else:
        result("Encrypt 8B text", False, f"short response: {len(resp)} bytes")

    # Binary data
    msg = bytes(range(8))
    resp = send_and_recv(ser, bytes([len(msg)]) + msg, 1 + len(msg) + 16 + 12)
    if len(resp) >= 1 + len(msg) + 16 + 12:
        ct = resp[1 : 1 + len(msg)]
        tag = resp[1 + len(msg) : 1 + len(msg) + 16]
        nonce = resp[1 + len(msg) + 16 : 1 + len(msg) + 16 + 12]
        try:
            pt = aesgcm.decrypt(nonce, ct + tag, None)
            result("Encrypt 8B binary", pt == msg)
        except Exception as e:
            result("Encrypt 8B binary", False, str(e))
    else:
        result("Encrypt 8B binary", False, f"short response: {len(resp)} bytes")


def group_nonce(ser):
    print("\n3. Nonce Tracking")

    if not HAS_CRYPTO:
        skip("Nonce increment", "cryptography not installed")
        return

    nonces = []

    for i in range(3):
        msg = bytes([0x41 + i])  # 'A', 'B', 'C'
        resp = send_and_recv(ser, bytes([1]) + msg, 1 + 1 + 16 + 12)
        if len(resp) >= 30:
            nonce = resp[18:30]
            nonces.append(nonce)

    if len(nonces) == 3:
        # Nonces should be sequential (incrementing counter)
        all_different = len(set(nonces)) == 3
        result("Nonces unique", all_different, " ".join(n.hex() for n in nonces) if VERBOSE else "")
    else:
        result("Nonces unique", False, "not enough responses")


def group_can_loopback(ser):
    print("\n4. CAN Loopback")

    for i in range(3):
        resp = send_and_recv(ser, b"\x0f", 100, timeout=3.0)
        text = resp.decode("ascii", errors="replace").strip()
        ok = "PASS" in text
        result(f"CAN loopback #{i+1}", ok, text if VERBOSE or not ok else "")


def group_can_status(ser):
    print("\n5. CAN Status")

    resp = send_and_recv(ser, b"\xfe", 100, timeout=2.0)
    text = resp.decode("ascii", errors="replace").strip()
    ok = "CAN:" in text and "OK" in text
    result("CAN status report", ok, text)


def group_rejection(ser):
    print("\n6. Rejection")

    # len > 128 should return ERR:LEN
    resp = send_and_recv(ser, b"\x81", 20, timeout=2.0)
    text = resp.decode("ascii", errors="replace").strip()
    result("Reject len > 128", "ERR" in text, text if VERBOSE else "")


def group_idle(ser):
    print("\n7. Idle")

    ser.reset_input_buffer()
    time.sleep(1.0)
    avail = ser.in_waiting
    result("No spurious output", avail == 0, f"{avail} bytes" if avail > 0 else "")


# =============================================================================
# Main
# =============================================================================


def main():
    global VERBOSE

    parser = argparse.ArgumentParser(description="C2000 encryptor checkout")
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()
    VERBOSE = args.verbose

    print(f"C2000 Encryptor Checkout - {args.port} @ {args.baud}")
    print("=" * 50)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=TIMEOUT_S)
    except serial.SerialException as e:
        print(f"Cannot open {args.port}: {e}")
        sys.exit(1)

    time.sleep(2)
    ser.reset_input_buffer()

    group_connection(ser)
    group_encryption(ser)
    group_nonce(ser)
    group_can_loopback(ser)
    group_can_status(ser)
    group_rejection(ser)
    group_idle(ser)

    ser.close()

    print(f"\n{'=' * 50}")
    total = passed + failed + skipped
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped ({total} total)")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
