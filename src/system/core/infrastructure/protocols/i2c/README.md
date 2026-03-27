# I2C Protocol Library

**Namespace:** `apex::protocols::i2c`
**Platform:** Linux (i2c-dev user-space driver)
**C++ Standard:** C++17
**Library:** `system_core_protocols_i2c`

Linux I2C master device abstraction using the i2c-dev user-space interface.

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

| Component         | Header                | RT-Safe | Description                             |
| ----------------- | --------------------- | ------- | --------------------------------------- |
| `Status`          | `I2cStatus.hpp`       | Yes     | Status codes for I2C operations         |
| `I2cStats`        | `I2cStats.hpp`        | Yes     | I/O and error statistics tracking       |
| `I2cConfig`       | `I2cConfig.hpp`       | Yes     | Address mode and bus configuration      |
| `I2cDevice`       | `I2cDevice.hpp`       | -       | Abstract device interface               |
| `I2cAdapter`      | `I2cAdapter.hpp`      | Partial | Linux i2c-dev implementation            |
| `I2cSocketDevice` | `I2cSocketDevice.hpp` | Partial | Unix socketpair transport for emulation |

| Question                                          | Module            |
| ------------------------------------------------- | ----------------- |
| How do I read/write to an I2C peripheral?         | `I2cAdapter`      |
| How do I do an atomic register read (write-read)? | `I2cAdapter`      |
| How do I test I2C logic without real hardware?    | `I2cSocketDevice` |
| How do I configure 7-bit or 10-bit addressing?    | `I2cConfig`       |
| How do I scan for devices on the bus?             | `I2cAdapter`      |
| How do I monitor I2C I/O and NACK statistics?     | `I2cStats`        |

---

## 2. When to Use

| Scenario                                                 | Use This Library?      |
| -------------------------------------------------------- | ---------------------- |
| Many peripherals sharing a 2-wire bus (EEPROM, RTC, IMU) | Yes -- I2cAdapter      |
| Atomic register reads (write address, read data)         | Yes -- I2cAdapter      |
| Testing I2C protocol logic without hardware              | Yes -- I2cSocketDevice |
| High-speed full-duplex transfers                         | No -- use SPI          |
| Serial stream with no addressing                         | No -- use UART         |

---

## 3. Performance

### Throughput and Latency

| Operation | Payload | Median (us) | Calls/s | CV%  |
| --------- | ------- | ----------- | ------- | ---- |
| Write     | 8 B     | 6.47        | 154.5K  | 7.7% |
| Write     | 64 B    | 6.11        | 163.8K  | 6.8% |
| Read      | 8 B     | 5.99        | 166.9K  | 6.6% |
| Read      | 64 B    | 6.02        | 166.1K  | 9.2% |
| WriteRead | 1+8 B   | 6.95        | 143.9K  | 3.0% |

### Overhead

| Operation        | Median (us) | Calls/s | CV%  |
| ---------------- | ----------- | ------- | ---- |
| `stats()` access | 0.018       | 56.8M   | 2.0% |

### Profiler Analysis

| Hotspot         | Self-Time | Type          |
| --------------- | --------- | ------------- |
| `read` (glibc)  | 67.2%     | Syscall-bound |
| `write` (glibc) | 28.4%     | Syscall-bound |

95.6% of CPU time is in kernel syscalls. IPC consistent with syscall-bound profile.

### Memory Footprint

| Component         | Stack | Heap                       |
| ----------------- | ----- | -------------------------- |
| `I2cAdapter`      | 320 B | ~32 B (device path string) |
| `I2cSocketDevice` | 96 B  | 0 B                        |
| `I2cConfig`       | 4 B   | 0 B                        |
| `I2cStats`        | 72 B  | 0 B                        |

---

## 4. Design Principles

- **i2c-dev backend** -- Wraps Linux `/dev/i2c-X` with ioctl-based configuration
- **Combined transactions** -- `writeRead()` uses `I2C_RDWR` for atomic register reads without bus release
- **SMBus PEC** -- Optional Packet Error Checking for data integrity via `I2cConfig::enablePec`
- **I2cSocketDevice** -- Unix socketpair transport for hardware emulation and testing without i2c-dev
- **ByteTrace support** -- Optional byte-level debugging via `protocols/common/ByteTrace` mixin
- **Statistics tracking** -- Transfer, byte, and NACK error counters via `I2cStats`
- **RT-safe I/O paths** -- Bounded syscalls after `configure()`, no allocation on hot path
- **Linux-only** -- Requires i2c-dev kernel driver (`CONFIG_I2C_CHARDEV`)
- **Not thread-safe** -- Concurrent transactions on a single adapter require external synchronization

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
  ERROR_UNSUPPORTED,
  ERROR_BUSY,
  ERROR_NACK
};

const char* toString(Status s) noexcept;  // @note RT-safe
```

### I2cConfig

```cpp
struct I2cConfig {
  AddressMode addressMode = AddressMode::SEVEN_BIT;
  bool enablePec = false;
  bool forceAccess = false;
  std::uint8_t retryCount = 0;

