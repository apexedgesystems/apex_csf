# RP2040 Memory Map

## Overview

The RP2040 (Raspberry Pi Pico) uses a dual Cortex-M0+ core architecture with
264 KB of SRAM and 2 MB of external QSPI flash.

## Flash (2 MB, XIP at 0x10000000)

```
Address         Size    Description
-----------     ------  ---------------------------
0x10000000      ~2 MB   Application code + data (XIP)
0x101FF000      4 KB    Key Store (last sector)
```

Flash is external QSPI (Winbond W25Q16JV on the Pico). The RP2040 maps it into
the address space via XIP (Execute In Place). Read access is memory-mapped; write
and erase require SDK flash functions that copy a small trampoline to SRAM and
execute from there.

**Sector erase:** 4 KB (the SDK `flash_range_erase()` minimum).
**Write alignment:** 256 bytes (flash page program granularity).
**Total sectors:** 512 (2 MB / 4 KB).

## SRAM (264 KB)

```
Address         Size    Description
-----------     ------  ---------------------------
0x20000000      256 KB  Main SRAM (4 banks x 64 KB, striped)
0x20040000        4 KB  SRAM4 (contiguous)
0x20041000        4 KB  SRAM5 (contiguous)
```

The four main SRAM banks are striped by word address for concurrent access from
both cores and the DMA engine. SRAM4 and SRAM5 are contiguous and useful for
DMA-friendly buffers.

## Key Store Layout

The key store occupies the last 4 KB sector of the 2 MB flash:

```
Flash offset    Flash address   Description
-----------     -------------   ---------------------------
0x1FF000        0x101FF000      Slot  0 (32 bytes)
0x1FF020        0x101FF020      Slot  1 (32 bytes)
...                             ...
0x1FF1E0        0x101FF1E0      Slot 15 (32 bytes)
0x1FF200        0x101FF200      Reserved (3,584 bytes)
```

**Page index:** 511 (last page in 512-page geometry).
**Total key data:** 512 bytes (16 slots x 32 bytes).
**Empty detection:** All 0xFF = erased flash = empty slot.

## Three-Platform Comparison

| Feature         | RP2040 (Pico)           | STM32L476RG (Nucleo)  | ATmega328P (Uno R3) |
| --------------- | ----------------------- | --------------------- | ------------------- |
| Core            | Dual Cortex-M0+ 133 MHz | Cortex-M4 80 MHz      | AVR 16 MHz          |
| SRAM            | 264 KB                  | 96 KB + 32 KB SRAM2   | 2 KB                |
| Flash           | 2 MB external QSPI      | 1 MB internal         | 32 KB internal      |
| Flash type      | XIP (memory-mapped)     | Internal NOR          | Internal NOR        |
| Sector erase    | 4 KB                    | 2 KB (page)           | 128 B (SPM page)    |
| Write align     | 256 bytes               | 8 bytes (dword)       | 2 bytes (word)      |
| Key store page  | 511 (last 4 KB)         | 510 (last 2 KB)       | EEPROM (1 KB)       |
| UARTs           | 2 (UART0, UART1)        | 5 (USART1-3, UART4-5) | 1 (USART0)          |
| FPU             | None (M0+)              | Single-precision      | None                |
| DWT             | None (M0+)              | Yes (cycle counter)   | None                |
| USB             | USB 1.1 device          | USB OTG FS            | None (CH340 bridge) |
| Plaintext limit | 256 B                   | 256 B                 | 48 B                |
| Key slots       | 16                      | 16                    | 4                   |
