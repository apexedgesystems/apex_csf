# UDP Socket Library

**Namespace:** `apex::protocols::udp`
**Platform:** Linux-only
**C++ Standard:** C++17
**Library:** `system_core_protocols_network_udp`

High-performance, datagram-oriented UDP socket implementation with epoll-based event loop, designed for real-time and embedded systems. Supports multicast, batch I/O, and connected/unconnected modes.

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

| Component         | Purpose                                  | RT-Safe I/O |
| ----------------- | ---------------------------------------- | ----------- |
| `UdpSocketServer` | Receive datagrams, reply to senders      | Yes         |
| `UdpSocketClient` | Send datagrams, connected or unconnected | Yes         |

### Headers

```cpp
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketServer.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/udp/inc/UdpTypes.hpp"
```

---

## 2. When to Use

| Scenario                                | Use UDP?              |
| --------------------------------------- | --------------------- |
| Low-latency small messages              | Yes                   |
| Message boundaries matter               | Yes                   |
| Multicast/broadcast required            | Yes                   |
| Telemetry where some loss is acceptable | Yes                   |
| Need guaranteed delivery                | No (use TCP)          |
| Same-machine IPC                        | Consider Unix sockets |

### Connected vs Unconnected Mode

| Mode            | Use Case                             | API                        |
| --------------- | ------------------------------------ | -------------------------- |
| **CONNECTED**   | Single peer, frequent messages       | `read()` / `write()`       |
| **UNCONNECTED** | Multiple peers, broadcast, multicast | `readFrom()` / `writeTo()` |

### UDP vs TCP vs Unix

| Aspect             | UDP         | TCP                    | Unix Socket     |
| ------------------ | ----------- | ---------------------- | --------------- |
| Latency            | ~9.3 us     | ~11.6 us               | ~7.5 us         |
| Delivery           | Best-effort | Guaranteed, ordered    | Guaranteed      |
| Message boundaries | Preserved   | Stream (no boundaries) | Stream or dgram |
| Connection         | Stateless   | Stateful               | Stateful        |
| Multicast          | Supported   | Not supported          | Not supported   |
| Scope              | Network     | Network                | Same machine    |

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), localhost echo test, 15 repeats per data point.

### Echo Latency

| Mode        | Payload | Median Latency | Throughput   | Jitter (CV) |
| ----------- | ------- | -------------- | ------------ | ----------- |
| Connected   | 64 B    | 9.3 us         | 107.8K msg/s | 7.0%        |
| Unconnected | 64 B    | 10.2 us        | 97.6K msg/s  | 10.4%       |
| Connected   | 1 KB    | 10.2 us        | 97.8K msg/s  | 5.5%        |

Latency is stable across payload sizes up to MTU (no fragmentation).

### Mode Comparison (64 B)

| Mode        | Median Latency | Throughput   | Jitter (CV) |
| ----------- | -------------- | ------------ | ----------- |
| Connected   | 9.6 us         | 104.1K msg/s | 9.4%        |
| Unconnected | 10.5 us        | 95.1K msg/s  | 6.5%        |

Connected mode is ~9% faster due to kernel caching the destination address.

### Send Throughput (64 B, fire-and-forget)

| Metric     | Value        |
| ---------- | ------------ |
| Median     | 1.23 us      |
| Throughput | 814K dgram/s |
| Jitter     | 2.2%         |

### Memory Footprint

| Component         | Stack      | Heap           |
| ----------------- | ---------- | -------------- |
| `UdpSocketServer` | ~400 bytes | 0 (after init) |
| `UdpSocketClient` | ~300 bytes | 0 (after init) |
| `DatagramStats`   | 64 bytes   | 0              |

---

## 4. Design Principles

### Zero-Allocation I/O

- All read/write paths use caller-provided buffers
- No internal heap allocation after `init()`
- Callbacks use `Delegate` (no `std::function` heap allocation)

### Epoll-Driven Event Loop

