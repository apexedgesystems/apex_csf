# Unix Domain Socket Library

**Namespace:** `apex::protocols::unix_socket`
**Platform:** Linux-only
**C++ Standard:** C++17
**Library:** `system_core_protocols_network_unix`

Low-latency local IPC via Unix domain sockets. Eliminates TCP/IP stack overhead when communicating between processes on the same machine.

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

| Component          | Purpose                             | RT-Safe I/O |
| ------------------ | ----------------------------------- | ----------- |
| `UnixSocketServer` | Accept connections, echo/dispatch   | Yes         |
| `UnixSocketClient` | Connect to server, request/response | Yes         |

### Headers

```cpp
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketServer.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketClient.hpp"
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixTypes.hpp"
```

---

## 2. When to Use

| Scenario                                    | Use Unix Sockets?    |
| ------------------------------------------- | -------------------- |
| Inter-process communication on same machine | Yes (fastest option) |
| Scheduler-to-worker communication           | Yes                  |
| Configuration/command channel               | Yes                  |
| Shared telemetry collection                 | Yes                  |
| Network communication to remote hosts       | No (use TCP/UDP)     |

### Unix vs TCP vs UDP

| Aspect             | Unix Socket     | TCP                    | UDP         |
| ------------------ | --------------- | ---------------------- | ----------- |
| Latency            | ~9.1 us         | ~11.6 us               | ~9.3 us     |
| Delivery           | Guaranteed      | Guaranteed, ordered    | Best-effort |
| Message boundaries | Stream or dgram | Stream (no boundaries) | Preserved   |
| Connection         | Stateful        | Stateful               | Stateless   |
| Scope              | Same machine    | Network                | Network     |
| Overhead           | Minimal (no IP) | Full TCP/IP stack      | UDP/IP      |

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), loopback echo test, 15 repeats per data point.

### Echo Latency

Measured on x86_64 (clang-21, -O2), loopback echo test, 15 repeats per data point.

| Payload | Median Latency | Throughput   | Jitter (CV) |
| ------- | -------------- | ------------ | ----------- |
| 64 B    | 9.1 us         | 110.5K msg/s | 14.6%       |
| 1 KB    | 9.1 us         | 109.5K msg/s | 8.7%        |

### Multi-Client Echo (4 clients, 256 B)

| Metric         | Value        |
| -------------- | ------------ |
| Median Latency | 5.5 us       |
| Throughput     | 180.8K msg/s |
| Jitter (CV)    | 18.7%        |

### Write Throughput (1 KB, fire-and-forget)

| Metric     | Value        |
| ---------- | ------------ |
| Median     | 0.63 us      |
| Throughput | 1.6M write/s |
| Bandwidth  | 1,638 MB/s   |
| Jitter     | 3.8%         |

### Comparison with localhost TCP

| Metric              | Unix Socket | localhost TCP | Ratio |
| ------------------- | ----------- | ------------- | ----- |
| Echo latency (64 B) | 9.1 us      | 11.6 us       | 1.27x |
| Throughput (64 B)   | 110K msg/s  | 86K msg/s     | 1.28x |
| Write throughput    | 1,638 MB/s  | 2,115 MB/s    | 0.77x |

Unix sockets are faster for echo round-trips (bypassing TCP/IP stack). TCP wins on raw write throughput due to kernel send-path optimizations.

### Memory Footprint

| Component          | Stack      | Heap           |
| ------------------ | ---------- | -------------- |
| `UnixSocketServer` | ~400 bytes | 0 (after init) |
| `UnixSocketClient` | ~300 bytes | 0 (after init) |
| `ConnectionStats`  | 64 bytes   | 0              |

---

## 4. Design Principles

### Zero-Allocation I/O

- All read/write paths use caller-provided buffers
- No internal heap allocation after `init()`
- Callbacks use `Delegate` (no `std::function` heap allocation)

### Epoll-Driven Event Loop

Single epoll instance owns all waiting (accept + client I/O) via `processEvents()`. Nonblocking I/O helpers never wait; they return immediately (EAGAIN-safe).

### Platform: Linux-Only

This library requires Linux-specific APIs and cannot run on bare-metal targets:

- **epoll:** Event-driven I/O multiplexing for server and client
- **Unix domain sockets:** `AF_UNIX` addressing via filesystem paths
- **eventfd:** Stop signal for breaking `epoll_wait()` from another thread
- **fcntl:** Nonblocking mode configuration
- **unlink:** Socket file cleanup

These dependencies are fundamental to the implementation, not incidental. Unix domain sockets require a kernel.

---

## 5. API Reference

### UnixSocketServer

```cpp
/**
 * @brief Constructs a Unix socket server at the specified path.
 * @param path Filesystem path for the socket (max 107 characters).
 * @param mode STREAM (default) or DATAGRAM.
 * @note NOT RT-safe: Allocates string storage.
 */
UnixSocketServer(const std::string& path, UnixSocketMode mode = UnixSocketMode::STREAM);

/**
 * @brief Initializes the server: creates socket, binds, listens (STREAM), sets up epoll.
 * @param unlinkExisting Remove existing socket file before binding.
 * @param error Optional output for error message.
 * @return Status code from UnixServerStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(bool unlinkExisting = true, std::string* error = nullptr);

// I/O (RT-safe: nonblocking syscalls, no allocation)
ssize_t read(int clientfd, apex::compat::bytes_span bytes, std::string* error = nullptr);
ssize_t write(int clientfd, apex::compat::bytes_span bytes, std::string* error = nullptr);
void closeConnection(int clientfd);  // NOT RT-safe: modifies client set under lock

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks (RT-safe Delegate, no heap allocation)
void setOnNewConnection(apex::concurrency::Delegate<void, int> callback);
void setOnClientReadable(apex::concurrency::Delegate<void, int> callback);
void setOnClientWritable(apex::concurrency::Delegate<void, int> callback);
void setOnConnectionClosed(apex::concurrency::Delegate<void, int> callback);

// Configuration
void setMaxConnections(size_t maxConns);  // RT-safe: sets member variable
size_t maxConnections() const;            // RT-safe
size_t connectionCount() const;           // NOT RT-safe: acquires mutex

// Statistics
ConnectionStats stats() const noexcept;  // RT-safe
void resetStats() noexcept;              // RT-safe
```

