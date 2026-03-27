# Bluetooth RFCOMM Library

**Namespace:** `apex::protocols::wireless::bluetooth`
**Platform:** Linux (AF_BLUETOOTH/BTPROTO_RFCOMM)
**C++ Standard:** C++17
**Library:** `system_core_protocols_wireless_bluetooth`

Bluetooth RFCOMM (Radio Frequency Communication) serial port emulation over Bluetooth.

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

| Component          | Header                               | RT-Safe | Description                        |
| ------------------ | ------------------------------------ | ------- | ---------------------------------- |
| `Status`           | `RfcommStatus.hpp`                   | Yes     | Status codes for RFCOMM operations |
| `RfcommStats`      | `RfcommStats.hpp`                    | Yes     | I/O statistics tracking            |
| `BluetoothAddress` | `RfcommConfig.hpp`                   | Yes     | MAC address parsing/formatting     |
| `RfcommConfig`     | `RfcommConfig.hpp`                   | Yes     | Connection configuration           |
| `RfcommDevice`     | `RfcommDevice.hpp`                   | -       | Abstract device interface          |
| `RfcommAdapter`    | `RfcommAdapter.hpp`                  | Partial | Linux socket implementation        |
| `RfcommLoopback`   | `RfcommLoopback.hpp`                 | Partial | Test utility (socketpair)          |
| `ByteTrace`        | `protocols/common/inc/ByteTrace.hpp` | Yes     | Optional byte-level tracing        |

| Question                                       | Module           |
| ---------------------------------------------- | ---------------- |
| How do I connect to a Bluetooth device?        | `RfcommAdapter`  |
| How do I test without real Bluetooth hardware? | `RfcommLoopback` |
| How do I trace bytes in/out?                   | `ByteTrace`      |
| What configuration is needed for connection?   | `RfcommConfig`   |
| How do I monitor I/O statistics?               | `RfcommStats`    |

---

## 2. When to Use

| Scenario                                          | Use This Library?       |
| ------------------------------------------------- | ----------------------- |
| Stream-oriented Bluetooth serial replacement      | Yes -- RFCOMM           |
| Legacy device compatibility (SPP profile)         | Yes -- RFCOMM           |
| Low-power, infrequent packet exchange             | No -- use BLE GATT      |
| Mobile device integration (BLE preferred)         | No -- use BLE GATT      |
| Testing Bluetooth protocol logic without hardware | Yes -- `RfcommLoopback` |

---

## 3. Performance

### Throughput and Latency

| Operation       | Payload | Median (us) | Calls/s | CV%   |
| --------------- | ------- | ----------- | ------- | ----- |
| Write           | 64 B    | 0.567       | 1.76M   | 2.9%  |
| Write           | 4 KB    | 1.212       | 825.1K  | 10.8% |
| Read            | 32 B    | 0.854       | 1.17M   | 2.1%  |
| Round-trip Echo | 16 B    | 7.263       | 137.7K  | 15.3% |

### Overhead

| Operation         | Median (us) | Calls/s | CV%   |
| ----------------- | ----------- | ------- | ----- |
| `stats()` access  | 0.020       | 49.5M   | 29.4% |
| Write + ByteTrace | 0.645       | 1.55M   | 13.0% |

Round-trip jitter (CV=15.3%) reflects background echo thread scheduling, not adapter overhead.

### Profiler Analysis

| Hotspot                 | Self-Time | Type                       |
| ----------------------- | --------- | -------------------------- |
| `__libc_read` (glibc)   | 41.7%     | Syscall-bound (socket)     |
| `__libc_write` (glibc)  | 41.7%     | Syscall-bound (socket)     |
| `ByteTrace::invokeTrac` | 8.3%      | CPU-bound (trace dispatch) |

83.3% of CPU time is in kernel syscalls. IPC 1.23 confirms syscall-bound profile.
No userspace optimization opportunities.

### Memory Footprint

| Component        | Stack  | Heap |
| ---------------- | ------ | ---- |
| `RfcommAdapter`  | ~208 B | 0 B  |
| `RfcommLoopback` | ~16 B  | 0 B  |
| `RfcommConfig`   | ~24 B  | 0 B  |
| `RfcommStats`    | ~88 B  | 0 B  |

---

## 4. Design Principles

- **FD injection pattern** -- `RfcommAdapter(int fd)` constructor enables testing without hardware
- **Non-blocking I/O** -- `poll()`-based timeout handling; all I/O paths are bounded
- **ByteTrace support** -- optional byte-level debugging via `protocols/common/ByteTrace`
- **Statistics tracking** -- connection and I/O counters via `RfcommStats`
- **RT-safe I/O paths** -- bounded syscalls, no allocation in `read()`/`write()`
- **Linux-only** -- requires AF_BLUETOOTH kernel support (BlueZ stack)
- **Not thread-safe** -- concurrent I/O on a single adapter requires external synchronization

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
  ERROR_CONNECTION_REFUSED,
  ERROR_HOST_UNREACHABLE,
  ERROR_ALREADY_CONNECTED,
  ERROR_NOT_CONNECTED
};

const char* toString(Status s) noexcept;  // @note RT-safe
```

### BluetoothAddress

```cpp
struct BluetoothAddress {
  std::array<std::uint8_t, 6> bytes;

