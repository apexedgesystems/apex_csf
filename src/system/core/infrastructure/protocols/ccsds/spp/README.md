# CCSDS SPP (Space Packet Protocol)

**Namespace:** `protocols::ccsds::spp`
**Platform:** Cross-platform
**C++ Standard:** C++17
**Library:** `system_core_protocols_ccsds_spp`

RT-safe implementation of CCSDS Space Packet Protocol per CCSDS 133.0-B-2 Blue Book.

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

| Component          | Header                       | RT-Safe | Description                                    |
| ------------------ | ---------------------------- | ------- | ---------------------------------------------- |
| CommonDefs         | `CcsdsSppCommonDefs.hpp`     | Yes     | Protocol constants, bit masks, octet positions |
| SppPrimaryHeader   | `CcsdsSppMessagePacker.hpp`  | Yes     | Primary header builder/serializer (6 octets)   |
| SppSecondaryHeader | `CcsdsSppMessagePacker.hpp`  | Yes     | Secondary header container (fixed-size)        |
| SppMsg             | `CcsdsSppMessagePacker.hpp`  | Yes     | Complete packet with fixed-size storage        |
| packPacket()       | `CcsdsSppMessagePacker.hpp`  | Yes     | Zero-alloc packing into caller buffer          |
| PacketViewer       | `CcsdsSppViewer.hpp`         | Yes     | Zero-copy packet parser                        |
| MutableSppMessageT | `CcsdsSppMutableMessage.hpp` | Yes     | Typed, mutable packet assembly                 |
| Processor          | `CcsdsSppProcessor.hpp`      | Yes     | Streaming packet extractor (PD+7)              |
| CcsdsTimeCode      | `CcsdsTimeCode.hpp`          | Yes     | CUC/CDS/CCS time code parsing                  |

| Question                                     | Module                               |
| -------------------------------------------- | ------------------------------------ |
| How do I create an SPP packet?               | `SppMsg::create()` or `packPacket()` |
| How do I read packet fields without copying? | `PacketViewer::create()`             |
| How do I extract packets from a byte stream? | `Processor::process()`               |
| How do I modify and re-serialize a packet?   | `MutableSppMessageT<T>`              |
| How do I peek at APID for routing?           | `PacketViewer::peekAPID()`           |
| What time code formats are supported?        | `CcsdsTimeCode.hpp` (CUC, CDS, CCS)  |

---

## 2. When to Use

| Scenario                                       | Use SPP?         |
| ---------------------------------------------- | ---------------- |
| Spacecraft telemetry/telecommand               | Yes              |
| Ground station packet processing               | Yes              |
| Mission-specific data encapsulation            | Yes              |
| Multi-APID packet multiplexing                 | Yes              |
| Variable-length encapsulation (Internet-style) | Use EPP          |
| Serial byte-stream framing                     | Use SLIP or COBS |

### SPP vs EPP

| Aspect           | SPP                        | EPP                   |
| ---------------- | -------------------------- | --------------------- |
| Standard         | CCSDS 133.0-B-2            | CCSDS 133.1-B-3       |
| Header size      | 6 octets (fixed)           | 4-8 octets (variable) |
| Max packet       | 65542 bytes                | 65536 bytes           |
| Secondary header | Optional, mission-specific | Protocol-defined      |
| Addressing       | 11-bit APID                | Protocol ID           |
| Use case         | Space link layer           | Encapsulation layer   |

**Choose SPP when:**

- Following CCSDS space link protocol standards
- APID-based packet routing is needed
- Secondary header carries mission time codes
- Interoperability with existing CCSDS ground systems

**Choose EPP when:**

- Encapsulating Internet Protocol packets over CCSDS links
- Variable-length protocol IDs are needed

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), pure in-memory operations, 15 repeats per data point.

### Packing Throughput

| Operation  | Payload | Latency (median) | Rate        | CV%   |
| ---------- | ------- | ---------------- | ----------- | ----- |
| packPacket | 8 B     | 0.078 us         | 12.9M ops/s | 14.9% |
| packPacket | 64 B    | 0.076 us         | 13.1M ops/s | 25.4% |
| packPacket | 1 KB    | 0.078 us         | 12.8M ops/s | 22.3% |

Latency is size-independent, confirming O(1) header manipulation with no payload copying.
High CV% is expected at sub-0.1 us resolution.

