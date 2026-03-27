# SLIP Framing Library

**Namespace:** `apex::protocols::slip`
**Platform:** Cross-platform
**C++ Standard:** C++17
**Library:** `protocols_framing_slip`

A high-performance, zero-allocation implementation of the Serial Line Internet Protocol (SLIP) framing layer, designed for real-time and embedded systems.

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

| Function / Class   | RT-Safety | Purpose                                             |
| ------------------ | --------- | --------------------------------------------------- |
| `encode()`         | RT-SAFE   | Encode payload into SLIP-framed message             |
| `encodePreSized()` | RT-SAFE   | Encode when buffer is pre-allocated with exact size |
| `decodeChunk()`    | RT-SAFE   | Streaming decode with configurable behavior         |
| `SlipCodec<N>`     | RT-SAFE   | Compile-time-sized codec with owned buffers         |

### Quick Usage

```cpp
#include "src/system/core/protocols/framing/slip/inc/SLIPFraming.hpp"

using namespace apex::protocols::slip;

// Encode a payload
std::array<uint8_t, 256> outBuf{};
std::array<uint8_t, 4> payload{0xDE, 0xAD, 0xBE, 0xEF};

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

| Scenario                                 | Use SLIP?                      |
| ---------------------------------------- | ------------------------------ |
| Adding message boundaries to TCP streams | Yes                            |
| Serial port communication                | Yes                            |
| Simple embedded protocols                | Yes                            |
| Lowest possible overhead                 | Consider COBS (fixed overhead) |
| Data contains many 0xC0/0xDB bytes       | Consider COBS (lower overhead) |
| Need reserved delimiter byte (0x00)      | Use COBS                       |

### SLIP vs COBS

| Aspect                    | SLIP               | COBS         |
| ------------------------- | ------------------ | ------------ |
| Delimiter byte            | 0xC0 (END)         | 0x00         |
| Worst-case overhead       | 2x (all 0xC0/0xDB) | ~0.4% fixed  |
| Typical overhead          | 0-5%               | 0.4-1%       |
| Implementation complexity | Simpler            | More complex |
| RFC specification         | RFC 1055           | IEEE paper   |

**Choose SLIP when:**

- Data rarely contains 0xC0 or 0xDB bytes
- Simplicity is preferred over minimal overhead
- Following industry-standard framing (RFC 1055)

**Choose COBS when:**

- Consistent, predictable overhead is required
- Data contains frequent 0xC0/0xDB bytes
- Need 0x00 as delimiter (some protocols require this)

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), 20 repeats per data point. Realistic data contains ~5% special bytes (END/ESC).

### Throughput by Payload Size

| Payload | Encode (MB/s) | Encode Latency | Decode (MB/s) | Decode Latency | CV% |
| ------- | ------------- | -------------- | ------------- | -------------- | --- |
| 64 B    | 242           | 0.25 us        | 1,220         | 0.05 us        | <4% |
| 256 B   | 475           | 0.51 us        | 4,364         | 0.06 us        | <4% |
| 1 KB    | 506           | 1.93 us        | 13,370        | 0.07 us        | <6% |
| 4 KB    | 517           | 7.56 us        | 30,062        | 0.13 us        | <7% |
| 16 KB   | 521           | 30.0 us        | 45,295        | 0.35 us        | <1% |
| 64 KB   | 512           | 122 us         | 30,359        | 2.06 us        | <2% |
| 256 KB  | 509           | 492 us         | 30,310        | 8.25 us        | <2% |

### Core Benchmarks (256 B payload, 15 repeats)

| Test              | Latency (us) | Calls/s | CV%   |
| ----------------- | ------------ | ------- | ----- |
| Encode clean      | 0.363        | 2.8M    | 20.1% |
| Encode worst      | 0.376        | 2.7M    | 10.0% |
| Encode realistic  | 0.374        | 2.7M    | 1.3%  |
| Decode clean      | 0.068        | 14.7M   | 6.0%  |
| Decode worst      | 0.503        | 2.0M    | 0.9%  |
| Decode streaming  | 0.328        | 3.1M    | 2.7%  |
| Decode multiframe | 0.401        | 2.5M    | 1.1%  |

### Overhead Analysis

| Data Pattern                | Overhead            |
| --------------------------- | ------------------- |
| Random binary data          | ~1-2%               |
| ASCII text                  | ~0.5%               |
| All 0xC0 bytes              | 100% (doubles size) |
| All 0xDB bytes              | 100% (doubles size) |
| Mixed with 5% special bytes | ~5%                 |

### Profiler Analysis

Profiling with gperftools (1000 Hz sampling, 100K cycles) shows time concentrated in core functions:

| Function      | Self-Time |
| ------------- | --------- |
| `encode`      | 87.5%     |
| `decodeChunk` | 85.1%     |

CPU-bound: all time is in the encode/decode algorithms, not in syscalls or overhead.

### Memory Footprint

| Component      | Stack       | Heap |
| -------------- | ----------- | ---- |
| Encoder        | Stateless   | 0    |
| `DecodeState`  | 16 bytes    | 0    |
| `SlipCodec<N>` | N \* 2 + 16 | 0    |

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
- **No heap allocation:** Caller-owned buffers or `SlipCodec<N>` stack buffers
- **BAREMETAL CMake flag:** Enables cross-compilation via `apex_add_library(... BAREMETAL)`
- **Tested toolchains:** arm-none-eabi-gcc, avr-gcc, xtensa-esp32-elf-gcc, riscv32-gcc

### Graceful Error Handling

- Clear status codes for all error conditions
- Configurable resynchronization on errors (`dropUntilEnd`)
- DoS protection via `maxFrameSize` limit
- Backpressure support via `OUTPUT_FULL` status

---

## 5. Module Reference

### Encoder

**RT-safe:** Yes (no allocations, bounded execution)

Converts a payload into a SLIP-framed message with escape substitution and configurable delimiters.

#### API

```cpp
/**
 * @brief Encode payload into SLIP frame.
 * @param payload Input bytes to encode.
 * @param out Output buffer for framed message.
 * @param outSize Size of output buffer.
 * @param leadingEnd Include leading END delimiter (default: true).
 * @param trailingEnd Include trailing END delimiter (default: true).
 * @return IoResult with status and byte counts.
 * @note RT-safe: No allocation, bounded execution.
 */
