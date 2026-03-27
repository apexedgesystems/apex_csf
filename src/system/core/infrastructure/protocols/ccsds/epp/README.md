# CCSDS EPP (Encapsulation Packet Protocol)

**Namespace:** `protocols::ccsds::epp`
**Platform:** Cross-platform
**C++ Standard:** C++17
**Library:** `system_core_protocols_ccsds_epp`

RT-safe implementation of CCSDS Encapsulation Packet Protocol per CCSDS 133.1-B-3 Blue Book.

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

| Component          | Header                       | RT-Safe | Description                                |
| ------------------ | ---------------------------- | ------- | ------------------------------------------ |
| CommonDefs         | `CcsdsEppCommonDefs.hpp`     | Yes     | Protocol constants, LoL values, bit masks  |
| EppHeader          | `CcsdsEppMessagePacker.hpp`  | Yes     | Header builder/serializer (1/2/4/8 octets) |
| EppMsg             | `CcsdsEppMessagePacker.hpp`  | Yes     | Complete packet with fixed-size storage    |
| packPacket()       | `CcsdsEppMessagePacker.hpp`  | Yes     | Zero-alloc packing into caller buffer      |
| PacketViewer       | `CcsdsEppViewer.hpp`         | Yes     | Zero-copy packet parser                    |
| EppHeaderView      | `CcsdsEppViewer.hpp`         | Yes     | Zero-copy header field access              |
| MutableEppMessageT | `CcsdsEppMutableMessage.hpp` | Yes     | Typed, mutable packet assembly             |
| Processor          | `CcsdsEppProcessor.hpp`      | Yes     | Streaming packet extractor (LoL-based)     |

| Question                                     | Module                                |
| -------------------------------------------- | ------------------------------------- |
| How do I create an EPP packet?               | `EppMsg::create*()` or `packPacket()` |
| How do I read packet fields without copying? | `PacketViewer::create()`              |
| How do I extract packets from a byte stream? | `Processor::process()`                |
| How do I modify and re-serialize a packet?   | `MutableEppMessageT<T>`               |
| How do I peek at Protocol ID for routing?    | `PacketViewer::peekProtocolId()`      |
| What header sizes are supported?             | 1, 2, 4, or 8 octets (per LoL field)  |

---

## 2. When to Use

| Scenario                                        | Use EPP?         |
| ----------------------------------------------- | ---------------- |
| Encapsulating IP packets over CCSDS links       | Yes              |
| Variable-length protocol identification         | Yes              |
| Internet Protocol over CCSDS (IPoC)             | Yes              |
| Idle packet insertion (link padding)            | Yes              |
| Spacecraft telemetry/telecommand (APID routing) | Use SPP          |
| Serial byte-stream framing                      | Use SLIP or COBS |

### EPP vs SPP

| Aspect       | EPP                                 | SPP              |
| ------------ | ----------------------------------- | ---------------- |
| Standard     | CCSDS 133.1-B-3                     | CCSDS 133.0-B-2  |
| Header size  | 1/2/4/8 octets (variable)           | 6 octets (fixed) |
| Max packet   | 4,294,967,295 bytes                 | 65,542 bytes     |
| Addressing   | 3-bit Protocol ID + 4-bit Extension | 11-bit APID      |
| Length field | 0/1/2/4 octets (per LoL)            | 2 octets (fixed) |
| Use case     | Encapsulation layer                 | Space link layer |

**Choose EPP when:**

- Encapsulating Internet Protocol packets over CCSDS links
- Variable-length protocol IDs are needed (up to 7-bit via extension)
- Large packets (>64KB) are required
- Idle packet insertion for link synchronization

**Choose SPP when:**

- Following CCSDS space link protocol standards
- APID-based packet routing is needed
- Secondary header carries mission time codes

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), pure in-memory operations, 15 repeats per data point.

### Packing Throughput

| Operation                | Payload | Latency (median) | Rate        | CV%   |
| ------------------------ | ------- | ---------------- | ----------- | ----- |
| packPacket (4-octet hdr) | 8 B     | 0.059 us         | 17.0M ops/s | 18.3% |
| packPacket (4-octet hdr) | 64 B    | 0.107 us         | 9.4M ops/s  | 40.6% |
| packPacket (4-octet hdr) | 1 KB    | 0.056 us         | 17.9M ops/s | 4.1%  |

Latency is effectively size-independent (~0.06 us typical). High CV% on 64B is expected
at sub-0.1 us resolution.

### Viewing Throughput