### Viewing Throughput

| Operation              | Payload | Latency (median) | Rate        | CV%   |
| ---------------------- | ------- | ---------------- | ----------- | ----- |
| PacketViewer::create   | 64 B    | 0.089 us         | 11.2M ops/s | 6.7%  |
| PacketViewer::peekAPID | 64 B    | 0.035 us         | 28.9M ops/s | 26.1% |

### Processor Throughput

| Operation            | Payload | Latency (median) | Rate                       | CV%   |
| -------------------- | ------- | ---------------- | -------------------------- | ----- |
| process (1 packet)   | 64 B    | 0.077 us         | 13.1M ops/s                | 8.4%  |
| process (10 packets) | 10x64 B | 0.204 us         | 4.9M bursts/s (49M pkts/s) | 29.5% |

### Overhead

| Operation           | Latency (median) | Rate         | CV%   |
| ------------------- | ---------------- | ------------ | ----- |
| `counters()` access | 0.007 us         | 137.0M ops/s | 11.6% |

### Profiler Analysis

CPU-bound: pure 6-octet primary header bit packing/unpacking. No syscalls.
Operations are sub-0.1 us -- too fast for 1000 Hz gperf sampling to accumulate
meaningful hotspot data. perf confirms IPC of 2.13 with branch-misses at 1.01%
and negligible cache misses -- no microarchitectural bottleneck.

### Memory Footprint

| Component              | Stack   | Heap |
| ---------------------- | ------- | ---- |
| SppMsgDefault (4KB)    | 4104 B  | 0 B  |
| SppMsgSmall (256B)     | 264 B   | 0 B  |
| ProcessorDefault (8KB) | ~8280 B | 0 B  |
| ProcessorSmall (4KB)   | ~4184 B | 0 B  |
| PacketViewer           | 80 B    | 0 B  |

---

## 4. Design Principles

### RT-Safety

All public APIs are RT-safe with no dynamic memory allocation:

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-safe** | No allocation, bounded execution, safe for real-time loops |

### Zero-Allocation Design

- Packing uses caller-provided output buffer (`packPacket`) or fixed-size internal storage (`SppMsg`)
- Viewing uses non-owning spans over existing data
- Processor uses fixed-size internal buffer (template parameter)
- No internal heap allocation in any code path

### Blue Book Compliance

All implementations reference CCSDS 133.0-B-2 sections in code comments:

- Primary header is always 6 octets (Section 4.1.3)
- Total packet length = PD + 7 (Section 4.1.3.3.4)
- Secondary header format is mission-specific (Section 4.1.4.2)
- Time codes follow CCSDS 301.0-B-4

### Thread Safety

- **PacketViewer:** Thread-safe (read-only, non-owning spans)
- **SppMsg/SppPrimaryHeader:** Thread-safe (value types, no shared state)
- **Processor:** NOT thread-safe (internal buffer state). Use separate instances per thread.
- **MutableSppMessageT:** NOT thread-safe (mutable state). Intended for single-threaded assembly.

---

## 5. API Reference

### Immutable Packer

**Header:** `inc/CcsdsSppMessagePacker.hpp`

```cpp
/**
 * @brief Build a validated 6-octet primary header.
 * @param version  Version number (0-7).
 * @param type     Packet type (false=TM, true=TC).
 * @param apid     Application Process Identifier (0-2047).
 * @param seqFlags Sequence flags (0-3).
 * @param seqCount Sequence count (0-16383).
 * @param pdl      Packet data length field value.
 * @param secHdr   Secondary header flag.
 * @return Validated SppPrimaryHeader or std::nullopt on invalid fields.
 * @note RT-safe: No allocation, returns by value.
 */
static std::optional<SppPrimaryHeader> SppPrimaryHeader::build(...) noexcept;

/**
 * @brief Build a complete SPP packet with fixed-size storage.
 * @return std::optional<SppMsg> or std::nullopt on validation failure.
 * @note RT-safe: Fixed-size array storage, no heap allocation.
 */
static std::optional<SppMsg> SppMsg::create(
    std::uint8_t version, bool type, std::uint16_t apid,
    std::uint8_t seqFlags, std::uint16_t seqCount,
    apex::compat::bytes_span timeCode,
    apex::compat::bytes_span ancillary,
    apex::compat::bytes_span userData) noexcept;

/**
 * @brief Zero-alloc packing into caller-provided buffer.
 * @param[out] out      Output buffer.
 * @param[in]  outLen   Output buffer capacity.
 * @param[out] written  Bytes written on success.
 * @return true on success, false on validation/size failure.
 * @note RT-safe: No allocation, direct write to caller buffer.
 */
bool packPacket(
    std::uint8_t version, bool type, std::uint16_t apid,
    std::uint8_t seqFlags, std::uint16_t seqCount,
    apex::compat::bytes_span timeCode,
    apex::compat::bytes_span ancillary,
    apex::compat::bytes_span userData,
    std::uint8_t* out, std::size_t outLen,
    std::size_t& written) noexcept;
```

