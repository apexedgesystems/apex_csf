# SPI Protocol Library

**Namespace:** `apex::protocols::spi`
**Platform:** Linux (spidev user-space driver)
**C++ Standard:** C++17
**Library:** `system_core_protocols_spi`

Linux SPI master device abstraction using the spidev user-space interface.

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
| `Status`          | `SpiStatus.hpp`       | Yes     | Status codes for SPI operations         |
| `SpiStats`        | `SpiStats.hpp`        | Yes     | I/O statistics tracking                 |
| `SpiConfig`       | `SpiConfig.hpp`       | Yes     | Mode, speed, and bit configuration      |
| `SpiDevice`       | `SpiDevice.hpp`       | -       | Abstract device interface               |
| `SpiAdapter`      | `SpiAdapter.hpp`      | Partial | Linux spidev implementation             |
| `SpiSocketDevice` | `SpiSocketDevice.hpp` | Partial | Unix socketpair transport for emulation |

| Question                                           | Module            |
| -------------------------------------------------- | ----------------- |
| How do I transfer data to/from an SPI peripheral?  | `SpiAdapter`      |
| How do I test SPI logic without real hardware?     | `SpiSocketDevice` |
| How do I configure SPI mode, speed, and bit order? | `SpiConfig`       |
| How do I monitor SPI I/O statistics?               | `SpiStats`        |
| How do I batch multiple transfers atomically?      | `SpiAdapter`      |

---

## 2. When to Use

| Scenario                                              | Use This Library?      |
| ----------------------------------------------------- | ---------------------- |
| High-speed peripheral communication (flash, ADC, DAC) | Yes -- SpiAdapter      |
| Full-duplex transfers with chip-select control        | Yes -- SpiAdapter      |
| Testing SPI protocol logic without hardware           | Yes -- SpiSocketDevice |
| Low-speed bus with many devices sharing 2 wires       | No -- use I2C          |
| Serial stream with no chip-select                     | No -- use UART         |

---

## 3. Performance

### Throughput and Latency

| Operation | Payload | Median (us) | Calls/s | CV%  |
| --------- | ------- | ----------- | ------- | ---- |
| Transfer  | 64 B    | 6.04        | 165.6K  | 6.9% |
| Transfer  | 1 KB    | 6.38        | 156.7K  | 6.3% |
| Transfer  | 4 KB    | 6.79        | 147.3K  | 4.5% |
| Write     | 64 B    | 5.98        | 167.2K  | 9.0% |
| Read      | 64 B    | 6.22        | 160.8K  | 5.9% |

### Overhead

| Operation        | Median (us) | Calls/s | CV%   |
| ---------------- | ----------- | ------- | ----- |
| `stats()` access | 0.013       | 79.4M   | 15.7% |

### Profiler Analysis

| Hotspot         | Self-Time | Type          |
| --------------- | --------- | ------------- |
| `read` (glibc)  | 68.5%     | Syscall-bound |
| `write` (glibc) | 26.5%     | Syscall-bound |

95.2% of CPU time is in kernel syscalls. IPC consistent with syscall-bound profile.

### Memory Footprint

| Component         | Stack | Heap                       |
| ----------------- | ----- | -------------------------- |
| `SpiAdapter`      | 304 B | ~32 B (device path string) |
| `SpiSocketDevice` | 112 B | 0 B                        |
| `SpiConfig`       | 16 B  | 0 B                        |
| `SpiStats`        | 40 B  | 0 B                        |

---

## 4. Design Principles

- **spidev backend** -- Wraps Linux `/dev/spidevX.Y` with ioctl-based configuration
- **Full-duplex native** -- SPI is inherently bidirectional; API reflects this with `transfer()`
- **Batch transfer** -- Multi-transfer `SPI_IOC_MESSAGE` for atomic CS control across messages
- **SpiSocketDevice** -- Unix socketpair transport for hardware emulation and testing without spidev
- **ByteTrace support** -- Optional byte-level debugging via `protocols/common/ByteTrace` mixin
- **Statistics tracking** -- Transfer and byte counters via `SpiStats`
- **RT-safe I/O paths** -- Bounded syscalls after `configure()`, no allocation on hot path
- **Linux-only** -- Requires spidev kernel driver (`CONFIG_SPI_SPIDEV`)
- **Not thread-safe** -- Concurrent transfers on a single adapter require external synchronization

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
  ERROR_BUSY
};

