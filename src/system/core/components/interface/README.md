# Interface Library

**Namespace:** `system_core::interface`
**Platform:** Linux (POSIX)
**C++ Standard:** C++23

External command/telemetry interface for bidirectional communication with ground systems or external clients over TCP using the APROTO protocol with configurable framing (SLIP or COBS).

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [Architecture](#4-architecture)
5. [Key Features](#5-key-features)
6. [API Reference](#6-api-reference)
7. [Usage Examples](#7-usage-examples)
8. [Requirements](#8-requirements)
9. [Testing](#9-testing)
10. [See Also](#10-see-also)

---

## 1. Quick Reference

| Component               | Purpose                                     | RT-Safe |
| ----------------------- | ------------------------------------------- | ------- |
| `InterfaceBase`         | Abstract socket management, RX/TX pipes     | Partial |
| `ApexInterface`         | Concrete APROTO implementation with routing | Partial |
| `BufferPool`            | Lock-free RT-safe buffer pool               | Yes     |
| `ComponentQueues`       | Per-component SPSC queue pairs              | Yes     |
| `MessageBuffer`         | Zero-copy message buffer with refcount      | Yes     |
| `ApexInterfaceTunables` | Configuration struct (host, port, framing)  | Yes     |
| `Status`                | Typed status codes                          | Yes     |

### Quick Example

```cpp
#include "src/system/core/components/interface/inc/ApexInterface.hpp"

using system_core::interface::ApexInterface;
using system_core::interface::ApexInterfaceTunables;
using system_core::interface::FramingType;
using system_core::interface::Status;

// Create interface
ApexInterface iface;

// Configure tunables
ApexInterfaceTunables tun{};
std::snprintf(tun.host.data(), tun.host.size(), "0.0.0.0");
tun.port = 9000;
tun.framing = FramingType::SLIP;

// Configure and start
Status st = iface.configure(tun);
if (st != Status::SUCCESS) {
  // Handle error
}

// Connect registry for component routing
iface.setRegistry(&registry);

// Poll in scheduler task or main loop
iface.pollSockets(10);  // 10ms timeout
```

---

## 2. When to Use

| Scenario                                                   | Use This Library?                                         |
| ---------------------------------------------------------- | --------------------------------------------------------- |
| Bidirectional cmd/tlm over TCP with APROTO routing         | Yes -- `ApexInterface`                                    |
| Per-component async command/telemetry queues               | Yes -- `ComponentQueues`                                  |
| Zero-copy message passing between interface and components | Yes -- `BufferPool` + `MessageBuffer`                     |
| Internal component-to-component messaging                  | Yes -- `postInternalCommand()` / `postMulticastCommand()` |
| Raw TCP/UDP socket management                              | No -- use `protocols/network/tcp` or `udp` directly       |
| Serial/UART communication                                  | No -- use `protocols/serial/uart`                         |

**Design intent:** External communication component for the executive. Single TCP socket with APROTO protocol, configurable framing (SLIP/COBS), lock-free queue routing to registered components. All runtime operations (poll, drain, route) are RT-safe.

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point, 10000 cycles.

### Buffer Pool Operations

| Operation                  | Median (us) | Calls/s | CV%  |
| -------------------------- | ----------- | ------- | ---- |
| Acquire + release cycle    | 0.077       | 13.1M   | 4.2% |
| Acquire + copy + release   | 0.084       | 11.9M   | 7.3% |
| Batch acquire/release (32) | 2.39        | 419K    | 3.6% |
| Refcount release           | 0.124       | 8.0M    | 2.8% |

### Queue Operations

| Operation            | Median (us) | Calls/s | CV%  |
| -------------------- | ----------- | ------- | ---- |
| SPSC push + pop      | 0.087       | 11.5M   | 2.3% |
| MPMC push + pop      | 0.106       | 9.4M    | 4.9% |
| Queue manager lookup | 0.061       | 16.5M   | 4.3% |

### Internal Bus

| Operation                     | Median (us) | Calls/s | CV%  |
| ----------------------------- | ----------- | ------- | ---- |
| Post internal command         | 0.138       | 7.2M    | 1.6% |
| Post internal telemetry       | 0.137       | 7.3M    | 7.2% |
| Multicast command (4 targets) | 0.501       | 2.0M    | 1.0% |
| Broadcast command (8 targets) | 1.258       | 795K    | 0.4% |

### Full Pipeline

| Operation                    | Median (us) | Calls/s | CV%  |
| ---------------------------- | ----------- | ------- | ---- |
| RX pipeline (decode + route) | 0.195       | 5.1M    | 3.4% |
| TX pipeline (encode + frame) | 0.847       | 1.2M    | 1.3% |
| Internal bus round-trip      | 0.124       | 8.0M    | 0.6% |

### Profiler Analysis (gperftools)

**TxPipeline (798 samples):**

| Function              | Self-Time | Type                               |
| --------------------- | --------- | ---------------------------------- |
| `slip::encode`        | 92.0%     | CPU-bound (SLIP framing dominates) |
| `aproto::buildHeader` | 2.1%      | CPU-bound (header construction)    |

**BufferPool AcquireRelease (77 samples):**

| Function                 | Self-Time | Type                         |
| ------------------------ | --------- | ---------------------------- |
| `LockFreeQueue::tryPush` | 24.7%     | CPU-bound (atomic CAS)       |
| `MessageBuffer::decRef`  | 16.9%     | CPU-bound (atomic decrement) |
| `LockFreeQueue::tryPop`  | 14.3%     | CPU-bound (atomic CAS)       |

**MulticastCommand4 (499 samples):**

| Function                          | Self-Time | Type                          |
| --------------------------------- | --------- | ----------------------------- |
| `_Mod_range_hashing::operator`    | 7.2%      | CPU-bound (hash table lookup) |
| `LockFreeQueue::tryPop`           | 5.4%      | CPU-bound (queue operations)  |
| `_Hashtable::_M_find_before_node` | 4.2%      | CPU-bound (hash chain walk)   |

---

## 4. Architecture

### Design Philosophy

**Single TCP socket for bidirectional cmd/tlm traffic with APROTO routing.**

| Component         | Responsibility                      |
| ----------------- | ----------------------------------- |
| `InterfaceBase`   | Socket lifecycle, RX/TX pipes       |
| `ApexInterface`   | APROTO routing, framing, system ops |
| `QueueManager`    | Per-component async queue pairs     |
| `ComponentQueues` | RT-safe SPSC queues for each model  |

This separation enables:

- Protocol-agnostic socket management in base class
- Configurable framing (SLIP/COBS) without socket changes
- Lock-free command/telemetry routing to components

### Library Structure

| File                               | Component               | Purpose                        |
| ---------------------------------- | ----------------------- | ------------------------------ |
| `InterfaceBase.hpp`                | `InterfaceBase`         | Abstract socket management     |
| `ApexInterface.hpp`                | `ApexInterface`         | Concrete APROTO implementation |
| `InterfaceStatus.hpp`              | `Status`                | Typed status codes             |
| `ApexInterfaceTunables.hpp`        | `ApexInterfaceTunables` | Configuration struct           |
| `ComponentQueues.hpp`              | `ComponentQueues`       | Per-component queue pairs      |
| `MessageBuffer.hpp`                | `MessageBuffer`         | Zero-copy message buffer       |
| `BufferPool.hpp`                   | `BufferPool`            | RT-safe buffer pool            |
| `InterfaceSocketConfiguration.hpp` | Config structs          | Socket endpoint config         |

---

## 5. Key Features

### APROTO Protocol

Commands and telemetry use the APROTO packet format:

```
APROTO Packet (14-byte header + payload)
+--------------------------------------------+
| Byte 0-1:  Magic ("AP" = 0x4150)           |
| Byte 2:    Version (1)                     |
| Byte 3:    Flags                           |
|   [0] internalOrigin (0=external, 1=internal)|
|   [1] isResponse (0=cmd, 1=response)       |
|   [2] ackRequested                         |
|   [3] crcPresent                           |
| Byte 4-7:  fullUid (target component)      |
| Byte 8-9:  Opcode                          |
| Byte 10-11: Sequence number                |
| Byte 12-13: Payload length                 |
+--------------------------------------------+
| Payload (0-65535 bytes)                    |
+--------------------------------------------+
| Optional CRC32 (4 bytes if flags.crc=1)    |
+--------------------------------------------+
```

### System Opcodes (0x0000-0x00FF)

| Opcode | Name       | Description             |
| ------ | ---------- | ----------------------- |
| 0x0000 | NOOP       | No operation, ACK only  |
| 0x0001 | PING       | Echo payload back       |
| 0x0002 | GET_STATUS | Return interface status |
| 0x0003 | RESET      | Reset decoder state     |

### Component Routing

Opcodes 0x0100+ are routed to components via Registry:

1. Lookup component by `fullUid`
2. Push to component's command inbox (async)
3. Component processes in `step()`, pushes response to tlm outbox
4. Interface drains tlm outbox during poll

### Zero-Copy Messaging

The interface uses a zero-copy architecture with pointer-based queues:

**MessageBuffer**: Actual-size allocation (not fixed 4KB) with APROTO metadata.

**BufferPool**: Lock-free RT-safe buffer pool with pre-allocated buffers.

**Pointer-based queues**: `SPSCQueue<MessageBuffer*>` instead of copying large structs.

**Interface-owned lifecycle**: ApexInterface manages all buffer allocation/release.

```cpp
// Interface owns BufferPool (pre-allocated at configure())
// Buffers allocated from pool on RX, released after dispatch

// Components receive handleCommand() callback (no buffer knowledge)
void MyComponent::handleCommand(const AprotoHeader& hdr,
                                apex::compat::rospan<std::uint8_t> payload) {
  // Process command directly from buffer
  // No copying, no buffer management
}

// Internal messaging uses same zero-copy pattern
iface.postInternalCommand(targetUid, opcode, payload);  // Interface allocates buffer
```

**Memory savings**: 99.8% reduction (384KB → 768 bytes per component)

### RT-Safety Summary

| Operation                     | RT-Safe | Notes                               |
| ----------------------------- | ------- | ----------------------------------- |
| `configure()`                 | No      | Network setup, allocations          |
| `shutdown()`                  | No      | Tears down servers                  |
| `pollSockets()`               | Yes     | Bounded work per call               |
| `allocateQueues()`            | No      | Before freeze only                  |
| `getQueues()`                 | Yes     | O(1) lookup                         |
| `drainTelemetryOutboxes()`    | Yes     | Lock-free queue operations          |
| `drainCommandsToComponents()` | Yes     | Lock-free, calls component handlers |

### Dedicated Component Log

Interface creates a dedicated log at `logs/interface.log`:

```cpp
iface.initInterfaceLog(logDir);
iface.configure(tun);  // Creates interface.log
```

---

## 6. API Reference

### 6.1 ApexInterfaceTunables

| Field                         | Type        | Default   | Purpose                       |
| ----------------------------- | ----------- | --------- | ----------------------------- |
| `host`                        | char[64]    | 127.0.0.1 | Bind address                  |
| `port`                        | uint16_t    | 9000      | TCP server port               |
| `framing`                     | FramingType | SLIP      | Framing protocol              |
| `crcEnabled`                  | uint8_t     | 0         | Enable CRC in APROTO packets  |
| `cmdQueueCapacity`            | uint16_t    | 32        | Per-component cmd queue size  |
| `tlmQueueCapacity`            | uint16_t    | 64        | Per-component tlm queue size  |
| `maxPayloadBytes`             | uint16_t    | 4096      | Max APROTO payload size       |
| `pollTimeoutMs`               | uint32_t    | 10        | Socket poll timeout           |
| `droppedFrameReportThreshold` | uint16_t    | 1         | NAK after N drops (0=disable) |

### 6.2 InterfaceBase

| Method                      | Purpose                          |
| --------------------------- | -------------------------------- |
| `configureSockets(cfg)`     | Configure and start servers      |
| `shutdown()`                | Stop servers, clear queues       |
| `pollSockets(timeoutMs)`    | Advance all servers' event loops |
| `pollSocket(id, timeoutMs)` | Advance one server's event loop  |
| `popRxMessage(id, msg)`     | Pop decoded RX message           |
| `enqueueTxMessage(id, msg)` | Enqueue TX message               |

### 6.3 ApexInterface

| Method                         | Purpose                       |
| ------------------------------ | ----------------------------- |
| `configure(tunables)`          | Configure with tunables       |
| `setRegistry(reg)`             | Connect registry for routing  |
| `allocateQueues(fullUid)`      | Allocate component queue pair |
| `getQueues(fullUid)`           | Get component queue pair      |
| `freezeQueues()`               | Disable further allocation    |
| `drainTelemetryOutboxes()`     | Transmit pending telemetry    |
| `drainCommandsToComponents(n)` | Dispatch pending commands     |

### 6.4 Status Codes

| Status                      | Meaning                         |
| --------------------------- | ------------------------------- |
| `SUCCESS`                   | Operation completed             |
| `ERROR_NOT_INITIALIZED`     | Requires configure() first      |
| `ERROR_ALREADY_INITIALIZED` | Call shutdown() before reconfig |
| `ERROR_CONFIG`              | Invalid configuration           |
| `ERROR_CREATE_SERVER`       | Socket server creation failed   |
| `ERROR_BIND_OR_LISTEN`      | Socket bind/listen failed       |
| `ERROR_QUEUE_FULL`          | Queue full, message dropped     |
| `ERROR_COMPONENT_NOT_FOUND` | Unknown component fullUid       |

---

## 7. Usage Examples

### 7.1 Basic Interface Setup

```cpp
ApexInterface iface;
iface.initInterfaceLog(logDir);

ApexInterfaceTunables tun{};
tun.port = 9000;
tun.framing = FramingType::SLIP;

iface.configure(tun);
iface.setRegistry(&registry);
```

### 7.2 Polling Loop

```cpp
// In scheduler task or main loop
while (running) {
  // Drain telemetry first (pre-tick)
  iface.drainTelemetryOutboxes();

  // Dispatch commands to components
  iface.drainCommandsToComponents(10);  // Max 10 per component

  // Poll for new I/O
  iface.pollSockets(10);
}
```

### 7.3 Component Queue Registration

```cpp
// During component registration (before freeze)
for (auto* model : models) {
  registry.registerComponent(model->fullUid(), model->componentName(), model);
  iface.allocateQueues(model->fullUid());
}

// Freeze both registry and queues
registry.freeze();
iface.freezeQueues();
```

---

## 8. Requirements

### Build Dependencies

- C++17 compiler (GCC 10+, Clang 12+)
- fmt library (formatting)
- POSIX sockets (sys/socket.h, netinet/in.h)

### Runtime

- Linux (epoll-based event loop)
- Network interface for TCP binding

---

## 9. Testing

| Directory    | Type                   | Tests | Runs with `make test` |
| ------------ | ---------------------- | ----- | --------------------- |
| `apex/utst/` | Unit tests             | 33    | Yes                   |
| `apex/ptst/` | Performance benchmarks | 20    | No (manual)           |

### Test Organization

| Component     | Test File                 | Tests  |
| ------------- | ------------------------- | ------ |
| ApexInterface | `ApexInterface_uTest.cpp` | 4      |
| BufferPool    | `BufferPool_uTest.cpp`    | 29     |
| **Total**     |                           | **33** |

---

## 10. See Also

- `src/system/core/components/registry/` - Component lookup for command routing
- `src/system/core/infrastructure/protocols/aproto/` - APROTO protocol codec
- `src/system/core/infrastructure/protocols/framing/slip/` - SLIP framing
- `src/system/core/infrastructure/protocols/framing/cobs/` - COBS framing
- `src/system/core/infrastructure/protocols/network/tcp/` - TCP socket server
- `src/system/core/infrastructure/protocols/network/udp/` - UDP socket server
- `src/system/core/executive/` - ApexExecutive integration
- `src/utilities/concurrency/` - SPSCQueue for RT-safe messaging