| Operation                    | Payload | Latency (median) | Rate        | CV%   |
| ---------------------------- | ------- | ---------------- | ----------- | ----- |
| PacketViewer::create         | 64 B    | 0.098 us         | 10.2M ops/s | 32.5% |
| PacketViewer::peekProtocolId | 64 B    | 0.035 us         | 28.7M ops/s | 9.5%  |

### Processor Throughput

| Operation            | Payload | Latency (median) | Rate                       | CV%  |
| -------------------- | ------- | ---------------- | -------------------------- | ---- |
| process (1 packet)   | 64 B    | 0.073 us         | 13.7M ops/s                | 2.5% |
| process (10 packets) | 10x64 B | 0.220 us         | 4.5M bursts/s (45M pkts/s) | 2.3% |

### Overhead

| Operation           | Latency (median) | Rate         | CV%   |
| ------------------- | ---------------- | ------------ | ----- |
| `counters()` access | 0.007 us         | 140.8M ops/s | 15.4% |

### Profiler Analysis

CPU-bound: pure header assembly (1-8 octet EPP header, LoL-based length dispatch).
No syscalls. Operations are sub-0.1 us -- too fast for 1000 Hz gperf sampling to
accumulate meaningful hotspot data. perf confirms IPC of 2.82 with branch-misses
at 0.11% and negligible cache misses -- no microarchitectural bottleneck.

### Memory Footprint

| Component              | Stack   | Heap |
| ---------------------- | ------- | ---- |
| EppMsgDefault (64KB)   | 65544 B | 0 B  |
| EppMsgSmall (256B)     | 264 B   | 0 B  |
| ProcessorDefault (8KB) | ~8280 B | 0 B  |
| ProcessorSmall (4KB)   | ~4184 B | 0 B  |
| PacketViewer           | 48 B    | 0 B  |

---

## 4. Design Principles

### RT-Safety

All public APIs are RT-safe with no dynamic memory allocation:

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-safe** | No allocation, bounded execution, safe for real-time loops |

### Zero-Allocation Design

- Packing uses caller-provided output buffer (`packPacket`) or fixed-size internal storage (`EppMsg`)
- Viewing uses non-owning spans over existing data
- Processor uses fixed-size internal buffer (template parameter)
- No internal heap allocation in any code path

### Blue Book Compliance

All implementations reference CCSDS 133.1-B-3 sections in code comments:

- Packet Version Number must be '111' (binary) = 7 (Section 4.1.2.2.2)
- Header length determined by Length of Length (LoL) field (Section 4.1.2.4)
- LoL=00: 1-octet header (Idle Packet, no payload)
- LoL=01: 2-octet header (1-byte Packet Length field)
- LoL=10: 4-octet header (2-byte Packet Length field)
- LoL=11: 8-octet header (4-byte Packet Length field)
- Max packet length: 4,294,967,295 octets (Section 4.1.1.2)
- Empty payload requires protocolId == 0 (Section 4.1.3.1.5)
- Protocol ID Extension must be 0 unless protocolId == '110' (Section 4.1.2.6.3)

### Header Structure

Per CCSDS 133.1-B-3 Section 4.1.2:

**First Octet (all variants):**

- Bits 0-2: Packet Version Number (PVN) - must be '111'
- Bits 3-5: Encapsulation Protocol ID (3 bits)
- Bits 6-7: Length of Length (LoL, 2 bits)

**Second Octet (4- and 8-octet headers):**

- Bits 0-3: User Defined Field (4 bits)
- Bits 4-7: Protocol ID Extension (4 bits)

**Octets 2-3 (8-octet header only):**

- CCSDS Defined Field (16 bits)

**Remaining Octets:**

- Packet Length Field (size determined by LoL)

### Thread Safety

- **PacketViewer:** Thread-safe (read-only, non-owning spans)
- **EppMsg/EppHeader:** Thread-safe (value types, no shared state)
- **Processor:** NOT thread-safe (internal buffer state). Use separate instances per thread.
- **MutableEppMessageT:** NOT thread-safe (mutable state). Intended for single-threaded assembly.

---

## 5. API Reference

### Immutable Packer

**Header:** `inc/CcsdsEppMessagePacker.hpp`

