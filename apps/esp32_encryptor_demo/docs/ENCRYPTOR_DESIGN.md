# ESP32 Encryptor Design

Detailed design for the `esp32_encryptor_demo` firmware application running on the
Arduino Nano ESP32. Covers encryption pipeline, dual channel architecture, serial
protocol, key management, task model, and ESP32-S3-specific constraints.

---

## Overview

The encryptor is a unidirectional encryption passthrough device with NVS-backed
key management. Plaintext bytes arrive on a data channel, are encrypted with
AES-256-GCM, and transmitted back as a SLIP-framed ciphertext packet. A separate
command channel provides key provisioning, diagnostics, and overhead measurement.

**Target hardware:** Arduino Nano ESP32 (ESP32-S3, Xtensa LX7 dual-core @ 240 MHz,
512 KB SRAM, 16 MB flash)

**Encryption:** AES-256-GCM (software implementation, standard 8-bit CHAR_BIT)

**Framing:** SLIP (RFC 1055) on both channels

**Language:** C++23 (ESP-IDF v5.5.3, GCC 14.2.0)

---

## Channel Architecture

Two independent channels, each with a distinct role:

| Channel | Interface                    | Connector                       | Baud                 | Role                                      |
| ------- | ---------------------------- | ------------------------------- | -------------------- | ----------------------------------------- |
| Data    | UART0 (GPIO43 TX, GPIO44 RX) | FTDI FT232RL (external adapter) | 115200               | Plaintext in, ciphertext out              |
| Command | USB Serial/JTAG (native)     | USB-C (onboard)                 | N/A (full-speed USB) | Key management, mode control, diagnostics |

**Why two channels:** The data path is high-throughput and continuous. The command
path is low-frequency and administrative. Separating them prevents command
processing from interfering with encryption throughput, and allows the command
channel to operate even when the data channel is saturated.

**Contrast with Arduino/C2000:** Those targets have a single UART and multiplex
data and commands on the same port. The ESP32 has UART0 for data and native USB
Serial/JTAG for commands, so no channel prefix byte or length-prefix multiplexing
is needed. This mirrors the STM32 and Pico dual-channel architectures.

**Contrast with Pico:** The Pico uses USB CDC via TinyUSB for the command channel.
The ESP32 uses the built-in USB Serial/JTAG controller, which provides the same
role with a simpler driver (no TinyUSB stack needed). Both present as `/dev/ttyACM*`
on the host.

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
| 0x04 | ERR_FLASH       | NVS write/erase failure                     |
| 0x05 | ERR_LOCKED      | Operation not permitted in current key mode |

---

## Encryption

### Algorithm

AES-256-GCM per FIPS 197 + NIST SP 800-38D. Software implementation via
`encryption_lite` library. Standard 8-bit CHAR_BIT on Xtensa LX7.

### Performance

Software AES-256-GCM on Xtensa LX7 @ 240 MHz. The ESP32-S3 lacks dedicated
AES-GCM hardware acceleration in software mode, but the 240 MHz clock speed
(highest in the encryptor family) provides ample margin. At 115200 baud
(~11.5 KB/s), the UART is the bottleneck, not the crypto.

### Key Selection Modes

| Mode   | Behavior                                                      |
| ------ | ------------------------------------------------------------- |
| RANDOM | Counter-based rotation through populated key slots per packet |
| LOCKED | Use a single fixed slot for all packets                       |

Mode is controlled via the KEY_LOCK (0x10) and KEY_UNLOCK (0x11) commands.

### Test Key

Sequential bytes `0x00..0x1F` (same as STM32/Arduino/Pico/C2000 encryptors)
for cross-platform verification. Provisioned to NVS slot 0 on first boot
if the key store is empty.

---

## Key Store

### NVS-Backed Storage