### Viewer

**Header:** `inc/CcsdsSppViewer.hpp`

```cpp
/**
 * @brief Create a zero-copy packet viewer from raw bytes.
 * @param raw  Raw packet bytes (must remain valid while viewer is used).
 * @return PacketViewer or std::nullopt if validation fails.
 * @note RT-safe: Span-based, no allocation.
 */
static std::optional<PacketViewer> PacketViewer::create(
    apex::compat::bytes_span raw) noexcept;

/**
 * @brief Fast APID extraction from raw bytes (minimal validation).
 * @param raw  At least 2 bytes of packet data.
 * @return APID value or std::nullopt if buffer too small.
 * @note RT-safe: 2-byte read, minimal validation.
 */
static std::optional<std::uint16_t> PacketViewer::peekAPID(
    apex::compat::bytes_span raw) noexcept;
```

**PrimaryHeaderView accessors** (all RT-safe, O(1) bit extraction):

- `version()` - Version number
- `type()` - Packet type (TM/TC)
- `apid()` - Application Process Identifier
- `sequenceFlags()` - Sequence flags
- `sequenceCount()` - Sequence count
- `packetDataLength()` - Packet data length field
- `hasSecondaryHeader()` - Secondary header flag

### Mutable Typed Messages

**Header:** `inc/CcsdsSppMutableMessage.hpp`

```cpp
/**
 * @brief Pack into fixed-size internal storage.
 * @return std::optional<SppMsg> or std::nullopt on failure.
 * @note RT-safe: Fixed-size array storage.
 */
std::optional<SppMsg<MaxLen>> MutableSppMessageT<T>::pack() const noexcept;

/**
 * @brief Zero-alloc pack into caller-provided buffer.
 * @param[out] out     Output buffer.
 * @param[in]  outLen  Output buffer capacity.
 * @return Bytes written, or std::nullopt on failure.
 * @note RT-safe: No allocation, direct write to caller buffer.
 */
std::optional<std::size_t> MutableSppMessageT<T>::packInto(
    std::uint8_t* out, std::size_t outLen) const noexcept;

/**
 * @brief Build a mutable message from a typed payload reference.
 * @note RT-safe: No allocation.
 */
static std::optional<MutableSppMessageT<T>> MutableSppMessageFactory::build(
    bool includeSecondary, std::uint8_t version, bool type,
    std::uint16_t apid, std::uint8_t seqFlags, std::uint16_t seqCount,
    const T& payload) noexcept;
```

### Streaming Processor

**Header:** `inc/CcsdsSppProcessor.hpp`

```cpp
/**
 * @brief Process incoming bytes and extract complete SPP packets.
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
void Processor::setConfig(ProcessorConfig cfg) noexcept;
```

**Status codes:**

| Status                   | Meaning                                    |
| ------------------------ | ------------------------------------------ |
| `OK`                     | Zero or more packets extracted             |
| `NEED_MORE`              | Not enough bytes for next packet           |
| `WARNING_DESYNC_DROPPED` | Dropped bytes to resync                    |
| `ERROR_LENGTH_OVER_MAX`  | Advertised length exceeds configured bound |
| `ERROR_BUFFER_FULL`      | Input dropped because fixed buffer is full |

### Time Code Parser

**Header:** `inc/CcsdsTimeCode.hpp`

Parses CCSDS time code formats per CCSDS 301.0-B-4:

- **CUC** (CCSDS Unsegmented Time Code) - agency-defined epoch
- **CDS** (CCSDS Day Segmented) - day count + milliseconds
- **CCS** (Calendar Segmented) - calendar date/time fields

