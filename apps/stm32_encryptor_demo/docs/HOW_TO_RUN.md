# How to Run: STM32 Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the NUCLEO-L476RG (STM32L476RG, Cortex-M4 @ 80 MHz).

---

## Prerequisites

### Hardware

- NUCLEO-L476RG plugged in via USB-C (ST-Link VCP for command channel)
- DSD TECH SH-U09C5 USB-TTL adapter (FTDI FT232RL for data channel)
- Host machine running Linux (tested on Ubuntu 22.04+)

### Wiring

The FTDI adapter connects to USART1 on the NUCLEO board. Set the adapter
voltage jumper to **3.3V** (STM32L4 pins are not 5V tolerant).

```
SH-U09C5 (FTDI)          NUCLEO-L476RG
-----------------         -------------------------
Pin 3 (TXD) -----------> PA10 / D2  (CN9 pin 3)  [USART1 RX]
Pin 4 (RXD) <----------- PA9  / D8  (CN5 pin 1)  [USART1 TX]
Pin 2 (GND) ------------ GND        (CN6 pin 7)
```

Do not connect VCC -- the NUCLEO board is powered by its own USB connection.

### Docker

```bash
make docker-dev-stm32
```

### udev Rules

Both the ST-Link debug probe and the FTDI adapter need stable device symlinks.
Add to `/etc/udev/rules.d/99-microcontrollers.rules`:

```
# STM32 NUCLEO - ST-Link VCP (command channel)
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", SYMLINK+="nucleo_0"

# STM32 NUCLEO - ST-Link USB debug probe (flash/reset access)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE:="0666"

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
make release APP=stm32_encryptor_demo
```

Output:

- `build/stm32/firmware/stm32_encryptor_demo.elf`
- `build/stm32/firmware/stm32_encryptor_demo.bin`
- `build/stm32/firmware/stm32_encryptor_demo.hex`
- `build/release/stm32_encryptor_demo.tar.gz` (release tarball)

---

## 2. Flash

```bash
make compose-stm32-flash STM32_FIRMWARE=stm32_encryptor_demo
```

Expected:

```
st-flash write stm32_encryptor_demo.bin 0x08000000
...
Flash written and target reset
[stm32-flash] stm32_encryptor_demo flashed
```

**Important:** After flashing, reset the board:

```bash
make compose-stm32-reset
```

The `st-flash` tool sometimes leaves the CPU halted after programming. Either
run the reset command above or press the black RESET button on the NUCLEO board.

The on-board LED (LD2, PA5) should begin blinking at 2 Hz after a successful
flash and reset.

---

## 3. Reset

```bash
make compose-stm32-reset
```

Sends an SWD reset via st-flash to restart the STM32L476RG. Use this after
flashing or to recover from a halted state.

---

## 4. Run Checkout

```bash
python3 apps/stm32_encryptor_demo/scripts/serial_checkout.py \
  --data-port /dev/ftdi_0 \
  --cmd-port /dev/nucleo_0
```

### Expected Output

```
STM32 Encryptor Checkout (dual UART)
  Data port:    /dev/ftdi_0
  Command port: /dev/nucleo_0
============================================================

--- Connection ---
  PASS  Data port exists: /dev/ftdi_0
  PASS  Data port open: /dev/ftdi_0 @ 115200
  PASS  Command port exists: /dev/nucleo_0
  PASS  Command port open: /dev/nucleo_0 @ 115200

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

All 40 checks across 11 groups should pass.

### Checkout Options

| Flag                       | Description                               |
| -------------------------- | ----------------------------------------- |
| `--data-port /dev/ttyUSB0` | Override data channel port (FTDI/UART1)   |
| `--cmd-port /dev/ttyACM0`  | Override command channel port (VCP/UART2) |
| `--baud 115200`            | Override baud rate                        |
| `--verbose`                | Show detailed output per test             |

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
| `/dev/nucleo_0` missing      | Check USB-C cable, verify udev rules for 0483:374b                 |
| `/dev/ftdi_0` missing        | Check FTDI adapter USB connection, verify udev rules for 0403:6001 |
| CPU halted after flash       | Run `make compose-stm32-reset` or press the black RESET button     |
| LED not blinking after flash | Reset the board; st-flash can leave CPU halted (see above)         |
| Port opens but no response   | Wait 2 seconds after opening for UART initialization               |
| Checkout skips groups        | Both ports required; verify data and command ports are available   |
| FTDI TX/RX LEDs not flashing | Check wiring: TXD->PA10, RXD->PA9, GND->GND                        |
| `cryptography` import error  | Install with `pip install cryptography` for decrypt verification   |
| Permission denied on port    | Add user to `dialout` group: `sudo usermod -aG dialout $USER`      |
| FTDI adapter at 5V           | Set voltage jumper to 3.3V; STM32L4 pins are not 5V tolerant       |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and channel architecture.