Single epoll instance owns all waiting via `processEvents()`. Nonblocking I/O helpers never wait; they return immediately (EAGAIN-safe).

### Batch I/O

`readBatch()` / `writeBatch()` use Linux `sendmmsg` / `recvmmsg` syscalls to amortize syscall overhead across multiple datagrams. Useful for high-throughput telemetry ingestion.

### Platform: Linux-Only

This library requires Linux-specific APIs and cannot run on bare-metal targets:

- **epoll:** Event-driven I/O multiplexing
- **POSIX sockets:** `socket()`, `bind()`, `connect()`, `sendto()`, `recvfrom()`
- **sendmmsg/recvmmsg:** Batch datagram I/O (Linux-specific)
- **Multicast:** `IP_ADD_MEMBERSHIP`, `IP_MULTICAST_IF`, `IP_MULTICAST_TTL`
- **eventfd:** Stop signal for breaking `epoll_wait()` from another thread

These dependencies are fundamental to the implementation, not incidental. UDP requires a kernel network stack.

---

## 5. API Reference

### UdpSocketServer

```cpp
/**
 * @brief Constructs a UDP server with bind address and port.
 * @param addr Bind address (empty for INADDR_ANY).
 * @param port Port number as string.
 * @note NOT RT-safe: Allocates string storage.
 */
UdpSocketServer(const std::string& addr, const std::string& port);

/**
 * @brief Initializes the server: socket, bind, epoll setup.
 * @param error Optional output for error message.
 * @return Status code from UdpServerStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// I/O (RT-safe: nonblocking syscalls, no allocation)
ssize_t read(apex::compat::bytes_span bytes, RecvInfo& info,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
ssize_t write(const RecvInfo& client, apex::compat::bytes_span bytes, int timeoutMilliSecs,
              std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// Batch I/O (RT-safe: Linux sendmmsg/recvmmsg)
int readBatch(apex::compat::bytes_span* bufs, RecvInfo* infos, size_t count);
int writeBatch(const RecvInfo* clients, const apex::compat::bytes_span* bufs, size_t count);

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks
void setOnDatagramReceived(apex::concurrency::Delegate<void> callback);  // RT-safe
void setOnError(const std::function<void(const std::string&)>& callback);

// Configuration (RT-safe: sets member variables)
void setReusePort(bool on);
void setBroadcast(bool on);
void setTosDscp(int tos);
void bindToDevice(const std::string& ifname);  // NOT RT-safe: setsockopt

// Multicast (NOT RT-safe: setsockopt syscalls)
bool joinMulticastV4(uint32_t groupBe, uint32_t ifaceBe);
bool leaveMulticastV4(uint32_t groupBe, uint32_t ifaceBe);
bool setMulticastLoopV4(bool on);
bool setMulticastTtlV4(int ttl);

// Statistics
DatagramStats stats() const noexcept;  // RT-safe
void resetStats() noexcept;            // RT-safe
```

### UdpSocketClient

```cpp
/**
 * @brief Constructs a UDP client for the specified server.
 * @param addr Server address (or empty for unconnected).
 * @param port Server port (or "0" for ephemeral).
 * @note NOT RT-safe: Allocates string storage.
 */
UdpSocketClient(const std::string& addr, const std::string& port);

/**
 * @brief Initializes the client: socket, connect/bind, epoll setup.
 * @param mode CONNECTED or UNCONNECTED.
 * @param error Optional output for error message.
 * @return Status code from UdpClientStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(UdpSocketMode mode,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// Connected mode I/O (RT-safe: nonblocking syscalls)
ssize_t read(apex::compat::bytes_span bytes, int timeoutMilliSecs,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
ssize_t write(apex::compat::bytes_span bytes, int timeoutMilliSecs,
              std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// Unconnected mode I/O (RT-safe: nonblocking syscalls)
ssize_t readFrom(apex::compat::bytes_span bytes, PeerInfo& peer, int timeoutMilliSecs,
                 std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
ssize_t writeTo(apex::compat::bytes_span bytes, const PeerInfo& peer, int timeoutMilliSecs,
                std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks (RT-safe Delegate, no heap allocation)
void setOnReadable(apex::concurrency::Delegate<void> callback);
void setOnWritable(apex::concurrency::Delegate<void> callback);

// Statistics
DatagramStats stats() const noexcept;  // RT-safe
void resetStats() noexcept;            // RT-safe
```

