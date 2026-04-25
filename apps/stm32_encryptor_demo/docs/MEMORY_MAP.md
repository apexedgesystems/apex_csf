# STM32L476RG Memory Map

Memory layout and budget for the `stm32_encryptor_demo` firmware running on the
NUCLEO-L476RG. The STM32L476RG has unified flash, two SRAM banks, and no EEPROM
(flash pages serve as non-volatile storage).

---

## Memory Regions

| Region       | Size   | Address Range             | Volatility   | Access                   |
| ------------ | ------ | ------------------------- | ------------ | ------------------------ |
| Flash Bank 1 | 512 KB | 0x0800_0000 - 0x0807_FFFF | Non-volatile | XIP, page-write via HAL  |
| Flash Bank 2 | 512 KB | 0x0808_0000 - 0x080F_FFFF | Non-volatile | XIP, page-write via HAL  |
| SRAM1        | 96 KB  | 0x2000_0000 - 0x2001_7FFF | Volatile     | Read/write, single-cycle |
| SRAM2        | 32 KB  | 0x1000_0000 - 0x1000_7FFF | Volatile     | Read/write, single-cycle |
| Peripherals  | 512 MB | 0x4000_0000 - 0x5FFF_FFFF | -            | Memory-mapped I/O        |

---

## Flash (1 MB)

Dual-bank flash with 2 KB pages (256 pages per bank, 512 pages total). Code
executes directly from flash (XIP -- execute in place). Write granularity is
8 bytes (double-word). Erasing is per-page (2 KB at a time, ~25 ms).

```
Bank 1 (0x0800_0000):
0x0800_0000 +---------------------------+
            | Vector Table              |  ~0.4 KB (98 vectors x 4 bytes)
0x0800_0188 +---------------------------+
            | Application Code (.text)  |  Variable
            | Constant Data (.rodata)   |  AES S-box, lookup tables
            | Init Data (.data image)   |  Copied to SRAM at startup
            +---------------------------+
            | (free)                    |
0x0807_FFFF +---------------------------+

Bank 2 (0x0808_0000):
0x0808_0000 +---------------------------+
            | (free)                    |  Entirely unused by application
            |                           |
            +---------------------------+
0x080F_F000 | Page 510: Key Store       |  2048 bytes (512 used, 1536 reserved)
0x080F_F800 +---------------------------+
            | Page 511: Reserved        |  2048 bytes (unused)
0x080F_FFFF +---------------------------+
```

**Key store (page 510, 0x080F_F000):** 16 slots x 32 bytes = 512 bytes used.
Remaining 1,536 bytes reserved for future metadata. An empty slot reads as 0xFF
(flash erased state). Overwriting a populated slot requires full page erase then
rewriting all slots.

**Flash endurance:** 10,000 write/erase cycles per page.

### Flash Usage

| Variant    | Code + Data | Percent of 1 MB | Pages Used |
| ---------- | ----------- | --------------- | ---------- |
| Bare-metal | 22,780 B    | 2.17%           | 12         |
| FreeRTOS   | 26,428 B    | 2.52%           | 13         |

Breakdown by component (FreeRTOS variant, from linker map):

| Component                                      | Approximate Size | Notes                                    |
| ---------------------------------------------- | ---------------- | ---------------------------------------- |
| Vector table                                   | 392 B            | 98 Cortex-M4 vectors                     |
| HAL drivers (GPIO, RCC, UART, Flash, DMA, PWR) | ~8 KB            | ST HAL library                           |
| FreeRTOS kernel                                | ~3.4 KB          | tasks, queue, list, timers, heap_4, port |
| AES-256-GCM (S-box + GHASH)                    | ~6 KB            | Software crypto                          |
| SLIP framing                                   | ~1 KB            | Encode + decode (shared library)         |
| CRC-16/XMODEM                                  | ~0.5 KB          | Bitwise implementation                   |
| EncryptorEngine                                | ~1.5 KB          | Data channel pipeline                    |
| CommandDeck                                    | ~2 KB            | 14 command handlers                      |
| KeyStore                                       | ~1 KB            | Flash page management                    |
| McuExecutive + McuScheduler                  | ~1.5 KB          | Cooperative scheduler                    |
| Main + init + stubs                            | ~1 KB            | Boot, UART init, task registration       |
| **Total**                                      | **~26.4 KB**     | **Matches linker output**                |

---

## SRAM1 (96 KB)

Main RAM for stack, heap, and all runtime data. Code does not reside in SRAM
(executes from flash via XIP).

### Bare-Metal Layout

