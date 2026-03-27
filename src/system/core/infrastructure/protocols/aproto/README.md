# APROTO Protocol Library

Lightweight binary protocol for commanding system components and receiving telemetry with optional CRC32 integrity checking and AEAD encryption.

**Library:** `system_core_protocols_aproto`
**Namespace:** `system_core::protocols::aproto`
**Header:** `inc/AprotoCodec.hpp`, `inc/AprotoTypes.hpp`, `inc/AprotoStatus.hpp`

---

## 1. Quick Reference

| Question                                   | Module         |
| ------------------------------------------ | -------------- |
| How do I build a command packet?           | `AprotoCodec`  |
| How do I parse a received packet?          | `AprotoCodec`  |
| How do I send an ACK/NAK response?         | `AprotoCodec`  |
| How do I add CRC32 integrity?              | `AprotoCodec`  |
| How do I route to a specific component?    | `AprotoTypes`  |
| How do I check encode/decode result codes? | `AprotoStatus` |

| Component          | Type          | Purpose                                                                                 | RT-Safe |
| ------------------ | ------------- | --------------------------------------------------------------------------------------- | ------- |
| `AprotoHeader`     | Struct        | 14-byte packed header (magic, version, flags, fullUid, opcode, sequence, payloadLength) | Yes     |
| `AprotoFlags`      | Struct        | Protocol flags (internalOrigin, isResponse, ackRequested, crcPresent, encryptedPresent) | Yes     |
| `AckPayload`       | Struct        | ACK/NAK response payload (cmdOpcode, cmdSequence, status)                               | Yes     |
| `CryptoMeta`       | Struct        | Encryption metadata (keyIndex, nonce)                                                   | Yes     |
| `PacketView`       | Struct        | Zero-copy view into encoded packet buffer                                               | Yes     |
| `buildHeader`      | Free function | Create AprotoHeader from parameters                                                     | Yes     |
| `encodeHeader`     | Free function | Serialize header to bytes                                                               | Yes     |
| `decodeHeader`     | Free function | Parse header from bytes                                                                 | Yes     |
| `encodePacket`     | Free function | Serialize header + payload + optional CRC                                               | Yes     |
| `validatePacket`   | Free function | Verify magic, version, CRC                                                              | Yes     |
| `createPacketView` | Free function | Create zero-copy view of encoded packet                                                 | Yes     |
| `getPayload`       | Free function | Extract payload span from packet                                                        | Yes     |
| `getCryptoMeta`    | Free function | Extract encryption metadata                                                             | Yes     |
| `computeCrc`       | Free function | Hardware-accelerated CRC32-C                                                            | Yes     |
| `encodeAckNak`     | Free function | Build ACK/NAK response packet                                                           | Yes     |
| `makeFlags`        | Free function | Construct flags from booleans                                                           | Yes     |
| `packetSize`       | Free function | Calculate total packet size from header                                                 | Yes     |
| `toString(Status)` | Free function | Convert status code to string                                                           | Yes     |
| `isSuccess`        | Free function | Check if status is success                                                              | Yes     |
| `isError`          | Free function | Check if status is an error                                                             | Yes     |
| `isWarning`        | Free function | Check if status is a warning                                                            | Yes     |

---

## 2. When to Use

| Scenario                                              | Use This Library?                   |
| ----------------------------------------------------- | ----------------------------------- |
| Command/telemetry protocol for system components      | Yes                                 |
| Component-addressed routing via fullUid               | Yes                                 |
| Need CRC32 integrity checking                         | Yes -- `crcPresent` flag            |
| Need AEAD encryption (AES-256-GCM, ChaCha20-Poly1305) | Yes -- `encryptedPresent` flag      |
| ACK/NAK response correlation                          | Yes -- `encodeAckNak`               |
| Generic message framing                               | No -- use SLIP or COBS              |
| High-level structured logging                         | No -- use `system_core_logs`        |
| Raw binary parsing without routing                    | No -- use protocol-specific parsers |

**Design intent:** Fixed 14-byte header enables O(1) header parsing. All operations use caller-provided buffers with no heap allocation. CRC32-C uses hardware acceleration (SSE4.2/ARM) for high-throughput integrity checking.

---

## 3. Performance

### Header Operations

| Operation      | Median (us) | Calls/s | CV%  |
| -------------- | ----------- | ------- | ---- |
| `buildHeader`  | 0.020       | 49.3M   | 1.8% |
| `encodeHeader` | 0.012       | 80.6M   | 3.1% |
| `decodeHeader` | 0.014       | 73.0M   | 3.0% |

