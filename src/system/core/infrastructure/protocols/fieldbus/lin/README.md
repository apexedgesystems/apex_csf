# LIN Protocol Library

**Namespace:** `apex::protocols::fieldbus::lin`
**Platform:** Linux
**C++ Standard:** C++17
**Library:** `system_core_protocols_fieldbus_lin`

Linux LIN (Local Interconnect Network) master/slave controller built over UART for automotive and industrial embedded systems.

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

| Component          | Header              | RT-Safe | Description                             |
| ------------------ | ------------------- | ------- | --------------------------------------- |
| `Status`           | `LinStatus.hpp`     | Yes     | Status codes for LIN operations         |
| `LinStats`         | `LinController.hpp` | Yes     | Frame and error statistics              |
| `LinConfig`        | `LinConfig.hpp`     | Yes     | Baud rate, checksum type, timeouts      |
| `LinScheduleEntry` | `LinConfig.hpp`     | Yes     | Master schedule table entry             |
| `LinController`    | `LinController.hpp` | Partial | Master/slave controller over UART       |
| `FrameBuffer`      | `LinFrame.hpp`      | Yes     | Fixed-size frame construction buffer    |
| `ParsedFrame`      | `LinFrame.hpp`      | Yes     | Frame parsing result                    |
| `ChecksumType`     | `LinFrame.hpp`      | Yes     | CLASSIC (LIN 1.x) or ENHANCED (LIN 2.x) |

| Question                                          | Module          |
| ------------------------------------------------- | --------------- |
| How do I send a LIN header as master?             | `LinController` |
| How do I request data from a slave?               | `LinController` |
| How do I respond to a header as slave?            | `LinController` |
| How do I build/parse frames without a controller? | `LinFrame.hpp`  |
| How do I configure baud rate and checksum type?   | `LinConfig`     |
| How do I monitor checksum and sync error counts?  | `LinStats`      |

---

## 2. When to Use

| Scenario                                             | Use This Library?           |
| ---------------------------------------------------- | --------------------------- |
| Automotive body electronics (seat, mirror, window)   | Yes -- LinController        |
| Low-speed sensor networks over single wire           | Yes -- LinController        |
| Master-scheduled LIN bus (header + response pattern) | Yes -- LinController        |
| Frame building/parsing without physical transport    | Yes -- LinFrame.hpp helpers |
| High-speed full-duplex data transfer                 | No -- use CAN or Ethernet   |
| Safety-critical powertrain or braking control        | No -- use CAN or FlexRay    |

---

## 3. Performance

### Protocol Operations

| Operation           | Median (us) | Calls/s | CV%  |
| ------------------- | ----------- | ------- | ---- |
| Header build        | 0.03        | 31.7M   | 5.8% |
| Full frame build    | 0.05        | 21.3M   | <1%  |
| Frame parse         | 0.05        | 22.4M   | 2.4% |
| PID calculate       | 0.35        | 2.9M    | 1.1% |
| Checksum (classic)  | 0.02        | 53.8M   | 2.0% |
| Checksum (enhanced) | 0.02        | 51.0M   | 2.6% |

### Full Transactions (PTY loopback)

| Operation            | Median (us) | Calls/s | CV%  |
| -------------------- | ----------- | ------- | ---- |
| Send header          | 0.76        | 1.3M    | 5.2% |
| Send frame (4 B)     | 1.44        | 694.9K  | 4.1% |
| Raw UART write (7 B) | 0.59        | 1.7M    | 1.0% |

### Profiler Analysis

| Hotspot               | Self-Time | Type          |
| --------------------- | --------- | ------------- |
| `write` (glibc)       | 56.4%     | Syscall-bound |
| `poll`                | 15.0%     | Syscall-bound |
| `read` (glibc)        | 9.1%      | Syscall-bound |
| `ioctl` (tcsendbreak) | 5.2%      | Syscall-bound |
| `calculateChecksum`   | 23.4%     | CPU-bound     |
| `FrameBuffer::append` | 14.9%     | CPU-bound     |

Full transactions are 85.7% kernel time (syscall-bound). Frame building is CPU-bound in checksum and buffer append.

### Memory Footprint