  /// @note NOT RT-safe: Parses string.
  static BluetoothAddress fromString(const char* str) noexcept;

  /// @note RT-safe: Formats to buffer.
  std::size_t toString(char* buf, std::size_t size) const noexcept;

  /// @note RT-safe.
  bool isValid() const noexcept;
  bool isBroadcast() const noexcept;
};
```

### RfcommConfig

```cpp
struct RfcommConfig {
  BluetoothAddress remoteAddress;
  std::uint8_t channel{1};           // RFCOMM channel 1-30
  int connectTimeoutMs{5000};
  int readTimeoutMs{1000};
  int writeTimeoutMs{1000};

  /// @note RT-safe.
  bool isValid() const noexcept;
};
```

### RfcommAdapter

```cpp
class RfcommAdapter : public RfcommDevice, public apex::protocols::ByteTrace {
public:
  /// @note NOT RT-safe: Default constructor.
  RfcommAdapter() noexcept;

  /// @note NOT RT-safe: FD injection (for testing).
  explicit RfcommAdapter(int connectedFd) noexcept;

  /// @note NOT RT-safe: Validates config.
  Status configure(const RfcommConfig& cfg);

  /// @note NOT RT-safe: Blocking syscalls.
  Status connect();

  /// @note NOT RT-safe: Close syscall.
  Status disconnect() noexcept;

  /// @note RT-safe: Bounded poll + read syscall.
  Status read(mutable_bytes_span buffer, std::size_t& bytesRead, int timeoutMs) noexcept;

  /// @note RT-safe: Bounded poll + write syscall.
  Status write(bytes_span data, std::size_t& bytesWritten, int timeoutMs) noexcept;

  /// @note RT-safe: O(1) struct access.
  const RfcommStats& stats() const noexcept;

  /// @note RT-safe: Atomic store.
  void setTraceEnabled(bool enabled) noexcept;

  /// @note RT-safe.
  bool isConnected() const noexcept;
  int fd() const noexcept;
};
```

### RfcommLoopback

```cpp
class RfcommLoopback {
public:
  /// @note NOT RT-safe: Opens socketpair.
  Status open() noexcept;

  /// @note NOT RT-safe: Closes fds.
  void close() noexcept;

  /// @note RT-safe.
  bool isOpen() const noexcept;

  /// @note RT-safe: Bounded poll + read/write.
  Status serverRead(mutable_bytes_span buffer, std::size_t& bytesRead, int timeoutMs) noexcept;
  Status serverWrite(bytes_span data, std::size_t& bytesWritten, int timeoutMs) noexcept;

  /// @note NOT RT-safe: Transfers ownership of client fd.
  int releaseClientFd() noexcept;
};
```

---

## 6. Usage Examples

### Production: Connect and Send

```cpp
#include "RfcommAdapter.hpp"
#include "RfcommConfig.hpp"

namespace bt = apex::protocols::wireless::bluetooth;

bt::RfcommAdapter adapter;
bt::RfcommConfig cfg;
cfg.remoteAddress = bt::BluetoothAddress::fromString("AA:BB:CC:DD:EE:FF");
cfg.channel = 1;

adapter.configure(cfg);
adapter.connect();

const std::uint8_t data[] = {0x01, 0x02, 0x03};
std::size_t written = 0;
adapter.write({data, sizeof(data)}, written, 1000);

adapter.disconnect();
```

### Testing with Loopback

```cpp
#include "RfcommAdapter.hpp"
#include "RfcommLoopback.hpp"
#include <gtest/gtest.h>

namespace bt = apex::protocols::wireless::bluetooth;

TEST(MyTest, RoundTrip) {
  bt::RfcommLoopback loopback;
  ASSERT_EQ(loopback.open(), bt::Status::SUCCESS);

  bt::RfcommAdapter adapter(loopback.releaseClientFd());

  const std::uint8_t remoteData[] = {0xDE, 0xAD};
  std::size_t written = 0;
  loopback.serverWrite({remoteData, sizeof(remoteData)}, written, 100);

  std::uint8_t buf[64];
  std::size_t bytesRead = 0;
  ASSERT_EQ(adapter.read({buf, sizeof(buf)}, bytesRead, 100), bt::Status::SUCCESS);
  EXPECT_EQ(bytesRead, 2u);
}
```

### ByteTrace Integration

```cpp
#include "RfcommAdapter.hpp"
#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"

namespace bt = apex::protocols::wireless::bluetooth;

bt::RfcommAdapter adapter;
adapter.attachTrace([](apex::protocols::TraceDirection dir,
                       const std::uint8_t* data, std::size_t len,
                       void*) noexcept {
  // Log bytes here
});
adapter.setTraceEnabled(true);
```

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test`  |
| --------- | ----------------- | ----- | ---------------------- |
| `utst/`   | Unit tests        | 74    | Yes                    |
| `dtst/`   | Development tests | 1     | No (hardware required) |
| `ptst/`   | Performance tests | 6     | No (manual)            |

---

## 8. See Also

- `protocols/serial/uart/` -- UART transport (similar socketpair test pattern)
- `protocols/common/` -- ByteTrace, common types
- `protocols/fieldbus/` -- CAN, LIN, Modbus transports