### Packet Operations (64B payload)

| Operation                   | Median (us) | Calls/s | CV%   |
| --------------------------- | ----------- | ------- | ----- |
| `encodePacket` (no CRC)     | 0.031       | 32.7M   | 3.9%  |
| `encodePacket` (with CRC)   | 0.087       | 11.5M   | 30.9% |
| `validatePacket` (with CRC) | 0.062       | 16.1M   | 2.9%  |
| `createPacketView`          | 0.104       | 9.6M    | 2.9%  |

### CRC Throughput

| Payload | Median (us) | Calls/s | Throughput |
| ------- | ----------- | ------- | ---------- |
| 64B     | 0.038       | 26.2M   | 1.6 GB/s   |
| 1KB     | 0.225       | 4.4M    | 4.3 GB/s   |
| 64KB    | 12.143      | 82.4K   | 5.2 GB/s   |

### ACK/NAK and Utility

| Operation                       | Median (us) | Calls/s | CV%  |
| ------------------------------- | ----------- | ------- | ---- |
| `encodeAckNak`                  | 0.048       | 21.0M   | 1.9% |
| `encodeNakWithCrc`              | 0.095       | 10.5M   | 4.5% |
| `roundTrip` (encode+decode+CRC) | 0.252       | 4.0M    | 1.6% |
| `getPayload`                    | 0.039       | 25.8M   | 4.2% |
| `toString(Status)`              | 0.009       | 112.4M  | 5.9% |

### Memory Footprint

| Component           | Stack                    | Heap |
| ------------------- | ------------------------ | ---- |
| `AprotoHeader`      | 14B (packed)             | 0    |
| `AprotoFlags`       | 1B                       | 0    |
| `AckPayload`        | 8B (packed)              | 0    |
| `CryptoMeta`        | 13B (packed)             | 0    |
| `PacketView`        | ~40B (header + spans)    | 0    |
| `encodePacket` call | Caller-owned buffer only | 0    |

---

## 4. Design Principles

- **Fixed header** -- 14-byte packed header, O(1) parse
- **Zero-copy viewing** -- `PacketView` references original buffer, no copying
- **No heap allocation** -- All encode/decode uses caller-provided buffers
- **Hardware-accelerated CRC** -- CRC32-C via SSE4.2 (x86) or ARM CRC32 intrinsics
- **Packed structures** -- Wire-ready without serialization overhead
- **Component addressing** -- `fullUid` routes to specific component instance
- **RT-safe throughout** -- All public functions are O(1) or O(n), no exceptions, no allocation
- **Optional security** -- AEAD encryption and CRC are opt-in via flags byte

---

## 5. API Reference

### Status Codes

```cpp
enum class Status : std::uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_MAGIC = 1,
  ERROR_INVALID_VERSION = 2,
  ERROR_INCOMPLETE = 3,
  ERROR_PAYLOAD_TOO_LARGE = 4,
  ERROR_BUFFER_TOO_SMALL = 5,
  ERROR_PAYLOAD_TRUNCATED = 6,
  ERROR_CRC_MISMATCH = 7,
  ERROR_DECRYPT_FAILED = 8,
  ERROR_ENCRYPT_FAILED = 9,
  ERROR_INVALID_KEY = 10,
  ERROR_MISSING_CRYPTO = 11,
  WARN_RESERVED_FLAGS = 12,
};

/// @note RT-safe: Returns static string, no allocation.
const char* toString(Status s) noexcept;
bool isSuccess(Status s) noexcept;
bool isError(Status s) noexcept;
bool isWarning(Status s) noexcept;
```

### Header Construction

```cpp
/// @note RT-safe: O(1), no allocation.
AprotoHeader buildHeader(std::uint32_t fullUid, std::uint16_t opcode,
                         std::uint16_t sequence, std::uint16_t payloadLen,
                         bool isResponse = false, bool ackRequested = false,
                         bool includeCrc = false) noexcept;
```

### Encode

```cpp
/// @note RT-safe: O(1), no allocation.
Status encodeHeader(const AprotoHeader& hdr,
                    apex::compat::mutable_bytes_span outBuf) noexcept;

/// @note RT-safe: O(n), no allocation.
Status encodePacket(const AprotoHeader& hdr,
                    apex::compat::rospan<std::uint8_t> payload,
                    apex::compat::mutable_bytes_span outBuf,
                    std::size_t& bytesWritten) noexcept;

/// @note RT-safe: O(1) without CRC, O(n) with CRC.
Status encodeAckNak(const AprotoHeader& cmdHeader, std::uint8_t statusCode,
                    apex::compat::mutable_bytes_span outBuf,
                    std::size_t& bytesWritten, bool includeCrc = false) noexcept;
```