| Component       | Stack | Heap |
| --------------- | ----- | ---- |
| `LinController` | ~64 B | 0 B  |
| `FrameBuffer`   | 20 B  | 0 B  |
| `LinConfig`     | 16 B  | 0 B  |
| `LinStats`      | 64 B  | 0 B  |

---

## 4. Design Principles

- **UART composition** -- `LinController` holds a reference to `UartDevice`; does not own it
- **Break via ioctl** -- Break field generated via `TCSBRK` ioctl (Linux), no baud rate trick
- **No allocation on I/O path** -- All buffers are caller-provided; `FrameBuffer` is stack-allocated
- **Collision detection** -- Optional readback comparison after each write via `enableCollisionDetection`
- **Dual checksum support** -- `ChecksumType::CLASSIC` (LIN 1.x, data only) and `ENHANCED` (LIN 2.x, PID + data)
- **ByteTrace support** -- Optional byte-level debugging via `protocols/common/ByteTrace` mixin
- **Not thread-safe** -- Concurrent access to a single controller requires external synchronization
- **Linux-only** -- Requires PTY or UART device; break generation uses Linux `TCSBRK`

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
  ERROR_CHECKSUM,
  ERROR_SYNC,
  ERROR_PARITY,
  ERROR_FRAME,
  ERROR_NO_RESPONSE,
  ERROR_BUS_COLLISION,
  ERROR_BREAK
};

const char* toString(Status s) noexcept;  // @note RT-safe
```

### LinConfig

```cpp
struct LinConfig {
  std::uint32_t baudRate{19200};               // 9600, 10417, or 19200
  ChecksumType checksumType{ENHANCED};         // CLASSIC (LIN 1.x) or ENHANCED (LIN 2.x)
  std::uint8_t breakThreshold{11};             // Break detection threshold (bit times)
  std::uint8_t interByteTimeoutBits{14};       // Max inter-byte gap
  std::uint16_t responseTimeoutMs{50};         // Slave response timeout
  bool enableCollisionDetection{true};         // Readback verification

  /// @note RT-safe: O(1), no allocation.
  UartConfig toUartConfig() const noexcept;

  /// @note RT-safe: O(1).
  std::uint32_t interByteTimeoutUs() const noexcept;

  /// @note RT-safe: O(1).
  std::uint32_t frameSlotTimeUs(std::size_t dataLen) const noexcept;
};
```

### LinController

```cpp
class LinController : public ByteTrace {
public:
  /// @note The UART device must outlive this controller.
  explicit LinController(UartDevice& uart) noexcept;

  /// @note NOT RT-safe: Configures underlying UART.
  Status configure(const LinConfig& config) noexcept;

  /// @note RT-safe: O(1).
  bool isConfigured() const noexcept;
  const LinConfig& config() const noexcept;

  // Master API

  /// @note RT-safe: Bounded syscall (TCSBRK ioctl).
  Status sendBreak() noexcept;

  /// @note RT-safe: Bounded syscalls.
  Status sendHeader(std::uint8_t frameId) noexcept;

  /// @note RT-safe: Bounded by response timeout.
  Status receiveResponse(std::uint8_t frameId, FrameBuffer& response,
                         ParsedFrame& parsed) noexcept;

  /// @note RT-safe: Bounded by response timeout.
  Status receiveResponse(std::uint8_t frameId, std::size_t dataLen,
                         FrameBuffer& response, ParsedFrame& parsed) noexcept;

  /// @note RT-safe: Bounded syscalls.
  Status sendFrame(std::uint8_t frameId, const std::uint8_t* data,
                   std::size_t dataLen) noexcept;

  /// @note RT-safe: Bounded by response timeout.
  Status requestFrame(std::uint8_t frameId, FrameBuffer& response,
                      ParsedFrame& parsed) noexcept;

  /// @note RT-safe: Bounded by response timeout.
  Status requestFrame(std::uint8_t frameId, std::size_t dataLen,
                      FrameBuffer& response, ParsedFrame& parsed) noexcept;

  // Slave API

  /// @note RT-safe: Bounded by response timeout.
  Status waitForHeader(std::uint8_t& frameId) noexcept;

  /// @note RT-safe: Bounded by timeout.
  Status waitForHeader(std::uint8_t& frameId, std::uint16_t timeoutMs) noexcept;

