# TCP Socket Library

**Namespace:** `apex::protocols::tcp`
**Platform:** Linux-only
**C++ Standard:** C++17
**Library:** `system_core_protocols_network_tcp`

High-performance, connection-oriented TCP socket implementation with epoll-based event loop, designed for real-time and embedded systems.

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

| Component         | Purpose                             | RT-Safe I/O |
| ----------------- | ----------------------------------- | ----------- |
| `TcpSocketServer` | Accept connections, stream I/O      | Yes         |
| `TcpSocketClient` | Connect to server, request/response | Yes         |

### Headers

```cpp
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketServer.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpTypes.hpp"
```

---

## 2. When to Use

| Scenario                                             | Use TCP?              |
| ---------------------------------------------------- | --------------------- |
| Need reliable, ordered byte stream                   | Yes                   |
| Large data transfers                                 | Yes                   |
| Connection state matters (connect/disconnect events) | Yes                   |
| Need guaranteed delivery                             | Yes                   |
| Low-latency small messages                           | Consider UDP          |
| Same-machine IPC                                     | Consider Unix sockets |

### TCP vs UDP vs Unix

| Aspect             | TCP                    | UDP         | Unix Socket     |
| ------------------ | ---------------------- | ----------- | --------------- |
| Delivery           | Guaranteed, ordered    | Best-effort | Guaranteed      |
| Latency            | ~10.5 us               | ~8.3 us     | ~7.5 us         |
| Connection         | Stateful               | Stateless   | Stateful        |
| Message boundaries | Stream (no boundaries) | Preserved   | Stream or dgram |
| Scope              | Network                | Network     | Same machine    |

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), localhost echo test, 15 repeats per data point.

### Echo Latency

| Payload | Median Latency | Throughput  | Jitter (CV) |
| ------- | -------------- | ----------- | ----------- |
| 64 B    | 11.6 us        | 86.4K msg/s | 7.7%        |
| 1 KB    | 11.8 us        | 84.9K msg/s | 5.7%        |

Latency is stable for typical RT control message sizes (64-1024 bytes).

### Multi-Client Echo (4 clients, 256 B)

| Metric               | Value       |
| -------------------- | ----------- |
| Median Latency       | 11.3 us     |
| Aggregate Throughput | 88.5K msg/s |
| Jitter (CV)          | 5.3%        |

### Write Throughput (1 KB, fire-and-forget)

| Metric     | Value        |
| ---------- | ------------ |
| Median     | 0.48 us      |
| Throughput | 2.1M write/s |
| Bandwidth  | 2,115 MB/s   |
| Jitter     | 3.8%         |

### Memory Footprint

| Component         | Stack      | Heap           |
| ----------------- | ---------- | -------------- |
| `TcpSocketServer` | ~400 bytes | 0 (after init) |
| `TcpSocketClient` | ~300 bytes | 0 (after init) |
| `ConnectionStats` | 64 bytes   | 0              |

---

## 4. Design Principles

### Zero-Allocation I/O

- All read/write paths use caller-provided buffers
- No internal heap allocation after `init()`
- Callbacks use `Delegate` (no `std::function` heap allocation)

### Epoll-Driven Event Loop

Single epoll instance owns all waiting (accept + client I/O) via `processEvents()`. Nonblocking I/O helpers never wait; they return immediately (EAGAIN-safe). Per-connection flow control and backpressure are handled internally.

### Platform: Linux-Only

This library requires Linux-specific APIs and cannot run on bare-metal targets:

- **epoll:** Event-driven I/O multiplexing for server and client
- **POSIX sockets:** `socket()`, `bind()`, `listen()`, `accept()`, `connect()`
- **TCP socket options:** `TCP_NODELAY`, `TCP_CORK`, `TCP_QUICKACK`, `SO_KEEPALIVE`
- **eventfd:** Stop signal for breaking `epoll_wait()` from another thread
- **fcntl:** Nonblocking mode configuration

These dependencies are fundamental to the implementation, not incidental. TCP requires a kernel network stack.

---

## 5. API Reference

### TcpSocketServer

