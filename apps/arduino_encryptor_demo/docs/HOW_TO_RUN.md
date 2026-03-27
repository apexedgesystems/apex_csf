# How to Run: Arduino Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the Arduino Uno R3 (ATmega328P).

---

## Prerequisites

### Hardware

- Arduino Uno R3 plugged in via USB-B cable
- Host machine running Linux (tested on Ubuntu 22.04+)

### Docker

```bash
make docker-dev-arduino
```

### udev Rules

The ATmega16U2 USB-serial bridge needs a stable device symlink. Add to
`/etc/udev/rules.d/99-microcontrollers.rules`:

```
# Arduino Uno R3 - ATmega16U2 USB-serial bridge
SUBSYSTEM=="tty", ATTRS{idVendor}=="2341", ATTRS{idProduct}=="0043", SYMLINK+="arduino_1"
```

Then reload: `sudo udevadm control --reload-rules && sudo udevadm trigger`

### Python

```bash
pip install pyserial cryptography
```

---

## 1. Build

```bash
make release APP=arduino_encryptor_demo
```

Output:

- `build/arduino/firmware/arduino_encryptor_demo.elf`
- `build/arduino/firmware/arduino_encryptor_demo.hex`
- `build/release/arduino_encryptor_demo.tar.gz` (release tarball)

---

## 2. Flash

```bash
make compose-arduino-flash \
  ARDUINO_FIRMWARE=arduino_encryptor_demo \
  ARDUINO_PORT=/dev/arduino_1
```

Expected:

```
avrdude done.  Thank you.
[arduino-flash] arduino_encryptor_demo flashed
```

The on-board LED (D13) should blink three times on startup, then settle into a
steady 2 Hz heartbeat.

---

## 3. Reset

```bash
make compose-arduino-reset ARDUINO_PORT=/dev/arduino_1
```

Toggles DTR to reset the ATmega328P via the auto-reset circuit. The board
re-enters the Optiboot bootloader briefly, then jumps to application code.

---

## 4. Run Checkout

```bash
python3 apps/arduino_encryptor_demo/scripts/serial_checkout.py
```

### Expected Output

```
Arduino Encryptor Checkout (single UART)
  Port: /dev/arduino_1
============================================================

--- Connection ---
  PASS  Port exists: /dev/arduino_1
  PASS  Port open: /dev/arduino_1 @ 115200

--- Data Channel ---
  PASS  Encrypt basic: 16 B plaintext -> 16 B ciphertext (key=0)
  PASS  Nonce increment: 1 -> 2

--- Key Store ---
  PASS  Erase all keys
  PASS  Write key slot 0
  PASS  Read key slot 0
  PASS  Write key slot 1
  PASS  Key store status (2 keys)

--- Key Mode ---
  PASS  Lock to key 0
  PASS  Locked mode consistent
  PASS  Mode status (locked)
  PASS  Unlock
  PASS  Mode status (random)

--- IV Management ---
  PASS  IV reset
  PASS  IV status
  PASS  IV after encrypt

--- Diagnostics ---
  PASS  Stats reset
  PASS  Stats query
  PASS  Stats after encrypt

--- Rejection ---
  PASS  Bad CRC rejected
  PASS  Too-short rejected
  PASS  No-keys rejected

--- Stress ---
  PASS  Re-provision key 0
  PASS  Continuous encrypt (20 packets)
  PASS  Encrypt roundtrip (decrypt verify)

--- Throughput ---
  PASS  Re-provision key 0
  PASS  Lock to key 0
  PASS  Throughput 16B x 50
  PASS  Throughput 32B x 50
  PASS  Throughput 48B x 25

--- Overhead ---
  PASS  Overhead query
  PASS  Overhead reset
  PASS  Idle overhead (1s)
  PASS  Fast-forward on
  PASS  Fast-forward overhead
  PASS  Fast-forward off

--- Idle ---
  PASS  UART idle

============================================================
Checkout Summary
============================================================
36/36 checks passed
Checkout: PASS
```

All 36 checks should pass.

### Checkout Options

| Flag                  | Description                   |
| --------------------- | ----------------------------- |
| `--port /dev/ttyACM1` | Override serial port          |
| `--baud 115200`       | Override baud rate            |
| `--verbose`           | Show detailed output per test |

---

## 5. Manual Testing

### Encrypt and Verify

```bash
python3 -c "
import serial, time, struct
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

# SLIP helpers
END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD

def slip_encode(data):
    out = bytearray([END])
    for b in data:
        if b == END: out.extend([ESC, ESC_END])
        elif b == ESC: out.extend([ESC, ESC_ESC])
        else: out.append(b)
    out.append(END)
    return bytes(out)

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc

ser = serial.Serial('/dev/arduino_1', 115200, timeout=5)
ser.rts = False
time.sleep(2)
ser.reset_input_buffer()

# Build data frame: SLIP( 0x00 + plaintext + CRC-16 )
msg = b'test'
crc = crc16(msg)
frame = slip_encode(bytes([0x00]) + msg + struct.pack('>H', crc))
ser.write(frame)
ser.flush()

time.sleep(1)
raw = ser.read(ser.in_waiting or 256)

# Decode SLIP response, strip channel byte
# Parse: key_index(1) + nonce(12) + ciphertext(N) + tag(16)
# (manual decode omitted for brevity -- use serial_checkout.py)
print(f'Response: {raw.hex()}')

ser.close()
"
```

---

## Troubleshooting

| Symptom                                  | Fix                                                                       |
| ---------------------------------------- | ------------------------------------------------------------------------- |
| `/dev/arduino_1` missing                 | Check USB cable, verify udev rules installed                              |
| Flash fails: `avrdude: stk500_getsync()` | Board not responding; press reset button and retry                        |
| LED not blinking after flash             | Verify avrdude reports success; try power-cycling the board               |
| Port opens but no response               | DTR toggle resets board; wait 2 seconds after open for bootloader timeout |
| Checkout fails on first test             | Board may still be in bootloader; increase settle time or press reset     |
| `cryptography` import error              | Install with `pip install cryptography` for decrypt verification          |
| Permission denied on port                | Add user to `dialout` group: `sudo usermod -aG dialout $USER`             |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and channel multiplexing scheme.
