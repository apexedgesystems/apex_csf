# UART Library

**Namespace:** `apex::protocols::serial::uart`
**Platform:** Linux-only
**C++ Standard:** C++17

High-performance UART (Universal Asynchronous Receiver-Transmitter) library for Linux serial port communication, designed for real-time and embedded systems. Supports RS-232, RS-422, RS-485, and USB-serial adapters with zero-allocation I/O paths.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [API Reference](#4-api-reference)
5. [Usage Examples](#5-usage-examples)
6. [Testing](#6-testing)
7. [See Also](#7-see-also)

---

## 1. Quick Reference

| Component     | Purpose                            | RT-Safe I/O     |
| ------------- | ---------------------------------- | --------------- |
| `UartDevice`  | Abstract device interface          | Yes             |
| `UartAdapter` | Linux termios backend              | Yes             |
| `UartStats`   | Byte/operation statistics counters | Yes             |
| `UartConfig`  | Configuration (baud, parity, etc.) | No (setup only) |
| `ByteTrace`   | Optional byte-level tracing mixin  | Depends         |
| `PtyPair`     | Pseudo-terminal helper for tests   | No (setup only) |

### Headers

```cpp
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartDevice.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStats.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"
```

---

## 2. When to Use

| Scenario                      | Use UART?             |
| ----------------------------- | --------------------- |
| Hardware serial communication | Yes                   |
| RS-232/RS-422/RS-485 devices  | Yes                   |
| USB-serial adapters           | Yes                   |
| Embedded sensor interfaces    | Yes                   |
| Need network connectivity     | No (use TCP/UDP)      |
| Same-machine IPC              | No (use Unix sockets) |
| Need CAN bus arbitration      | No (use CAN)          |

### UART vs Other Protocols

| Aspect       | UART                 | TCP                 | CAN                    |
| ------------ | -------------------- | ------------------- | ---------------------- |
| **Use Case** | Point-to-point       | Internet/reliable   | Embedded/automotive    |
| **Topology** | 1:1 (or 1:N for 485) | Many:many           | Broadcast bus          |
| **Latency**  | Microseconds         | Milliseconds        | Sub-millisecond        |
| **Delivery** | Best-effort          | Guaranteed, ordered | Prioritized, broadcast |
| **Hardware** | Simple, ubiquitous   | Ethernet/WiFi       | Dedicated bus          |

### When to Choose UART

- **Hardware interfaces:** GPS modules, IMUs, radio modems, debug consoles
- **Legacy systems:** Many industrial devices use RS-232/RS-485
- **Simple point-to-point:** No network stack overhead
- **USB-serial adapters:** FTDI, CP210x, CH340, PL2303

### When to Choose Other Protocols

- **Network connectivity:** Use TCP/UDP for IP communication
- **Multi-node broadcast:** Use CAN for deterministic bus arbitration
- **Same-machine IPC:** Unix sockets have lower latency
- **High throughput:** Ethernet/USB bulk transfers exceed serial speeds

---

## 3. Performance

### Throughput

UART throughput is limited by the configured baud rate:

| Baud Rate | Max Throughput | Typical Use Case          |
| --------- | -------------- | ------------------------- |
| 9600      | ~960 B/s       | Legacy devices, AT modems |
| 115200    | ~11.5 KB/s     | Debug consoles, GPS       |
| 921600    | ~92 KB/s       | High-speed sensors        |
| 3000000   | ~300 KB/s      | FTDI/CP210x max           |

### Software Latency (PTY loopback, 64B payload)

Measured on x86_64 (clang-21, -O2).

| Operation           | Median (us) |   CV% | Calls/sec |
| ------------------- | ----------: | ----: | --------: |
| Write               |        4.55 | 23.1% |   219,700 |
| Read                |        4.73 | 11.1% |   211,300 |
| Round-trip          |        8.77 | 14.6% |   114,100 |
| Write burst         |        6.76 |  5.5% |   148,000 |
| Large write (16 KB) |       30.92 |  3.0% |    32,300 |

### Memory Footprint

| Component     | Stack      | Heap                |
| ------------- | ---------- | ------------------- |
| `UartAdapter` | ~300 bytes | 0 (after configure) |
| `UartConfig`  | ~64 bytes  | 0                   |
| `UartStats`   | 64 bytes   | 0                   |

---

## 4. API Reference

### UartDevice (Abstract Interface)

**RT-safe:** Yes (no allocations on I/O paths)

```cpp
class UartDevice {
public:
  virtual Status configure(const UartConfig& config) noexcept = 0;
  virtual Status read(uint8_t* buffer, size_t bufferSize,
                      size_t& bytesRead, int timeoutMs) noexcept = 0;
  virtual Status write(const uint8_t* data, size_t dataSize,
                       size_t& bytesWritten, int timeoutMs) noexcept = 0;
  virtual Status flush(bool flushRx, bool flushTx) noexcept = 0;
  virtual Status close() noexcept = 0;
  virtual bool isOpen() const noexcept = 0;
  virtual int fd() const noexcept = 0;              // For epoll integration
  virtual const UartStats& stats() const noexcept = 0;
  virtual void resetStats() noexcept = 0;
  virtual const char* devicePath() const noexcept = 0;
};
```

### Key Types

```cpp
enum class BaudRate : uint32_t {
  B_9600 = 9600, B_19200 = 19200, B_38400 = 38400,
  B_57600 = 57600, B_115200 = 115200, B_230400 = 230400,
  B_460800 = 460800, B_921600 = 921600, B_1000000 = 1000000,
  B_2000000 = 2000000, B_3000000 = 3000000, B_4000000 = 4000000
  // ... and more
};

enum class DataBits : uint8_t { FIVE, SIX, SEVEN, EIGHT };
enum class Parity : uint8_t { NONE, ODD, EVEN };
enum class StopBits : uint8_t { ONE, TWO };
enum class FlowControl : uint8_t { NONE, HARDWARE, SOFTWARE };

struct UartConfig {
  BaudRate baudRate = BaudRate::B_115200;
  DataBits dataBits = DataBits::EIGHT;
  Parity parity = Parity::NONE;
  StopBits stopBits = StopBits::ONE;
  FlowControl flowControl = FlowControl::NONE;

  struct Rs485Config {
    bool enabled = false;
    bool rtsOnSend = true;
    bool rtsAfterSend = false;
    uint32_t delayRtsBeforeSendUs = 0;
    uint32_t delayRtsAfterSendUs = 0;
  } rs485;

  bool lowLatency = false;      // TIOCGSERIAL low_latency flag
  bool exclusiveAccess = true;  // flock() exclusive lock
};

struct UartStats {
  uint64_t bytesRx;             // Total bytes received
  uint64_t bytesTx;             // Total bytes transmitted
  uint64_t readsCompleted;      // Successful read operations
  uint64_t writesCompleted;     // Successful write operations
  uint64_t readWouldBlock;      // Times read returned WOULD_BLOCK
  uint64_t writeWouldBlock;     // Times write returned WOULD_BLOCK
  uint64_t readErrors;          // Read errors (ERROR_*)
  uint64_t writeErrors;         // Write errors (ERROR_*)

  void reset() noexcept;
  uint64_t totalBytes() const noexcept;
  uint64_t totalErrors() const noexcept;
  uint64_t totalOperations() const noexcept;
  uint64_t totalWouldBlock() const noexcept;
};
```

### Status Codes

| Status                 | Meaning                             |
| ---------------------- | ----------------------------------- |
| `SUCCESS`              | Operation completed successfully    |
| `WOULD_BLOCK`          | Not ready now (nonblocking mode)    |
| `ERROR_TIMEOUT`        | True OS/backend timeout             |
| `ERROR_CLOSED`         | Device closed/disconnected          |
| `ERROR_INVALID_ARG`    | Bad argument (null buffer, etc.)    |
| `ERROR_NOT_CONFIGURED` | Called before configure()           |
| `ERROR_IO`             | Backend I/O or OS error             |
| `ERROR_UNSUPPORTED`    | Feature not supported (e.g., RS485) |
| `ERROR_BUSY`           | Device already in use (flock)       |

### Timeout Semantics

| timeoutMs | Behavior                       |
| --------- | ------------------------------ |
| `< 0`     | Block until I/O is ready       |
| `== 0`    | Nonblocking poll (immediate)   |
| `> 0`     | Bounded wait (up to timeoutMs) |

### RT Extensions

```cpp
// Vectored I/O for scatter-gather (single syscall)
Status writeVectored(const struct iovec* iov, int iovcnt,
                     size_t& bytesWritten, int timeoutMs) noexcept;
Status readVectored(struct iovec* iov, int iovcnt,
                    size_t& bytesRead, int timeoutMs) noexcept;

// Span API for zero-copy integration
Status read(apex::compat::mutable_bytes_span buffer,
            size_t& bytesRead, int timeoutMs) noexcept;
Status write(apex::compat::bytes_span data,
             size_t& bytesWritten, int timeoutMs) noexcept;
```

---

## 5. Usage Examples

### Basic Send/Receive

```cpp
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartAdapter.hpp"

using namespace apex::protocols::serial::uart;

int main() {
  UartAdapter adapter("/dev/ttyUSB0");

  UartConfig cfg{};
  cfg.baudRate = BaudRate::B_115200;
  cfg.dataBits = DataBits::EIGHT;
  cfg.parity = Parity::NONE;
  cfg.stopBits = StopBits::ONE;

  if (adapter.configure(cfg) != Status::SUCCESS) {
    return 1;
  }

  // Write data
  std::uint8_t txData[] = {0x01, 0x02, 0x03, 0x04};
  std::size_t bytesWritten = 0;
  adapter.write(txData, sizeof(txData), bytesWritten, 100);

  // Read response
  std::uint8_t rxBuffer[256];
  std::size_t bytesRead = 0;
  if (adapter.read(rxBuffer, sizeof(rxBuffer), bytesRead, 1000) == Status::SUCCESS) {
    // Process rxBuffer[0..bytesRead-1]
  }
}
```

### RS-485 Half-Duplex

```cpp
UartConfig cfg{};
cfg.baudRate = BaudRate::B_115200;
cfg.rs485.enabled = true;
cfg.rs485.rtsOnSend = true;       // Assert RTS during transmit
cfg.rs485.rtsAfterSend = false;   // Deassert RTS after transmit
cfg.rs485.delayRtsBeforeSendUs = 0;
cfg.rs485.delayRtsAfterSendUs = 0;

UartAdapter adapter("/dev/ttyS1");
Status status = adapter.configure(cfg);
if (status == Status::ERROR_UNSUPPORTED) {
  // Device does not support RS-485 mode (PTY, some USB adapters)
}
```

### Vectored I/O (Scatter-Gather)

```cpp
// Send header + payload in single syscall (reduces latency jitter)
std::uint8_t header[4] = {0xAA, 0x55, 0x00, 0x08};
std::uint8_t payload[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

struct iovec iov[2];
iov[0].iov_base = header;
iov[0].iov_len = sizeof(header);
iov[1].iov_base = payload;
iov[1].iov_len = sizeof(payload);

std::size_t bytesWritten = 0;
adapter.writeVectored(iov, 2, bytesWritten, 100);
```

### Statistics Monitoring

```cpp
UartAdapter adapter("/dev/ttyUSB0");
adapter.configure(UartConfig{});

// ... perform I/O operations ...

// Get current statistics (RT-safe, returns const reference)
const UartStats& s = adapter.stats();
std::cout << "Bytes sent: " << s.bytesTx << "\n";
std::cout << "Bytes received: " << s.bytesRx << "\n";
std::cout << "Total operations: " << s.totalOperations() << "\n";
std::cout << "Errors: " << s.totalErrors() << "\n";

// Reset for next measurement period
adapter.resetStats();
```

### Testing with PTY (No Hardware)

```cpp
#include "src/system/core/infrastructure/protocols/serial/uart/inc/PtyPair.hpp"

PtyPair pty;
if (pty.open() != Status::SUCCESS) {
  return 1;
}

// UartAdapter connects to slave side (like real hardware)
UartAdapter adapter(pty.slavePath());
adapter.configure(UartConfig{});

// Test code injects data via master side
std::uint8_t testData[] = {0xDE, 0xAD, 0xBE, 0xEF};
std::size_t written = 0;
pty.writeMaster(testData, sizeof(testData), written, 100);

// Adapter reads the injected data
std::uint8_t buffer[64];
std::size_t bytesRead = 0;
adapter.read(buffer, sizeof(buffer), bytesRead, 100);
// buffer now contains testData
```

### Byte-Level Tracing

```cpp
#include "src/system/core/infrastructure/protocols/serial/uart/inc/ByteTrace.hpp"

// Define trace callback (RT-safe if using RT-safe destination)
void myTraceCallback(TraceDirection dir, const std::uint8_t* data,
                     std::size_t len, void* userData) noexcept {
  char buf[256];
  formatTraceMessage(dir, data, len, buf, sizeof(buf), "UART");
  // Write to your log/buffer/console...
  std::cout << buf << std::endl;  // Not RT-safe (example only)
}

UartAdapter adapter("/dev/ttyUSB0");
adapter.configure(UartConfig{});

// Attach callback and enable (setup phase, NOT RT-safe)
adapter.attachTrace(myTraceCallback, nullptr);
adapter.setTraceEnabled(true);

// I/O operations now invoke callback
std::uint8_t data[] = {0xAA, 0xBB};
std::size_t written = 0;
adapter.write(data, sizeof(data), written, 100);
// Callback receives: TraceDirection::TX, data, 2 bytes

// Disable tracing (RT-safe toggle)
adapter.setTraceEnabled(false);

// Detach callback (teardown phase, NOT RT-safe)
adapter.detachTrace();
```

**RT-Safe Tracing with SystemLog:**

```cpp
// Use SystemLog in ASYNC mode for RT-safe tracing
auto sysLog = std::make_shared<logs::SystemLog>(
    "trace.log", logs::SystemLog::Mode::ASYNC, 4096);

void rtSafeCallback(TraceDirection dir, const std::uint8_t* data,
                    std::size_t len, void* userData) noexcept {
  char buf[256];
  formatTraceMessage(dir, data, len, buf, sizeof(buf), "UART");
  static_cast<logs::SystemLog*>(userData)->debug("TRACE", buf);  // RT-safe
}

adapter.attachTrace(rtSafeCallback, sysLog.get());
```

---

## 6. Testing

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run UART library tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R Uart
```

### Test Coverage

| Test File               | Focus                         |
| ----------------------- | ----------------------------- |
| `UartStatus_uTest.cpp`  | Status enum and toString()    |
| `UartStats_uTest.cpp`   | Statistics struct methods     |
| `UartConfig_uTest.cpp`  | Configuration enums           |
| `PtyPair_uTest.cpp`     | PTY test utility              |
| `UartAdapter_uTest.cpp` | Full adapter via PTY loopback |

### Test Requirements

- Linux with PTY support (all modern distributions)
- No special hardware required (PTY provides virtual serial port)
- Docker environment provides required permissions

### Performance Tests

```bash
# Run performance benchmarks
./build/native-linux-debug/bin/ptests/ProtocolsSerialUart_PTEST --csv results.csv
```

---

## 7. See Also

- **Serial Overview** (`../`) - Protocol selection guide
- **Framing Protocols** (`../../framing/`) - SLIP/COBS for framed messages over UART
- **CAN Bus** (`../../fieldbus/can/`) - For broadcast bus networks
- **Network Protocols** (`../../network/`) - TCP/UDP for IP networks
- [Linux Serial HOWTO](https://tldp.org/HOWTO/Serial-HOWTO.html) - System configuration
- [termios(3)](https://man7.org/linux/man-pages/man3/termios.3.html) - Terminal I/O interface
