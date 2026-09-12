# STM32 Encryptor Design

Detailed design for the `stm32_encryptor_demo` firmware application running on the
NUCLEO-L476RG. Covers encryption pipeline, dual UART architecture, serial protocol,
key management, task model, and STM32-specific constraints. The same application
builds for the NUCLEO-F767ZI; [Board Differences](#board-differences) at the end
lists what the board description changes.

---

## Overview

The encryptor is a unidirectional encryption passthrough device with flash-backed
key management. Plaintext bytes arrive on a data channel, are encrypted with
AES-256-GCM, and transmitted back as a SLIP-framed ciphertext packet. A separate
command channel provides key provisioning, diagnostics, and overhead measurement.

**Target hardware:** NUCLEO-L476RG (STM32L476RG, ARM Cortex-M4 @ 80 MHz,
1 MB flash, 96 KB SRAM)

**Encryption:** AES-256-GCM (software implementation, standard 8-bit CHAR_BIT)

**Framing:** SLIP (RFC 1055) on both channels

**Language:** C++20 (arm-none-eabi-gcc; the STM32 toolchain caps at C++20)

---

## Channel Architecture

Two independent UART channels, each with a distinct role:

| Channel | UART   | Connector                       | Baud   | Role                                      |
| ------- | ------ | ------------------------------- | ------ | ----------------------------------------- |
| Data    | USART1 | FTDI FT232RL (external adapter) | 115200 | Plaintext in, ciphertext out              |
| Command | USART2 | ST-Link VCP (USB-C)             | 115200 | Key management, mode control, diagnostics |

**Why two channels:** The data path is high-throughput and continuous. The command
path is low-frequency and administrative. Separating them prevents command
processing from interfering with encryption throughput, and allows the command
channel to operate even when the data channel is saturated.

**Contrast with Arduino/C2000:** Those targets have a single UART and multiplex
data and commands on the same port with a one-byte channel prefix inside every
SLIP frame. The L476RG has two independent UARTs, so no prefix is needed there.
The F767ZI build takes the single-UART shape: both channels ride the ST-Link
VCP and every frame carries the prefix (0x00 data, 0x01 command). The prefix
size is a board constant that feeds `EncryptorSizing`, so the same engine and
command deck serve both boards; the prefix is transport framing and stays
outside the CRC.

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
`encryption_mcu` library. Standard 8-bit CHAR_BIT on ARM Cortex-M4.

### Performance

Software AES-256-GCM on Cortex-M4 @ 80 MHz achieves ~2-4 MB/s throughput. At
115200 baud (~11.5 KB/s), the UART is the bottleneck, not the crypto.

### Key Selection Modes

| Mode   | Behavior                                                      |
| ------ | ------------------------------------------------------------- |
| RANDOM | Counter-based rotation through populated key slots per packet |
| LOCKED | Use a single fixed slot for all packets                       |

Mode is controlled via the KEY_LOCK (0x10) and KEY_UNLOCK (0x11) commands.

### Test Key

Sequential bytes `0x00..0x1F` (same as Arduino/Pico/ESP32/C2000 encryptors)
for cross-platform verification. Provisioned to flash slot 0 on first boot
if the key store is empty.

---

## Key Store

### Flash-Backed Storage (Page 510)

Keys are stored on a dedicated flash page in the STM32L476's bank 2. The page
is not part of the application image.

```
Page 510 (0x080FF000 - 0x080FF7FF):  Key store (2048 bytes)
  Slot  0: bytes 0x000-0x01F  (32 bytes)
  Slot  1: bytes 0x020-0x03F  (32 bytes)
  ...
  Slot 15: bytes 0x1E0-0x1FF  (32 bytes)
  Unused:  bytes 0x200-0x7FF  (1536 bytes, reserved)
```

16 slots x 32 bytes = 512 bytes used out of 2048. A 512-byte RAM cache provides
RT-safe reads during the encryption hot path.

**Slot detection:** Empty slots read as 0xFF (flash erased state). A bitmap
scan at boot determines which slots are populated.

**Write strategy:** STM32L4 flash writes are 8 bytes (double-word) at a time.
If the target slot is empty, write directly. If populated, read all slots to
RAM, erase the page, and rewrite all slots with the updated key.

**Write performance:** ~1 ms per double-word write, ~25 ms per page erase.

**Endurance:** 10,000 write cycles per page.

### Comparison with Arduino Key Store

| Property          | STM32 (Flash Page 510)  | Arduino (EEPROM)    |
| ----------------- | ----------------------- | ------------------- |
| Capacity          | 16 slots (512 B)        | 4 slots (128 B)     |
| Write granularity | 8 bytes (double-word)   | 1 byte              |
| Erase required    | Yes (full page erase)   | No                  |
| Write time        | ~1 ms/double-word       | 3.3 ms/byte         |
| Endurance         | 10K cycles/page         | 100K cycles/byte    |
| RAM cache         | Full slot cache (512 B) | None (read-through) |

---

## Task Model

### McuExecutive (100 Hz)

The firmware uses a cooperative scheduler with five tasks. Two execution modes
are supported (selected at compile time via `APEX_USE_FREERTOS`):

| Mode                 | Tick Source        | Description                            |
| -------------------- | ------------------ | -------------------------------------- |
| Bare-metal (default) | Stm32SysTickSource | SysTick interrupt + WFI                |
| FreeRTOS             | FreeRtosTickSource | vTaskDelayUntil inside a FreeRTOS task |

Task configuration is identical in both modes:

```
profilerStartTask:  100 Hz  priority=127   (DWT cycle start)
ledBlinkTask:         2 Hz  priority=0     (freqN=1, freqD=50)
dataChannelTask:    100 Hz  priority=0     (SLIP decode + encrypt + transmit)
commandTask:         20 Hz  priority=0     (freqN=1, freqD=5)
profilerEndTask:    100 Hz  priority=-128  (DWT cycle end)
```

The profiler tasks bracket the useful work at the highest and lowest priorities
to measure per-tick CPU overhead via the DWT cycle counter.

### Initialization Sequence

```
main():
  HAL_Init()                # HAL timebase (SysTick)
  SystemClock_Config()      # MSI + PLL -> 80 MHz
  GPIO_Init()               # LED PA5
  tracker.enableDwt()       # DWT cycle counter
  Startup blinks (3x)      # Visual confirmation
  dataUart.init(115200)     # USART1 (FTDI) 8N1, interrupt-driven
  cmdUart.init(115200)      # USART2 (VCP) 8N1, interrupt-driven
  keyStore.init()           # Scan flash page 510 for populated slots
  if store empty:
    provision test key      # Write 0x00..0x1F to slot 0
  engine.loadActiveKey()    # Load key from store into encrypt engine
  registerSchedulerTasks()  # 5 tasks into McuExecutive
  exec.init() + exec.run()  # Blocks forever (cooperative loop)
```

In FreeRTOS mode, the executive runs inside a dedicated FreeRTOS task (2 KB
stack, priority 3). The FreeRTOS scheduler is started after task creation.

### Interrupt Service Routines

| Vector            | Handler                   | Purpose                       |
| ----------------- | ------------------------- | ----------------------------- |
| USART1_IRQHandler | dataUart.irqHandler()     | FTDI data channel RX/TX       |
| USART2_IRQHandler | cmdUart.irqHandler()      | VCP command channel RX/TX     |
| SysTick_Handler   | HAL_IncTick + tick source | HAL timebase + scheduler tick |

---

## Overhead Measurement

The DWT cycle counter (DWT->CYCCNT) runs at core clock speed (80 MHz). The
profiler start and end tasks sample CYCCNT at the boundaries of each scheduler
tick to measure per-tick CPU cost.

**Budget:** At 100 Hz, each tick has 800,000 cycles. An idle tick costs
~600-700 cycles (under 0.1% of budget) in both execution modes; the
checkout's one-second idle window sees a peak of ~2,850 cycles when the
command poll coincides with a tick. A tick that services a KEY_STORE_ERASE
or a populated-slot rewrite blocks for the ~25 ms page erase and shows up
in the OVERHEAD max as ~1.8 M cycles (over two tick budgets), which is why
flash operations are confined to the command channel and marked NOT
RT-safe.

**Fast-forward mode:** When enabled via the FASTFORWARD command, the executive
skips the wait-for-tick delay and runs tasks back-to-back. This reveals the
maximum achievable scheduler rate: ~608 cycles per tick at minimum in both
modes, about 130 kHz at 80 MHz (the checkout reports the rate from the last
sample, so a single run prints anywhere from ~100 to ~130 kHz).

---

## Board Differences

The board description under `inc/boards/` supplies every board-specific
value; the pipeline, protocol, key-store semantics, and checkout script are
the same on both boards.

| Item                          | NUCLEO-L476RG                                             | NUCLEO-F767ZI                                                                                  |
| ----------------------------- | --------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Core / SYSCLK                 | Cortex-M4F, 80 MHz (MSI 4 MHz + PLL)                      | Cortex-M7, 216 MHz (HSE bypass 8 MHz + PLL, over-drive, 7 wait states)                         |
| Caches                        | ART accelerator (HAL conf)                                | Instruction cache on; data cache off so flash reads after key-store writes need no maintenance |
| Channels                      | USART1 PA9/PA10 (FTDI data), USART2 PA2/PA3 (VCP command) | USART3 PD8/PD9 (VCP), both channels, prefix byte                                               |
| Tasks                         | dataChannelTask 100 Hz + commandTask 20 Hz                | channelTask 100 Hz (decode + route)                                                            |
| Heartbeat                     | LD2, PA5                                                  | LD1, PB0                                                                                       |
| Key store                     | Page 510, 2 KB, ~25 ms erase                              | Sector 11, 256 KB single-bank (128 KB dual-bank), ~0.93 s erase                                |
| Flash programming             | 64-bit double-word                                        | 32-bit word (double-word needs external Vpp)                                                   |
| Tick budget (100 Hz)          | 800,000 cycles                                            | 2,160,000 cycles                                                                               |
| Idle tick (measured)          | ~600-700 cycles                                           | ~430-450 cycles                                                                                |
| Fast-forward floor (measured) | ~608 cycles (~130 kHz)                                    | ~432 cycles (~500 kHz)                                                                         |
| DWT                           | enable CYCCNT                                             | unlock (LAR) then enable CYCCNT                                                                |
| Flash / RAM, bare-metal       | 23,344 B / 7,472 B                                        | 20,624 B / 7,492 B                                                                             |
| Flash / RAM, FreeRTOS         | 26,664 B / 15,976 B                                       | 23,888 B / 15,992 B                                                                            |

The sector erase shows up in the OVERHEAD max as ~200.9 M cycles on the
F767ZI, the same NOT-RT-safe command path that costs ~1.8 M cycles on the
L476; the checkout's `--timeout` covers it. The FreeRTOS port directory is
`ARM_CM4F` on the L476 and `ARM_CM7/r0p1` on the F767.

---

## File Layout

```
stm32_encryptor_demo/
+-- CMakeLists.txt              # Build config (apex_add_firmware, APEX_USE_FREERTOS option)
+-- release.mk                  # Release manifest (make release APP=...)
+-- STM32L476RG.ld              # Linker script, L476RG
+-- STM32F767ZI.ld              # Linker script, F767ZI
+-- docs/
|   +-- ENCRYPTOR_DESIGN.md     # This document
|   +-- FREERTOS_NOTES.md       # FreeRTOS architecture and design notes
|   +-- HOW_TO_RUN.md           # Build, flash, verify steps
|   +-- MEMORY_MAP.md           # Flash/RAM layout, section placement
|   +-- NUCLEO_L476RG_PINOUT.md # Board pinout, headers, LEDs
|   +-- SH_U09C5_PINOUT.md     # FTDI adapter pinout
+-- inc/
|   +-- boards/                 # Board descriptions (Board.hpp selects one)
|   |   +-- nucleo_l476rg.hpp
|   |   +-- nucleo_f767zi.hpp
|   +-- stm32l4xx_hal_conf.h    # HAL config, L4 (GPIO, RCC, PWR, Flash, UART, DMA)
|   +-- stm32f7xx_hal_conf.h    # HAL config, F7 (same module set)
|   +-- FreeRTOSConfig.h        # FreeRTOS kernel configuration (L476RG)
|   +-- CommandDeck.hpp         # Command channel handler (14 opcodes)
|   +-- EncryptorCommon.hpp     # Shared types, sizing template, protocol enums
|   +-- EncryptorConfig.hpp     # STM32-specific sizing (256B, 16 slots)
|   +-- EncryptorEngine.hpp     # Data channel encrypt pipeline
|   +-- KeyStore.hpp            # Flash-backed key storage (16 slots, page 510)
|   +-- OverheadTracker.hpp     # DWT cycle counter measurement
+-- scripts/
|   +-- serial_checkout.py      # Automated checkout (40 checks, 11 groups)
+-- src/
|   +-- main.cpp                # Application entry, task registration, ISRs
|   +-- CommandDeck.cpp         # Command dispatch and response builder
|   +-- EncryptorEngine.cpp     # CRC validate, encrypt, SLIP encode, transmit
|   +-- KeyStore.cpp            # Flash page management (read/write/erase/bitmap)
|   +-- cxx_stubs.cpp           # C++ runtime stubs (no exceptions/RTTI on bare-metal)
```