---

## 6. Usage Examples

### Echo Server

```cpp
#include "src/system/core/protocols/network/udp/inc/UdpSocketServer.hpp"

using namespace apex::protocols::udp;

struct EchoContext {
  UdpSocketServer* server;
};

void echoCallback(void* ctx) noexcept {
  auto* ec = static_cast<EchoContext*>(ctx);
  std::array<uint8_t, 1500> buf{};
  UdpSocketServer::RecvInfo info{};

  for (;;) {
    ssize_t n = ec->server->read(
        apex::compat::bytes_span(buf.data(), buf.size()), info);
    if (n <= 0) break;  // Drained (EAGAIN)

    apex::compat::bytes_span out(buf.data(), static_cast<size_t>(n));
    (void)ec->server->write(info, out, 0);
  }
}

int main() {
  UdpSocketServer server("0.0.0.0", "9000");
  std::string error;
  if (server.init(error) != UDP_SERVER_SUCCESS) {
    std::cerr << "Init failed: " << error << "\n";
    return 1;
  }

  EchoContext ctx{&server};
  server.setOnDatagramReceived(
      apex::concurrency::Delegate<void>{echoCallback, &ctx});

  while (true) {
    server.processEvents(100);
  }
}
```

### Connected Client

```cpp
#include "src/system/core/protocols/network/udp/inc/UdpSocketClient.hpp"

using namespace apex::protocols::udp;

int main() {
  UdpSocketClient client("127.0.0.1", "9000");
  std::string error;
  if (client.init(UdpSocketMode::CONNECTED, error) != UDP_CLIENT_SUCCESS) {
    std::cerr << "Init failed: " << error << "\n";
    return 1;
  }

  // Send datagram
  std::array<uint8_t, 64> request{/* ... */};
  client.write(apex::compat::bytes_span(request.data(), request.size()), 0);

  // Read response
  std::array<uint8_t, 1024> response{};
  ssize_t n = client.read(
      apex::compat::bytes_span(response.data(), response.size()), 1000);
  if (n > 0) {
    // Process response...
  }
}
```

### Multicast Receiver

```cpp
UdpSocketServer mcastSrv("", "5000");  // Bind ANY:5000
mcastSrv.setReusePort(true);           // Allow multiple listeners

std::string error;
if (mcastSrv.init(error) != UDP_SERVER_SUCCESS) return 1;

// Join multicast group 239.1.2.3
uint32_t GROUP_BE = ::inet_addr("239.1.2.3");
mcastSrv.joinMulticastV4(GROUP_BE, 0);  // 0 = default interface
mcastSrv.setMulticastLoopV4(false);
mcastSrv.setMulticastTtlV4(1);

mcastSrv.setOnDatagramReceived(
    apex::concurrency::Delegate<void>{recvCallback, &mcastSrv});

while (true) {
  mcastSrv.processEvents(100);
}
```

---

## 7. Testing

### Test Organization

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 5     | Yes                   |
| `ptst/`   | Perf | 6     | No (manual)           |

### Test Requirements

- Linux with loopback networking (all modern distributions)
- No special hardware required
- Docker environment provides required permissions

---

## 8. See Also

- **TCP Sockets** (`../tcp/`) - For reliable streams
- **Unix Sockets** (`../unix/`) - For local IPC (lower latency)
- **Framing Protocols** (`../../framing/`) - SLIP/COBS for message boundaries
- **CAN Bus** (`../../fieldbus/can/`) - For broadcast bus networks
- **ByteTrace** (`../../common/inc/ByteTrace.hpp`) - Byte-level tracing mixin
