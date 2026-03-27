# C2000 Encryptor Design

Detailed design for the `c2000_encryptor_demo` firmware application running on the
LAUNCHXL-F280049C. Covers encryption pipeline, CAN loopback, serial protocol,
task model, and C2000-specific constraints.

---

## Overview

The encryptor is a unidirectional encryption passthrough device with CAN bus
diagnostics. Plaintext bytes arrive on a serial channel, are encrypted with
AES-256-GCM, and transmitted back as a framed ciphertext packet. CAN loopback
provides internal bus verification without external hardware.

**Target hardware:** LAUNCHXL-F280049C (TMS320F280049C, C28x DSP @ 100 MHz,
256 KB flash, 24 KB SRAM)

**Encryption:** AES-256-GCM (software implementation, 16-bit CHAR_BIT safe)

**Framing:** Length-prefix protocol (single UART, no SLIP)

**Language:** C++03 (TI CGT limitation) with `compat_legacy.hpp` shims

---

## Channel Architecture

Single UART channel, multiplexed by command byte:

| Channel      | UART  | Connector              | Baud   | Role                        |
| ------------ | ----- | ---------------------- | ------ | --------------------------- |
| Data/Command | SCI-A | XDS110 backchannel USB | 115200 | Encrypt + CAN + diagnostics |

**Why single channel:** The F280049C LaunchPad has one user-accessible UART
via the XDS110 debug probe backchannel. Unlike the STM32 encryptor (dual UART),
commands and data share the same port using a length-prefix protocol.

---

## Serial Protocol

### Request/Response Format

| Command      | Request                   | Response                                       |
| ------------ | ------------------------- | ---------------------------------------------- |
| Echo test    | `[0x00]`                  | `OK\r\n`                                       |
| Encrypt      | `[len:1] [plaintext:len]` | `[len:1] [ciphertext:len] [tag:16] [nonce:12]` |
| CAN status   | `[0xFE]`                  | `CAN: OK TXOK CNT=N\r\n`                       |
| CAN loopback | `[0xFF]`                  | `CAN LOOPBACK PASS #N TX:... RX:...\r\n`       |
| Bad length   | `[len > 128]`             | `ERR:LEN\r\n`                                  |

### Encrypt Output Format

```
Response:
  +-----+----------------+----------+---------+
  | len | ciphertext (N) | auth_tag | nonce   |
  +-----+----------------+----------+---------+
  | 1B  | N bytes        | 16 bytes | 12 bytes|
```

- **len**: Echo of input length (1 byte)
- **ciphertext**: AES-256-GCM encrypted data (same length as plaintext)
- **auth_tag**: 128-bit GCM authentication tag
- **nonce**: 96-bit nonce used for this packet (big-endian counter)

The nonce auto-increments after each successful encryption.

---

## Encryption

### Algorithm

AES-256-GCM per FIPS 197 + NIST SP 800-38D. Software implementation in
`inc/aes256gcm.h` -- no hardware crypto on F280049C.

### 16-Bit CHAR_BIT

The C28x ISA is 16-bit word-addressable. `sizeof(char) == 1` but each char
is 16 bits wide. The AES implementation uses `uint16_t` arrays (one byte per
word) and masks all byte operations with `& 0xFF` via a `B()` macro:

```c
#define B(x) ((uint16_t)(x) & 0xFFu)
```

This produces output identical to standard 8-bit implementations (verified
against Python `cryptography` library).

### Performance

| Payload  | Time | Notes                                    |
| -------- | ---- | ---------------------------------------- |
| 4 bytes  | ~2s  | Sub-block, single AES + GHASH            |
| 8 bytes  | ~4s  | Sub-block                                |
| 16 bytes | ~8s  | Full AES block                           |
| 64 bytes | >15s | 4 blocks, unoptimized GF(2^128) multiply |

The GF(2^128) multiply is bit-serial (128 iterations x 16 bytes). A table-based
optimization would improve throughput ~256x but increases flash usage by 4 KB.

