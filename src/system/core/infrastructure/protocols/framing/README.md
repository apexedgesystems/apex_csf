# Framing Protocols Library

**Namespace:** `apex::protocols::{slip,cobs}`
**Platform:** Cross-platform
**C++ Standard:** C++17

High-performance, zero-allocation framing protocols for adding message boundaries to byte streams. Designed for real-time and embedded systems where predictable overhead and bounded execution are critical.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Protocol Selection Guide](#2-protocol-selection-guide)
3. [Performance Characteristics](#3-performance-characteristics)
4. [Design Principles](#4-design-principles)
5. [Common Patterns](#5-common-patterns)
6. [Real-Time Considerations](#6-real-time-considerations)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Reference

| Protocol               | Namespace               | Best For                         | Overhead     |
| ---------------------- | ----------------------- | -------------------------------- | ------------ |
| [SLIP](slip/README.md) | `apex::protocols::slip` | Simple framing, RFC standard     | 0-5% typical |
| [COBS](cobs/README.md) | `apex::protocols::cobs` | Bounded overhead, 0x00 delimiter | ~0.4% fixed  |

### Headers

```cpp
// SLIP
#include "src/system/core/protocols/framing/slip/inc/SLIPFraming.hpp"

// COBS
#include "src/system/core/protocols/framing/cobs/inc/COBSFraming.hpp"
```

### Quick Usage

```cpp
// SLIP encoding
using namespace apex::protocols::slip;
std::array<uint8_t, 256> out{};
auto result = encode(payload, out.data(), out.size());

// COBS encoding
using namespace apex::protocols::cobs;
std::array<uint8_t, 256> out{};
auto result = encode(payload, out.data(), out.size());
```

---

## 2. Protocol Selection Guide

| Question                             | Recommended Protocol      |
| ------------------------------------ | ------------------------- |
| Need predictable, bounded overhead?  | **COBS** (~0.4% max)      |
| Following RFC 1055 standard?         | **SLIP**                  |
| Data contains many 0xC0/0xDB bytes?  | **COBS** (lower overhead) |
| Need 0x00 as delimiter byte?         | **COBS**                  |
| Simplest implementation?             | **SLIP**                  |
| Worst-case size prediction critical? | **COBS**                  |
| Serial port communication?           | Either (SLIP more common) |
| Adding boundaries to TCP streams?    | Either                    |

### Decision Tree

```
Is bounded worst-case overhead critical?
  |
  +-- YES --> COBS (max 1 byte per 254)
  |
  +-- NO --> Does data contain many 0xC0/0xDB bytes?
              |
              +-- YES --> COBS (avoids 2x expansion)
              |
              +-- NO --> SLIP (simpler, RFC standard)
```

### Protocol Comparison

| Aspect                  | SLIP                   | COBS              |
| ----------------------- | ---------------------- | ----------------- |
| **Delimiter byte**      | 0xC0 (END)             | 0x00              |
| **Escape mechanism**    | ESC + substitution     | Run-length codes  |
| **Worst-case overhead** | 2x (all special bytes) | ~0.4% (1 per 254) |
| **Typical overhead**    | 0-5%                   | 0.4-1%            |
| **Empty payload size**  | 2 bytes                | 2 bytes           |
| **Specification**       | RFC 1055               | IEEE paper        |
| **Implementation**      | Simpler                | More complex      |

---

## 3. Performance Characteristics

### Throughput Comparison

| Payload Size | SLIP Encode | SLIP Decode | COBS Encode | COBS Decode |
| ------------ | ----------- | ----------- | ----------- | ----------- |
| 64 bytes     | ~2.5 GB/s   | ~2.2 GB/s   | ~2.0 GB/s   | ~1.8 GB/s   |
| 256 bytes    | ~3.0 GB/s   | ~2.8 GB/s   | ~2.5 GB/s   | ~2.3 GB/s   |
| 1 KB         | ~3.2 GB/s   | ~3.0 GB/s   | ~2.8 GB/s   | ~2.6 GB/s   |
| 4 KB         | ~3.3 GB/s   | ~3.1 GB/s   | ~3.0 GB/s   | ~2.8 GB/s   |

### Latency

| Payload Size | SLIP    | COBS    |
| ------------ | ------- | ------- |
| 64 bytes     | <50 ns  | <60 ns  |
| 256 bytes    | <100 ns | <120 ns |
| 1 KB         | <300 ns | <400 ns |
| 4 KB         | ~1 us   | ~1.5 us |

### Overhead by Data Pattern

| Data Pattern       | SLIP Overhead | COBS Overhead |
| ------------------ | ------------- | ------------- |
| Random binary      | 1-2%          | ~0.4%         |
| ASCII text         | ~0.5%         | ~0.4%         |
| All 0x00 bytes     | 0%            | ~0.4%         |
| All 0xC0 bytes     | 100% (2x)     | ~0.4%         |
| All 0xDB bytes     | 100% (2x)     | ~0.4%         |
| Mixed (5% special) | ~5%           | ~0.4%         |

**Key insight:** COBS has consistent overhead regardless of data content, while SLIP overhead depends on the frequency of special bytes (0xC0, 0xDB).

---

## 4. Design Principles

### RT-Safety

All public APIs in both protocols are RT-safe:

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-SAFE** | No allocation, bounded execution, safe for real-time loops |

### Zero-Allocation Design

- Encoding uses caller-provided output buffer
- Decoding uses caller-provided frame buffer
- No internal heap allocation in encode or decode paths
- Bounded execution time proportional to input size

### Streaming Decode

Both protocols support streaming decode for handling fragmented input:

```cpp
DecodeState state{};
DecodeConfig cfg{};

// Process chunks as they arrive
for (const auto& chunk : chunks) {
  auto result = decodeChunk(state, cfg, chunk, outBuf, outSize);
  if (result.frameCompleted) {
    processFrame(outBuf, result.bytesProduced);
    state.reset();
  }
}
```

### Error Recovery

Both protocols support automatic resynchronization:

| Protocol | Config Option        | Behavior                             |
| -------- | -------------------- | ------------------------------------ |
| SLIP     | `dropUntilEnd`       | Skip bytes until next END delimiter  |
| COBS     | `dropUntilDelimiter` | Skip bytes until next 0x00 delimiter |

---

## 5. Common Patterns

### TCP Stream Framing

Add message boundaries to TCP byte streams:

```cpp
// Sender
auto encoded = slip::encode(message, txBuf, txSize);
tcpClient.write(txBuf, encoded.bytesProduced);

// Receiver
slip::DecodeState state{};
slip::DecodeConfig cfg{};

tcpServer.setOnClientReadable([&](int fd) {
  auto n = server.read(fd, rxBuf);
  auto result = slip::decodeChunk(state, cfg, rxBuf, n, frameBuf, frameSize);
  if (result.frameCompleted) {
    handleMessage(frameBuf, result.bytesProduced);
    state.reset();
  }
});
```

### Serial Port Communication

Frame messages over UART/serial:

```cpp
// Transmit
auto encoded = slip::encode(command, txBuf, txSize);
serial.write(txBuf, encoded.bytesProduced);

// Receive with timeout handling
slip::DecodeState state{};
while (serial.available()) {
  uint8_t byte = serial.read();
  auto result = slip::decodeChunk(state, cfg, &byte, 1, frameBuf, frameSize);
  if (result.frameCompleted) {
    processResponse(frameBuf, result.bytesProduced);
    state.reset();
  }
}
```

### Buffer Sizing

```cpp
// SLIP: Worst case is 2x + 2 (all special bytes + delimiters)
constexpr size_t SLIP_BUFFER = PAYLOAD_SIZE * 2 + 2;

// COBS: Worst case is payload + ceil(payload/254) + 1 (delimiter)
constexpr size_t COBS_BUFFER = PAYLOAD_SIZE + (PAYLOAD_SIZE / 254) + 2;
```

---

## 6. Real-Time Considerations

### RT-Safe Functions (all encode/decode operations)

- `encode()` - Stateless, bounded execution
- `encodePreSized()` - No bounds checking variant (SLIP)
- `decodeChunk()` - Streaming decode, bounded per chunk

### Memory Footprint

| Component     | SLIP                | COBS                |
| ------------- | ------------------- | ------------------- |
| Encoder state | 0 bytes (stateless) | 0 bytes (stateless) |
| Decoder state | ~16 bytes           | ~16 bytes           |
| Code size     | ~1 KB               | ~1.5 KB             |

### Execution Time Bounds

Both protocols have O(n) execution time where n is input size:

- **SLIP:** ~0.3 ns/byte encode, ~0.35 ns/byte decode
- **COBS:** ~0.4 ns/byte encode, ~0.45 ns/byte decode

---

## 7. Testing

Run tests using the standard Docker workflow:

```bash
# Build first
docker compose run --rm -T dev-cuda make debug

# Run all framing tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L framing

# Run specific protocol tests
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L slip
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L cobs
```

### Test Coverage

| Protocol | Test File               | Tests |
| -------- | ----------------------- | ----- |
| SLIP     | `SLIPFraming_uTest.cpp` | 23    |
| COBS     | `COBSFraming_uTest.cpp` | 16    |

---

## 8. See Also

- **[SLIP Protocol](slip/README.md)** - Detailed SLIP API reference
- **[COBS Protocol](cobs/README.md)** - Detailed COBS API reference
- **[Network Protocols](../network/)** - TCP/UDP for transport layer
- **[RFC 1055](https://datatracker.ietf.org/doc/html/rfc1055)** - SLIP specification
- **[COBS Paper](https://www.stuartcheshire.org/papers/cobsforton.pdf)** - Original IEEE/ACM paper
