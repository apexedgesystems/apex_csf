# Arduino Encryptor Design

Detailed design for the `arduino_encryptor_demo` firmware application running on the
Arduino Uno R3. Covers encryption pipeline, channel multiplexing, serial protocol,
task model, and ATmega328P-specific constraints.

---

## Overview

The encryptor is a unidirectional encryption passthrough device with EEPROM-backed
key management. Plaintext bytes arrive on a serial channel, are encrypted with
AES-256-GCM, and transmitted back as a SLIP-framed ciphertext packet. A command
channel shares the same UART for key provisioning, diagnostics, and overhead
measurement.

**Target hardware:** Arduino Uno R3 (ATmega328P, AVR 8-bit @ 16 MHz,
32 KB flash, 2 KB SRAM, 1 KB EEPROM)

**Encryption:** AES-256-GCM (software implementation, standard 8-bit CHAR_BIT)

**Framing:** SLIP (RFC 1055) with channel prefix byte for multiplexing

**Language:** C++17 (avr-gcc)

---

## Channel Architecture

Single UART channel, multiplexed by a 1-byte channel prefix inside each SLIP frame:

| Channel | Prefix | UART   | Connector                 | Baud   | Role                         |
| ------- | ------ | ------ | ------------------------- | ------ | ---------------------------- |
| Data    | 0x00   | USART0 | USB-B (ATmega16U2 bridge) | 115200 | Encrypt pipeline             |
| Command | 0x01   | USART0 | USB-B (ATmega16U2 bridge) | 115200 | Key management + diagnostics |

**Why single channel:** The ATmega328P has one hardware USART, which is permanently
connected to the ATmega16U2 USB-to-UART bridge on the Uno R3. There is no second
independent UART available. Data and command channels share the same physical port
using a SLIP-level channel prefix byte. The main loop decodes SLIP frames, reads the
first byte to determine the channel, strips the prefix, and dispatches the remaining
payload to `EncryptorEngine` (data) or `CommandDeck` (command).

**Contrast with STM32:** The STM32 encryptor uses two independent UARTs (USART2 for
data, USART3 for commands) with no prefix byte. The Arduino version adds the 1-byte
channel prefix to compensate for having a single port.

---

## Serial Protocol

### SLIP Framing

All communication uses SLIP (RFC 1055) framing with leading and trailing END
delimiters (0xC0). Each SLIP-decoded frame begins with a channel prefix byte:

```
Wire format:
  [END] [channel:1] [payload...] [END]

After SLIP decode:
  [channel:1] [payload...]

Channel byte is stripped before dispatch:
  Data (0x00):  payload = plaintext + CRC-16
  Command (0x01): payload = opcode + [args] + CRC-16
```

### Data Channel Protocol

| Operation | Request (inside SLIP)             | Response (inside SLIP)                                  |
| --------- | --------------------------------- | ------------------------------------------------------- |
| Encrypt   | `[0x00] [plaintext:N] [CRC-16:2]` | `[0x00] [key_idx:1] [nonce:12] [ciphertext:N] [tag:16]` |

CRC-16/XMODEM covers the plaintext only (channel prefix is transport-level framing).
Maximum plaintext size: 48 bytes.

### Encrypt Output Format

```
Response payload (after channel prefix):
  +-----------+---------+----------------+----------+
  | key_index | nonce   | ciphertext (N) | auth_tag |
  +-----------+---------+----------------+----------+
  | 1 byte    | 12 bytes| N bytes        | 16 bytes |
```

- **key_index**: Slot index of the key used for this encryption
- **nonce**: 96-bit nonce used for this packet (big-endian counter)
- **ciphertext**: AES-256-GCM encrypted data (same length as plaintext)
- **auth_tag**: 128-bit GCM authentication tag

The nonce auto-increments after each successful encryption.

### Command Channel Protocol

| Opcode | Name             | Request Payload     | Response Payload                                          |
| ------ | ---------------- | ------------------- | --------------------------------------------------------- |
| 0x01   | KEY_STORE_WRITE  | `[slot:1] [key:32]` | (status only)                                             |
| 0x02   | KEY_STORE_READ   | `[slot:1]`          | `[key:32]`                                                |
| 0x03   | KEY_STORE_ERASE  | (none)              | (status only)                                             |
| 0x04   | KEY_STORE_STATUS | (none)              | `[count:1] [bitmap:1]`                                    |
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