const char* toString(Status s) noexcept;  // @note RT-safe
```

### SpiConfig

```cpp
struct SpiConfig {
  SpiMode mode = SpiMode::MODE_0;          // CPOL/CPHA
  BitOrder bitOrder = BitOrder::MSB_FIRST;
  std::uint8_t bitsPerWord = 8;
  std::uint32_t maxSpeedHz = 1000000;
  bool csHigh = false;
  bool threeWire = false;
  bool loopback = false;
  bool noCs = false;
  bool ready = false;

  /// @note RT-safe.
  bool isValid() const noexcept;
};
```

### SpiAdapter

```cpp
class SpiAdapter : public SpiDevice, public ByteTrace {
public:
  /// @note NOT RT-safe: Opens device path.
  explicit SpiAdapter(const std::string& devicePath);

  /// @note NOT RT-safe: Constructs path from bus/CS.
  SpiAdapter(std::uint32_t bus, std::uint32_t chipSelect);

  /// @note NOT RT-safe: Opens device, performs ioctls.
  Status configure(const SpiConfig& config) noexcept;

  /// @note RT-safe: Bounded ioctl, no allocation.
  Status transfer(bytes_span txData, mutable_bytes_span rxData, int timeoutMs) noexcept;

  /// @note RT-safe: Wrapper around transfer().
  Status write(bytes_span data, int timeoutMs) noexcept;

  /// @note RT-safe: Wrapper around transfer().
  Status read(mutable_bytes_span buffer, int timeoutMs) noexcept;

  /// @note RT-safe: Bounded ioctl, no allocation.
  Status transferBatch(const TransferDesc* transfers, std::size_t count, int timeoutMs) noexcept;

  /// @note NOT RT-safe: Releases file descriptor.
  Status close() noexcept;

  /// @note RT-safe: O(1), no allocation.
  bool isOpen() const noexcept;
  int fd() const noexcept;
  const SpiStats& stats() const noexcept;

  /// @note RT-safe: O(1), no allocation.
  void resetStats() noexcept;
};
```

### SpiSocketDevice

```cpp
class SpiSocketDevice : public SpiDevice {
public:
  /// @note NOT RT-safe: Stores fd.
  explicit SpiSocketDevice(int socketFd, bool ownsFd = true) noexcept;

  /// @note NOT RT-safe: Validates config.
  Status configure(const SpiConfig& config) noexcept;

  /// @note RT-safe: Bounded socket I/O.
  Status transfer(bytes_span txData, mutable_bytes_span rxData, int timeoutMs) noexcept;

  /// @note NOT RT-safe: Releases file descriptor.
  Status close() noexcept;
};
```

Wire protocol: `[length:4 LE][txData:length]` request, `[rxData:length]` response.

---

## 6. Usage Examples

### Production: Configure and Transfer

```cpp
#include "SpiAdapter.hpp"

namespace sp = apex::protocols::spi;

sp::SpiAdapter spi(0, 0);  // bus 0, chip-select 0

sp::SpiConfig cfg;
cfg.mode = sp::SpiMode::MODE_0;
cfg.maxSpeedHz = 1000000;
cfg.bitsPerWord = 8;

spi.configure(cfg);

// Full-duplex transfer
const std::uint8_t tx[] = {0x9F, 0x00, 0x00, 0x00};
std::uint8_t rx[4] = {};
spi.transfer({tx, sizeof(tx)}, {rx, sizeof(rx)}, 1000);
```

### Batch Transfers

```cpp
namespace sp = apex::protocols::spi;

sp::SpiAdapter::TransferDesc batch[2];

std::uint8_t cmd = 0x05;
batch[0].txBuf = &cmd;
batch[0].length = 1;
batch[0].csChange = false;  // keep CS asserted between transfers

std::uint8_t data[256];
batch[1].rxBuf = data;
batch[1].length = 256;

spi.transferBatch(batch, 2, 1000);
```

### Testing with SpiSocketDevice

```cpp
#include "SpiSocketDevice.hpp"
#include <sys/socket.h>

namespace sp = apex::protocols::spi;

int fds[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

sp::SpiSocketDevice device(fds[0], true);
sp::SpiConfig cfg;
device.configure(cfg);

// Peer (HW_MODEL) side: read [len:4][txData:len], write [rxData:len]
```

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 40    | Yes                   |
| `ptst/`   | Performance tests | 5     | No (manual)           |

---

## 8. See Also

- `protocols/serial/uart/` -- UART transport (similar PtyPair test pattern)
- `protocols/i2c/` -- I2C bus protocol (complement to SPI)
- `protocols/wireless/bluetooth/` -- Bluetooth RFCOMM (similar FD injection)
- `utilities/compatibility/` -- Span shims for C++17