  /// @note RT-safe: Bounded syscalls.
  Status respondToHeader(std::uint8_t frameId, const std::uint8_t* data,
                         std::size_t dataLen) noexcept;

  /// @note RT-safe: O(1).
  const LinStats& stats() const noexcept;
  void resetStats() noexcept;
};
```

### Frame Helpers (LinFrame.hpp)

```cpp
// Frame ID utilities
constexpr bool isValidFrameId(std::uint8_t id) noexcept;       // @note RT-safe
constexpr bool isDiagnosticFrame(std::uint8_t id) noexcept;    // @note RT-safe
constexpr std::uint8_t calculatePid(std::uint8_t id) noexcept; // @note RT-safe
constexpr bool verifyPidParity(std::uint8_t pid) noexcept;     // @note RT-safe
constexpr std::size_t dataLengthFromId(std::uint8_t id) noexcept; // @note RT-safe

// Checksum
std::uint8_t calculateChecksum(const std::uint8_t* data, std::size_t len,
                               std::uint8_t pid, ChecksumType type) noexcept; // @note RT-safe
bool verifyChecksum(const std::uint8_t* data, std::size_t len,
                    std::uint8_t checksum, std::uint8_t pid,
                    ChecksumType type) noexcept;  // @note RT-safe

// Frame building (RT-safe, no allocation)
Status buildHeader(FrameBuffer& buf, std::uint8_t frameId) noexcept;
Status buildResponse(FrameBuffer& buf, std::uint8_t pid, const std::uint8_t* data,
                     std::size_t dataLen, ChecksumType type) noexcept;
Status buildFrame(FrameBuffer& buf, std::uint8_t frameId, const std::uint8_t* data,
                  std::size_t dataLen, ChecksumType type) noexcept;

// Frame parsing (RT-safe, no allocation)
Status parseResponse(const std::uint8_t* data, std::size_t len, std::uint8_t pid,
                     std::size_t expectedDataLen, ChecksumType type,
                     ParsedFrame& result) noexcept;
Status parseFrame(const std::uint8_t* frame, std::size_t len,
                  ChecksumType type, ParsedFrame& result) noexcept;
```

---

## 6. Usage Examples

### Master: Request Data from Slave

```cpp
#include "LinController.hpp"
#include "UartAdapter.hpp"

namespace fl = apex::protocols::fieldbus::lin;
namespace su = apex::protocols::serial::uart;

su::UartAdapter uart("/dev/ttyUSB0");
fl::LinController lin(uart);

fl::LinConfig cfg;
cfg.baudRate = 19200;
cfg.checksumType = fl::ChecksumType::ENHANCED;
lin.configure(cfg);

fl::FrameBuffer response;
fl::ParsedFrame parsed;
fl::Status status = lin.requestFrame(0x10, response, parsed);

if (status == fl::Status::SUCCESS) {
  // parsed.data[0..parsed.dataLength-1] contains received bytes
}
```

### Master: Send Data to Slave

```cpp
const std::uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
fl::Status status = lin.sendFrame(0x20, data, sizeof(data));
```

### Slave Mode

```cpp
std::uint8_t frameId = 0;
fl::Status status = lin.waitForHeader(frameId);

if (status == fl::Status::SUCCESS && frameId == 0x30) {
  const std::uint8_t reply[] = {0x12, 0x34};
  lin.respondToHeader(frameId, reply, sizeof(reply));
}
```

### Low-Level Frame Building

```cpp
#include "LinFrame.hpp"

namespace fl = apex::protocols::fieldbus::lin;

fl::FrameBuffer frame;
const std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
fl::buildFrame(frame, 0x10, data, sizeof(data), fl::ChecksumType::ENHANCED);

fl::ParsedFrame parsed;
fl::parseFrame(frame.data, frame.length, fl::ChecksumType::ENHANCED, parsed);
```

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 87    | Yes                   |
| `ptst/`   | Performance tests | 13    | No (manual)           |

---

## 8. See Also

- `protocols/serial/uart/` -- UART transport (physical layer for LIN)
- `protocols/fieldbus/can/` -- CAN bus (higher-speed fieldbus alternative)
- `protocols/fieldbus/modbus/` -- Modbus (industrial fieldbus protocol)
- `utilities/checksums/crc/` -- CRC utilities used by fieldbus protocols
