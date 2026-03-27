# How to Run: Pico Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the Raspberry Pi Pico (RP2040, dual Cortex-M0+ @ 133 MHz).

---

## Prerequisites

### Hardware

- Raspberry Pi Pico plugged in via USB Micro-B (native USB CDC for command channel)
- DSD TECH SH-U09C5 USB-TTL adapter (FTDI FT232RL for data channel)
- Host machine running Linux (tested on Ubuntu 22.04+)

### Wiring

The FTDI adapter connects to UART0 on the Pico. Set the adapter voltage
jumper to **3.3V** (RP2040 GPIO is 3.3V logic).

```
SH-U09C5 (FTDI)          Raspberry Pi Pico
-----------------         -------------------------
Pin 3 (TXD) -----------> GP1 (Pin 2)  [UART0 RX]
Pin 4 (RXD) <----------- GP0 (Pin 1)  [UART0 TX]
Pin 2 (GND) ------------ GND (Pin 3)
```

Do not connect VCC -- the Pico is powered by its own USB connection.
See [BREADBOARD_SETUP.md](BREADBOARD_SETUP.md) for the full wiring diagram.

### Docker

```bash
make docker-dev-pico
```

### udev Rules

Both the Pico USB CDC and the FTDI adapter need stable device symlinks.
Add to `/etc/udev/rules.d/99-microcontrollers.rules`:

```
# Raspberry Pi Pico - USB CDC (command channel)
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", SYMLINK+="pico_0"

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
make release APP=pico_encryptor_demo
```

Output:

- `build/pico/firmware/pico_encryptor_demo.elf`
- `build/pico/firmware/pico_encryptor_demo.bin`
- `build/pico/firmware/pico_encryptor_demo.uf2`
- `build/release/pico_encryptor_demo.tar.gz` (release tarball)

---

## 2. Flash

```bash
make compose-pico-flash PICO_FIRMWARE=pico_encryptor_demo
```

Expected:

```
picotool load pico_encryptor_demo.uf2
...
Loading into flash
[pico-flash] pico_encryptor_demo flashed
```

**Alternative (UF2 drag-and-drop):** Hold the BOOTSEL button while plugging
in the Pico. It mounts as a USB mass storage device. Copy the `.uf2` file
to the mounted drive. The Pico reboots automatically after the copy completes.

**Important:** After flashing via picotool, reset the board:

```bash
make compose-pico-reset
```

The onboard LED (GP25) should begin blinking at 2 Hz after a successful
flash and reset.

---

## 3. Reset

```bash
make compose-pico-reset
```

Sends a reset via picotool to restart the RP2040. Use this after flashing
or to recover from a halted state.

---

## 4. Run Checkout

```bash
python3 apps/pico_encryptor_demo/scripts/serial_checkout.py \
  --data-port /dev/ftdi_0 \
  --cmd-port /dev/pico_0
```

### Expected Output

```
Pico Encryptor Checkout (dual channel)
  Data port:    /dev/ftdi_0
  Command port: /dev/pico_0
============================================================

--- Connection ---
  PASS  Data port exists: /dev/ftdi_0
  PASS  Data port open: /dev/ftdi_0 @ 115200
  PASS  Command port exists: /dev/pico_0
  PASS  Command port open: /dev/pico_0 @ 115200

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

All 40 checks across 11 groups should pass. The Overhead group reports zeros
on the Pico because the Cortex-M0+ lacks a DWT cycle counter.

### Checkout Options

| Flag                       | Description                             |
| -------------------------- | --------------------------------------- |
| `--data-port /dev/ttyUSB0` | Override data channel port (FTDI/UART0) |
| `--cmd-port /dev/ttyACM0`  | Override command channel port (USB CDC) |
| `--baud 115200`            | Override baud rate                      |
| `--verbose`                | Show detailed output per test           |

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

| Symptom                      | Fix                                                                |
| ---------------------------- | ------------------------------------------------------------------ |
| `/dev/pico_0` missing        | Check USB Micro-B cable, verify udev rules for 2e8a:000a           |
| `/dev/ftdi_0` missing        | Check FTDI adapter USB connection, verify udev rules for 0403:6001 |
| LED not blinking after flash | Reset the board with `make compose-pico-reset` or replug USB       |
| Port opens but no response   | Wait 2 seconds after opening for USB CDC enumeration               |
| Checkout skips groups        | Both ports required; verify data and command ports are available   |
| FTDI TX/RX LEDs not flashing | Check wiring: TXD->GP1, RXD->GP0, GND->GND                         |
| `cryptography` import error  | Install with `pip install cryptography` for decrypt verification   |
| Permission denied on port    | Add user to `dialout` group: `sudo usermod -aG dialout $USER`      |
| FTDI adapter at 5V           | Set voltage jumper to 3.3V; RP2040 GPIO is 3.3V logic              |
| Pico not entering BOOTSEL    | Hold BOOTSEL before plugging in USB; release after mount           |
| Overhead reports all zeros   | Expected on Cortex-M0+ (no DWT cycle counter)                      |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and channel architecture.