```cpp
/**
 * @brief Build an idle packet header (1 octet, LoL=00).
 * @param protocolId  Protocol ID (should be 0 for idle).
 * @return EppHeader or std::nullopt on invalid fields.
 * @note RT-safe: No allocation.
 */
static std::optional<EppHeader> EppHeader::buildIdle(
    std::uint8_t protocolId = 0) noexcept;

/**
 * @brief Build a 2-octet header.
 * @param protocolId    Encapsulation Protocol ID (3 bits).
 * @param packetLength  Total packet length (max 255).
 * @return EppHeader or std::nullopt on invalid fields.
 * @note RT-safe: No allocation.
 */
static std::optional<EppHeader> EppHeader::build2Octet(
    std::uint8_t protocolId, std::uint8_t packetLength) noexcept;

/**
 * @brief Build a 4-octet header.
 * @param protocolId    Encapsulation Protocol ID (3 bits).
 * @param userDefined   User Defined field (4 bits).
 * @param protocolIde   Protocol ID Extension (4 bits).
 * @param packetLength  Total packet length (max 65535).
 * @return EppHeader or std::nullopt on invalid fields.
 * @note RT-safe: No allocation.
 */
static std::optional<EppHeader> EppHeader::build4Octet(
    std::uint8_t protocolId, std::uint8_t userDefined,
    std::uint8_t protocolIde, std::uint16_t packetLength) noexcept;

/**
 * @brief Build an 8-octet header.
 * @param protocolId    Encapsulation Protocol ID (3 bits).
 * @param userDefined   User Defined field (4 bits).
 * @param protocolIde   Protocol ID Extension (4 bits).
 * @param ccsdsDefined  CCSDS Defined field (16 bits).
 * @param packetLength  Total packet length (max 4,294,967,295).
 * @return EppHeader or std::nullopt on invalid fields.
 * @note RT-safe: No allocation.
 */
static std::optional<EppHeader> EppHeader::build8Octet(
    std::uint8_t protocolId, std::uint8_t userDefined,
    std::uint8_t protocolIde, std::uint16_t ccsdsDefined,
    std::uint32_t packetLength) noexcept;

/**
 * @brief Create an idle packet (1-octet header, no payload).
 * @note RT-safe: Fixed-size array storage.
 */
static std::optional<EppMsg> EppMsg::createIdle(
    std::uint8_t protocolId = 0) noexcept;

/**
 * @brief Create a 2-octet header packet.
 * @note RT-safe: Fixed-size array storage.
 */
static std::optional<EppMsg> EppMsg::create2Octet(
    std::uint8_t protocolId,
    apex::compat::bytes_span payload) noexcept;

/**
 * @brief Create a 4-octet header packet.
 * @note RT-safe: Fixed-size array storage.
 */
static std::optional<EppMsg> EppMsg::create4Octet(
    std::uint8_t protocolId, std::uint8_t userDefined,
    std::uint8_t protocolIde,
    apex::compat::bytes_span payload) noexcept;

/**
 * @brief Create an 8-octet header packet.
 * @note RT-safe: Fixed-size array storage.
 */
static std::optional<EppMsg> EppMsg::create8Octet(
    std::uint8_t protocolId, std::uint8_t userDefined,
    std::uint8_t protocolIde, std::uint16_t ccsdsDefined,
    apex::compat::bytes_span payload) noexcept;

/**
 * @brief Zero-alloc packing into caller-provided buffer.
 * @param[in]  headerVariant  Header size (1, 2, 4, or 8).
 * @param[in]  protocolId     Encapsulation Protocol ID (3 bits).
 * @param[in]  userDefined    User Defined field (4 bits).
 * @param[in]  protocolIde    Protocol ID Extension (4 bits).
 * @param[in]  ccsdsDefined   CCSDS Defined field (16 bits).
 * @param[in]  payload        Encapsulated data.
 * @param[out] outBuf         Output buffer.
 * @param[in]  outCapacity    Output buffer capacity.
 * @param[out] bytesWritten   Bytes written on success.
 * @return true on success, false on validation/size failure.
 * @note RT-safe: No allocation, direct write to caller buffer.
 */
bool packPacket(
    std::uint8_t headerVariant, std::uint8_t protocolId,
    std::uint8_t userDefined, std::uint8_t protocolIde,
    std::uint16_t ccsdsDefined, apex::compat::bytes_span payload,
    std::uint8_t* outBuf, std::size_t outCapacity,
    std::size_t& bytesWritten) noexcept;
```

### Viewer

**Header:** `inc/CcsdsEppViewer.hpp`

