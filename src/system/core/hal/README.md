# Hardware Abstraction Layer (HAL)

**Namespace:** `apex::hal`
**Platform:** Platform-agnostic interfaces with STM32, AVR, Pico, and ESP32 implementations
**C++ Standard:** C++23

Hardware abstraction for embedded peripherals, internal storage, and tick
sources. Provides platform-agnostic interfaces (UART, CAN, SPI, I2C, Flash) and
platform-specific implementations with zero-allocation I/O paths. Includes
hardware tick sources for driving the McuExecutive main loop on bare-metal
targets. Designed for real-time systems with deterministic behavior on the hot
path.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Architecture](#2-architecture)
3. [Interfaces](#3-interfaces)
4. [STM32 Implementations](#4-stm32-implementations)
5. [AVR Implementations](#5-avr-implementations)
6. [Pico Implementations](#6-pico-implementations)
7. [ESP32 Implementations](#7-esp32-implementations)
8. [Mock Mode](#8-mock-mode)
9. [Testing](#9-testing)
10. [Adding a New Platform](#10-adding-a-new-platform)

---

## 1. Quick Reference

### Interfaces (header-only, `hal_interface`)

| Header       | Purpose                          | RT-Safe I/O |
| ------------ | -------------------------------- | ----------- |
| `IUart.hpp`  | UART interface (buffered, async) | Yes         |
| `ICan.hpp`   | CAN bus interface (framed)       | Yes         |
| `ISpi.hpp`   | SPI master interface (polling)   | Yes         |
| `II2c.hpp`   | I2C master interface (polling)   | Yes         |
| `IFlash.hpp` | Internal flash (page-based)      | Read only   |

### STM32 Implementations (header-only, `hal_stm32`)

| Header           | Peripheral | Mode             | Buffers              |
| ---------------- | ---------- | ---------------- | -------------------- |
| `Stm32Uart.hpp`  | USART      | Interrupt-driven | RX + TX (template)   |
| `Stm32Can.hpp`   | bxCAN      | Interrupt-driven | RX ring (template)   |
| `Stm32Spi.hpp`   | SPI        | Polling          | None                 |
| `Stm32I2c.hpp`   | I2C        | Polling          | None                 |
| `Stm32Flash.hpp` | Flash      | Blocking         | None (memory-mapped) |

### STM32 Tick Sources (header-only, `hal_stm32`)

| Header                     | Timer    | Mode            | Notes                              |
| -------------------------- | -------- | --------------- | ---------------------------------- |
| `Stm32SysTickSource.hpp`   | SysTick  | ISR prescaler   | Shares SysTick with HAL (1kHz)     |
| `Stm32TimerTickSource.hpp` | TIM6/7   | Dedicated ISR   | Direct register access, no HAL_TIM |
| `FreeRtosTickSource.hpp`   | FreeRTOS | vTaskDelayUntil | Yields to lower-priority tasks     |

### AVR Implementations (header-only, `hal_avr`)

| Header                   | Peripheral | Mode             | Buffers            |
| ------------------------ | ---------- | ---------------- | ------------------ |
| `AvrUart.hpp`            | USART0     | Interrupt-driven | RX + TX (template) |
| `AvrEeprom.hpp`          | Internal   | Blocking         | None               |
| `AvrTimerTickSource.hpp` | Timer1     | CTC ISR          | None               |

### Pico Implementations (header-only, `hal_pico`)

| Header                  | Peripheral | Mode              | Buffers                  |
| ----------------------- | ---------- | ----------------- | ------------------------ |
| `PicoUart.hpp`          | UART0/1    | Interrupt-driven  | RX + TX (template)       |
| `PicoFlash.hpp`         | XIP Flash  | Blocking          | Shadow buffer (template) |
| `PicoUsbCdc.hpp`        | USB CDC    | Polling (TinyUSB) | TX buffer (template)     |
| `PicoSysTickSource.hpp` | SysTick    | ISR prescaler     | None                     |

### ESP32 Implementations (header-only, `hal_esp32`)

| Header                     | Peripheral | Mode       | Buffers                  |
| -------------------------- | ---------- | ---------- | ------------------------ |
| `Esp32Uart.hpp`            | UART0/1/2  | Driver API | RX + TX (template)       |
| `Esp32NvsFlash.hpp`        | NVS        | Blocking   | Shadow buffer (template) |
| `Esp32UsbCdc.hpp`          | USB CDC    | TinyUSB    | TX buffer (template)     |
| `Esp32TimerTickSource.hpp` | esp_timer  | Callback   | None                     |

### Headers

```cpp
// Interfaces (platform-agnostic)
#include "src/system/core/hal/base/IUart.hpp"
#include "src/system/core/hal/base/ICan.hpp"
#include "src/system/core/hal/base/ISpi.hpp"
#include "src/system/core/hal/base/II2c.hpp"
#include "src/system/core/hal/base/IFlash.hpp"

// STM32 implementations (requires STM32 HAL at link time)
#include "src/system/core/hal/stm32/inc/Stm32Uart.hpp"
#include "src/system/core/hal/stm32/inc/Stm32Can.hpp"
#include "src/system/core/hal/stm32/inc/Stm32Spi.hpp"
#include "src/system/core/hal/stm32/inc/Stm32I2c.hpp"
#include "src/system/core/hal/stm32/inc/Stm32Flash.hpp"
#include "src/system/core/hal/stm32/inc/Stm32SysTickSource.hpp"
#include "src/system/core/hal/stm32/inc/Stm32TimerTickSource.hpp"
#include "src/system/core/hal/stm32/inc/FreeRtosTickSource.hpp"

// AVR implementations (requires avr-libc)
#include "src/system/core/hal/avr/inc/AvrUart.hpp"
#include "src/system/core/hal/avr/inc/AvrEeprom.hpp"
#include "src/system/core/hal/avr/inc/AvrTimerTickSource.hpp"

// Pico implementations (requires Pico SDK)
#include "src/system/core/hal/pico/inc/PicoUart.hpp"
#include "src/system/core/hal/pico/inc/PicoFlash.hpp"
#include "src/system/core/hal/pico/inc/PicoUsbCdc.hpp"
#include "src/system/core/hal/pico/inc/PicoSysTickSource.hpp"

// ESP32 implementations (requires ESP-IDF)
#include "src/system/core/hal/esp32/inc/Esp32Uart.hpp"
#include "src/system/core/hal/esp32/inc/Esp32NvsFlash.hpp"
#include "src/system/core/hal/esp32/inc/Esp32UsbCdc.hpp"
#include "src/system/core/hal/esp32/inc/Esp32TimerTickSource.hpp"
```

---

## 2. Architecture

```
hal/
  base/             Platform-agnostic interfaces (IUart, ICan, ISpi, II2c, IFlash)
    utst/           Interface type and enum tests
  stm32/
    inc/            STM32 implementations (peripherals + tick sources)
    utst/           STM32 mock-mode tests
  avr/
    inc/            AVR implementations (UART, EEPROM, tick source)
    utst/           AVR mock-mode tests
  pico/
    inc/            Pico implementations (UART, Flash, USB CDC, tick source)
    utst/           Pico mock-mode tests
  esp32/
    inc/            ESP32 implementations (UART, NVS, USB CDC, tick source)
    utst/           ESP32 mock-mode tests
```

### Design Principles

- **Interface/implementation split.** Abstract interfaces in `hal/base/` define
  the contract. Platform implementations in subdirectories (e.g., `hal/stm32/`)
  provide the concrete behavior.
- **Header-only.** All libraries are header-only. No `.cpp` files to compile.
  Platform implementations use `#ifndef APEX_HAL_<PLATFORM>_MOCK` to conditionally
  include real SDK calls or mock stubs.
- **Zero allocation on I/O paths.** All transfer, send, recv, read, and write
  methods use caller-provided buffers. No heap allocation after `init()`.
- **Double-init guard.** All implementations call `deinit()` if already
  initialized when `init()` is called again. Safe reconfiguration without
  manual teardown.
- **Statistics tracking.** Every implementation tracks byte counts, transfer
  counts, and error counts via a stats struct. `stats()` returns a const
  reference (RT-safe). `resetStats()` clears counters.

### CMake Libraries

| Library         | Type      | Dependencies    | Bare-Metal |
| --------------- | --------- | --------------- | ---------- |
| `hal_interface` | INTERFACE | None            | Yes        |
| `hal_stm32`     | INTERFACE | `hal_interface` | Yes        |
| `hal_avr`       | INTERFACE | `hal_interface` | Yes        |
| `hal_pico`      | INTERFACE | `hal_interface` | Yes        |
| `hal_esp32`     | INTERFACE | `hal_interface` | Yes        |

---

## 3. Interfaces

Each interface defines:

- **Status enum** (`UartStatus`, `CanStatus`, etc.) with `OK = 0` and typed error codes
- **Config struct** with sensible defaults
- **Stats struct** with `reset()` and `totalErrors()` helpers
- **Abstract class** with pure virtual lifecycle and I/O methods
- **`toString()` free function** for status-to-string conversion (static literals)

### Common Pattern

```cpp
// All interfaces follow this lifecycle:
device.init(config);          // NOT RT-safe: configures hardware
device.init(config, opts);    // NOT RT-safe: with platform-specific options

// I/O (RT-safe after init):
device.write(...);
device.read(...);

// Status (RT-safe):
device.isInitialized();
device.isBusy();
device.stats();
device.resetStats();

// Teardown:
device.deinit();              // NOT RT-safe
```

### Interface Details

| Interface | I/O Model   | Key Methods                                                |
| --------- | ----------- | ---------------------------------------------------------- |
| `IUart`   | Byte stream | `write()`, `read()`, `print()`, `println()`                |
| `ICan`    | Framed      | `send(CanFrame)`, `recv(CanFrame)`, `addFilter()`          |
| `ISpi`    | Byte array  | `transfer()`, `write()`, `read()`                          |
| `II2c`    | Addressed   | `write(addr,...)`, `read(addr,...)`, `writeRead(addr,...)` |
| `IFlash`  | Page-based  | `read()`, `write()`, `erasePage()`, `erasePages()`         |

### RT-Safety Summary

| Method            | RT-Safe | Notes                             |
| ----------------- | ------- | --------------------------------- |
| `init()`          | No      | Configures clocks, GPIO, NVIC     |
| `deinit()`        | No      | Releases hardware resources       |
| `write()`         | Yes\*   | After init (\*Flash: NOT RT-safe) |
| `read()`          | Yes     | After init                        |
| `transfer()`      | Yes     | SPI only, after init              |
| `send()`          | Yes     | CAN only, after init              |
| `recv()`          | Yes     | CAN only, after init              |
| `writeRead()`     | Yes     | I2C only, after init              |
| `erasePage()`     | No      | Flash only, blocks CPU            |
| `erasePages()`    | No      | Flash only, blocks CPU            |
| `geometry()`      | Yes     | Flash only, const after init      |
| `isInitialized()` | Yes     | Always                            |
| `isBusy()`        | Yes     | After init                        |
| `stats()`         | Yes     | After init                        |
| `resetStats()`    | Yes     | After init                        |

---

## 4. STM32 Implementations

### Supported Families

New-generation peripherals with modern register layouts:

| Family  | UART (RDR/TDR) | CAN (bxCAN) | SPI | I2C (TIMINGR) | Flash (page) |
| ------- | -------------- | ----------- | --- | ------------- | ------------ |
| STM32L4 | Yes            | Yes         | Yes | Yes           | Yes          |
| STM32G4 | Yes            | Yes         | Yes | Yes           | Yes          |
| STM32H7 | Yes            | Planned     | Yes | Planned       | Planned      |

**Not supported:** STM32F1, STM32F2, STM32F4 (legacy peripheral registers).

### Construction

Templated implementations (UART, CAN) require compile-time buffer sizes.
Non-templated implementations (SPI, I2C, Flash) use polling with no internal
buffers. Flash has no pins or peripheral pointer (internal to the MCU).

```cpp
// UART: interrupt-driven with configurable RX/TX buffer sizes
Stm32Uart<256, 256> uart(USART2, uartPins);

// CAN: interrupt-driven with configurable RX ring buffer
Stm32Can<32> can(CAN1, canPins);

// SPI: polling, no internal buffers
Stm32Spi spi(SPI1, spiPins);

// I2C: polling, no internal buffers
Stm32I2c i2c(I2C1, i2cPins);

// Flash: blocking, no pins or peripheral pointer needed
Stm32Flash flash;

// Tick sources: implement executive::mcu::ITickSource
Stm32SysTickSource sysTick(100);           // 100 Hz, prescales 1kHz SysTick
Stm32TimerTickSource timerTick(TIM6, 100); // 100 Hz via TIM6
FreeRtosTickSource freertosTs(100);        // 100 Hz via vTaskDelayUntil
```

### Platform-Specific Options

Each implementation accepts an optional platform-specific options struct:

| Struct              | Fields                                   | Default |
| ------------------- | ---------------------------------------- | ------- |
| `Stm32UartOptions`  | `nvicPreemptPriority`, `nvicSubPriority` | 0, 0    |
| `Stm32CanOptions`   | `nvicPreemptPriority`, `nvicSubPriority` | 1, 0    |
| `Stm32SpiOptions`   | `timeoutMs`                              | 1000    |
| `Stm32I2cOptions`   | `timeoutMs`                              | 1000    |
| `Stm32FlashOptions` | `timeoutMs`                              | 5000    |

### Pin Descriptors

Each implementation uses a pin descriptor struct:

```cpp
// UART
Stm32UartPins pins = {
  .txPort = GPIOA, .txPin = GPIO_PIN_2,
  .rxPort = GPIOA, .rxPin = GPIO_PIN_3,
  .alternate = GPIO_AF7_USART2
};

// CAN
Stm32CanPins pins = {
  .txPort = GPIOB, .txPin = GPIO_PIN_9,
  .rxPort = GPIOB, .rxPin = GPIO_PIN_8,
  .alternate = GPIO_AF9_CAN1
};

// SPI
Stm32SpiPins pins = {
  .clkPort = GPIOA, .clkPin = GPIO_PIN_5,
  .mosiPort = GPIOA, .mosiPin = GPIO_PIN_7,
  .misoPort = GPIOA, .misoPin = GPIO_PIN_6,
  .csPort = GPIOA, .csPin = GPIO_PIN_4,
  .alternate = GPIO_AF5_SPI1
};

// I2C
Stm32I2cPins pins = {
  .sclPort = GPIOB, .sclPin = GPIO_PIN_6,
  .sdaPort = GPIOB, .sdaPin = GPIO_PIN_7,
  .alternate = GPIO_AF4_I2C1
};
```

---

## 5. AVR Implementations

### Supported Targets

| Target      | UART (USART0) | EEPROM | Timer1 Tick |
| ----------- | ------------- | ------ | ----------- |
| ATmega328P  | Yes           | Yes    | Yes         |
| ATmega328PB | Yes           | Yes    | Yes         |

### Construction

```cpp
// UART: interrupt-driven with static circular buffers
AvrUart<128, 128> uart;  // 128-byte RX + TX buffers

// EEPROM: blocking read/write
AvrEeprom<1024> eeprom;  // 1024-byte capacity

// Tick source: Timer1 CTC mode
AvrTimerTickSource tickSource(100);  // 100 Hz
```

### Mock Define

`APEX_HAL_AVR_MOCK` - guards avr/io.h, avr/interrupt.h, avr/eeprom.h, ISR macros,
and all register access. Enables host-side testing with shadow buffers.

---

## 6. Pico Implementations

### Supported Targets

| Target | UART | XIP Flash | USB CDC | SysTick |
| ------ | ---- | --------- | ------- | ------- |
| RP2040 | Yes  | Yes       | Yes     | Yes     |

### Construction

```cpp
// UART: interrupt-driven, specify uart_inst_t* and GPIO pins
PicoUart<512, 512> uart(uart0, 0, 1);  // UART0, TX=GP0, RX=GP1

// Flash: XIP flash with shadow buffer for mock mode
PicoFlash<4096> flash;  // 4KB region

// USB CDC: polling via TinyUSB
PicoUsbCdc<512> cdc;  // 512-byte TX buffer

// Tick source: SysTick prescaler
PicoSysTickSource tickSource(100);  // 100 Hz
```

### Mock Define

`APEX_HAL_PICO_MOCK` - guards hardware/uart.h, hardware/flash.h, tusb.h, SysTick
CMSIS registers. Shadow buffers simulate flash and USB CDC in host-side tests.

---

## 7. ESP32 Implementations

### Supported Targets

| Target   | UART | NVS Flash | USB CDC | Timer Tick |
| -------- | ---- | --------- | ------- | ---------- |
| ESP32    | Yes  | Yes       | No      | Yes        |
| ESP32-S2 | Yes  | Yes       | Yes     | Yes        |
| ESP32-S3 | Yes  | Yes       | Yes     | Yes        |

### Construction

```cpp
// UART: ESP-IDF driver API with configurable buffers
Esp32Uart<256, 256> uart(UART_NUM_1, 17, 16);  // UART1, TX=17, RX=16

// NVS Flash: key-value persistent storage with shadow buffer
Esp32NvsFlash<512> nvs("app_data");  // Namespace "app_data"

// USB CDC: TinyUSB with TX buffer
Esp32UsbCdc<512> cdc;

// Tick source: esp_timer or FreeRTOS-based
Esp32TimerTickSource tickSource(100);  // 100 Hz
```

### Mock Define

`APEX_HAL_ESP32_MOCK` - guards driver/uart.h, nvs.h, tusb.h, FreeRTOS includes.
Shadow buffers simulate NVS and USB CDC in host-side tests.

---

## 8. Mock Mode

All implementations support host-side testing via platform-specific preprocessor
defines. When defined:

- No SDK/HAL headers are included
- No real hardware is accessed
- Default constructor available (no peripheral pointer or pin descriptor)
- I/O methods provide deterministic mock behavior

### Mock Defines

| Platform | Define                |
| -------- | --------------------- |
| STM32    | `APEX_HAL_STM32_MOCK` |
| AVR      | `APEX_HAL_AVR_MOCK`   |
| Pico     | `APEX_HAL_PICO_MOCK`  |
| ESP32    | `APEX_HAL_ESP32_MOCK` |

### Mock Behavior Summary

| Peripheral    | Method              | Mock Behavior                            |
| ------------- | ------------------- | ---------------------------------------- |
| UART (all)    | `write()`           | Queues bytes to TX buffer, returns count |
| UART (all)    | `read()`            | Returns 0 (empty RX buffer)              |
| CAN (STM32)   | `send()`            | Returns OK, increments stats             |
| CAN (STM32)   | `recv()`            | Returns WOULD_BLOCK (empty buffer)       |
| SPI (STM32)   | `transfer()`        | Loopback (copies TX to RX)               |
| I2C (STM32)   | `writeRead()`       | Fills RX with incrementing pattern       |
| Flash (all)   | `read()/write()`    | Operates on shadow buffer in memory      |
| Flash (all)   | `erasePage()`       | Fills page region with 0xFF              |
| EEPROM (AVR)  | `read()/write()`    | Operates on 1KB shadow buffer            |
| USB CDC (all) | `write()`           | Copies to TX buffer, returns count       |
| Tick sources  | `waitForNextTick()` | Increments tick, returns immediately     |

---

## 9. Testing

```bash
# Build and run all tests (includes HAL)
make compose-testp

# Run only HAL tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R TestHal
```

### Test Targets

| Target             | Platform | Tests | Focus                             |
| ------------------ | -------- | ----- | --------------------------------- |
| `TestHalInterface` | Agnostic | 69    | Interface types, enums, defaults  |
| `TestHalStm32`     | STM32    | 210   | Mock lifecycle, I/O, tick sources |
| `TestHalAvr`       | AVR      | 69    | Mock lifecycle, EEPROM, tick      |
| `TestHalPico`      | Pico     | 93    | Mock lifecycle, flash, USB CDC    |
| `TestHalEsp32`     | ESP32    | 105   | Mock lifecycle, NVS, USB CDC      |

All tests run on the host using mock mode. No hardware required.

### Build Verification

```bash
# Verify STM32 firmware compiles with real HAL
make compose-stm32

# Verify Pico firmware compiles with real SDK
make compose-pico

# Verify ESP32 firmware compiles with real IDF
make compose-esp32

# Verify AVR firmware compiles with real avr-libc
make compose-arduino
```

---

## 10. Adding a New Platform

To add support for a new MCU family (e.g., `nrf52`):

1. Create `hal/nrf52/` with `inc/` and `CMakeLists.txt`
2. Implement each interface (e.g., `Nrf52Uart.hpp` extends `IUart`)
3. Add mock mode support (`APEX_HAL_NRF52_MOCK`)
4. Add `hal/nrf52/utst/` with mock-mode unit tests and `CMakeLists.txt`
5. Add `if (EXISTS ...)` block in `hal/CMakeLists.txt`
6. Update this README with the new platform table
