# ATmega328P Memory Map

Memory layout and budget analysis for the `arduino_encryptor_demo` firmware running on
the Arduino Uno R3. The ATmega328P has three separate address spaces: program
flash, SRAM, and EEPROM.

---

## Memory Regions

| Region        | Size  | Address Range   | Volatility   | Access                             |
| ------------- | ----- | --------------- | ------------ | ---------------------------------- |
| Program Flash | 32 KB | 0x0000 - 0x7FFF | Non-volatile | Read (execute), page-write via SPM |
| SRAM          | 2 KB  | 0x0100 - 0x08FF | Volatile     | Read/write, single-cycle           |
| EEPROM        | 1 KB  | 0x000 - 0x3FF   | Non-volatile | Read/write, byte-granular          |
| I/O Registers | 224 B | 0x0020 - 0x00FF | Volatile     | Read/write (peripheral control)    |
| GP Registers  | 32 B  | 0x0000 - 0x001F | Volatile     | r0 - r31 (CPU working registers)   |

---

## Program Flash (32 KB)

The flash stores executable code and constant data. Organized in 128-byte pages
(256 pages total). Can only be written via the SPM (Store Program Memory)
instruction, which operates on full pages.

```
0x0000 +---------------------------+
       | Interrupt Vector Table    |  52 bytes (26 vectors x 2 words)
0x0034 +---------------------------+
       | Application Code (.text)  |  Variable
       | Constant Data (.rodata)   |  String literals, lookup tables
       | Init Data (.data image)   |  Copied to SRAM at startup
       +---------------------------+
       | (free)                    |
       +---------------------------+
0x7E00 | Optiboot Bootloader       |  512 bytes (0.5 KB)
0x7FFF +---------------------------+
```

**Bootloader:** Optiboot occupies the top 512 bytes. The BOOTRST fuse directs
reset to 0x7E00. The bootloader listens for STK500 protocol on USART0, then
jumps to 0x0000 if no upload is detected. Available application space: 31.5 KB.

**Flash endurance:** 10,000 write/erase cycles per page. Flash is not used for
runtime data storage in this application (EEPROM is used for keys instead).

### Flash Budget Estimate

| Component                      | Estimated Size | Notes                                           |
| ------------------------------ | -------------- | ----------------------------------------------- |
| Bootloader (Optiboot)          | 512 B          | Fixed, cannot reclaim                           |
| Vector table                   | 52 B           | 26 interrupt vectors                            |
| AES-256-GCM (S-box + GHASH)    | ~6-8 KB        | Dominant cost: 256-byte S-box, GF(2^128) tables |
| SLIP framing                   | ~400 B         | Encode + decode                                 |
| CRC-16/XMODEM                  | ~200 B         | Bitwise (no lookup table to save SRAM)          |
| UART driver (interrupt-driven) | ~500 B         | ISR + ring buffer logic                         |
| LiteExecutive + SchedulerLite  | ~800 B         | Cooperative scheduler, tick source              |
| Command handler                | ~1.5 KB        | 14 opcodes, dispatch, response builder          |
| Encrypt pipeline               | ~1 KB          | Frame parse, CRC check, encrypt, output build   |
| Key store (EEPROM driver)      | ~400 B         | Read/write/erase, bitmap scan                   |
| Main + init                    | ~300 B         | Peripheral init, task registration              |
| C runtime (crt0, .data copy)   | ~200 B         | Startup code                                    |
| **Estimated total**            | **~12-14 KB**  | **38-44% of 31.5 KB available**                 |

Flash is not the binding constraint. SRAM is.

---

## SRAM (2 KB)

All runtime state lives in 2048 bytes of SRAM. The Harvard architecture means
code does not consume SRAM -- only variables, buffers, and the stack.

```
0x0100 +---------------------------+  RAMSTART
       | .data (initialized vars)  |  Copied from flash at startup
       +---------------------------+
       | .bss (zero-init vars)     |  Cleared to zero at startup
       +---------------------------+
       | (free / heap)             |  Grows upward (not used -- no malloc)
       |                           |
       |                           |
       | (stack)                   |  Grows downward from RAMEND
0x08FF +---------------------------+  RAMEND (SP init)
```

**Stack:** Grows downward from 0x08FF. The stack holds return addresses (2 bytes
each on ATmega328P), ISR context saves (~20 bytes per nested interrupt), and
local variables. A typical call depth of 8-10 frames with ISR nesting needs
~150-200 bytes of stack.

**Heap:** Not used. No dynamic allocation (no `malloc`, no `new`). All buffers
are statically allocated.

### SRAM Budget (Actual -- Post-Optimization)

Final firmware: **Program 20,244 B (61.8%), Data 1,280 B (62.5%).**

See `SRAM_OPTIMIZATION_PASS_1.md` and `SRAM_OPTIMIZATION_PASS_2.md` for the
full optimization history (3,533 -> 1,280 bytes across 2 passes + UART fixes).