```cpp
/**
 * @brief Create a zero-copy packet viewer from raw bytes.
 * @param bytes  Raw packet bytes (must remain valid while viewer is used).
 * @return PacketViewer or std::nullopt if validation fails.
 * @note RT-safe: Span-based, no allocation.
 */
static std::optional<PacketViewer> PacketViewer::create(
    apex::compat::bytes_span bytes) noexcept;

/**
 * @brief Detailed validation (diagnostic use).
 * @param bytes  Full packet bytes.
 * @return OK on success, specific error code on failure.
 * @note RT-safe.
 */
static ValidationError PacketViewer::validate(
    apex::compat::bytes_span bytes) noexcept;

/**
 * @brief Peek Protocol ID without full packet parsing.
 * @param bytes  At least 1 byte of packet data.
 * @return Protocol ID (3 bits) or std::nullopt if empty.
 * @note RT-safe: 1-byte read, minimal validation.
 */
static std::optional<std::uint8_t> PacketViewer::peekProtocolId(
    apex::compat::bytes_span bytes) noexcept;

/**
 * @brief Peek Length of Length without full packet parsing.
 * @param bytes  At least 1 byte of packet data.
 * @return LoL (2 bits) or std::nullopt if empty.
 * @note RT-safe: 1-byte read.
 */
static std::optional<std::uint8_t> PacketViewer::peekLoL(
    apex::compat::bytes_span bytes) noexcept;
```

**EppHeaderView accessors** (all RT-safe, O(1) bit extraction):

- `version()` - Packet Version Number (3 bits)
- `protocolId()` - Encapsulation Protocol ID (3 bits)
- `lengthOfLength()` - Length of Length field (2 bits)
- `userDefined()` - User Defined field (4 bits, 4/8-octet headers)
- `protocolIdExtension()` - Protocol ID Extension (4 bits, 4/8-octet headers)
- `ccsdsDefined()` - CCSDS Defined field (16 bits, 8-octet header only)
- `packetLengthField()` - Raw Packet Length field value
- `isIdle()` - Check if idle packet (LoL=00, protocolId=0)

### Mutable Typed Messages

**Header:** `inc/CcsdsEppMutableMessage.hpp`

```cpp
/**
 * @brief Pack into fixed-size internal storage.
 * @return std::optional<EppMsg> or std::nullopt on failure.
 * @note RT-safe: Fixed-size array storage.
 */
std::optional<EppMsg<MaxPacketSize>> MutableEppMessageT<T>::pack() const noexcept;

/**
 * @brief Zero-alloc pack into caller-provided buffer.
 * @param[out] out     Output buffer.
 * @param[in]  outLen  Output buffer capacity.
 * @return Bytes written, or std::nullopt on failure.
 * @note RT-safe: No allocation, direct write to caller buffer.
 */
std::optional<std::size_t> MutableEppMessageT<T>::packInto(
    std::uint8_t* out, std::size_t outLen) const noexcept;

/**
 * @brief Build a mutable message from a typed payload reference.
 * @note RT-safe: No allocation.
 */
static std::optional<MutableEppMessageT<T>> MutableEppMessageFactory::build(
    std::uint8_t headerVariant, std::uint8_t protocolId,
    std::uint8_t userDefined, std::uint8_t protocolIde,
    std::uint16_t ccsdsDefined, const T& payloadInstance) noexcept;

/**
 * @brief Build an idle packet (no payload).
 * @note RT-safe: No allocation.
 */
static std::optional<MutableEppMessageT<T>> MutableEppMessageFactory::buildIdle(
    std::uint8_t protocolId = 0) noexcept;
```

### Streaming Processor

**Header:** `inc/CcsdsEppProcessor.hpp`

```cpp
/**
 * @brief Process incoming bytes and extract complete EPP packets.
 * @param bytes  Input byte chunk.
 * @return ProcessResult with status, bytesConsumed, packetsExtracted, resyncDrops.
 * @note RT-safe: Fixed-size buffer, bounded execution.
 */
ProcessResult Processor::process(apex::compat::bytes_span bytes) noexcept;

/**
 * @brief Set per-packet delivery callback.
 * @param delegate  Function pointer + context for zero-copy packet delivery.
 * @note RT-safe: Simple assignment.
 */
void Processor::setPacketCallback(PacketDelegate delegate) noexcept;

/**
 * @brief Configure processor behavior.
 * @param cfg  maxPacketLength, dropUntilValidHeader, compactThreshold.
 * @note RT-safe: Simple copy.
 */
void Processor::setConfig(const ProcessorConfig& cfg) noexcept;
```

**Status codes:**

| Status                   | Meaning                                    |
| ------------------------ | ------------------------------------------ |
| `OK`                     | Zero or more packets extracted             |
| `NEED_MORE`              | Not enough bytes for next packet           |
| `WARNING_DESYNC_DROPPED` | Dropped bytes to resync                    |
| `ERROR_LENGTH_OVER_MAX`  | Advertised length exceeds configured bound |
| `ERROR_BUFFER_FULL`      | Input dropped because fixed buffer is full |

