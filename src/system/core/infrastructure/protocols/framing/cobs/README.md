# COBS Framing Library

**Namespace:** `apex::protocols::cobs`
**Platform:** Cross-platform
**C++ Standard:** C++17
**Library:** `protocols_framing_cobs`

A high-performance, zero-allocation implementation of the Consistent Overhead Byte Stuffing (COBS) framing layer, designed for real-time and embedded systems.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [When to Use](#2-when-to-use)
3. [Performance](#3-performance)
4. [Design Principles](#4-design-principles)
5. [Module Reference](#5-module-reference)
6. [Requirements](#6-requirements)
7. [Testing](#7-testing)
8. [See Also](#8-see-also)

---

## 1. Quick Reference

| Function / Class | RT-Safety | Purpose                                     |
| ---------------- | --------- | ------------------------------------------- |
| `encode()`       | RT-SAFE   | Encode payload into COBS-framed message     |
| `decodeChunk()`  | RT-SAFE   | Streaming decode with configurable behavior |
| `CobsCodec<N>`   | RT-SAFE   | Compile-time-sized codec with owned buffers |

### Quick Usage

```cpp
#include "src/system/core/protocols/framing/cobs/inc/COBSFraming.hpp"

using namespace apex::protocols::cobs;

// Encode a payload
std::array<uint8_t, 256> outBuf{};
std::array<uint8_t, 4> payload{0xDE, 0xAD, 0x00, 0xEF};

auto result = encode(
    apex::compat::bytes_span(payload.data(), payload.size()),
    outBuf.data(), outBuf.size());

if (result.status == Status::OK) {
  // outBuf[0..result.bytesProduced) holds the framed message
}

// Decode streamed chunks
DecodeConfig cfg{};
DecodeState st{};
std::array<uint8_t, 256> frameBuf{};

auto r = decodeChunk(st, cfg,
    apex::compat::bytes_span(chunk.data(), chunk.size()),
    frameBuf.data(), frameBuf.size());

if (r.status == Status::OK && r.frameCompleted) {
  // frameBuf[0..r.bytesProduced) contains decoded payload
}
```

---

## 2. When to Use

| Scenario                                 | Use COBS?     |
| ---------------------------------------- | ------------- |
| Need consistent, predictable overhead    | Yes           |
| Data contains many 0x00 bytes            | Yes           |
| Need 0x00 as delimiter byte              | Yes           |
| Protocols requiring reserved byte values | Yes           |
| Simplicity over efficiency               | Consider SLIP |
| Following RFC standard (1055)            | Use SLIP      |

### COBS vs SLIP

| Aspect                    | COBS         | SLIP                   |
| ------------------------- | ------------ | ---------------------- |
| Delimiter byte            | 0x00         | 0xC0 (END)             |
| Worst-case overhead       | ~0.4% fixed  | 2x (all special bytes) |
| Typical overhead          | 0.4-1%       | 0-5%                   |
| Implementation complexity | More complex | Simpler                |
| Specification             | IEEE paper   | RFC 1055               |

**Choose COBS when:**

- Consistent, bounded overhead is critical
- Data may contain frequent 0xC0/0xDB bytes
- Protocol requires 0x00 as delimiter
- Worst-case size prediction is needed

**Choose SLIP when:**

- Simplicity is preferred
- Data rarely contains special bytes
- Following RFC 1055 standard
- Slightly higher typical overhead is acceptable

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), 20 repeats per data point. Realistic data contains ~5% zero bytes.

### Throughput by Payload Size

| Payload | Encode (MB/s) | Encode Latency | Decode (MB/s) | Decode Latency | CV% |
| ------- | ------------- | -------------- | ------------- | -------------- | --- |
| 64 B    | 177           | 0.35 us        | 643           | 0.10 us        | <4% |
| 256 B   | 1,121         | 0.22 us        | 1,517         | 0.16 us        | <4% |
| 1 KB    | 1,239         | 0.79 us        | 1,507         | 0.65 us        | <4% |
| 4 KB    | 1,259         | 3.10 us        | 1,377         | 2.84 us        | <5% |
| 16 KB   | 1,204         | 13.0 us        | 1,173         | 13.3 us        | <6% |
| 64 KB   | 990           | 63.1 us        | 1,006         | 62.1 us        | <4% |
| 256 KB  | 924           | 270 us         | 990           | 253 us         | <3% |

### Core Benchmarks (256 B payload, 15 repeats)

| Test             | Latency (us) | Calls/s | CV%   |
| ---------------- | ------------ | ------- | ----- |
| Encode clean     | 0.063        | 15.9M   | 20.8% |
| Encode worst     | 1.537        | 651K    | 1.9%  |
| Encode realistic | 0.214        | 4.7M    | 4.9%  |
| Decode clean     | 0.085        | 11.8M   | 4.9%  |
| Decode worst     | 1.428        | 700K    | 4.7%  |
| Decode realistic | 0.212        | 4.7M    | 3.0%  |
| Decode streaming | 1.173        | 853K    | 2.7%  |

### Overhead Analysis

| Data Pattern             | Overhead       |
| ------------------------ | -------------- |
| Random binary data       | ~0.4%          |
| ASCII text               | ~0.4%          |
| All 0x00 bytes           | ~0.4%          |
| Long runs without 0x00   | ~0.4%          |
| Worst case (any pattern) | 1 byte per 254 |

The key advantage of COBS is **bounded overhead**: maximum 1 byte per 254 bytes of input, regardless of data content.

### Profiler Analysis

Profiling with gperftools (1000 Hz sampling, 100K cycles) shows time concentrated in core functions:

| Function      | Self-Time |
| ------------- | --------- |
| `decodeChunk` | 94.7%     |

CPU-bound: all time is in the decode algorithm, not in syscalls or overhead.

### Memory Footprint

| Component      | Stack       | Heap |
| -------------- | ----------- | ---- |
| Encoder        | Stateless   | 0    |
| `DecodeState`  | 16 bytes    | 0    |
| `CobsCodec<N>` | N \* 2 + 16 | 0    |

---

## 4. Design Principles

### RT-Safety

All public APIs are RT-safe with no dynamic memory allocation:

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-SAFE** | No allocation, bounded execution, safe for real-time loops |

### Zero-Allocation Design

- Encoding uses caller-provided output buffer
- Decoding uses caller-provided frame buffer
- No internal heap allocation in encode or decode paths
- Bounded execution time proportional to input size

### Baremetal Support

Freestanding-safe for bare-metal targets (STM32, Arduino, Pico, ESP32):

- **C headers only:** `<stddef.h>`, `<stdint.h>`, `<string.h>` (freestanding-guaranteed)
- **No exceptions, no RTTI:** `noexcept` throughout
- **No heap allocation:** Caller-owned buffers or `CobsCodec<N>` stack buffers
- **BAREMETAL CMake flag:** Enables cross-compilation via `apex_add_library(... BAREMETAL)`
- **Tested toolchains:** arm-none-eabi-gcc, avr-gcc, xtensa-esp32-elf-gcc, riscv32-gcc

### Graceful Error Handling

- Clear status codes for all error conditions
- Configurable resynchronization on errors (`dropUntilDelimiter`)
- DoS protection via `maxFrameSize` limit
- Backpressure support via `OUTPUT_FULL` status

### Delimiter Safety

- COBS guarantees no `0x00` bytes in encoded output (except delimiter)
- Safe for protocols requiring reserved byte values
- Unambiguous frame boundaries on byte-oriented channels

---

## 5. Module Reference

### Encoder

**RT-safe:** Yes (no allocations, bounded execution)

Converts a payload into a COBS-encoded message with optional trailing delimiter.

#### API

```cpp
/**
 * @brief Encode payload into COBS frame.
 * @param payload Input bytes to encode.
 * @param out Output buffer for framed message.
 * @param outCapacity Size of output buffer.
 * @param trailingDelimiter Append trailing delimiter (default: true).
 * @return IoResult with status and byte counts.
 * @note RT-safe: No allocation, bounded execution.
 */
[[nodiscard]] IoResult encode(apex::compat::bytes_span payload,
                              std::uint8_t* out, std::size_t outCapacity,
                              bool trailingDelimiter = true) noexcept;
```

### Decoder

**RT-safe:** Yes (no allocations, bounded execution)

Stateful streaming decoder that reconstructs COBS frames from arbitrary input chunks.

#### Key Types

```cpp
struct DecodeConfig {
  std::size_t maxFrameSize{4096};      ///< DoS protection limit
  bool dropUntilDelimiter{true};       ///< Resync policy on errors
  bool requireTrailingDelimiter{true}; ///< Frame completion policy
};

struct DecodeState {
  // Internal state for streaming decode
  void reset() noexcept;  ///< Reset to initial state
};
```

#### Status Codes

| Status                    | Meaning                                     |
| ------------------------- | ------------------------------------------- |
| `OK`                      | Operation completed successfully            |
| `NEED_MORE`               | Awaiting more input (mid-run)               |
| `OUTPUT_FULL`             | Output buffer insufficient                  |
| `ERROR_MISSING_DELIMITER` | Frame ended without delimiter (strict mode) |
| `ERROR_DECODE`            | Invalid code byte (code must be 1-255)      |
| `ERROR_OVERSIZE`          | Frame exceeded maxFrameSize                 |

### CobsCodec

**RT-safe:** Yes (compile-time-sized buffers, no allocations)

Templated convenience wrapper with owned encode/decode buffers. Ideal for bare-metal and ISR-driven UART applications where buffer management should be invisible.

```cpp
#include "src/system/core/protocols/framing/cobs/inc/CobsCodec.hpp"

CobsCodec<128> codec;  // 128-byte max decoded frame

// Encode
auto enc = codec.encode(apex::compat::bytes_span(payload, len));
// codec.encodeBuf() contains enc.bytesProduced bytes

// Decode (streaming)
auto dec = codec.feedDecode(apex::compat::bytes_span(rxBuf, rxLen));
if (dec.frameCompleted) {
  auto frame = codec.decodedPayload();  // span over decoded bytes
}
```

---

## 6. Requirements

### Build Requirements

- **Compiler:** C++17 or later (GCC 9+, Clang 10+)
- **Dependencies:** None (header + source only)
- **Platform:** Linux, Windows, bare-metal (no OS-specific dependencies)

### Buffer Sizing

- **Worst-case encoded size:** `payload.size() + (payload.size() / 254) + 2`
- **Typical overhead:** 0.4-1.0% for most data patterns
- **Decoder state size:** Less than 16 bytes

### COBS Protocol Overhead

- Maximum overhead: 1 byte per 254 bytes (approximately 0.4%)
- Empty payload: 2 bytes (code byte 0x01 + delimiter)

---

## 7. Testing

### Test Organization

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 33    | Yes                   |
| `ptst/`   | Perf | 27    | No (manual)           |

### Test Requirements

- Cross-platform (no OS-specific dependencies)
- No special hardware required

---

## 8. See Also

- **SLIP Framing** (`../slip/`) - Alternative with RFC specification
- **Network Protocols** (`../../network/`) - TCP/UDP for transport
- [COBS Wikipedia](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing) - Protocol overview
- [Original COBS Paper](https://www.stuartcheshire.org/papers/cobsforton.pdf) - IEEE/ACM specification