Keys are stored in ESP-IDF's Non-Volatile Storage (NVS) partition, which
provides wear-leveled key-value storage over flash. This is simpler than the
raw flash sector/page approach used on STM32 (page 510) and Pico (sector 511).

```
NVS partition (24 KB, namespace "encryptor"):
  Slot  0: key "key_00"  (32 bytes, NVS blob)
  Slot  1: key "key_01"  (32 bytes, NVS blob)
  ...
  Slot 15: key "key_15"  (32 bytes, NVS blob)
```

16 slots x 32 bytes = 512 bytes of key material. A 512-byte RAM cache provides
RT-safe reads during the encryption hot path.

**Slot detection:** Empty slots read as 0xFF (erased state in cache). At boot,
all 16 NVS keys are read into the RAM cache. Missing keys leave the cache at
the erased default. A bitmap scan determines which slots are populated.

**Write strategy:** NVS handles flash wear leveling internally. Writing a key
calls `nvs_set_blob()` followed by `nvs_commit()`. No manual sector erase or
page management required.

**Write performance:** NVS write latency is variable (~1-10 ms depending on
internal garbage collection), but command channel writes are infrequent.

**Endurance:** NVS distributes writes across the full 24 KB partition. Effective
endurance far exceeds the raw flash cycle limit (100K cycles per sector).

### Comparison with Other Key Stores

| Property          | ESP32 (NVS)             | Pico (Flash Sector 511)  | STM32 (Flash Page 510)  |
| ----------------- | ----------------------- | ------------------------ | ----------------------- |
| Capacity          | 16 slots (512 B)        | 16 slots (512 B)         | 16 slots (512 B)        |
| Storage type      | Key-value blobs         | Raw flash sector         | Raw flash page          |
| Write granularity | Per-key blob            | 256 bytes (page program) | 8 bytes (double-word)   |
| Erase required    | No (NVS handles)        | Yes (4 KB sector erase)  | Yes (2 KB page erase)   |
| Wear leveling     | Automatic               | None (single sector)     | None (single page)      |
| RAM cache         | Full slot cache (512 B) | Full slot cache (512 B)  | Full slot cache (512 B) |
| Endurance         | High (wear-leveled)     | 100K cycles/sector       | 10K cycles/page         |

---

## Task Model

### McuExecutive (100 Hz) inside FreeRTOS

The firmware uses a cooperative scheduler with five tasks. ESP-IDF runs FreeRTOS
by default; the McuExecutive runs inside a pinned FreeRTOS task on core 0
(unicore mode via `CONFIG_FREERTOS_UNICORE=y`).

```
profilerStartTask:  100 Hz  priority=127   (CCOUNT cycle start)
ledBlinkTask:         2 Hz  priority=0     (freqN=1, freqD=50)
dataChannelTask:    100 Hz  priority=0     (SLIP decode + encrypt + transmit)
commandTask:         20 Hz  priority=0     (freqN=1, freqD=5)
profilerEndTask:    100 Hz  priority=-128  (CCOUNT cycle end)
```

The profiler tasks bracket the useful work at the highest and lowest priorities
to measure per-tick CPU overhead via the Xtensa CCOUNT register.

### Tick Source

The `Esp32TimerTickSource` uses `esp_timer` (a high-resolution hardware timer)
to generate 100 Hz ticks. Each tick fires a callback that sends a FreeRTOS task
notification to the executive task, which unblocks from `ulTaskNotifyTake()`.
This provides efficient blocking without busy-waiting.

### Initialization Sequence

```
app_main():
  ws2812Init()              # RMT peripheral for RGB LED
  Startup blinks (3x)      # Visual confirmation (green pulses)
  dataUart.init(115200)     # UART0 (FTDI) 8N1
  cmdUart.init()            # USB Serial/JTAG (baud rate ignored)
  keyStore.init()           # NVS init, read all slots into RAM cache
  if store empty:
    provision test key      # Write 0x00..0x1F to slot 0
  engine.loadActiveKey()    # Load key from store into encrypt engine
  tracker.enableDwt()       # No-op on Xtensa (CCOUNT always active)
  xTaskCreate(executiveTask)  # Pinned FreeRTOS task (4 KB stack, priority 3)
  return                    # app_main returns to FreeRTOS idle task

executiveTask():
  register 5 tasks          # profiler start/end + led + data + command
  exec.init() + exec.run()  # Blocks forever (cooperative loop via esp_timer)
```