Command request wire format: `SLIP( [0x01] [opcode:1] [payload...] [CRC-16:2] )`

Command response wire format: `SLIP( [0x01] [opcode:1] [status:1] [payload...] [CRC-16:2] )`

CRC-16/XMODEM covers opcode + payload (channel prefix is transport-level framing).

### Status Codes

| Code | Name            | Meaning                                     |
| ---- | --------------- | ------------------------------------------- |
| 0x00 | OK              | Success                                     |
| 0x01 | ERR_INVALID_CMD | Unknown opcode                              |
| 0x02 | ERR_BAD_PAYLOAD | Payload length or CRC mismatch              |
| 0x03 | ERR_KEY_SLOT    | Invalid or empty key slot                   |
| 0x04 | ERR_FLASH       | EEPROM read/write failure                   |
| 0x05 | ERR_LOCKED      | Operation not permitted in current key mode |

---

## Encryption

### Algorithm

AES-256-GCM per FIPS 197 + NIST SP 800-38D. Software implementation via
`encryption_lite` library. Standard 8-bit CHAR_BIT on AVR (unlike the C2000 port
which requires 16-bit workarounds).

### Performance

Encryption on the 8-bit AVR is significantly slower than on 32-bit targets due to
the lack of 32-bit ALU operations and hardware crypto. The GF(2^128) multiply used
in GCM's GHASH step is particularly expensive on an 8-bit core.

### Key Selection Modes

| Mode   | Behavior                                      |
| ------ | --------------------------------------------- |
| RANDOM | Rotate through populated key slots per packet |
| LOCKED | Use a single fixed slot for all packets       |

Mode is controlled via the KEY_LOCK (0x10) and KEY_UNLOCK (0x11) commands.

### Test Key

Sequential bytes `0x00..0x1F` (same as STM32/Pico/ESP32/C2000 encryptors)
for cross-platform verification. Stored in PROGMEM and provisioned to EEPROM
slot 0 on first boot if the key store is empty.

---

## Key Store

### EEPROM-Backed Storage

The ATmega328P has 1 KB of byte-addressable EEPROM. Key storage uses the first
128 bytes (4 slots x 32 bytes each). Unlike STM32 flash, EEPROM supports
individual byte writes without page erase.

```
0x000 +---------------------------+
      | Key Slot 0                |  32 bytes (AES-256 key)
0x020 +---------------------------+
      | Key Slot 1                |  32 bytes
0x040 +---------------------------+
      | Key Slot 2                |  32 bytes
0x060 +---------------------------+
      | Key Slot 3                |  32 bytes
0x080 +---------------------------+
      | (reserved / future)       |  896 bytes
0x3FF +---------------------------+
```

**Slot detection:** Empty slots read as 0xFF (EEPROM erased state). A bitmap
scan at boot determines which slots are populated. Only the bitmap and count
are cached in RAM (2 bytes); keys are read directly from EEPROM on demand.

**Write performance:** ~3.3 ms per byte, so a 32-byte key write takes ~106 ms.
Writes are infrequent (provisioning only) and happen outside the real-time loop.

**Endurance:** 100,000 write cycles per byte (10x more than STM32 flash pages).

### Comparison with STM32 Key Store

| Property          | Arduino (EEPROM)    | STM32 (Flash Page 510) |
| ----------------- | ------------------- | ---------------------- |
| Capacity          | 4 slots (128 B)     | 16 slots (512 B)       |
| Write granularity | 1 byte              | 8 bytes (double-word)  |
| Erase required    | No                  | Yes (full page erase)  |
| Write time        | 3.3 ms/byte         | ~10 us/double-word     |
| Endurance         | 100K cycles/byte    | 10K cycles/page        |
| RAM cache         | None (read-through) | Full slot cache        |

---

## Task Model

### McuExecutive (100 Hz)

The firmware uses a cooperative scheduler driven by Timer1 compare-match interrupts
at 100 Hz. Four tasks are registered:

```
profilerStartTask:  100 Hz  priority=127   (Timer0 cycle start)
ledBlinkTask:         2 Hz  priority=0     (freqN=1, freqD=50)
channelTask:        100 Hz  priority=0     (SLIP decode + channel dispatch)
profilerEndTask:    100 Hz  priority=-128  (Timer0 cycle end)
```