---

## 6. Usage Examples

### Build a packet with and without secondary header

```cpp
#include "CcsdsSppMessagePacker.hpp"

namespace cs = protocols::ccsds::spp;

// No secondary header
std::array<std::uint8_t, 3> payload{0x01, 0x02, 0x03};
auto pkt = cs::SppMsg<>::create(
    /*version*/ 0, /*type*/ false, /*apid*/ 0x123,
    /*seqFlags*/ 3, /*seqCount*/ 55,
    /*timeCode*/ {}, /*ancillary*/ {},
    apex::compat::bytes_span(payload.data(), payload.size()));

// With secondary header (timeCode + ancillary)
std::array<std::uint8_t, 2> tc{0xAA, 0xBB};
std::array<std::uint8_t, 1> an{0xCC};
auto pkt2 = cs::SppMsg<>::create(
    0, true, 0x045, 2, 7,
    apex::compat::bytes_span(tc.data(), tc.size()),
    apex::compat::bytes_span(an.data(), an.size()),
    {}); // secondary-only is allowed
```

### RT-safe zero-alloc packing

```cpp
namespace cs = protocols::ccsds::spp;

std::array<std::uint8_t, 64> buf{};
std::array<std::uint8_t, 8> payload{};
std::size_t written = 0;

bool ok = cs::packPacket(
    0, false, 0x123, 3, 42,
    {}, {}, apex::compat::bytes_span{payload.data(), payload.size()},
    buf.data(), buf.size(), written);
// buf[0..written) contains the serialized packet
```

### Read packet fields (zero-copy)

```cpp
#include "CcsdsSppViewer.hpp"

namespace cs = protocols::ccsds::spp;

apex::compat::bytes_span raw(packet.data(), packet.size());
auto viewer = cs::PacketViewer::create(raw);
if (viewer) {
    bool sec = viewer->pri.hasSecondaryHeader();
    std::uint16_t pd = viewer->pri.packetDataLength();
    std::uint16_t apid = viewer->pri.apid();
}
```

### Fast APID routing

```cpp
namespace cs = protocols::ccsds::spp;

auto apid = cs::PacketViewer::peekAPID(rawBytes);
if (apid && *apid == 0x123) {
    // Route to handler
}
```

### Typed mutable message (zero-alloc write)

```cpp
#include "CcsdsSppMutableMessage.hpp"

namespace cs = protocols::ccsds::spp;

struct Payload { std::uint16_t a; std::uint16_t b; };
Payload payload{0x0A0B, 0x0C0D};

auto m = cs::MutableSppMessageFactory::build<Payload>(
    /*includeSecondary*/ false, /*version*/ 0, /*type*/ false,
    /*apid*/ 0x123, /*seqFlags*/ 3, /*seqCount*/ 7,
    payload);

std::array<std::uint8_t, 64> out{};
auto written = m->packInto(out.data(), out.size());
// out[0..*written) holds the serialized packet
```

### Streaming extract with callback

```cpp
#include "CcsdsSppProcessor.hpp"

namespace cs = protocols::ccsds::spp;

cs::ProcessorDefault proc;

std::size_t packetCount = 0;
auto onPacket = [](void* ctx, apex::compat::bytes_span pkt) noexcept {
    auto* count = static_cast<std::size_t*>(ctx);
    ++(*count);
};
proc.setPacketCallback(cs::PacketDelegate{onPacket, &packetCount});

auto r = proc.process(apex::compat::bytes_span{chunk.data(), chunk.size()});
if (r.status == cs::Status::NEED_MORE) {
    // Wait for more data
}
```

---

## 7. Testing

### Test Organization

| Directory | Type | Tests   | Runs with `make test` |
| --------- | ---- | ------- | --------------------- |
| `tst/`    | Unit | 5 files | Yes                   |
| `ptst/`   | Perf | 8       | No (manual)           |

---

## 8. See Also

- **EPP** (`../epp/`) - Encapsulation Packet Protocol (complementary)
- **COBS Framing** (`../../framing/cobs/`) - COBS framing (serial transport layer)
- **SLIP Framing** (`../../framing/slip/`) - SLIP framing (serial transport layer)
- **Compatibility** (`../../../../../utilities/compatibility/`) - Span shims for C++17
