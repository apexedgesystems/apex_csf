# How to Run: STM32 Encryptor

Step-by-step commands to build, flash, and verify the AES-256-GCM encryptor
firmware on the NUCLEO-L476RG (STM32L476RG, Cortex-M4 @ 80 MHz). The same
demo also builds for the NUCLEO-F767ZI (STM32F767ZI, Cortex-M7 @ 216 MHz)
and the NUCLEO-F446RE (STM32F446RE, Cortex-M4 @ 180 MHz); their differences
are collected in [Other boards](#other-boards) at the end.

---

## Prerequisites

### Hardware

- NUCLEO-L476RG plugged in via USB (ST-Link: SWD for flashing, VCP for the
  command channel)
- DSD TECH SH-U09C5 USB-TTL adapter (FTDI FT232RL for the data channel)
- A Linux host for both USB cables. Two arrangements are in use:
  - **Laptop-direct**: both cables on the development machine; the
    `compose-stm32-*` make targets flash from the dev container and the
    udev symlinks below name the ports.
  - **Pi rig**: both cables on the Raspberry Pi 4 at
    `kalex@raspberrypi.local` (the same rig the HIL demo uses). The
    firmware and checkout script are copied over and `st-flash` runs on the
    Pi. The checkout record in this document was taken on this rig.

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
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", SYMLINK+="nucleo_l476rg_0"

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

The deployable is the release package. Building it is the same command a
user runs to ship the app:

```bash
make release APP=stm32_encryptor_demo
```

This builds the stm32 platform inside the dev-stm32 container (every
firmware image in that preset, not only this app), then stages this app's
artifacts and tars them:

- `build/release/stm32_encryptor_demo/stm32/firmware/stm32_encryptor_demo.elf`
- `build/release/stm32_encryptor_demo/stm32/firmware/stm32_encryptor_demo.bin`
- `build/release/stm32_encryptor_demo/stm32/firmware/stm32_encryptor_demo.hex`
- `build/release/stm32_encryptor_demo.tar.gz`

Flash from the staged copy under `build/release/`: that is the artifact a
deployment ships, and it is what the checkout record below was taken on.

The build summary prints flash and RAM usage; the expected figures are in
[MEMORY_MAP.md](MEMORY_MAP.md).

### FreeRTOS variant

The release manifest registers the bare-metal build only. The FreeRTOS
variant is a development build of the same platform preset:

```bash
make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=ON"
```

It writes `build/mcu-stm32-relwithdebinfo/firmware/stm32_encryptor_demo.{elf,bin,hex}`.
The option is a CMake cache flag on the shared build directory, so a
following `make release APP=stm32_encryptor_demo` or
`make compose-stm32 CMAKE_EXTRA_ARGS="-DAPEX_USE_FREERTOS=OFF"` switches
it back before the next bare-metal package.

---

## 2. Flash

### Laptop-direct

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

### Pi rig

The ST-Link is on the Pi's USB, so the Pi's own `st-flash` programs the
board. Copy the binary and the checkout script over, then flash and reset
in one step:

```bash
ssh kalex@raspberrypi.local 'mkdir -p ~/apex/stm32_encryptor_demo'
scp build/release/stm32_encryptor_demo/stm32/firmware/stm32_encryptor_demo.bin \
    demos/stm32_encryptor_demo/scripts/serial_checkout.py \
    kalex@raspberrypi.local:~/apex/stm32_encryptor_demo/
ssh kalex@raspberrypi.local 'cd ~/apex/stm32_encryptor_demo && \
    sudo st-flash write stm32_encryptor_demo.bin 0x08000000 && \
    sudo st-flash reset'
```

`st-flash` verifies the write ("Flash written and verified") before the
reset. The Pi needs `stlink-tools`, `python3-serial`, and
`python3-cryptography`.

---

## 3. Reset

```bash
make compose-stm32-reset                          # laptop-direct
ssh kalex@raspberrypi.local 'sudo st-flash reset'  # Pi rig
```

Sends an SWD reset via st-flash to restart the STM32L476RG. Use this after
flashing or to recover from a halted state.

---

## 4. Run Checkout

Laptop-direct (the udev symlinks are the script defaults):

```bash
python3 demos/stm32_encryptor_demo/scripts/serial_checkout.py \
  --data-port /dev/ftdi_0 \
  --cmd-port /dev/nucleo_l476rg_0
```

Pi rig (no udev symlinks there: the ST-Link VCP enumerates as
`/dev/ttyACM0` and the FTDI adapter as `/dev/ttyUSB0`):

```bash
ssh kalex@raspberrypi.local 'cd ~/apex/stm32_encryptor_demo && \
    python3 serial_checkout.py --data-port /dev/ttyUSB0 --cmd-port /dev/ttyACM0'
```

### Expected Output

Per-check PASS lines print as each group runs (shown here for the
Connection group; `--verbose` adds the measured values), followed by the
summary:

```
STM32 Encryptor Checkout
  Data channel:    /dev/ttyUSB0
  Command channel: /dev/ttyACM0
============================================================

--- Connection ---
  PASS  Data port exists: /dev/ttyUSB0
  PASS  Data port open: /dev/ttyUSB0 @ 115200
  PASS  Command port exists: /dev/ttyACM0
  PASS  Command port open: /dev/ttyACM0 @ 115200

  ...

============================================================
Checkout Summary
============================================================

  [PASS] Connection
        [PASS] Data port exists
        [PASS] Data port open
        [PASS] Command port exists
        [PASS] Command port open

  [PASS] Data Channel
        [PASS] Encrypt basic
        [PASS] Nonce increment

  [PASS] Key Store
        [PASS] Erase all keys
        [PASS] Write key slot 0
        [PASS] Read key slot 0
        [PASS] Write key slot 1
        [PASS] Key store status (2 keys)

  [PASS] Key Mode
        [PASS] Lock to key 0
        [PASS] Locked mode consistent
        [PASS] Mode status (locked)
        [PASS] Unlock
        [PASS] Mode status (random)

  [PASS] IV Management
        [PASS] IV reset
        [PASS] IV status
        [PASS] IV after encrypt

  [PASS] Diagnostics
        [PASS] Stats reset
        [PASS] Stats query
        [PASS] Stats after encrypt

  [PASS] Rejection
        [PASS] Bad CRC rejected
        [PASS] Too-short rejected
        [PASS] No-keys rejected

  [PASS] Stress
        [PASS] Re-provision key 0
        [PASS] Continuous encrypt (20 packets)
        [PASS] Encrypt roundtrip (decrypt verify)

  [PASS] Throughput
        [PASS] Re-provision key 0
        [PASS] Lock to key 0
        [PASS] Throughput 16B x 100
        [PASS] Throughput 64B x 100
        [PASS] Throughput 128B x 50
        [PASS] Throughput 256B x 50

  [PASS] Overhead
        [PASS] Overhead query
        [PASS] Overhead reset
        [PASS] Idle overhead (1s)
        [PASS] Fast-forward on
        [PASS] Fast-forward overhead
        [PASS] Fast-forward off

  [PASS] Idle
        [PASS] Data channel idle
        [PASS] Command channel idle

40/40 checks passed
Checkout: PASS
```

All 40 checks across 11 groups should pass. The throughput figures the
script prints are host round-trip bound (one request, one response, wait
for the reply), not a firmware ceiling: on the Pi rig they land at roughly
11-53 packets/s depending on payload size.

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

## Other boards

One knob selects the board at configure time, `APEX_STM32_BOARD`, the same
way `APEX_USE_FREERTOS` selects the execution mode. The USB-only boards need
no FTDI adapter: plug the ST-Link USB in and everything runs over the
board's udev name. What changes:

| Item          | NUCLEO-L476RG                                  | NUCLEO-F767ZI                                                                                                  | NUCLEO-F446RE                                            |
| ------------- | ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| Board knob    | `nucleo_l476rg` (default)                      | `nucleo_f767zi`                                                                                                | `nucleo_f446re`                                          |
| Core / clock  | Cortex-M4F @ 80 MHz                            | Cortex-M7 @ 216 MHz (HSE bypass 8 MHz, PLL, over-drive)                                                        | Cortex-M4F @ 180 MHz (HSE bypass 8 MHz, PLL, over-drive) |
| Channels      | Two UARTs: FTDI (data) + ST-Link VCP (command) | One UART: the ST-Link VCP carries both, each SLIP frame prefixed with a channel byte (0x00 data, 0x01 command) | Same as the F767ZI                                       |
| Heartbeat LED | LD2, PA5                                       | LD1 (green), PB0                                                                                               | LD2, PA5                                                 |
| Key store     | Flash page 510 (2 KB)                          | Flash sector 11 (256 KB single-bank); erase about a second                                                     | Flash sector 7 (128 KB); erase under a second            |
| Tick budget   | 800,000 cycles                                 | 2,160,000 cycles                                                                                               | 1,800,000 cycles                                         |
| udev name     | `/dev/nucleo_l476rg_0`                         | `/dev/nucleo_f767zi_0`                                                                                         | `/dev/nucleo_f446re_0`                                   |

The udev rules key on each board's ST-Link serial (`st-info --probe` prints
it) with the interface-02 tty, one rule per board.