The profiler tasks bracket the useful work at the highest and lowest priorities to
measure per-tick CPU overhead via Timer0.

### Timer Allocation

| Timer  | Usage                              | Configuration                           |
| ------ | ---------------------------------- | --------------------------------------- |
| Timer0 | Overhead measurement               | Normal mode, prescaler=64, overflow ISR |
| Timer1 | McuExecutive tick source (100 Hz) | CTC mode, prescaler varies              |
| Timer2 | Unused                             | Available for future PWM or timing      |

### Initialization Sequence

```
main():
  DDRB |= (1 << PB5)    # LED pin as output
  Startup blinks (3x)   # Visual confirmation
  uart.init(115200)      # USART0 8N1, interrupt-driven
  tracker.enableTimer()  # Timer0 for overhead measurement
  keyStore.init()        # Scan EEPROM for populated key slots
  if store empty:
    provision test key   # Copy from PROGMEM to EEPROM slot 0
  engine.loadActiveKey() # Load key from store into encrypt engine
  SLIP config            # maxFrameSize, trailing END, drop-until-END
  registerTasks()        # 4 tasks into McuExecutive
  exec.init()            # Configure Timer1
  exec.run()             # Blocks forever (cooperative loop)
```

### Interrupt Service Routines

| Vector            | Handler                        | Purpose                                 |
| ----------------- | ------------------------------ | --------------------------------------- |
| USART_RX_vect     | AvrUart::rxIsr()               | Push received byte into 64-byte RX ring |
| USART_UDRE_vect   | AvrUart::udreIsr()             | Pull next byte from 32-byte TX ring     |
| TIMER1_COMPA_vect | AvrTimerTickSource::timerIsr() | Signal 100 Hz scheduler tick            |
| TIMER0_OVF_vect   | OverheadTracker::overflowIsr() | Extend 8-bit Timer0 range               |

---

## Overhead Measurement

Timer0 runs at F_CPU/64 = 250 kHz (4 us per tick). The 8-bit counter overflows
every 256 ticks (1.024 ms). The OverheadTracker combines TCNT0 reads with a
software overflow counter to measure the elapsed Timer0 ticks between the first
task (profilerStart) and last task (profilerEnd) in each scheduler tick.

**Budget:** At 100 Hz, each tick has 10 ms = 2500 Timer0 ticks. The OVERHEAD
command reports last/min/max/count/budget in Timer0 tick units.

**Fast-forward mode:** When enabled via the FASTFORWARD command, the
McuExecutive skips the wait-for-tick delay and runs tasks back-to-back. This
reveals the maximum achievable scheduler rate, limited by task execution time
rather than the 100 Hz timer.

---

## File Layout

```
arduino_encryptor_demo/
+-- CMakeLists.txt              # Build config (apex_add_firmware)
+-- release.mk                  # Release manifest (make release APP=...)
+-- docs/
|   +-- ENCRYPTOR_DESIGN.md     # This document
|   +-- HOW_TO_RUN.md           # Build, flash, verify steps
|   +-- ARDUINO_UNO_R3_PINOUT.md # Board pinout, headers, LEDs
|   +-- MEMORY_MAP.md           # Flash/SRAM/EEPROM layout, budget analysis
+-- inc/
|   +-- CommandDeck.hpp         # Command channel handler (14 opcodes)
|   +-- EncryptorCommon.hpp     # Shared types, sizing template, protocol enums
|   +-- EncryptorConfig.hpp     # Arduino-specific sizing (48B, 4 slots, 1B prefix)
|   +-- EncryptorEngine.hpp     # Data channel encrypt pipeline
|   +-- KeyStore.hpp            # EEPROM-backed key storage (4 slots)
|   +-- OverheadTracker.hpp     # Timer0 per-tick overhead measurement
+-- scripts/
|   +-- serial_checkout.py      # Automated checkout (36 checks, 11 groups)
+-- src/
|   +-- main.cpp                # Application entry, task registration, ISRs
|   +-- CommandDeck.cpp         # Command dispatch and response builder
|   +-- EncryptorEngine.cpp     # CRC validate, encrypt, SLIP encode, transmit
|   +-- KeyStore.cpp            # EEPROM read/write/erase, bitmap scan
|   +-- cxx_stubs.cpp           # C++ runtime stubs (no exceptions/RTTI on AVR)
```