[[nodiscard]] IoResult encode(apex::compat::bytes_span payload,
                              std::uint8_t* out, std::size_t outSize,
                              bool leadingEnd = true,
                              bool trailingEnd = true) noexcept;

/**
 * @brief Encode into pre-sized buffer (no bounds checking).
 * @param payload Input bytes to encode.
 * @param out Output buffer (must be at least worstCaseSize bytes).
 * @return IoResult with status and byte counts.
 * @note RT-safe: No allocation, bounded execution.
 */
[[nodiscard]] IoResult encodePreSized(apex::compat::bytes_span payload,
                                      std::uint8_t* out) noexcept;
```

### Decoder

**RT-safe:** Yes (no allocations, bounded execution)

Stateful streaming decoder that reconstructs SLIP frames from arbitrary input chunks.

#### Key Types

```cpp
struct DecodeConfig {
  std::uint32_t maxFrameSize{4096};   ///< DoS protection limit
  bool allowEmptyFrame{false};        ///< Accept empty frames as valid
  bool dropUntilEnd{true};            ///< Resync policy on errors
  bool requireTrailingEnd{true};      ///< Frame completion policy
};

struct DecodeState {
  // Internal state for streaming decode
  void reset() noexcept;  ///< Reset to initial state
};
```

#### Status Codes

| Status                    | Meaning                                      |
| ------------------------- | -------------------------------------------- |
| `OK`                      | Operation completed successfully             |
| `NEED_MORE`               | Awaiting more input (mid-escape sequence)    |
| `OUTPUT_FULL`             | Output buffer insufficient                   |
| `ERROR_MISSING_DELIMITER` | Data before starting delimiter (strict mode) |
| `ERROR_INCOMPLETE_ESCAPE` | Stream ended with unresolved ESC             |
| `ERROR_INVALID_ESCAPE`    | ESC followed by invalid byte                 |
| `ERROR_OVERSIZE`          | Frame exceeded maxFrameSize                  |

### SlipCodec

**RT-safe:** Yes (compile-time-sized buffers, no allocations)

Templated convenience wrapper with owned encode/decode buffers. Ideal for bare-metal and ISR-driven UART applications where buffer management should be invisible.

```cpp
#include "src/system/core/protocols/framing/slip/inc/SlipCodec.hpp"

SlipCodec<128> codec;  // 128-byte max decoded frame

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

- **Worst-case encoded size:** `payload.size() * 2 + 2`
- **Typical overhead:** 0-5% for most data patterns
- **Decoder state size:** Less than 16 bytes

---

## 7. Testing

### Test Organization

| Directory | Type | Tests | Runs with `make test` |
| --------- | ---- | ----- | --------------------- |
| `utst/`   | Unit | 40    | Yes                   |
| `ptst/`   | Perf | 26    | No (manual)           |

### Test Requirements

- Cross-platform (no OS-specific dependencies)
- No special hardware required

---

## 8. See Also

- **COBS Framing** (`../cobs/`) - Alternative with fixed overhead
- **Network Protocols** (`../../network/`) - TCP/UDP for transport
- [RFC 1055](https://datatracker.ietf.org/doc/html/rfc1055) - SLIP specification
