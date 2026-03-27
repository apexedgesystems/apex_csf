# CAN Bus Library

**Namespace:** `apex::protocols::fieldbus::can`
**Platform:** Linux-only (SocketCAN)
**C++ Standard:** C++17

High-performance, frame-oriented CAN bus implementation with SocketCAN backend, designed for
real-time and embedded systems. Supports classic CAN (DLC 0-8) with zero-allocation I/O paths.

---

## Table of Contents

1. [Quick Start](#1-quick-start)
2. [Key Features](#2-key-features)
3. [Performance](#3-performance)
4. [API Reference](#4-api-reference)
5. [Common Workflows](#5-common-workflows)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Start

| Component       | Purpose                                 | RT-Safe I/O     |
| --------------- | --------------------------------------- | --------------- |
| `CanDevice`     | Abstract device interface               | Yes             |
| `CANBusAdapter` | SocketCAN backend (PF_CAN/SOCK_RAW)     | Yes             |
| `CanStats`      | Frame statistics counters               | Yes             |
| `CanEventLoop`  | Epoll-based event loop for multi-device | Yes (poll)      |
| `VCanInterface` | Virtual CAN helper for tests            | No (setup only) |

### Headers

```cpp
#include "src/system/core/protocols/fieldbus/can/inc/CanDevice.hpp"
#include "src/system/core/protocols/fieldbus/can/inc/CanBusAdapter.hpp"
#include "src/system/core/protocols/fieldbus/can/inc/CanStats.hpp"
#include "src/system/core/protocols/fieldbus/can/inc/CanEventLoop.hpp"
#include "src/system/core/protocols/fieldbus/can/inc/VCanInterface.hpp"
```

---

## 2. Key Features

| Scenario                             | Use CAN?              |
| ------------------------------------ | --------------------- |
| Vehicle/industrial bus communication | Yes                   |
| Real-time sensor networks            | Yes                   |
| Deterministic message delivery       | Yes                   |
| Multi-node broadcast messaging       | Yes                   |
| Need TCP/IP stack                    | No (use TCP/UDP)      |
| Same-machine IPC                     | No (use Unix sockets) |

### CAN vs Other Protocols

| Aspect         | CAN                    | TCP                 | UDP                 |
| -------------- | ---------------------- | ------------------- | ------------------- |
| **Use Case**   | Embedded/automotive    | Internet/reliable   | Real-time/telemetry |
| **Frame Size** | 0-8 bytes (classic)    | Stream (unlimited)  | Up to MTU           |
| **Latency**    | Sub-millisecond        | Milliseconds        | Microseconds        |
| **Delivery**   | Prioritized, broadcast | Guaranteed, ordered | Best-effort         |
| **Network**    | Fieldbus (physical)    | IP network          | IP network          |

### When to Choose CAN

- **Embedded systems:** Automotive ECUs, industrial controllers, robotics
- **Deterministic timing:** Prioritized arbitration ensures bounded latency
- **Broadcast messaging:** All nodes see all messages on the bus
- **Hardware timestamps:** Precise timing for control systems

### When to Choose Other Protocols

- **Large payloads:** CAN is limited to 8 bytes (classic); use TCP for larger data
- **IP network:** CAN requires dedicated hardware; use TCP/UDP for network communication
- **Same machine:** Unix sockets are faster for local IPC

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), virtual CAN loopback, 15 repeats per data point.

### Bus Throughput

CAN bus throughput is limited by the physical bus speed (bitrate), not the software:

| Bitrate  | Max Frame Rate  | Effective Data Rate |
| -------- | --------------- | ------------------- |
| 125 kbps | ~1,000 frames/s | ~8 KB/s             |
| 250 kbps | ~2,000 frames/s | ~16 KB/s            |
| 500 kbps | ~4,000 frames/s | ~32 KB/s            |
| 1 Mbps   | ~8,000 frames/s | ~64 KB/s            |

### Software Latency (SocketCAN)

| Operation              | Median Latency | Throughput    | CV%  |
| ---------------------- | -------------- | ------------- | ---- |
| Loopback (1 B)         | 0.773 us       | 1.3M frames/s | 1.6% |
| Loopback (8 B)         | 0.766 us       | 1.3M frames/s | 1.0% |
| Send (fire-and-forget) | 0.540 us       | 1.9M frames/s | 9.7% |
| Event loop (with data) | 1.177 us       | 850K cycles/s | 0.9% |
| Stats overhead         | 0.774 us       | 1.3M frames/s | 4.7% |

### Profiler Analysis

gperftools sampling (100K cycles) identifies 90.2% of CPU time in the kernel CAN socket
write syscall (`__GI___libc_write`). perf hardware counters confirm IPC of 2.49 with 0.33%
branch misprediction and negligible cache pressure. Syscall-bound: no userspace optimization
opportunity exists.

### Memory Footprint

| Component       | Stack      | Heap                |
| --------------- | ---------- | ------------------- |
| `CANBusAdapter` | ~200 bytes | 0 (after configure) |
| `CanFrame`      | 24 bytes   | 0                   |
| `CanConfig`     | ~80 bytes  | Per-filter vector   |

---

## 4. API Reference

### CanDevice (Abstract Interface)

**RT-safe:** Yes (no allocations on I/O paths)

```cpp
class CanDevice {
public:
  virtual const std::string& description() const = 0;
  virtual const std::string& channel() const = 0;
  virtual Status configure(const CanConfig& cfg) = 0;
  virtual Status recv(CanFrame& outFrame, int timeoutMs) = 0;
  virtual Status send(const CanFrame& frame, int timeoutMs) = 0;
  virtual void enableLogging(bool enable) = 0;
  virtual CanStats stats() const noexcept = 0;   // RT-safe
  virtual void resetStats() noexcept = 0;        // RT-safe
  // Batch receive: drain multiple frames in one call
  virtual size_t recvBatch(CanFrame* frames, size_t maxFrames, int timeoutMs) = 0;
};
```

### Key Types

```cpp
struct CanId {
  uint32_t id;      // 11-bit (standard) or 29-bit (extended)
  bool extended;    // True for 29-bit extended frames
  bool remote;      // True for RTR frames
  bool error;       // True for error frames
};

struct CanFrame {
  CanId canId;
  uint8_t dlc;                    // Payload length (0..8)
  std::array<uint8_t, 8> data;    // First dlc bytes are valid
  std::optional<uint64_t> timestampNs;  // Optional HW timestamp
};

struct CanConfig {
  uint32_t bitrate = 500000;      // Not applied by SocketCAN adapter
  bool listenOnly = false;        // Not applied by SocketCAN adapter
  bool loopback = false;          // Kernel loopback (CAN_RAW_LOOPBACK)
  std::vector<CanFilter> filters; // Empty = accept all
};

// Fixed-size alternative (no heap allocation)
template <size_t N> struct CanConfigFixed {
  uint32_t bitrate = 500000;
  bool listenOnly = false;
  bool loopback = false;
  std::array<CanFilter, N> filters;
  size_t filterCount = 0;

  bool addFilter(const CanFilter& f) noexcept;  // RT-safe
  void clearFilters() noexcept;                 // RT-safe
  CanConfig toCanConfig() const;                // Allocates (for API compat)
};

struct CanStats {
  uint64_t framesSent;        // Total frames transmitted
  uint64_t framesReceived;    // Total frames received
  uint64_t errorFrames;       // Error frames received (canId.error == true)
  uint64_t sendWouldBlock;    // Times send() returned WOULD_BLOCK
  uint64_t recvWouldBlock;    // Times recv() returned WOULD_BLOCK
  uint64_t sendErrors;        // Times send() returned ERROR_*
  uint64_t recvErrors;        // Times recv() returned ERROR_*
  uint64_t bytesTransmitted;  // Total payload bytes sent
  uint64_t bytesReceived;     // Total payload bytes received

  void reset() noexcept;                        // Zero all counters
  uint64_t totalFrames() const noexcept;        // sent + received
  uint64_t totalErrors() const noexcept;        // all error counters
  uint64_t totalBytes() const noexcept;         // all bytes
};
```

### Status Codes

| Status                 | Meaning                          |
| ---------------------- | -------------------------------- |
| `SUCCESS`              | Operation completed successfully |
| `WOULD_BLOCK`          | Not ready now (nonblocking mode) |
| `ERROR_TIMEOUT`        | True OS/backend timeout          |
| `ERROR_CLOSED`         | Device/channel closed            |
| `ERROR_INVALID_ARG`    | Bad argument (e.g., DLC > 8)     |
| `ERROR_NOT_CONFIGURED` | Called before configure()        |
| `ERROR_IO`             | Backend I/O or OS error          |
| `ERROR_UNSUPPORTED`    | Feature not supported            |

### Timeout Semantics

| timeoutMs | Behavior                       |
| --------- | ------------------------------ |
| `< 0`     | Block until I/O is ready       |
| `== 0`    | Nonblocking poll (immediate)   |
| `> 0`     | Bounded wait (up to timeoutMs) |

---

## 5. Common Workflows

### Basic Send/Receive

```cpp
#include "src/system/core/protocols/fieldbus/can/inc/CanBusAdapter.hpp"

using namespace apex::protocols::fieldbus::can;

int main() {
  CANBusAdapter adapter("Controller", "can0");

  CanConfig cfg{};
  cfg.loopback = true;
  if (adapter.configure(cfg) != Status::SUCCESS) {
    return 1;
  }

  // Send a frame
  CanFrame tx{};
  tx.canId = CanId{.id = 0x123, .extended = false};
  tx.dlc = 4;
  tx.data = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0};
  adapter.send(tx, 100);

  // Receive a frame
  CanFrame rx{};
  if (adapter.recv(rx, 100) == Status::SUCCESS) {
    // Process rx.canId, rx.dlc, rx.data
  }
}
```

### Acceptance Filters

```cpp
CanConfig cfg{};
cfg.loopback = true;

// Accept standard ID 0x123 exactly
cfg.filters.push_back(CanFilter{
  .id = 0x123,
  .mask = 0x7FF,      // All 11 bits must match
  .extended = false
});

// Accept extended IDs 0x1ABCD00-0x1ABCDFF
cfg.filters.push_back(CanFilter{
  .id = 0x1ABCD00,
  .mask = 0x1FFFFF00, // Upper 21 bits must match
  .extended = true
});

adapter.configure(cfg);
```

### Batch Frame Draining

```cpp
CANBusAdapter adapter("ECU", "can0");
adapter.configure(CanConfig{});

// Drain all available frames (up to 16) in one call
std::array<CanFrame, 16> buffer;
std::size_t count = adapter.recvBatch(buffer.data(), buffer.size(), 100);

for (std::size_t i = 0; i < count; ++i) {
  // Process buffer[i].canId, buffer[i].dlc, buffer[i].data
}
```

### Event-Driven Reception

```cpp
#include "src/system/core/protocols/fieldbus/can/inc/CanEventLoop.hpp"

CanEventLoop loop;
loop.init();

CANBusAdapter can0("ECU0", "can0");
can0.configure(CanConfig{});

// RT-safe callback using Delegate (no heap allocation)
auto handler = [](void* ctx, CANBusAdapter* adapter, uint32_t events) noexcept {
  CanFrame frame;
  while (adapter->recv(frame, 0) == Status::SUCCESS) {
    // Process frame
  }
};

loop.add(&can0, CanEventCallback{handler, nullptr});

// Event loop (blocks up to 100ms per iteration)
while (running) {
  loop.poll(100);
}
```

### Statistics Monitoring

```cpp
CANBusAdapter adapter("ECU", "can0");
adapter.configure(CanConfig{});

// ... perform I/O operations ...

// Get current statistics (RT-safe, returns by value)
CanStats s = adapter.stats();
std::cout << "Frames sent: " << s.framesSent << "\n";
std::cout << "Frames received: " << s.framesReceived << "\n";
std::cout << "Total bytes: " << s.totalBytes() << "\n";
std::cout << "Errors: " << s.totalErrors() << "\n";

// Reset for next measurement period
adapter.resetStats();
```

### Virtual CAN for Testing

```cpp
#include "src/system/core/protocols/fieldbus/can/inc/VCanInterface.hpp"

using namespace apex::protocols::fieldbus::can::util;

VCanInterface vcan("vcan0", /*autoTeardown=*/true, /*useSudo=*/true);
if (!vcan.setup()) {
  // Requires NET_ADMIN privileges
  return 1;
}

CANBusAdapter adapter("Test", vcan.interfaceName());
// ... run tests ...
// vcan destructor auto-tears down the interface
```

---

## 6. Requirements

- Linux kernel with SocketCAN support (`CONFIG_CAN`, `CONFIG_CAN_RAW`)
- `vcan` kernel module for virtual CAN (tests only)
- NET_ADMIN capability for vcan interface creation
- Physical CAN hardware or virtual CAN interface at runtime

---

## 7. Testing

Run tests using the standard Docker workflow:

```bash
# Build
make compose-debug

# Run all tests
make compose-testp

# Run CAN library tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L can
```

### Test Organization

| Directory | Type | Files | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `tst/`    | Unit | 12    | Yes                   |
| `ptst/`   | Perf | 1     | No (manual)           |

### Expected Output

```
100% tests passed, 0 tests failed
```

---

## 8. See Also

- **Fieldbus Overview** (`../`) - Protocol selection guide
- **Network Protocols** (`../../network/`) - TCP/UDP for IP networks
- [Linux SocketCAN](https://www.kernel.org/doc/html/latest/networking/can.html) - Kernel documentation
- [CAN Specification](https://www.bosch-semiconductors.com/ip-modules/can-ip-modules/) - Bosch CAN 2.0