### FreeRTOS Configuration

| Parameter               | Value                                   |
| ----------------------- | --------------------------------------- |
| Unicore mode            | Enabled (core 0 only)                   |
| Executive task stack    | 4 KB (4096 words)                       |
| Executive task priority | 3 (above idle, below critical)          |
| C++ exceptions          | Disabled                                |
| C++ RTTI                | Disabled                                |
| Task watchdog           | Disabled (executive loop timing varies) |

---

## Overhead Measurement

The Xtensa LX7 CCOUNT special register runs at core clock speed (240 MHz). The
profiler start and end tasks sample CCOUNT at the boundaries of each scheduler
tick to measure per-tick CPU cost.

**Resolution:** 1 cycle = 4.17 ns at 240 MHz. The 32-bit counter wraps every
~17.9 seconds, but per-tick deltas (10 ms window) are well within range.

**Budget:** At 100 Hz, each tick has 2,400,000 cycles.

**Fast-forward mode:** When enabled via the FASTFORWARD command, the executive
skips the wait-for-tick delay and runs tasks back-to-back. This reveals the
maximum achievable scheduler rate, limited by task execution time rather than
the 100 Hz timer.

**Contrast with Pico:** The Cortex-M0+ lacks a DWT cycle counter, so the Pico
returns zeros for all overhead queries. The ESP32 provides real measurements
comparable to the STM32's DWT-based profiler.

---

## File Layout

```
esp32_encryptor_demo/
+-- CMakeLists.txt              # Standalone build (ESP-IDF, apex_add_firmware)
+-- release.mk                  # Release manifest (make release APP=...)
+-- sdkconfig.defaults          # ESP-IDF configuration (unicore, 240 MHz, 16 MB flash)
+-- docs/
|   +-- ARDUINO_NANO_ESP32_PINOUT.md  # Board pinout, GPIO assignments
|   +-- BREADBOARD_SETUP.md     # Breadboard wiring diagram
|   +-- ENCRYPTOR_DESIGN.md     # This document
|   +-- HOW_TO_RUN.md           # Build, flash, verify steps
|   +-- MEMORY_MAP.md           # Flash/SRAM layout, four-platform comparison
|   +-- SH_U09C5_PINOUT.md     # FTDI adapter pinout
+-- inc/
|   +-- CommandDeck.hpp         # Command channel handler (14 opcodes)
|   +-- EncryptorCommon.hpp     # Shared types, sizing template, protocol enums
|   +-- EncryptorConfig.hpp     # ESP32-specific sizing (256B, 16 slots, no prefix)
|   +-- EncryptorEngine.hpp     # Data channel encrypt pipeline
|   +-- KeyStore.hpp            # NVS-backed key storage (16 slots)
|   +-- OverheadTracker.hpp     # CCOUNT cycle counter measurement
+-- scripts/
|   +-- serial_checkout.py      # Automated checkout (40 checks, 11 groups)
+-- src/
|   +-- main.cpp                # Application entry, FreeRTOS task, WS2812 driver
|   +-- CommandDeck.cpp         # Command dispatch and response builder
|   +-- EncryptorEngine.cpp     # CRC validate, encrypt, SLIP encode, transmit
|   +-- KeyStore.cpp            # NVS key management (read/write/erase/bitmap)
+-- .claude_docs/               # (gitignored) Dev reference material
    +-- PORTING_NOTES.md        # Implementation history and ESP32-S3 constraints
```