### Test Key

Sequential bytes `0x00..0x1F` (same as STM32/Pico/ESP32/Arduino encryptors)
for cross-platform verification.

---

## CAN Bus

### Internal Loopback

CAN-A configured in internal loopback mode (no external transceiver required).
TX frames are received back on the same controller for data integrity
verification.

| Parameter         | Value                                 |
| ----------------- | ------------------------------------- |
| Peripheral        | CAN-A (DCAN)                          |
| Bit rate          | 500 kbps                              |
| Mode              | Internal loopback (`CAN_TEST_LBACK`)  |
| TX message object | 1 (ID 0x100, standard frame)          |
| RX message object | 2 (filter: ID 0x100, mask 0x7FF)      |
| Payload           | `DEADBEEFCAFE` + 16-bit frame counter |

### External Bus (Future)

The LaunchPad has an onboard CAN transceiver (SN65HVD234, U10) connected to
J14. External bus communication requires:

1. Remove `CAN_enableTestMode(CANA_BASE, CAN_TEST_LBACK)` from `initCan()`
2. Connect J14 CAN-H/CAN-L/GND to external CAN bus
3. Ensure 120 ohm termination at both ends

See `.claude_docs/LESSONS_LEARNED.md` for debugging notes on external CAN.

---

## Clock Configuration

The default C2000Ware `device.h` assumes a 20 MHz external crystal. The
LAUNCHXL-F280049C requires INTOSC2 (10 MHz internal oscillator):

```
SYSCLK = 10 MHz (INTOSC2) x 20 (IMULT) / 2 (SYSDIV) = 100 MHz
LSPCLK = SYSCLK / 4 = 25 MHz
```

Local copies of `device.h` and `device.c` in `inc/` and `src/` contain this
fix. The `_FLASH` define enables flash wait-state configuration and ramfunc
copy in `Device_init()`.

---

## Task Model

No LiteExecutive (C++03 limitation). Simple polling loop:

```
main():
  Device_init()          # PLL, flash, watchdog
  initGpio()             # LED, SCI pins
  sciUart.init(115200)   # SCI-A via C2000Uart HAL class
  initCan()              # CAN-A loopback

  loop:
    if SCI RX available:
      dispatch command (echo / encrypt / CAN)
    else:
      toggle heartbeat LED
      delay 250ms
```

The heartbeat LED (GPIO34/LD5) blinks at ~2 Hz while idle and toggles on
encryption activity.

---

## File Layout

```
c2000_encryptor_demo/
+-- CMakeLists.txt              # Build config (apex_add_firmware)
+-- release.mk                  # Release manifest (make release APP=...)
+-- F280049C.cmd                # Linker command file (flash layout)
+-- LAUNCHXL_F280049C.ccxml     # UniFlash/DSLite target config
+-- docs/
|   +-- ENCRYPTOR_DESIGN.md     # This document
|   +-- HOW_TO_RUN.md               # Build, flash, verify steps
|   +-- LAUNCHXL_F280049C_PINOUT.md # Board pinout, headers, LEDs
|   +-- MEMORY_MAP.md               # Flash/RAM layout, section placement
+-- inc/
|   +-- aes256gcm.h             # AES-256-GCM (C, 16-bit CHAR_BIT safe)
|   +-- device.h                # Local device.h (INTOSC2 clock fix)
|   +-- device_cfg.h            # Clock config overrides
+-- scripts/
|   +-- serial_checkout.py      # Automated checkout (11 tests)
+-- src/
|   +-- main.cpp                # Application (C++03)
|   +-- cxx_stubs.cpp           # C++ runtime stubs
|   +-- device.c                # Local device.c (flash init)
+-- .claude_docs/               # (gitignored) Dev reference material
    +-- LESSONS_LEARNED.md      # Board-specific debug notes
    +-- spru514z.pdf            # TI C2000 CGT compiler reference
```
