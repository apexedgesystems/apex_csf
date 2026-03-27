# Modbus Protocol Library

**Namespace:** `apex::protocols::fieldbus::modbus`
**Platform:** Linux
**C++ Standard:** C++17
**Library:** `system_core_protocols_fieldbus_modbus`

Linux Modbus RTU and TCP master implementation for industrial automation and fieldbus communication.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [Design Principles](#4-design-principles)
5. [API Reference](#5-api-reference)
6. [Usage Examples](#6-usage-examples)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Reference

| Component            | Header                   | RT-Safe | Description                          |
| -------------------- | ------------------------ | ------- | ------------------------------------ |
| `Status`             | `ModbusStatus.hpp`       | Yes     | Status codes for Modbus operations   |
| `ExceptionCode`      | `ModbusException.hpp`    | Yes     | Modbus exception codes from slave    |
| `ModbusStats`        | `ModbusStats.hpp`        | Yes     | Transaction and error statistics     |
| `ModbusResult`       | `ModbusMaster.hpp`       | Yes     | Status + exception code result type  |
| `MasterConfig`       | `ModbusConfig.hpp`       | Yes     | Retry count and timeout defaults     |
| `ModbusMaster`       | `ModbusMaster.hpp`       | Partial | High-level master for RTU or TCP     |
| `ModbusTransport`    | `ModbusTransport.hpp`    | -       | Abstract transport interface         |
| `ModbusRtuTransport` | `ModbusRtuTransport.hpp` | Partial | Serial RTU transport over UART       |
| `ModbusTcpTransport` | `ModbusTcpTransport.hpp` | Partial | Modbus TCP transport over TCP socket |
| `ModbusFrame`        | `ModbusFrame.hpp`        | Yes     | Frame building and parsing helpers   |
| `FrameBuffer`        | `ModbusFrame.hpp`        | Yes     | Fixed-size 256-byte frame buffer     |
| `ModbusTypes`        | `ModbusTypes.hpp`        | Yes     | Register and coil type aliases       |

| Question                                            | Module               |
| --------------------------------------------------- | -------------------- |
| How do I read holding/input registers from a slave? | `ModbusMaster`       |
| How do I write registers or coils to a slave?       | `ModbusMaster`       |
| How do I use Modbus over serial (RS-485)?           | `ModbusRtuTransport` |
| How do I use Modbus over Ethernet (TCP/IP)?         | `ModbusTcpTransport` |
| How do I build Modbus frames without a master?      | `ModbusFrame.hpp`    |
| How do I monitor transaction and error counts?      | `ModbusStats`        |

---

## 2. When to Use

| Scenario                                          | Use This Library?         |
| ------------------------------------------------- | ------------------------- |
| Industrial PLC communication (RS-485 or Ethernet) | Yes -- ModbusMaster       |
| SCADA/HMI integration via Modbus RTU or TCP       | Yes -- ModbusMaster       |
| Modbus over serial RS-232/RS-485 (RTU framing)    | Yes -- ModbusRtuTransport |
| Modbus over TCP/IP (MBAP header)                  | Yes -- ModbusTcpTransport |
| Low-level frame building without master lifecycle | Yes -- ModbusFrame.hpp    |
| High-speed broadcast messaging                    | No -- use CAN             |
| Safety-critical powertrain or motion control      | No -- use CAN or PROFINET |

---

## 3. Performance

### Protocol Operations

| Operation                 | Median (us) | Calls/s | CV%   |
| ------------------------- | ----------- | ------- | ----- |
| Frame build (read req)    | 0.07        | 14.3M   | 15.3% |
| Frame build (write multi) | 0.19        | 5.3M    | 2.1%  |
| Frame build (read coils)  | 0.08        | 12.5M   | 5.7%  |
| CRC-16 (6 B typical)      | 0.04        | 25.0M   | 12.9% |
| CRC-16 (254 B max)        | 1.00        | 1.0M    | 1.4%  |
| Response parse (10 reg)   | 0.04        | 25.3M   | 1.7%  |

### Full RTU Transactions (PTY loopback)

| Transaction                 | Median (us) | Calls/s | CV%  |
| --------------------------- | ----------- | ------- | ---- |
| Read Holding Registers (10) | 383         | 2.6K    | 2.4% |
| Write Single Register       | 390         | 2.6K    | 1.7% |

### Profiler Analysis

| Hotspot                | Self-Time | Type          |
| ---------------------- | --------- | ------------- |
| `CrcTable::updateImpl` | 87.5%     | CPU-bound     |
| `array::operator[]`    | 11.9%     | CPU-bound     |
| `poll`                 | 50.0%     | Syscall-bound |
| `write` (glibc)        | 32.0%     | Syscall-bound |
| `read` (glibc)         | 6.0%      | Syscall-bound |

CRC computation is table-lookup-bound. Full RTU transactions are 88% kernel time (syscall-bound).

### Memory Footprint

| Component            | Stack  | Heap                |
| -------------------- | ------ | ------------------- |
| `FrameBuffer`        | 264 B  | 0 B                 |
| `ModbusMaster`       | ~64 B  | 0 B                 |
| `ModbusRtuTransport` | ~128 B | 0 B                 |
| `ModbusTcpTransport` | ~256 B | ~64 B (socket path) |

---

## 4. Design Principles

- **Transport abstraction** -- `ModbusMaster` works over any `ModbusTransport`; RTU and TCP are interchangeable
- **No allocation on I/O path** -- All frame buffers are pre-allocated (`FrameBuffer` is stack-based, 264 B)
- **Dual transport** -- RTU uses CRC-16 over UART; TCP uses MBAP header over TCP socket with transaction IDs
- **CRC via utilities** -- CRC-16 computed via `utilities_checksums_crc` table-lookup
- **ByteTrace support** -- Optional byte-level debugging via `protocols/common/ByteTrace` mixin in transports
- **Retry logic** -- Configurable retry count in `MasterConfig`; transport manages inter-frame gaps
- **Not thread-safe** -- Concurrent access to a single master or transport requires external synchronization

---

## 5. API Reference

### Status

```cpp
enum class Status : std::uint8_t {
  SUCCESS = 0,
  WOULD_BLOCK,
  ERROR_TIMEOUT,
  ERROR_CLOSED,
  ERROR_INVALID_ARG,
  ERROR_NOT_CONFIGURED,
  ERROR_IO,
  ERROR_CRC,
  ERROR_FRAME,
  ERROR_EXCEPTION
};

const char* toString(Status s) noexcept;  // @note RT-safe
```

### ModbusResult

```cpp
struct ModbusResult {
  Status status{SUCCESS};
  ExceptionCode exceptionCode{NONE};

  /// @note RT-safe: no allocation or I/O.
  bool ok() const noexcept;
  bool isException() const noexcept;

  static ModbusResult success() noexcept;    // @note RT-safe
  static ModbusResult error(Status) noexcept; // @note RT-safe
  static ModbusResult exception(ExceptionCode) noexcept; // @note RT-safe
};
```

### ModbusMaster

```cpp
class ModbusMaster {
public:
  /// @note RT-safe: no allocation or I/O.
  ModbusMaster(ModbusTransport* transport, const MasterConfig& config);

  // Read operations

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult readHoldingRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                    std::uint16_t quantity, std::uint16_t* values,
                                    int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult readInputRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                  std::uint16_t quantity, std::uint16_t* values,
                                  int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult readCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                         std::uint16_t quantity, std::uint8_t* values,
                         int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult readDiscreteInputs(std::uint8_t unitAddr, std::uint16_t startAddr,
                                  std::uint16_t quantity, std::uint8_t* values,
                                  int timeoutMs = -1) noexcept;

  // Write operations

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult writeSingleRegister(std::uint8_t unitAddr, std::uint16_t regAddr,
                                   std::uint16_t value, int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult writeMultipleRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                      const std::uint16_t* values, std::uint16_t quantity,
                                      int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult writeSingleCoil(std::uint8_t unitAddr, std::uint16_t coilAddr,
                               bool value, int timeoutMs = -1) noexcept;

  /// @note RT-safe: Uses pre-allocated buffers.
  ModbusResult writeMultipleCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                                  const std::uint8_t* values, std::uint16_t quantity,
                                  int timeoutMs = -1) noexcept;
};
```

### ModbusTransport (Abstract Interface)

```cpp
class ModbusTransport {
public:
  virtual Status open() noexcept = 0;           // NOT RT-safe
  virtual Status close() noexcept = 0;          // NOT RT-safe
  virtual bool isOpen() const noexcept = 0;     // @note RT-safe
  virtual Status sendRequest(const FrameBuffer& frame, int timeoutMs) noexcept = 0;    // @note RT-safe
  virtual Status receiveResponse(FrameBuffer& frame, int timeoutMs) noexcept = 0;      // @note RT-safe
  virtual Status flush() noexcept = 0;          // @note RT-safe
  virtual ModbusStats stats() const noexcept = 0;   // @note RT-safe
  virtual void resetStats() noexcept = 0;           // @note RT-safe
  virtual const char* description() const noexcept = 0; // @note RT-safe
};
```

---

## 6. Usage Examples

### RTU Master: Read Holding Registers

```cpp
#include "ModbusMaster.hpp"
#include "ModbusRtuTransport.hpp"
#include "UartAdapter.hpp"

namespace fm = apex::protocols::fieldbus::modbus;
namespace su = apex::protocols::serial::uart;

su::UartAdapter uart("/dev/ttyUSB0");
su::UartConfig uartCfg;
uartCfg.baudRate = su::BaudRate::B_9600;
uart.configure(uartCfg);

fm::ModbusRtuConfig rtuCfg;
rtuCfg.responseTimeoutMs = 500;
fm::ModbusRtuTransport transport(&uart, rtuCfg, 9600);
transport.open();

fm::ModbusMaster master(&transport, fm::MasterConfig{});

std::uint16_t values[10];
fm::ModbusResult result = master.readHoldingRegisters(1, 0, 10, values);

if (result.ok()) {
  // values[0..9] contain register values
} else if (result.isException()) {
  // result.exceptionCode contains the Modbus exception
}
```

### TCP Master: Write Register

```cpp
#include "ModbusMaster.hpp"
#include "ModbusTcpTransport.hpp"

namespace fm = apex::protocols::fieldbus::modbus;

fm::ModbusTcpConfig tcpCfg;
tcpCfg.connectTimeoutMs = 3000;
fm::ModbusTcpTransport transport("192.168.1.100", 502, tcpCfg);
transport.open();

fm::ModbusMaster master(&transport, fm::MasterConfig{});
fm::ModbusResult result = master.writeSingleRegister(1, 100, 0x1234);
```

### Low-Level Frame Building

```cpp
#include "ModbusFrame.hpp"

namespace fm = apex::protocols::fieldbus::modbus;

fm::FrameBuffer frame;
fm::Status status = fm::buildReadHoldingRegistersRequest(frame, 1, 0, 10);
// frame.data contains: 01 03 00 00 00 0A C5 CD (unit + FC + addr + qty + CRC)
```

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 132   | Yes                   |
| `ptst/`   | Performance tests | 12    | No (manual)           |
| `dtst/`   | Development tests | 16    | No (manual)           |

---

## 8. See Also

- `protocols/serial/uart/` -- UART transport (physical layer for Modbus RTU)
- `protocols/network/tcp/` -- TCP transport (physical layer for Modbus TCP)
- `protocols/fieldbus/can/` -- CAN bus (alternative fieldbus)
- `protocols/fieldbus/lin/` -- LIN bus (automotive sub-bus)
- `utilities/checksums/crc/` -- CRC-16 used in RTU framing