```cpp
/**
 * @brief Constructs a TCP server with bind address and port.
 * @param addr Bind address (empty for INADDR_ANY).
 * @param port Port number as string.
 * @note NOT RT-safe: Allocates string storage.
 */
TcpSocketServer(const std::string& addr, const std::string& port);

/**
 * @brief Initializes the server: socket, bind, listen, epoll setup.
 * @param error Optional output for error message.
 * @return Status code from TcpServerStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// I/O (RT-safe: nonblocking syscalls, no allocation)
ssize_t read(int clientfd, apex::compat::bytes_span bytes,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
ssize_t write(int clientfd, apex::compat::bytes_span bytes,
              std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
void writeAll(apex::compat::bytes_span bytes);  // NOT RT-safe: iterates client set under lock
void closeConnection(int clientfd);              // NOT RT-safe: modifies client set under lock

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks (RT-safe Delegate, no heap allocation)
void setOnNewConnection(apex::concurrency::Delegate<void, int> callback);
void setOnClientReadable(apex::concurrency::Delegate<void, int> callback);
void setOnClientWritable(apex::concurrency::Delegate<void, int> callback);
void setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback);

// Configuration (RT-safe: sets member variables)
void setMaxConnections(size_t maxConns);
void setNodelayDefault(bool on);     // TCP_NODELAY
void setCorkDefault(bool on);        // TCP_CORK
void setKeepalive(bool on, int idleSec, int intvlSec, int cnt);
void setBufferSizes(int rcvBytes, int sndBytes);
void setLingerDefault(bool enable, int timeoutSecs);

// Statistics
ConnectionStats stats() const noexcept;   // RT-safe
void resetStats() noexcept;               // RT-safe
size_t connectionCount() const;           // NOT RT-safe: acquires mutex
```

### TcpSocketClient

```cpp
/**
 * @brief Constructs a TCP client for the specified server.
 * @param addr Server address.
 * @param port Server port.
 * @note NOT RT-safe: Allocates string storage.
 */
TcpSocketClient(const std::string& addr, const std::string& port);

/**
 * @brief Initializes the client: connect with timeout, epoll setup.
 * @param timeoutMilliSecs Connection timeout.
 * @param error Optional output for error message.
 * @return Status code from TcpClientStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(int timeoutMilliSecs,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// I/O (NOT RT-safe: blocks on epoll_wait for timeout)
ssize_t read(apex::compat::mutable_bytes_span bytes, int timeoutMilliSecs,
             std::optional<std::reference_wrapper<std::string>> error = std::nullopt);
ssize_t write(apex::compat::bytes_span bytes, int timeoutMilliSecs,
              std::optional<std::reference_wrapper<std::string>> error = std::nullopt);

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks (RT-safe Delegate, no heap allocation)
void setOnReadable(apex::concurrency::Delegate<void> callback);
void setOnWritable(apex::concurrency::Delegate<void> callback);
void setOnDisconnected(apex::concurrency::Delegate<void> callback);

// Statistics
ConnectionStats stats() const noexcept;  // RT-safe
void resetStats() noexcept;              // RT-safe
```

---

## 6. Usage Examples

### Echo Server

```cpp
#include "src/system/core/protocols/network/tcp/inc/TcpSocketServer.hpp"

using namespace apex::protocols::tcp;

struct EchoContext {
  TcpSocketServer* server;
};

void echoCallback(void* ctx, int clientfd) noexcept {
  auto* ec = static_cast<EchoContext*>(ctx);
  std::array<uint8_t, 4096> buf{};
  ssize_t n = ec->server->read(clientfd, buf, 0);
  if (n > 0) {
    apex::compat::bytes_span out(buf.data(), static_cast<size_t>(n));
    (void)ec->server->write(clientfd, out);
  }
}

int main() {
  TcpSocketServer server("0.0.0.0", "9000");
  std::string error;
  if (server.init(error) != TCP_SERVER_SUCCESS) {
    std::cerr << "Init failed: " << error << "\n";
    return 1;
  }

  EchoContext ctx{&server};
  server.setOnClientReadable(
      apex::concurrency::Delegate<void, int>{echoCallback, &ctx});

  while (true) {
    server.processEvents(100);
  }
}
```

### Client Request/Response

```cpp
#include "src/system/core/protocols/network/tcp/inc/TcpSocketClient.hpp"

using namespace apex::protocols::tcp;

int main() {
  TcpSocketClient client("127.0.0.1", "9000");
  std::string error;
  if (client.init(1000, error) != TCP_CLIENT_SUCCESS) {
    std::cerr << "Connect failed: " << error << "\n";
    return 1;
  }

  // Send request
  std::array<uint8_t, 64> request{/* ... */};
  client.write(apex::compat::bytes_span(request.data(), request.size()), 1000);

  // Read response
  std::array<uint8_t, 1024> response{};
  ssize_t n = client.read(
      apex::compat::mutable_bytes_span(response.data(), response.size()), 1000);
  if (n > 0) {
    // Process response...
  }
}
```

---

## 7. Testing

### Test Organization

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 2     | Yes                   |
| `ptst/`   | Perf | 6     | No (manual)           |

### Test Requirements

- Linux with loopback networking (all modern distributions)
- No special hardware required
- Docker environment provides required permissions

---

## 8. See Also

- **UDP Sockets** (`../udp/`) - For datagram-based communication
- **Unix Sockets** (`../unix/`) - For local IPC (lower latency)
- **Framing Protocols** (`../../framing/`) - SLIP/COBS for message boundaries over TCP
- **CAN Bus** (`../../fieldbus/can/`) - For broadcast bus networks
- **ByteTrace** (`../../common/inc/ByteTrace.hpp`) - Byte-level tracing mixin
