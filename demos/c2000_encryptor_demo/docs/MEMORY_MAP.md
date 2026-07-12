# F280049C Memory Map

Memory layout for the `c2000_encryptor_demo` firmware running on the
LAUNCHXL-F280049C. The TMS320F280049C has 256 KB flash (2 banks), 24 KB SRAM,
and a 16-bit word-addressable architecture (`CHAR_BIT == 16`).

---

## Memory Regions

| Region       | Size  | Address Range         | Volatility   | Access                          |
| ------------ | ----- | --------------------- | ------------ | ------------------------------- |
| Flash Bank 0 | 64 KB | 0x08_0000 - 0x08_FFFF | Non-volatile | XIP, sector-erase via driverlib |
| Flash Bank 1 | 64 KB | 0x09_0000 - 0x09_FFFF | Non-volatile | XIP, sector-erase via driverlib |
| RAMM0        | 1 KB  | 0x00_00F6 - 0x00_03FF | Volatile     | Program memory (PAGE 0)         |
| RAMM1        | 1 KB  | 0x00_0400 - 0x00_07F7 | Volatile     | Data memory (PAGE 1)            |
| RAMLS0-4     | 10 KB | 0x00_8000 - 0x00_A7FF | Volatile     | Program memory (PAGE 0)         |
| RAMLS5-7     | 6 KB  | 0x00_A800 - 0x00_BFFF | Volatile     | Data memory (PAGE 1)            |
| RAMGS0-3     | 32 KB | 0x00_C000 - 0x01_3FF7 | Volatile     | Data memory (PAGE 1)            |

---

## Flash (128 KB usable)

Dual-bank flash with 4 KB sectors (16 sectors per bank). Each bank can be
erased independently. Write granularity is 16-bit word. Erasing is per-sector
(4 KB at a time).

```
Bank 0 (0x08_0000):
0x08_0000 +-------------------------------+
          | code_start (2 words)          |  Boot entry -> _c_int00
0x08_0002 +-------------------------------+
          | (sector 0 remainder)          |
0x08_1000 +-------------------------------+
          | .cinit, .TI.ramfunc (load)    |  Init tables, flash functions
0x08_2000 +-------------------------------+
          | .text                         |  Application + driverlib code
          |   main.cpp                    |
          |   driverlib (GPIO, SCI, CAN)  |
          |   AES-256-GCM (S-box, GHASH)  |
          |   C runtime (boot, exit)      |
0x08_4000 +-------------------------------+
          | .econst                       |  Constants (S-box table: 512 B)
0x08_7000 +-------------------------------+
          | (free)                        |
0x08_FFFF +-------------------------------+

Bank 1 (0x09_0000):
0x09_0000 +-------------------------------+
          | (entirely free)               |  Available for key store, logs
0x09_FFFF +-------------------------------+
```

### Flash Usage

| Component                                 | Size      | Notes                                  |
| ----------------------------------------- | --------- | -------------------------------------- |
| Boot + init                               | ~200 B    | `code_start`, `_c_int00`               |
| Application (main.cpp)                    | ~600 B    | Encrypt loop, CAN, SCI dispatch        |
| Driverlib (GPIO, SCI, CAN, SysCtl, Flash) | ~4 KB     | Pre-compiled library                   |
| AES-256-GCM                               | ~3 KB     | S-box (512 B), key expand, GHASH, GCTR |
| C/C++ runtime                             | ~1 KB     | boot, args_main, exit, cxx_stubs       |
| **Total**                                 | **~9 KB** | **~7% of 128 KB**                      |

---

## RAM

The C28x uses a two-page memory model: PAGE 0 for program, PAGE 1 for data.

### PAGE 0 (Program RAM)

```
0x00_8000 +-------------------------------+  RAMLS0
          | .TI.ramfunc (run)             |  Flash_initModule, SysCtl_delay
          |   (~72 bytes)                 |
          +-------------------------------+
          | .cio                          |  C I/O buffers
0x00_8800 +-------------------------------+  RAMLS1-4 (free)
          | (available)                   |
0x00_A7FF +-------------------------------+
```

### PAGE 1 (Data RAM)

```
0x00_0400 +-------------------------------+  RAMM1
          | .stack (768 B)                |  Call stack (grows down)
0x00_07F7 +-------------------------------+

0x00_A800 +-------------------------------+  RAMLS5
          | .ebss (globals)               |
          |   nonce[12]       (24 B)      |  16-bit words
          |   rxBuf[128]      (256 B)     |
          |   ctBuf[128]      (256 B)     |
          |   tagBuf[16]      (32 B)      |
          |   TEST_KEY[32]    (64 B)      |
          |   sciUart instance (~8 B)     |
          |   counters, flags  (~16 B)    |
          +-------------------------------+
          | .esysmem (heap, 256 B)        |
0x00_AFFF +-------------------------------+  RAMLS6-7 (free)

0x00_C000 +-------------------------------+  RAMGS0-3
          | (available, 32 KB)            |
0x01_3FF7 +-------------------------------+
```

### RAM Usage

| Section     | Size        | Location | Notes                         |
| ----------- | ----------- | -------- | ----------------------------- |
| .stack      | 768 B       | RAMM1    | AES encrypt uses ~400 B peak  |
| .ebss       | ~700 B      | RAMLS5   | Globals (buffers, key, state) |
| .esysmem    | 256 B       | RAMLS5   | Heap (unused by application)  |
| .TI.ramfunc | ~72 B       | RAMLS0   | Flash/delay functions         |
| **Total**   | **~1.8 KB** |          | **~7% of 24 KB**              |

---

## C28x Memory Model

The C28x is 16-bit word-addressable:

- `sizeof(char) == 1` but `CHAR_BIT == 16`
- All addresses are word addresses (not byte addresses)
- A `uint16_t` array of 128 elements uses 128 words = 256 bytes of storage
- The AES S-box (256 entries) uses 512 bytes (256 x 16-bit words)

This affects the AES implementation -- all byte operations must be explicitly
masked to 8 bits. See `inc/aes256gcm.h` and ENCRYPTOR_DESIGN.md for details.

---

## Comparison: F280049C vs STM32L476RG

| Parameter       | F280049C          | STM32L476RG          | Ratio  |
| --------------- | ----------------- | -------------------- | ------ |
| Architecture    | 16-bit C28x DSP   | 32-bit ARM Cortex-M4 | -      |
| Clock           | 100 MHz           | 80 MHz               | 1.25x  |
| Flash           | 128 KB (2 banks)  | 1 MB (2 banks)       | 0.125x |
| RAM             | 24 KB             | 128 KB               | 0.19x  |
| Word size       | 16-bit            | 32-bit               | 0.5x   |
| CHAR_BIT        | 16                | 8                    | 2x     |
| C++ Standard    | C++03             | C++20                | -      |
| Hardware CAN    | DCAN (2 channels) | bxCAN (1 channel)    | 2x     |
| Hardware Crypto | None              | AES peripheral       | -      |
| Encryptor flash | ~9 KB (7%)        | ~26 KB (2.5%)        | 0.35x  |
| Encryptor RAM   | ~1.8 KB (7%)      | ~7.5 KB (8%)         | 0.24x  |