### UnixSocketClient

```cpp
/**
 * @brief Constructs a Unix socket client for the specified server path.
 * @param serverPath Path to the server's socket file.
 * @param mode STREAM (default) or DATAGRAM.
 * @note NOT RT-safe: Allocates string storage.
 */
UnixSocketClient(const std::string& serverPath, UnixSocketMode mode = UnixSocketMode::STREAM);

/**
 * @brief Initializes the client: creates socket, connects, sets up epoll.
 * @param error Optional output for error message.
 * @return Status code from UnixClientStatus.
 * @note NOT RT-safe: System calls, memory allocation.
 */
uint8_t init(std::string* error = nullptr);

// I/O (RT-safe: nonblocking syscalls, no allocation)
ssize_t read(apex::compat::bytes_span bytes, std::string* error = nullptr);
ssize_t write(apex::compat::bytes_span bytes, std::string* error = nullptr);
bool waitReadable(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait

// Event loop
void processEvents(int timeoutMilliSecs);  // NOT RT-safe: blocks on epoll_wait
void stop();                                // RT-safe: writes to eventfd

// Callbacks (RT-safe Delegate, no heap allocation)
void setOnReadable(apex::concurrency::Delegate<void> callback);
void setOnWritable(apex::concurrency::Delegate<void> callback);
void setOnDisconnected(apex::concurrency::Delegate<void> callback);

// State
bool isConnected() const noexcept;         // RT-safe
const std::string& serverPath() const noexcept;  // RT-safe

// Statistics
ConnectionStats stats() const noexcept;  // RT-safe
void resetStats() noexcept;              // RT-safe
```

### Status Codes

| Server Status                     | Meaning                          |
| --------------------------------- | -------------------------------- |
| `UNIX_SERVER_SUCCESS`             | Operation succeeded              |
| `UNIX_SERVER_ERR_PATH_TOO_LONG`   | Path exceeds 107 characters      |
| `UNIX_SERVER_ERR_UNLINK`          | Failed to remove existing socket |
| `UNIX_SERVER_ERR_SOCKET_CREATION` | Failed to create socket          |
| `UNIX_SERVER_ERR_BIND`            | Failed to bind to path           |
| `UNIX_SERVER_ERR_LISTEN`          | Failed to listen (STREAM mode)   |
| `UNIX_SERVER_ERR_EPOLL_CREATION`  | Failed to create epoll           |
| `UNIX_SERVER_ERR_EPOLL_CONFIG`    | Failed to configure epoll        |

---

## 6. Usage Examples

### Echo Server

```cpp
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketServer.hpp"

using namespace apex::protocols::unix_socket;

struct EchoContext {
  UnixSocketServer* server;
};

void echoCallback(void* ctx, int clientfd) noexcept {
  auto* ec = static_cast<EchoContext*>(ctx);
  std::array<uint8_t, 1024> buf{};
  apex::compat::bytes_span span(buf.data(), buf.size());
  ssize_t n = ec->server->read(clientfd, span);
  if (n > 0) {
    apex::compat::bytes_span out(buf.data(), static_cast<size_t>(n));
    (void)ec->server->write(clientfd, out);
  }
}

int main() {
  UnixSocketServer server("/tmp/echo.sock");
  if (server.init(true) != UNIX_SERVER_SUCCESS) return 1;

  EchoContext ctx{&server};
  server.setOnClientReadable(
      apex::concurrency::Delegate<void, int>{echoCallback, &ctx});

  while (true) {
    server.processEvents(100);
  }
}
```

### Simple Client

```cpp
#include "src/system/core/infrastructure/protocols/network/unix/inc/UnixSocketClient.hpp"

using namespace apex::protocols::unix_socket;

int main() {
  UnixSocketClient client("/tmp/echo.sock");
  std::string error;
  if (client.init(&error) != UNIX_CLIENT_SUCCESS) {
    std::cerr << "Connect failed: " << error << "\n";
    return 1;
  }

  // Send request
  std::array<uint8_t, 4> request{0xDE, 0xAD, 0xBE, 0xEF};
  client.write(apex::compat::bytes_span(request.data(), request.size()));

  // Wait for response
  if (client.waitReadable(1000)) {
    std::array<uint8_t, 32> response{};
    ssize_t n = client.read(apex::compat::bytes_span(response.data(), response.size()));
    // Process response...
  }
}
```

---

## 7. Testing

### Test Organization

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 6     | Yes                   |
| `ptst/`   | Perf | 6     | No (manual)           |

### Test Requirements

- Linux with Unix domain socket support (all modern distributions)
- No special hardware required
- Docker environment provides required permissions

---

## 8. See Also

- **TCP Sockets** (`../tcp/`) - For network communication
- **UDP Sockets** (`../udp/`) - For datagram-based network communication
- **Framing Protocols** (`../../framing/`) - SLIP/COBS for message boundaries
- **CAN Bus** (`../../fieldbus/can/`) - For broadcast bus networks
- **ByteTrace** (`../../common/inc/ByteTrace.hpp`) - Byte-level tracing mixin