```
0x2000_0000 +---------------------------+  SRAM1 start
            | .data (initialized vars)  |  ~108 bytes (from flash)
            +---------------------------+
            | .bss (zero-init vars)     |  ~5,200 bytes
            +---------------------------+
            |   UART1 RX ring (512 B)   |
            |   UART1 TX ring (512 B)   |
            |   UART2 RX ring (128 B)   |
            |   UART2 TX ring (128 B)   |
            |   SLIP decode buf (258 B) |
            |   SLIP cmd buf (64 B)     |
            |   Plaintext work (256 B)  |
            |   Ciphertext work (285 B) |
            |   SLIP encode work (572 B)|
            |   Key cache (512 B)       |
            |   AES state (~60 B)       |
            |   Executive state (~300 B)|
            |   Statistics, flags, etc. |
            +---------------------------+
            | (free)                    |  ~88.5 KB
            |                           |
            |                           |
            | (stack)                   |  ~2 KB (grows down from top)
0x2001_7FFF +---------------------------+  SRAM1 end
```

### FreeRTOS Layout

```
0x2000_0000 +---------------------------+  SRAM1 start
            | .data (initialized vars)  |  ~108 bytes
            +---------------------------+
            | .bss (zero-init vars)     |  ~15,800 bytes
            +---------------------------+
            |   FreeRTOS heap (8 KB)    |  heap_4 allocator
            |     Executive task (2 KB) |  (from heap)
            |     Idle task (512 B)     |  (from heap)
            |     TCBs (~256 B)         |  (from heap)
            |   FreeRTOS kernel (~1 KB) |  Ready lists, tick count, etc.
            |   Application buffers     |  Same as bare-metal (~3.2 KB)
            +---------------------------+
            | (free)                    |  ~80 KB
            |                           |
            | (ISR stack)               |  ~2 KB (MSP, linker script)
0x2001_7FFF +---------------------------+  SRAM1 end
```

### SRAM Usage

| Variant    | Used     | Percent of 96 KB | Free     |
| ---------- | -------- | ---------------- | -------- |
| Bare-metal | 7,456 B  | 7.58%            | 90,736 B |
| FreeRTOS   | 15,976 B | 16.25%           | 82,216 B |

### Buffer Allocation Detail

| Buffer             | Size        | Purpose                                          |
| ------------------ | ----------- | ------------------------------------------------ |
| UART1 RX ring      | 512 B       | Interrupt-driven data channel input              |
| UART1 TX ring      | 512 B       | Interrupt-driven data channel output             |
| UART2 RX ring      | 128 B       | Interrupt-driven command channel input           |
| UART2 TX ring      | 128 B       | Interrupt-driven command channel output          |
| SLIP decode (data) | 258 B       | Data channel frame assembly (MAX_INPUT_FRAME)    |
| SLIP decode (cmd)  | 64 B        | Command channel frame assembly                   |
| Plaintext work     | 256 B       | CRC-validated plaintext (after CRC strip)        |
| Ciphertext work    | 285 B       | Output frame assembly (1 + 12 + 256 + 16)        |
| SLIP encode work   | 572 B       | Worst-case SLIP encoding of output (2 x 285 + 2) |
| Key cache          | 512 B       | RAM copy of flash key store (16 x 32)            |
| **Total buffers**  | **~3.3 KB** |                                                  |

---

## SRAM2 (32 KB)

Secondary RAM bank at 0x1000_0000. Currently unused by the encryptor firmware.
Available for future use (DMA buffers, secondary data structures, etc.).

SRAM2 can be hardware write-protected via the SYSCFG_SWPR register (useful for
protecting critical data from errant writes). It is also retained in some
low-power modes where SRAM1 is not.

---

## Comparison: STM32L476RG vs ATmega328P

| Parameter            | STM32L476RG             | ATmega328P           | Ratio       |
| -------------------- | ----------------------- | -------------------- | ----------- |
| Architecture         | 32-bit ARM Cortex-M4    | 8-bit AVR            | -           |
| Clock                | 80 MHz                  | 16 MHz               | 5x          |
| Program Memory       | 1 MB flash              | 32 KB flash          | 32x         |
| RAM                  | 96 KB + 32 KB           | 2 KB                 | 64x         |
| Non-volatile Storage | Flash page (2 KB each)  | 1 KB EEPROM          | 2x per page |
| NV Write Granularity | 8 bytes (double-word)   | 1 byte               | 8x finer    |
| NV Write Endurance   | 10K cycles/page         | 100K cycles/byte     | 10x more    |
| NV Erase Required    | Yes (page erase)        | No (byte-write)      | Simpler     |
| Key Slots            | 16                      | 4                    | 4x          |
| Max Plaintext        | 256 bytes               | 48 bytes             | 5.3x        |
| UART Channels        | 2 (independent)         | 1 (shared with USB)  | 2x          |
| Hardware Crypto      | AES peripheral (future) | None                 | -           |
| Hardware RNG         | TRNG                    | None                 | -           |
| FPU                  | Single-precision        | None                 | -           |
| Operating Voltage    | 1.71-3.6V               | 5V                   | Different   |
| Encryptor Flash Used | 26.4 KB (2.5%)          | ~12-14 KB (est. 40%) | ~2x         |
| Encryptor RAM Used   | 7.5-16 KB (8-16%)       | ~1.3 KB (est. 62%)   | ~6-12x      |
