# How to Run: ESP32 Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the Arduino Nano ESP32 (ESP32-S3, Xtensa LX7 dual-core @ 240 MHz).

---

## Prerequisites

### Hardware

- Arduino Nano ESP32 plugged in via USB-C (native USB Serial/JTAG for command channel)
- DSD TECH SH-U09C5 USB-TTL adapter (FTDI FT232RL for data channel)
- Host machine running Linux (tested on Ubuntu 22.04+)

### Wiring

The FTDI adapter connects to UART0 on the ESP32. Set the adapter voltage
jumper to **3.3V** (ESP32-S3 GPIO is 3.3V logic and NOT 5V tolerant).

```
SH-U09C5 (FTDI)          Arduino Nano ESP32
-----------------         -------------------------
Pin 3 (TXD) -----------> D0 (R14, GPIO44) [UART0 RX]
Pin 4 (RXD) <----------- D1 (R15, GPIO43) [UART0 TX]
Pin 2 (GND) ------------ GND (L14)
```

Do not connect VCC -- the ESP32 is powered by its own USB-C connection.
See [BREADBOARD_SETUP.md](BREADBOARD_SETUP.md) for the full wiring diagram.

### Docker

```bash
make docker-dev-esp32
```

### udev Rules

Both the ESP32 USB Serial/JTAG and the FTDI adapter need stable device symlinks.
Add to `/etc/udev/rules.d/99-microcontrollers.rules`:

```
# Arduino Nano ESP32 - USB Serial/JTAG (command channel)
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", SYMLINK+="esp32_0"

# FTDI FT232RL - SH-U09C5 USB-TTL adapter (data channel)
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", SYMLINK+="ftdi_0"
```

Then reload: `sudo udevadm control --reload-rules && sudo udevadm trigger`

### Python

```bash
pip install pyserial cryptography
```

---

## 1. Build

```bash
make release APP=esp32_encryptor_demo
```

Output:

- `build/esp32/firmware/esp32_encryptor_demo.elf`
- `build/esp32/firmware/esp32_encryptor_demo.bin`
- `build/release/esp32_encryptor_demo.tar.gz` (release tarball)

---

## 2. Flash

```bash
make compose-esp32-flash ESP32_FIRMWARE=esp32_encryptor_demo ESP32_PORT=/dev/esp32_0
```

Expected:

```
esptool.py ...
Writing at 0x00010000 ...
[esp32-flash] esp32_encryptor_demo flashed
```

**Important:** After flashing, reset the board:

```bash
make compose-esp32-reset ESP32_PORT=/dev/esp32_0
```

The onboard RGB LED (GPIO48) should begin pulsing green at 2 Hz after a
successful flash and reset.

---

## 3. Reset

```bash
make compose-esp32-reset ESP32_PORT=/dev/esp32_0
```

Sends a reset to restart the ESP32-S3. Use this after flashing or to recover
from a halted state.

---

## 4. Run Checkout

```bash
python3 apps/esp32_encryptor_demo/scripts/serial_checkout.py \
  --data-port /dev/ftdi_0 \
  --cmd-port /dev/esp32_0
```

### Expected Output

```
ESP32 Encryptor Checkout (dual channel)
  Data port:    /dev/ftdi_0
  Command port: /dev/esp32_0
============================================================

--- Connection ---
  PASS  Data port exists: /dev/ftdi_0
  PASS  Data port open: /dev/ftdi_0 @ 115200
  PASS  Command port exists: /dev/esp32_0
  PASS  Command port open: /dev/esp32_0 @ 115200

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
  PASS  Throughput 16B x 100
  PASS  Throughput 64B x 100
  PASS  Throughput 128B x 50
  PASS  Throughput 256B x 50

--- Overhead ---
  PASS  Overhead query
  PASS  Overhead reset
  PASS  Idle overhead (1s)
  PASS  Fast-forward on
  PASS  Fast-forward overhead
  PASS  Fast-forward off

--- Idle ---
  PASS  Data channel idle
  PASS  Command channel idle

============================================================
Checkout Summary
============================================================
40/40 checks passed
Checkout: PASS
```

All 40 checks across 11 groups should pass. The Overhead group reports real
cycle counts from the Xtensa LX7 CCOUNT register at 240 MHz.

### Checkout Options

| Flag                       | Description                                     |
| -------------------------- | ----------------------------------------------- |
| `--data-port /dev/ttyUSB0` | Override data channel port (FTDI/UART0)         |
| `--cmd-port /dev/ttyACM0`  | Override command channel port (USB Serial/JTAG) |
| `--baud 115200`            | Override baud rate                              |
| `--verbose`                | Show detailed output per test                   |

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

ser = serial.Serial('/dev/ftdi_0', 115200, timeout=5)
time.sleep(2)
ser.reset_input_buffer()

# Build data frame: SLIP( plaintext + CRC-16 )
msg = b'test'
crc = crc16(msg)
frame = slip_encode(msg + struct.pack('>H', crc))
ser.write(frame)
ser.flush()

time.sleep(1)
raw = ser.read(ser.in_waiting or 256)

# Decode SLIP response
# Parse: key_index(1) + nonce(12) + ciphertext(N) + tag(16)
# (manual decode omitted for brevity -- use serial_checkout.py)
print(f'Response: {raw.hex()}')

ser.close()
"
```

---

## Troubleshooting

| Symptom                         | Fix                                                                |
| ------------------------------- | ------------------------------------------------------------------ |
| `/dev/esp32_0` missing          | Check USB-C cable, verify udev rules for 303a:1001                 |
| `/dev/ftdi_0` missing           | Check FTDI adapter USB connection, verify udev rules for 0403:6001 |
| RGB LED not pulsing after flash | Reset the board with `make compose-esp32-reset` or replug USB-C    |
| Port opens but no response      | Wait 2 seconds after opening for USB CDC enumeration               |
| Checkout skips groups           | Both ports required; verify data and command ports are available   |
| FTDI TX/RX LEDs not flashing    | Check wiring: TXD->D0 (GPIO44), RXD->D1 (GPIO43), GND->GND         |
| `cryptography` import error     | Install with `pip install cryptography` for decrypt verification   |
| Permission denied on port       | Add user to `dialout` group: `sudo usermod -aG dialout $USER`      |
| FTDI adapter at 5V              | Set voltage jumper to 3.3V; ESP32-S3 GPIO is NOT 5V tolerant       |
| Boot loop or brownout           | Ensure USB-C cable supports data (not charge-only)                 |
| ESP-IDF log spam on boot        | Expected at WARN level; does not affect encryptor operation        |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and channel architecture.