### Build

A build directory holds one board (the toolchain refuses an in-place
switch), so remove the stm32 build directory when changing boards:

```bash
rm -rf build/mcu-stm32-relwithdebinfo
make release APP=stm32_encryptor_demo CMAKE_EXTRA_ARGS="-DAPEX_STM32_BOARD=nucleo_f767zi"
# or: -DAPEX_STM32_BOARD=nucleo_f446re
```

The artifacts stage under the same `build/release/stm32_encryptor_demo/`
paths as the L476 build; the staging directory holds whichever board was
configured. The FreeRTOS variant adds `-DAPEX_USE_FREERTOS=ON` to the same
option string with `make compose-stm32`.

### Flash and reset

The board is on the development machine, so flash with the host's
`st-flash`, selecting the probe by serial when more than one ST-Link is
attached:

```bash
st-flash --connect-under-reset --serial <st-link serial> write \
    build/release/stm32_encryptor_demo/stm32/firmware/stm32_encryptor_demo.bin 0x08000000
st-flash --connect-under-reset --serial <st-link serial> reset
```

`--connect-under-reset` holds the core in reset while the probe attaches;
without it a running F767 image can refuse the connection ("Can not
connect to target"). The heartbeat LED blinks at 2 Hz after the reset.

### Checkout

```bash
python3 demos/stm32_encryptor_demo/scripts/serial_checkout.py \
  --shared-port /dev/nucleo_f767zi_0 --timeout 6
# or: --shared-port /dev/nucleo_f446re_0 --timeout 6
```

`--shared-port` opens one handle for both channels and prefixes and routes
the frames. `--timeout 6` covers the key-store sector erase, which the
L476's default 2 s would cut short. The same 40 checks run; the Connection
group reports the command channel as sharing the data port.

---

## Troubleshooting

| Symptom                                             | Fix                                                                      |
| --------------------------------------------------- | ------------------------------------------------------------------------ |
| `/dev/nucleo_l476rg_0` missing                      | Check USB-C cable, verify udev rules for 0483:374b                       |
| `/dev/ftdi_0` missing                               | Check FTDI adapter USB connection, verify udev rules for 0403:6001       |
| CPU halted after flash                              | Run `make compose-stm32-reset` or press the black RESET button           |
| LED not blinking after flash                        | Reset the board; st-flash can leave CPU halted (see above)               |
| Port opens but no response                          | Wait 2 seconds after opening for UART initialization                     |
| Checkout skips groups                               | Both ports required; verify data and command ports are available         |
| FTDI TX/RX LEDs not flashing                        | Check wiring: TXD->PA10, RXD->PA9, GND->GND                              |
| `cryptography` import error                         | Install with `pip install cryptography` for decrypt verification         |
| Permission denied on port                           | Add user to `dialout` group: `sudo usermod -aG dialout $USER`            |
| FTDI adapter at 5V                                  | Set voltage jumper to 3.3V; STM32L4 pins are not 5V tolerant             |
| Board switch refused                                | The stm32 build directory remembers its board; remove it and rebuild     |
| Sector-flash board checkout times out on key writes | Pass `--timeout 6`; a 128 KB or 256 KB sector erase takes up to a second |

---

## Protocol Reference

See [ENCRYPTOR_DESIGN.md](ENCRYPTOR_DESIGN.md) for the full serial protocol
specification, encryption details, and channel architecture.
