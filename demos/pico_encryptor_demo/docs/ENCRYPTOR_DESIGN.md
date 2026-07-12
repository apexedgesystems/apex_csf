# Pico Encryptor Design

Detailed design for the `pico_encryptor_demo` firmware application running on the
Raspberry Pi Pico. Covers encryption pipeline, dual channel architecture, serial
protocol, key management, task model, and RP2040-specific constraints.

---

## Overview

The encryptor is a unidirectional encryption passthrough device with flash-backed
key management. Plaintext bytes arrive on a data channel, are encrypted with
AES-256-GCM, and transmitted back as a SLIP-framed ciphertext packet. A separate
command channel provides key provisioning, diagnostics, and overhead measurement.

**Target hardware:** Raspberry Pi Pico (RP2040, dual ARM Cortex-M0+ @ 133 MHz,
2 MB flash, 264 KB SRAM)

**Encryption:** AES-256-GCM (software implementation, standard 8-bit CHAR_BIT)

**Framing:** SLIP (RFC 1055) on both channels

**Language:** C++17 (arm-none-eabi-gcc)

---

## Channel Architecture

Two independent channels, each with a distinct role:

| Channel | Interface              | Connector                       | Baud                 | Role                                      |
| ------- | ---------------------- | ------------------------------- | -------------------- | ----------------------------------------- |
| Data    | UART0 (GP0 TX, GP1 RX) | FTDI FT232RL (external adapter) | 115200               | Plaintext in, ciphertext out              |
| Command | USB CDC (native)       | USB Micro-B (onboard)           | N/A (full-speed USB) | Key management, mode control, diagnostics |

**Why two channels:** The data path is high-throughput and continuous. The command
path is low-frequency and administrative. Separating them prevents command
processing from interfering with encryption throughput, and allows the command
channel to operate even when the data channel is saturated.

**Contrast with Arduino/C2000:** Those targets have a single UART and multiplex
data and commands on the same port. The Pico has UART0 for data and native USB
CDC for commands, so no channel prefix byte or length-prefix multiplexing is
needed. This mirrors the STM32 dual-channel architecture.

**Contrast with STM32:** The STM32 uses two hardware UARTs (USART1 for data,
USART2 via ST-Link VCP for commands). The Pico replaces the second UART with
native USB CDC, which provides the same role without an external debug probe.

---

## Serial Protocol

### SLIP Framing

All communication uses SLIP (RFC 1055) framing with leading and trailing END
delimiters (0xC0). The data and command channels each have an independent SLIP
decoder instance.

### Data Channel Protocol

| Operation | Request (inside SLIP)      | Response (inside SLIP)                           |
| --------- | -------------------------- | ------------------------------------------------ |
| Encrypt   | `[plaintext:N] [CRC-16:2]` | `[key_idx:1] [nonce:12] [ciphertext:N] [tag:16]` |

CRC-16/XMODEM covers the plaintext only. Maximum plaintext size: 256 bytes.

### Encrypt Output Format

```
Response payload:
  +-----------+---------+----------------+----------+
  | key_index | nonce   | ciphertext (N) | auth_tag |
  +-----------+---------+----------------+----------+
  | 1 byte    | 12 bytes| N bytes        | 16 bytes |
```

- **key_index**: Slot index of the key used for this encryption (0-15)
- **nonce**: 96-bit nonce used for this packet (big-endian counter)
- **ciphertext**: AES-256-GCM encrypted data (same length as plaintext)
- **auth_tag**: 128-bit GCM authentication tag

No CRC on the output: the GCM auth tag provides both integrity and authenticity.
The nonce auto-increments after each successful encryption.

### Command Channel Protocol

| Opcode | Name             | Request Payload     | Response Payload                                          |
| ------ | ---------------- | ------------------- | --------------------------------------------------------- |
| 0x01   | KEY_STORE_WRITE  | `[slot:1] [key:32]` | (status only)                                             |
| 0x02   | KEY_STORE_READ   | `[slot:1]`          | `[key:32]`                                                |
| 0x03   | KEY_STORE_ERASE  | (none)              | (status only)                                             |
| 0x04   | KEY_STORE_STATUS | (none)              | `[count:1] [bitmap:2]`                                    |
| 0x10   | KEY_LOCK         | `[slot:1]`          | (status only)                                             |
| 0x11   | KEY_UNLOCK       | (none)              | (status only)                                             |
| 0x12   | KEY_MODE_STATUS  | (none)              | `[mode:1] [slot:1]`                                       |
| 0x20   | IV_RESET         | (none)              | (status only)                                             |
| 0x21   | IV_STATUS        | (none)              | `[nonce:12] [frame_count:4]`                              |
| 0x30   | STATS            | (none)              | `[framesOk:4] [framesErr:4] [bytesIn:4] [bytesOut:4]`     |
| 0x31   | STATS_RESET      | (none)              | (status only)                                             |
| 0x40   | OVERHEAD         | (none)              | `[last:4] [min:4] [max:4] [count:4] [budget:4] [flags:1]` |
| 0x41   | OVERHEAD_RESET   | (none)              | (status only)                                             |
| 0x42   | FASTFORWARD      | `[enable:1]`        | (status only)                                             |