  /// @note RT-safe.
  bool isValid() const noexcept;
};
```

### I2cAdapter

```cpp
class I2cAdapter : public I2cDevice, public ByteTrace {
public:
  /// @note NOT RT-safe: Opens device path.
  explicit I2cAdapter(const std::string& devicePath);

  /// @note NOT RT-safe: Constructs path from bus number.
  explicit I2cAdapter(std::uint32_t busNumber);

  /// @note NOT RT-safe: Opens device, performs ioctls.
  Status configure(const I2cConfig& config) noexcept;

  /// @note RT-safe: Single ioctl after configure.
  Status setSlaveAddress(std::uint16_t address) noexcept;

  /// @note RT-safe: Bounded ioctl, no allocation.
  Status read(mutable_bytes_span buffer, std::size_t& bytesRead, int timeoutMs) noexcept;

  /// @note RT-safe: Bounded ioctl, no allocation.
  Status write(bytes_span data, std::size_t& bytesWritten, int timeoutMs) noexcept;

  /// @note RT-safe: Bounded I2C_RDWR ioctl, no allocation.
  Status writeRead(bytes_span writeData, mutable_bytes_span readBuffer,
                   std::size_t& bytesRead, int timeoutMs) noexcept;

  /// @note RT-safe: Wrapper around writeRead().
  Status readRegister(std::uint8_t regAddr, std::uint8_t& value, int timeoutMs) noexcept;

  /// @note RT-safe: Wrapper around write().
  Status writeRegister(std::uint8_t regAddr, std::uint8_t value, int timeoutMs) noexcept;

  /// @note RT-safe: Wrapper around writeRead().
  Status readRegisters(std::uint8_t regAddr, mutable_bytes_span buffer,
                       std::size_t& bytesRead, int timeoutMs) noexcept;

  /// @note RT-safe: Quick write/read probe.
  bool probeDevice() noexcept;

  /// @note NOT RT-safe: Releases file descriptor.
  Status close() noexcept;

  /// @note RT-safe: O(1), no allocation.
  bool isOpen() const noexcept;
  int fd() const noexcept;
  const I2cStats& stats() const noexcept;
  void resetStats() noexcept;
};
```

### I2cSocketDevice

```cpp
class I2cSocketDevice : public I2cDevice {
public:
  /// @note NOT RT-safe: Stores fd.
  explicit I2cSocketDevice(int socketFd, bool ownsFd = true) noexcept;

  /// @note NOT RT-safe: Validates config.
  Status configure(const I2cConfig& config) noexcept;

  /// @note RT-safe: Single ioctl after configure.
  Status setSlaveAddress(std::uint16_t address) noexcept;

  /// @note RT-safe: Bounded socket I/O.
  Status read(mutable_bytes_span buffer, std::size_t& bytesRead, int timeoutMs) noexcept;
  Status write(bytes_span data, std::size_t& bytesWritten, int timeoutMs) noexcept;
  Status writeRead(bytes_span writeData, mutable_bytes_span readBuffer,
                   std::size_t& bytesRead, int timeoutMs) noexcept;

  /// @note NOT RT-safe: Releases file descriptor.
  Status close() noexcept;
};
```

Wire protocol: `[addr:2 LE][wLen:2 LE][rLen:2 LE][wData:wLen]` request,
`[status:1][rData:rLen]` response.

---

## 6. Usage Examples

### Production: Configure and Transfer

```cpp
#include "I2cAdapter.hpp"

namespace ic = apex::protocols::i2c;

ic::I2cAdapter i2c(1);  // /dev/i2c-1

ic::I2cConfig cfg;
cfg.addressMode = ic::AddressMode::SEVEN_BIT;
i2c.configure(cfg);

i2c.setSlaveAddress(0x50);  // EEPROM

// Write to a register
i2c.writeRegister(0x10, 0xFF, 1000);

// Read from a register
std::uint8_t value;
i2c.readRegister(0x00, value, 1000);
```

### Combined Write-Read (Atomic Register Read)

```cpp
namespace ic = apex::protocols::i2c;

// Read WHO_AM_I register (atomic: no bus release between write and read)
std::uint8_t regAddr = 0x0F;
std::uint8_t whoAmI;
std::size_t bytesRead;
i2c.writeRead({&regAddr, 1}, {&whoAmI, 1}, bytesRead, 1000);
```

### Testing with I2cSocketDevice

```cpp
#include "I2cSocketDevice.hpp"
#include <sys/socket.h>

namespace ic = apex::protocols::i2c;

int fds[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

ic::I2cSocketDevice device(fds[0], true);
ic::I2cConfig cfg;
device.configure(cfg);
device.setSlaveAddress(0x50);

// Peer (HW_MODEL) side: read [addr:2][wLen:2][rLen:2][wData:wLen],
// write [status:1][rData:rLen]
```

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 39    | Yes                   |
| `ptst/`   | Performance tests | 6     | No (manual)           |

---

## 8. See Also

- `protocols/spi/` -- SPI transport (similar device pattern, full-duplex)
- `protocols/serial/uart/` -- UART transport (similar PtyPair test pattern)
- `protocols/fieldbus/can/` -- CAN bus protocol
- `utilities/compatibility/` -- Span shims for C++17
