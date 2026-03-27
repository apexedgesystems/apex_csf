# ESP32-S3 Memory Map (Arduino Nano ESP32)

Memory layout for the ESP32-S3 as configured by ESP-IDF with default partition
table. The Arduino Nano ESP32 has generous memory -- no optimization needed.

## Internal Memory

| Region   | Address     | Size   | Description                              |
| -------- | ----------- | ------ | ---------------------------------------- |
| ROM      | 0x4000_0000 | 384 KB | Mask ROM (bootloader, crypto HW drivers) |
| SRAM     | 0x3FC8_8000 | 512 KB | Internal SRAM (data + instruction)       |
| RTC SRAM | 0x5000_0000 | 8 KB   | RTC fast memory (deep sleep retention)   |

## External Memory

| Region | Size  | Interface      | Description                          |
| ------ | ----- | -------------- | ------------------------------------ |
| Flash  | 16 MB | Quad SPI (XIP) | Program storage, NVS, OTA partitions |
| PSRAM  | 8 MB  | Octal SPI      | External RAM (not used by encryptor) |

## Flash Partition Table (ESP-IDF default)

| Partition | Offset  | Size   | Type     | Description                          |
| --------- | ------- | ------ | -------- | ------------------------------------ |
| nvs       | 0x9000  | 24 KB  | data/nvs | Non-Volatile Storage (**key store**) |
| phy_init  | 0xF000  | 4 KB   | data/phy | WiFi/BT calibration                  |
| factory   | 0x10000 | ~1 MB  | app      | Application firmware                 |
| (free)    | --      | ~14 MB | --       | Available for OTA or data            |

## Encryptor Memory Usage

### SRAM Budget

| Component                | Estimated Size | Notes                     |
| ------------------------ | -------------- | ------------------------- |
| FreeRTOS kernel          | ~20 KB         | Heap, task stacks, timers |
| LiteExecutive task stack | 4 KB           | Pinned to core 0          |
| UART RX/TX buffers       | 2 KB           | 512 + 512 per UART        |
| USB CDC buffers          | 512 B          | TinyUSB managed           |
| SLIP encode/decode       | ~1 KB          | Frame buffers             |
| AES-256-GCM context      | ~1 KB          | Key schedule + state      |
| Application variables    | ~2 KB          | KeyStore, engine, stats   |
| **Total**                | **~31 KB**     | **6% of 512 KB**          |

No memory optimization needed. The ESP32-S3 has 16x the SRAM of the STM32L476
and 256x the SRAM of the Arduino Uno.

### NVS Key Storage

Key material is stored in the NVS (Non-Volatile Storage) partition. NVS
provides wear-leveled key-value storage over flash, simpler than the raw flash
sector approach used on STM32 and Pico.

| Parameter      | Value                              |
| -------------- | ---------------------------------- |
| Partition      | `nvs` (24 KB)                      |
| Namespace      | `encryptor`                        |
| Key slots      | 16 x 32 bytes (AES-256 keys)       |
| Total key data | 512 bytes                          |
| Wear leveling  | Automatic (NVS handles internally) |

### Comparison with Other Platforms

| Resource    | STM32L476      | Arduino Uno   | Pico RP2040      | ESP32-S3        |
| ----------- | -------------- | ------------- | ---------------- | --------------- |
| SRAM        | 128 KB         | 2 KB          | 264 KB           | **512 KB**      |
| Flash       | 1 MB           | 32 KB         | 2 MB             | **16 MB**       |
| Key storage | Flash page 510 | EEPROM (1 KB) | Flash sector 511 | **NVS (24 KB)** |
| Key slots   | 16             | 4             | 16               | **16**          |
| Clock       | 80 MHz         | 16 MHz        | 125 MHz          | **240 MHz**     |
| Cores       | 1              | 1             | 2                | **2**           |