Command request wire format: `SLIP( [opcode:1] [payload...] [CRC-16:2] )`

Command response wire format: `SLIP( [opcode:1] [status:1] [payload...] [CRC-16:2] )`

CRC-16/XMODEM covers opcode + payload.

### Status Codes

| Code | Name            | Meaning                                     |
| ---- | --------------- | ------------------------------------------- |
| 0x00 | OK              | Success                                     |
| 0x01 | ERR_INVALID_CMD | Unknown opcode                              |
| 0x02 | ERR_BAD_PAYLOAD | Payload length or CRC mismatch              |
| 0x03 | ERR_KEY_SLOT    | Invalid or empty key slot                   |
| 0x04 | ERR_FLASH       | Flash write/erase failure                   |
| 0x05 | ERR_LOCKED      | Operation not permitted in current key mode |

---

## Encryption

### Algorithm

AES-256-GCM per FIPS 197 + NIST SP 800-38D. Software implementation via
`encryption_mcu` library. Standard 8-bit CHAR_BIT on ARM Cortex-M0+.

### Performance

Software AES-256-GCM on dual Cortex-M0+ @ 133 MHz. The RP2040 lacks hardware
crypto acceleration, but the higher clock speed compared to the STM32 (133 vs
80 MHz) partially compensates for the simpler M0+ pipeline. At 115200 baud
(~11.5 KB/s), the UART is the bottleneck, not the crypto.

### Key Selection Modes

| Mode   | Behavior                                                      |
| ------ | ------------------------------------------------------------- |
| RANDOM | Counter-based rotation through populated key slots per packet |
| LOCKED | Use a single fixed slot for all packets                       |

Mode is controlled via the KEY_LOCK (0x10) and KEY_UNLOCK (0x11) commands.

### Test Key

Sequential bytes `0x00..0x1F` (same as STM32/Arduino/ESP32/C2000 encryptors)
for cross-platform verification. Provisioned to flash slot 0 on first boot
if the key store is empty.

---

## Key Store

### Flash-Backed Storage (Sector 511)

Keys are stored on the last 4 KB sector of the RP2040's external QSPI flash.
The sector is not part of the application image.

```
Sector 511 (offset 0x1FF000, address 0x101FF000):  Key store (4096 bytes)
  Slot  0: bytes 0x000-0x01F  (32 bytes)
  Slot  1: bytes 0x020-0x03F  (32 bytes)
  ...
  Slot 15: bytes 0x1E0-0x1FF  (32 bytes)
  Unused:  bytes 0x200-0xFFF  (3,584 bytes, reserved)
```

16 slots x 32 bytes = 512 bytes used out of 4096. A 512-byte RAM cache provides
RT-safe reads during the encryption hot path.

**Slot detection:** Empty slots read as 0xFF (flash erased state). A bitmap
scan at boot determines which slots are populated.

**Write strategy:** The RP2040 flash requires 256-byte aligned page programs
via `flash_range_program()`. If the target slot is empty, write the 256-byte
page containing it. If populated, erase the entire 4 KB sector and rewrite
all slots from the RAM cache with the updated key.

**Write performance:** Sector erase ~45 ms, page program ~1 ms.

**Endurance:** 100,000 write cycles per sector (external QSPI flash spec).

**XIP constraint:** Flash writes disable Execute In Place. The SDK copies a
small trampoline to SRAM and executes from there during flash operations.
Interrupts are disabled for the duration of each flash write or erase.

### Comparison with STM32 Key Store

| Property          | Pico (Flash Sector 511)  | STM32 (Flash Page 510)  |
| ----------------- | ------------------------ | ----------------------- |
| Capacity          | 16 slots (512 B)         | 16 slots (512 B)        |
| Sector/page size  | 4 KB                     | 2 KB                    |
| Write granularity | 256 bytes (page program) | 8 bytes (double-word)   |
| Erase required    | Yes (4 KB sector erase)  | Yes (2 KB page erase)   |
| Write time        | ~1 ms/page               | ~1 ms/double-word       |
| Endurance         | 100K cycles/sector       | 10K cycles/page         |
| RAM cache         | Full slot cache (512 B)  | Full slot cache (512 B) |
| Flash type        | External QSPI (XIP)      | Internal NOR            |

---

## Task Model

### McuExecutive (100 Hz)

The firmware uses a cooperative scheduler with three tasks. The SysTick timer
is prescaled from 1 kHz to 100 Hz by `PicoSysTickSource`.

```
ledBlinkTask:       2 Hz  priority=0     (freqN=1, freqD=50)
dataChannelTask:  100 Hz  priority=0     (SLIP decode + encrypt + transmit)
commandTask:       20 Hz  priority=0     (freqN=1, freqD=5)
```