| Symbol                               | Size        | % of SRAM | Notes                                     |
| ------------------------------------ | ----------- | --------- | ----------------------------------------- |
| engine (EncryptorEngine)             | 307 B       | 15.0%     | Crypto state + buffers (MAX_PLAINTEXT=48) |
| exec (LiteExecutive<8, uint32_t>)    | 144 B       | 7.0%      | 8-task table + uint32_t counters          |
| uart (AvrUart<64, 32>)               | 128 B       | 6.3%      | 64 RX + 32 TX rings + state               |
| commandDeck                          | 121 B       | 5.9%      | Response + SLIP encode buffers            |
| decodeBuf                            | 51 B        | 2.5%      | SLIP decode output                        |
| eeprom                               | 46 B        | 2.2%      | Flash interface state                     |
| tracker                              | 21 B        | 1.0%      | Timer0 overhead measurement               |
| tickSource                           | 10 B        | 0.5%      | Timer1 tick source                        |
| Other (slipState, slipCfg, ISR ptrs) | 13 B        | 0.6%      |                                           |
| vtables + compiler-generated         | ~339 B      | 16.6%     | Virtual dispatch overhead                 |
| **Total (.data + .bss)**             | **1,280 B** | **62.5%** |                                           |
| **Stack (free)**                     | **768 B**   | **37.5%** |                                           |

### Key Reductions from STM32 Variant

1. **MAX_PLAINTEXT_SIZE: 256 -> 48 bytes.** Largest single impact. Cascades
   through SLIP buffers, ciphertext work buffer, and encode buffer.

2. **Key slots: 16 -> 4.** RAM cache eliminated entirely (read-through from
   EEPROM). Only bitmap + count remain in RAM (2 bytes).

3. **Single UART.** Eliminates the second set of RX/TX ring buffers.
   Command and data share one channel via 1-byte prefix multiplexing.

4. **Template LiteExecutive<8, uint32_t>.** Task table 32 -> 8 slots (-288 B).
   Counter type uint64_t -> uint32_t saves 12 bytes of SRAM and ~2 KB of
   flash (eliminates avr-gcc 64-bit software arithmetic routines).

5. **PROGMEM for constant data.** AES S-box (256 B) and test key (32 B)
   stored in flash via `PROGMEM` + `pgm_read_byte()`.

### Stack Safety

With 768 bytes free for the stack, margin is comfortable. Verified by running
the full 36-check serial checkout script including sustained encryption stress
tests (20 consecutive packets) without corruption.

---

## EEPROM (1 KB)

Byte-addressable non-volatile storage. Each byte can be written independently
without erasing a page (unlike flash). The EEPROM replaces the STM32's flash
page 510 for key storage.

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

**Key slot detection:** An empty slot reads as 0xFF (EEPROM erased state, same
convention as STM32 flash). Bitmap scanning at boot checks each slot.

**Write characteristics:**

- Write time: 3.3 ms per byte (hardware timed, CPU blocks or polls EEPE bit)
- 32-byte key write: ~106 ms (sequential byte writes)
- No page erase needed (individual byte writes always succeed)
- Endurance: 100,000 write cycles per byte

**Advantage over STM32 flash:** Byte-granular writes eliminate the
read-all/erase-page/rewrite-all cycle needed on STM32 when overwriting a
populated key slot. Individual slot overwrites are straightforward.

**4 slots x 32 bytes = 128 bytes used.** 896 bytes remain for future metadata
(timestamps, counters, configuration).

---

## Comparison: ATmega328P vs STM32L476RG

| Parameter            | ATmega328P          | STM32L476RG               | Ratio       |
| -------------------- | ------------------- | ------------------------- | ----------- |
| Architecture         | 8-bit AVR           | 32-bit ARM Cortex-M4      | -           |
| Clock                | 16 MHz              | 80 MHz                    | 5x          |
| Program Memory       | 32 KB flash         | 1 MB flash                | 32x         |
| RAM                  | 2 KB SRAM           | 96 KB SRAM1 + 32 KB SRAM2 | 64x         |
| Non-volatile Storage | 1 KB EEPROM         | Flash page (2 KB each)    | 2x per page |
| NV Write Granularity | 1 byte              | 8 bytes (double-word)     | 8x finer    |
| NV Write Endurance   | 100K cycles/byte    | 10K cycles/page           | 10x more    |
| NV Erase Required    | No (byte-write)     | Yes (page erase)          | Simpler     |
| Key Slots            | 4                   | 16                        | 4x          |
| Max Plaintext        | 48 bytes            | 256 bytes                 | 5.3x        |
| UART Channels        | 1 (shared with USB) | 2 (independent)           | 2x          |
| Hardware Crypto      | None                | AES peripheral (future)   | -           |
| Hardware RNG         | None                | TRNG                      | -           |
| FPU                  | None                | Single-precision          | -           |
| Operating Voltage    | 5V                  | 1.71-3.6V                 | Different   |