### Decode

```cpp
/// @note RT-safe: O(1), no allocation.
Status decodeHeader(apex::compat::rospan<std::uint8_t> buf,
                    AprotoHeader& out) noexcept;

/// @note RT-safe: O(n) for CRC, no allocation.
Status validatePacket(apex::compat::rospan<std::uint8_t> packet) noexcept;

/// @note RT-safe: O(n) if CRC present, O(1) otherwise.
Status createPacketView(apex::compat::rospan<std::uint8_t> packet,
                        PacketView& view) noexcept;

/// @note RT-safe: O(1), no allocation.
apex::compat::rospan<std::uint8_t>
getPayload(apex::compat::rospan<std::uint8_t> packet) noexcept;

/// @note RT-safe: O(1), no allocation.
Status getCryptoMeta(apex::compat::rospan<std::uint8_t> packet,
                     CryptoMeta& meta) noexcept;
```

### CRC Utilities

```cpp
/// @note RT-safe: O(n), hardware-accelerated CRC32-C.
std::uint32_t computeCrc(apex::compat::rospan<std::uint8_t> data) noexcept;

/// @note RT-safe: O(n), no allocation.
Status appendCrc(apex::compat::rospan<std::uint8_t> data,
                 apex::compat::mutable_bytes_span outBuf,
                 std::size_t offset) noexcept;
```

---

## 6. Usage Examples

### Building a Command Packet

```cpp
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"

namespace aproto = system_core::protocols::aproto;

aproto::AprotoHeader hdr = aproto::buildHeader(
    0x00010002,  // fullUid (componentId=1, instanceIndex=2)
    0x0100,      // opcode (custom command)
    1,           // sequence
    4,           // payloadLength
    false,       // isResponse
    true,        // ackRequested
    true         // includeCrc
);

std::uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
std::uint8_t buf[64];
std::size_t written = 0;

aproto::Status st = aproto::encodePacket(hdr, {payload, 4}, {buf, sizeof(buf)}, written);
if (!aproto::isSuccess(st)) {
    // Handle error
}
```

### Parsing a Received Packet

```cpp
namespace aproto = system_core::protocols::aproto;

aproto::Status st = aproto::validatePacket({rxBuf, rxLen});
if (!aproto::isSuccess(st)) {
    // Invalid packet
}

aproto::PacketView view{};
st = aproto::createPacketView({rxBuf, rxLen}, view);

std::uint32_t targetUid = view.header.fullUid;
std::uint16_t opcode    = view.header.opcode;
auto payload            = view.payload;

if (view.isEncrypted()) {
    aproto::CryptoMeta meta{};
    aproto::getCryptoMeta({rxBuf, rxLen}, meta);
    // Decrypt payload using meta.keyIndex and meta.nonce
}
```

### Sending an ACK Response

```cpp
namespace aproto = system_core::protocols::aproto;

std::uint8_t respBuf[64];
std::size_t written = 0;

aproto::Status st = aproto::encodeAckNak(
    cmdHdr, 0,  // statusCode 0 = success (ACK)
    {respBuf, sizeof(respBuf)}, written,
    true  // includeCrc
);
```

### Internal vs External Origin

The `internalOrigin` flag distinguishes command sources without changing the dispatch path.

| Scenario          | Origin   | Example                               |
| ----------------- | -------- | ------------------------------------- |
| Ground command    | External | GCS sends mode change                 |
| Model-to-model    | Internal | Health monitor reconfigures scheduler |
| Executive control | Internal | Executive sends shutdown to all       |

---

## 7. Testing

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 40    | Yes                   |
| `ptst/`   | Performance tests | 15    | No (manual)           |

Tests verify encode/decode round-trips, CRC integrity, ACK/NAK correlation, status code classification, flags construction, and packet size calculation. All tests are platform-agnostic with no hardware dependencies.

---

## 8. See Also

- `src/system/core/infrastructure/protocols/framing/slip/` -- SLIP framing for wire transport
- `src/system/core/infrastructure/protocols/framing/cobs/` -- COBS framing for wire transport
- `src/system/core/infrastructure/protocols/common/` -- ByteTrace mixin for protocol I/O debugging
- `src/utilities/checksums/crc/` -- CRC32 hardware-accelerated implementation
- `src/utilities/encryption/` -- AEAD encryption (AES-GCM, ChaCha20-Poly1305)