**No profiler tasks:** The Cortex-M0+ lacks a DWT cycle counter (that is a
Cortex-M3+ feature). The `OverheadTracker` is a no-op stub that returns zeros
for all queries. The OVERHEAD command responds with valid protocol frames
containing zero values.

**Bare-metal only:** No RTOS support on the Pico. The `McuExecutive` runs
directly in `main()` with `PicoSysTickSource` providing timing via WFI
(Wait For Interrupt).

### Tick Source

The Pico SDK configures SysTick at 1 kHz via `pico_time`. The
`PicoSysTickSource` prescales 1 kHz down to 100 Hz for the executive. The
RP2040 runs at 133 MHz, so the SysTick reload value is 133000 for a 1 kHz tick.

### Initialization Sequence

```
main():
  stdio_init_all()          # Pico SDK (clocks, USB CDC stack)
  gpio_init(GP25)           # LED pin as output
  Startup blinks (3x)      # Visual confirmation
  irq_set_exclusive_handler # Register UART0 IRQ
  dataUart.init(115200)     # UART0 (FTDI) 8N1, interrupt-driven
  cmdUart.init()            # USB CDC (baud rate ignored)
  flash.init()              # Flash interface
  keyStore.init()           # Scan sector 511 for populated slots
  if store empty:
    provision test key      # Write 0x00..0x1F to slot 0
  engine.loadActiveKey()    # Load key from store into encrypt engine
  tracker.enableDwt()       # No-op on M0+ (API consistency)
  registerSchedulerTasks()  # 3 tasks into McuExecutive
  exec.init() + exec.run()  # Blocks forever (cooperative loop via SysTick WFI)
```

### Interrupt Service Routines

| Vector      | Handler                          | Purpose                             |
| ----------- | -------------------------------- | ----------------------------------- |
| UART0_IRQ   | dataUart.irqHandler()            | FTDI data channel RX/TX             |
| isr_systick | PicoSysTickSource::isrCallback() | SysTick prescaler (1 kHz to 100 Hz) |

USB CDC is handled by the TinyUSB stack running on a background timer
configured by `stdio_init_all()`. The `PicoUsbCdc` class calls
`tud_cdc_read()` / `tud_cdc_write()` directly from the command task.

---

## Overhead Measurement

The Cortex-M0+ does not have a DWT cycle counter. The `OverheadTracker` class
is a no-op stub: `enableDwt()`, `start()`, and `stop()` do nothing. All queries
return zero values.

The OVERHEAD command still works at the protocol level. Responses contain valid
SLIP-framed packets with zero values for last/min/max/count/budget and a flags
byte indicating that measurement is unavailable. This allows the checkout script
to run the full Overhead test group without special-casing the Pico.

**Fast-forward mode:** When enabled via the FASTFORWARD command, the executive
skips the wait-for-tick delay and runs tasks back-to-back. On the Pico this
reveals the maximum achievable scheduler rate, limited by task execution time
rather than the 100 Hz timer. Overhead values remain zero.

---

## File Layout

```
pico_encryptor_demo/
+-- CMakeLists.txt              # Standalone build (Pico SDK, apex_add_firmware)
+-- release.mk                  # Release manifest (make release APP=...)
+-- docs/
|   +-- BREADBOARD_SETUP.md     # Breadboard wiring diagram
|   +-- ENCRYPTOR_DESIGN.md     # This document
|   +-- HOW_TO_RUN.md           # Build, flash, verify steps
|   +-- MEMORY_MAP.md           # Flash/SRAM layout, three-platform comparison
|   +-- RASPBERRY_PI_PICO_PINOUT.md # Board pinout, GPIO assignments
|   +-- SH_U09C5_PINOUT.md     # FTDI adapter pinout
+-- inc/
|   +-- CommandDeck.hpp         # Command channel handler (14 opcodes)
|   +-- EncryptorCommon.hpp     # Shared types, sizing template, protocol enums
|   +-- EncryptorConfig.hpp     # Pico-specific sizing (256B, 16 slots, no prefix)
|   +-- EncryptorEngine.hpp     # Data channel encrypt pipeline
|   +-- KeyStore.hpp            # Flash-backed key storage (16 slots, sector 511)
|   +-- OverheadTracker.hpp     # No-op stub (M0+ has no DWT cycle counter)
+-- scripts/
|   +-- serial_checkout.py      # Automated checkout (40 checks, 11 groups)
+-- src/
|   +-- main.cpp                # Application entry, task registration, ISRs
|   +-- CommandDeck.cpp         # Command dispatch and response builder
|   +-- EncryptorEngine.cpp     # CRC validate, encrypt, SLIP encode, transmit
|   +-- KeyStore.cpp            # Flash sector management (read/write/erase/bitmap)
|   +-- cxx_stubs.cpp           # C++ runtime stubs (pure virtual, guards, atexit)
+-- .claude_docs/               # (gitignored) Dev reference material
    +-- PORTING_NOTES.md        # Implementation history and RP2040 constraints
```
