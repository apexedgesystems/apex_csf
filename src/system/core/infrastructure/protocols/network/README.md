# Network Protocols Library

**Namespace:** `apex::protocols::{tcp,udp,unix_socket}`
**Platform:** Linux-only
**C++ Standard:** C++17

High-performance, low-latency network socket implementations designed for real-time and embedded systems. Provides TCP, UDP, and Unix domain socket transports with a unified epoll-based event loop architecture.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Protocol Selection Guide](#2-protocol-selection-guide)
3. [Performance Characteristics](#3-performance-characteristics)
4. [Design Principles](#4-design-principles)
5. [Protocol Reference](#5-protocol-reference)
6. [Common Patterns](#6-common-patterns)
7. [Real-Time Considerations](#7-real-time-considerations)
8. [Testing](#8-testing)
9. [See Also](#9-see-also)

---

## 1. Quick Reference

| Protocol               | Namespace                      | Best For                           | Latency |
| ---------------------- | ------------------------------ | ---------------------------------- | ------- |
| [TCP](tcp/README.md)   | `apex::protocols::tcp`         | Reliable streams, ordered delivery | ~12 us  |
| [UDP](udp/README.md)   | `apex::protocols::udp`         | Low-latency datagrams, multicast   | ~10 us  |
| [Unix](unix/README.md) | `apex::protocols::unix_socket` | Local IPC, lowest latency          | ~5-8 us |

### Headers

```cpp
// TCP
#include "src/system/core/protocols/network/tcp/inc/TcpSocketServer.hpp"
#include "src/system/core/protocols/network/tcp/inc/TcpSocketClient.hpp"

// UDP
#include "src/system/core/protocols/network/udp/inc/UdpSocketServer.hpp"
#include "src/system/core/protocols/network/udp/inc/UdpSocketClient.hpp"

// Unix Domain Sockets
#include "src/system/core/protocols/network/unix/inc/UnixSocketServer.hpp"
#include "src/system/core/protocols/network/unix/inc/UnixSocketClient.hpp"
```

---

## 2. Protocol Selection Guide

| Question                                         | Recommended Protocol        |
| ------------------------------------------------ | --------------------------- |
| Need reliable, ordered byte stream?              | **TCP**                     |
| Need message boundaries preserved?               | **UDP** or **Unix (DGRAM)** |
| Communicating between processes on same machine? | **Unix** (2-5x faster)      |
| Need multicast/broadcast support?                | **UDP**                     |
| Need lowest possible latency for local IPC?      | **Unix**                    |
| Communicating over network to remote hosts?      | **TCP** or **UDP**          |
| Small control messages with strict timing?       | **UDP** or **Unix**         |
| Large data transfers requiring reliability?      | **TCP**                     |
| High-frequency telemetry where some loss is OK?  | **UDP**                     |

### Decision Tree

```
Is communication local (same machine)?
  |
  +-- YES --> Unix Domain Sockets (fastest)
  |
  +-- NO --> Is reliability required?
              |
              +-- YES --> TCP (ordered, reliable)
              |
              +-- NO --> UDP (lowest network latency)
```

---

## 3. Performance Characteristics

Measured on localhost echo test with 64-1024 byte payloads:

| Metric               | TCP       | UDP       | Unix        |
| -------------------- | --------- | --------- | ----------- |
| **Median Latency**   | 12.3 us   | 10.6 us   | ~5-8 us     |
| **P10 Latency**      | 12.0 us   | 9.6 us    | ~4-6 us     |
| **P90 Latency**      | 14.0 us   | 11.0 us   | ~8-10 us    |
| **Throughput (64B)** | 81K msg/s | 94K msg/s | ~120K msg/s |
| **Throughput (1KB)** | 80K msg/s | 99K msg/s | ~110K msg/s |
| **Jitter (CV)**      | 3-8%      | 4-7%      | 3-6%        |

### Key Observations

- **Syscall-bound**: 100% of CPU time is in kernel syscalls (send, recv, epoll_wait)
- **No userspace overhead**: Delegate callbacks add no measurable latency
- **Unix sockets bypass TCP/IP stack**: Eliminates checksum, congestion control, and protocol overhead
- **UDP slightly faster than TCP**: No connection state, no acknowledgment overhead

### Payload Size Impact

| Payload Size | TCP Latency | UDP Latency         |
| ------------ | ----------- | ------------------- |
| 32 bytes     | 12.0 us     | 9.7 us              |
| 64 bytes     | 12.3 us     | 10.6 us             |
| 256 bytes    | 12.5 us     | 10.4 us             |
| 1 KB         | 12.5 us     | 10.1 us             |
| 4 KB         | 15.0 us     | N/A (fragmentation) |

Latency is relatively stable across small-to-medium payloads (RT control messages).

---

## 4. Design Principles

### RT-Safety Annotations

| Annotation      | Meaning                                                      |
| --------------- | ------------------------------------------------------------ |
| **RT-safe**     | No allocation, bounded execution, safe for real-time loops   |
| **NOT RT-safe** | May allocate or have unbounded I/O; call from non-RT context |

### RT-Safety Summary

| Method               | RT-Safe? | Notes                               |
| -------------------- | -------- | ----------------------------------- |
| `init()`             | NO       | Syscalls, memory allocation         |
| `processEvents()`    | NO       | epoll_wait syscall                  |
| `read()` / `write()` | YES      | Nonblocking syscalls, no allocation |
| `stats()`            | YES      | Returns copy of struct              |
| Callbacks            | YES      | Delegate is POD-like, no heap       |

### Zero-Allocation I/O

- All read/write operations use caller-provided buffers (`apex::compat::bytes_span`)
- No internal heap allocation in hot paths
- Fixed-size array overloads for embedded use
- Statistics tracking uses simple struct copies

### Single-Epoll Architecture

- One `epoll_wait()` call per `processEvents()` iteration
- Edge-triggered (EPOLLET) for high efficiency
- Unified stop mechanism via `eventfd`
- Callbacks execute on the event-loop thread

### Delegate Callbacks

All callbacks use `apex::concurrency::Delegate` for RT-safety:

```cpp
// RT-safe callback pattern
void myCallback(void* ctx, int clientfd) noexcept {
  auto* self = static_cast<MyHandler*>(ctx);
  // Handle event...
}

server.setOnClientReadable(
    apex::concurrency::Delegate<void, int>{myCallback, this});
```

---

## 5. Protocol Reference

### TCP

**Best for:** Reliable byte streams, connection-oriented communication, ordered delivery

```cpp
using namespace apex::protocols::tcp;

// Server
TcpSocketServer server("127.0.0.1", "9000");
server.init();
server.setOnClientReadable(
    apex::concurrency::Delegate<void, int>{echoCallback, &server});

while (running) {
  server.processEvents(100);
}

// Client
TcpSocketClient client("127.0.0.1", "9000");
client.init(1000);  // 1s connect timeout
client.write(data, 1000);
client.read(buffer, 1000);
```

**Key Features:**

- Connection state callbacks (connect, readable, writable, close)
- Max connections limit
- Graceful shutdown (SO_LINGER)
- TCP_NODELAY, TCP_CORK, keepalive configuration
- Per-connection statistics

### UDP

**Best for:** Low-latency datagrams, multicast, message-oriented protocols

```cpp
using namespace apex::protocols::udp;

// Server
UdpSocketServer server("127.0.0.1", "9000");
server.init();
server.setOnDatagramReceived(
    apex::concurrency::Delegate<void>{recvCallback, &server});

while (running) {
  server.processEvents(100);
}

// Client (connected mode)
UdpSocketClient client("127.0.0.1", "9000");
client.init(UdpSocketMode::CONNECTED);
client.write(data);
client.read(buffer);
```

**Key Features:**

- Connected and unconnected modes
- Multicast support (join/leave groups, TTL, loopback control)
- Batch I/O (sendmmsg/recvmmsg for high throughput)
- DSCP/TOS configuration
- Per-datagram sender info (RecvInfo)

### Unix Domain Sockets

**Best for:** Local IPC, lowest latency, inter-process communication

```cpp
using namespace apex::protocols::unix_socket;

// Server
UnixSocketServer server("/tmp/my.sock");
server.init(true);  // true = unlink existing
server.setOnClientReadable(
    apex::concurrency::Delegate<void, int>{echoCallback, &server});

while (running) {
  server.processEvents(100);
}

// Client
UnixSocketClient client("/tmp/my.sock");
client.init();
client.write(data);
client.waitReadable(1000);
client.read(buffer);
```

**Key Features:**

- STREAM and DATAGRAM modes
- Same API pattern as TCP/UDP
- Auto-cleanup of socket file on destruction
- Path length validation (max 107 characters)
- 2-5x lower latency than localhost TCP

---

## 6. Common Patterns

### Echo Server Pattern

```cpp
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
  server.init();

  EchoContext ctx{&server};
  server.setOnClientReadable(
      apex::concurrency::Delegate<void, int>{echoCallback, &ctx});

  while (true) {
    server.processEvents(100);
  }
}
```

### Request-Response Client

```cpp
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
ssize_t n = client.read(response, 1000);
if (n > 0) {
  processResponse(response.data(), n);
}
```

### High-Frequency UDP Telemetry

```cpp
UdpSocketClient sender("", "0");  // Ephemeral local port
sender.init(UdpSocketMode::UNCONNECTED);

UdpSocketClient::PeerInfo target{/* populate with receiver address */};

// Send telemetry at high rate
while (running) {
  std::array<uint8_t, 64> packet = buildTelemetry();
  sender.writeTo(apex::compat::bytes_span(packet.data(), packet.size()), target, 0);
  std::this_thread::sleep_for(std::chrono::microseconds(100));
}
```

---

## 7. Real-Time Considerations

### RT-Safe Functions (safe for real-time loops)

- `read()` / `write()` - Nonblocking, no allocation
- `stats()` - Returns struct copy
- `connectionCount()` - Simple counter read
- `stop()` - Writes to eventfd (bounded)

### NOT RT-Safe Functions (call from initialization/cleanup only)

- `init()` - Creates sockets, allocates internal structures
- `processEvents()` - Calls epoll_wait (may block)
- Destructor - Closes sockets, deallocates

### Recommended Configuration

1. **Initialize before entering RT context**

   ```cpp
   server.init();  // Before RT loop starts
   ```

2. **Keep callbacks short**

   ```cpp
   // Good: Quick read and queue for processing
   void callback(void* ctx, int fd) noexcept {
     auto* self = static_cast<Handler*>(ctx);
     ssize_t n = self->server->read(fd, self->buf, 0);
     if (n > 0) self->queue.push(self->buf, n);
   }
   ```

3. **Use dedicated I/O thread**

   ```cpp
   std::thread ioThread([&]() {
     while (running) {
       server.processEvents(10);  // Short timeout for responsiveness
     }
   });
   ```

---

## 8. Testing

Run tests using the standard Docker workflow:

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run all network protocol tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R "Tcp|Udp|Unix"

# Run specific protocol tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R TcpSocket
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R UdpSocket
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -R UnixSocket
```

### Test Organization

| Protocol | Test File              | Description                        |
| -------- | ---------------------- | ---------------------------------- |
| TCP      | `TcpSocket_uTest.cpp`  | Server/client, echo, multi-client  |
| UDP      | `UdpSocket_uTest.cpp`  | Connected/unconnected, multicast   |
| Unix     | `UnixSocket_uTest.cpp` | STREAM mode, echo, max connections |

---

## 9. See Also

- **[TCP Protocol](tcp/README.md)** - Detailed TCP API reference
- **[UDP Protocol](udp/README.md)** - Detailed UDP API reference
- **[Unix Domain Sockets](unix/README.md)** - Detailed Unix socket API reference
- **[Framing Protocols](../framing/)** - SLIP and COBS for message framing over streams
- **[Concurrency Library](../../../../utilities/concurrency/)** - Delegate, ThreadPool for async handling