---

## 6. Usage Examples

### Build packets with different header variants

```cpp
#include "CcsdsEppMessagePacker.hpp"

namespace ce = protocols::ccsds::epp;

// 1-octet idle packet (no payload)
auto idle = ce::EppMsg<>::createIdle();

// 2-octet header with payload
std::array<std::uint8_t, 3> payload{0x01, 0x02, 0x03};
auto pkt2 = ce::EppMsg<>::create2Octet(
    /*protocolId*/ 1,
    apex::compat::bytes_span(payload.data(), payload.size()));

// 4-octet header with extended fields
auto pkt4 = ce::EppMsg<>::create4Octet(
    /*protocolId*/ 1, /*userDefined*/ 0x05, /*protocolIde*/ 0,
    apex::compat::bytes_span(payload.data(), payload.size()));

// 8-octet header with CCSDS Defined field
auto pkt8 = ce::EppMsg<>::create8Octet(
    /*protocolId*/ 6, /*userDefined*/ 0x0A, /*protocolIde*/ 0x0B,
    /*ccsdsDefined*/ 0x1234,
    apex::compat::bytes_span(payload.data(), payload.size()));
```

### RT-safe zero-alloc packing

```cpp
namespace ce = protocols::ccsds::epp;

std::array<std::uint8_t, 64> buf{};
std::array<std::uint8_t, 8> payload{};
std::size_t written = 0;

bool ok = ce::packPacket(
    ce::EPP_HEADER_4_OCTET, /*protocolId*/ 1,
    /*userDefined*/ 0, /*protocolIde*/ 0, /*ccsdsDefined*/ 0,
    apex::compat::bytes_span{payload.data(), payload.size()},
    buf.data(), buf.size(), written);
// buf[0..written) contains the serialized packet
```

### Read packet fields (zero-copy)

```cpp
#include "CcsdsEppViewer.hpp"

namespace ce = protocols::ccsds::epp;

apex::compat::bytes_span raw(packet.data(), packet.size());
auto viewer = ce::PacketViewer::create(raw);
if (viewer) {
    std::uint8_t ver = viewer->hdr.version();
    std::uint8_t pid = viewer->hdr.protocolId();
    std::uint8_t lol = viewer->hdr.lengthOfLength();
    auto payload = viewer->encapsulatedData();
}
```

### Fast Protocol ID routing

```cpp
namespace ce = protocols::ccsds::epp;

auto pid = ce::PacketViewer::peekProtocolId(rawBytes);
if (pid && *pid == 1) {
    // Route to protocol handler
}
```

### Typed mutable message (zero-alloc write)

```cpp
#include "CcsdsEppMutableMessage.hpp"

namespace ce = protocols::ccsds::epp;

struct Payload { std::uint16_t a; std::uint16_t b; };
Payload data{0x0A0B, 0x0C0D};

auto m = ce::MutableEppMessageFactory::build<Payload>(
    ce::EPP_HEADER_4_OCTET, /*protocolId*/ 1,
    /*userDefined*/ 0x05, /*protocolIde*/ 0,
    /*ccsdsDefined*/ 0, data);

std::array<std::uint8_t, 64> out{};
auto written = m->packInto(out.data(), out.size());
// out[0..*written) holds the serialized packet
```

### Streaming extract with callback

```cpp
#include "CcsdsEppProcessor.hpp"

namespace ce = protocols::ccsds::epp;

ce::ProcessorDefault proc;

std::size_t packetCount = 0;
auto onPacket = [](void* ctx, apex::compat::bytes_span pkt) noexcept {
    auto* count = static_cast<std::size_t*>(ctx);
    ++(*count);
};
proc.setPacketCallback(ce::PacketDelegate{onPacket, &packetCount});

auto r = proc.process(apex::compat::bytes_span{chunk.data(), chunk.size()});
if (r.status == ce::Status::NEED_MORE) {
    // Wait for more data
}
```

---

## 7. Testing

### Test Organization

| Directory | Type | Tests              | Runs with `make test` |
| --------- | ---- | ------------------ | --------------------- |
| `tst/`    | Unit | 4 files (65 tests) | Yes                   |
| `ptst/`   | Perf | 8                  | No (manual)           |

---

## 8. See Also

- **SPP** (`../spp/`) - Space Packet Protocol (complementary)
- **COBS Framing** (`../../framing/cobs/`) - COBS framing (serial transport layer)
- **SLIP Framing** (`../../framing/slip/`) - SLIP framing (serial transport layer)
- **Compatibility** (`../../../../../utilities/compatibility/`) - Span shims for C++17
