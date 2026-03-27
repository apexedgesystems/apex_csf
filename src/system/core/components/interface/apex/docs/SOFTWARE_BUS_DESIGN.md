# Software Bus Design Notes

## Context

Comparison of Apex protocol stack against CCSDS/cFS architecture raised questions
about layering, protocol weight, and bus flexibility.

## CCSDS Stack vs Apex Stack

| Layer         | CCSDS                        | Apex (current)                     |
| ------------- | ---------------------------- | ---------------------------------- |
| Application   | SPP (APID + cmd/tlm headers) | APROTO (fullUid + opcode + crypto) |
| Transfer      | TM / TC / AOS frames         | SLIP/COBS + TCP/UDP (no frames)    |
| Encapsulation | EPP (wrap non-CCSDS data)    | EPP (same)                         |

Key insight: APROTO and SPP are peers (both application-layer). In cFS, SPP is
the universal bus format and all application semantics (command codes, checksums)
ride in SPP user data via cFS-defined secondary headers. Apex made the
application protocol explicit and first-class rather than layering it inside SPP.

## Protocol-Agnostic Software Bus

Design consideration: the software bus should route and deliver messages
independent of wire format. The underlying protocol becomes a pluggable codec.

### Bus Contract

```
Bus sees:     (destination, payload bytes, metadata)
Codec sees:   serialize(destination, opcode, payload) -> wire bytes
              deserialize(wire bytes) -> (destination, opcode, payload)
Routing:      map destination identifier -> subscriber(s)
```

### Codec Implementations

| Codec   | Use Case                                     |
| ------- | -------------------------------------------- |
| APROTO  | Default. Internal + external with encryption |
| SPP     | cFS-compatible bus for CCSDS missions        |
| Compact | Lightweight internal-only (see below)        |
| CAN     | Fieldbus with 8-byte frame constraint        |

Swapping codecs should be a configuration decision, not a code change.

### Address Translation

APROTO uses 24-bit fullUid, SPP uses 11-bit APID. An SPP codec would need a
static mapping table (APID <-> fullUid) provided at configuration time.

### Feature Gaps Across Codecs

Not all codecs support all features. The bus must handle this gracefully:

| Feature      | APROTO | SPP | Compact |
| ------------ | ------ | --- | ------- |
| Encryption   | Native | No  | No      |
| ACK/NAK      | Native | No  | No      |
| Segmentation | No     | Yes | No      |
| Time codes   | No     | Yes | No      |

Features not supported by a codec are either unavailable on that bus
configuration or handled at a higher layer.

## APROTO Header Weight Analysis

Current APROTO header is 14 bytes fixed. For small commands (4-byte payload),
overhead is ~78%. With CRC, ~82%.

### Header Breakdown

| Field          | Size | Needed on internal bus?      |
| -------------- | ---- | ---------------------------- |
| Magic (0x5041) | 2B   | No (bus guarantees framing)  |
| Version        | 1B   | Rarely needed internally     |
| Flags          | 1B   | Yes (ACK, internal/external) |
| fullUid        | 4B   | Yes (routing key)            |
| Opcode         | 2B   | Yes                          |
| Sequence       | 2B   | Yes (ACK correlation)        |
| Payload length | 2B   | Yes (variable-length msgs)   |

Magic + version are sync/validation fields that belong in the transport layer,
not the application protocol. On a trusted internal bus with guaranteed framing,
they are redundant.

### Compact Internal Profile

A lean internal-only format dropping transport concerns:

```
flags(1) + fullUid(4) + opcode(2) + sequence(2) + length(2) = 11 bytes
```

### Compact as Strict Subset of APROTO

Critical design constraint: the compact internal header must be a strict subset
of the full APROTO header, not a different structure. The compact fields live
inside the full format at the same offsets:

```
Compact (internal):  [flags|fullUid|opcode|seq|len|payload]
                      \____________ same bytes ____________/
APROTO (external):   [magic|ver| flags|fullUid|opcode|seq|len |crypto|payload|CRC]
                      ^^^^^^^^^^^                               ^^^^^^^^^^^^^^^^^^
                      added by Interface on tx, stripped on rx
```

This means:

- No field reordering or repacking at the boundary
- Interface component does ONE thing: wrap (tx) / unwrap (rx)
- Zero-copy inbound: validate, advance pointer past magic/version/crypto,
  inner span IS the compact message ready for the bus
- Zero-copy outbound: prepend wrapper header, append crypto + CRC

### Interface Component Simplicity

The Interface is the external gateway. The less work it does, the better.

With compact-as-subset, the Interface responsibility is minimal:

| Direction | Interface Action                                  |
| --------- | ------------------------------------------------- |
| Inbound   | Validate magic/version/CRC, decrypt if needed,    |
|           | strip outer wrapper, forward inner compact to bus |
| Outbound  | Wrap compact message with magic/version,          |
|           | encrypt if needed, append CRC, send on link       |

No format translation. No re-serialization. No payload touching.

Components on the bus never see or think about encryption, CRC, or wire format.
They post and receive compact messages. The Interface is the only component
that knows about external protocol concerns.

### Dual-Profile Summary

| Profile  | Header | Use Case                                  |
| -------- | ------ | ----------------------------------------- |
| Internal | 11B    | Bus traffic, trusted, no crypto           |
| External | 14B+   | Ground links, encryption, full validation |

Same logical message, different wire encoding. The compact format lives inside
the full format. The Interface wraps/unwraps at the boundary.

## Codec Abstraction Layer

### Design Principle: Adapt, Don't Unify

APROTO and CCSDS SPP have different fields, endianness, and header sizes.
Rewriting one to match the other would mean losing features (APROTO loses
fullUid/encryption) or adding non-standard fields (SPP gets bloated).

Instead, each protocol keeps its own internal implementation. The commonality
is the interface they present to the bus: a thin adapter that maps
protocol-specific fields into a common result.

### Common Decode Result

```cpp
struct DecodeResult {
    uint32_t   routingKey;  // fullUid (APROTO) or APID->fullUid (SPP)
    uint16_t   opcode;      // opcode (APROTO) or cmd code from secondary hdr (SPP)
    uint16_t   sequence;    // both protocols have sequence numbers
    bool       isResponse;  // flags.isResponse (APROTO) or type==TM (SPP)
    bytes_span payload;     // the bytes the component cares about
};
```

This maps directly to MessageBuffer metadata fields. The bus only sees
DecodeResult, never protocol-specific types like AprotoHeader or SppPrimaryHeader.

### Adapter Mapping Per Protocol

| DecodeResult field | APROTO source    | SPP source                       |
| ------------------ | ---------------- | -------------------------------- |
| routingKey         | header.fullUid   | APID -> fullUid lookup table     |
| opcode             | header.opcode    | cFS cmd code in secondary header |
| sequence           | header.sequence  | sequenceCount                    |
| isResponse         | flags.isResponse | type == TM (0)                   |
| payload            | getPayload()     | user data field                  |

### Encode Adapter (Reverse Direction)

```cpp
encode(routingKey, opcode, sequence, isResponse, payload) -> wire bytes
```

| Encode input | APROTO output    | SPP output                     |
| ------------ | ---------------- | ------------------------------ |
| routingKey   | header.fullUid   | fullUid -> APID reverse lookup |
| opcode       | header.opcode    | cmd code in secondary header   |
| sequence     | header.sequence  | sequenceCount                  |
| isResponse   | flags.isResponse | type = TM(0) or TC(1)          |
| payload      | encodePacket()   | packPacket() with user data    |

### What Does NOT Change

- AprotoCodec.hpp stays as-is (encode/decode functions unchanged)
- CcsdsSppMessagePacker.hpp stays as-is
- Each protocol's tests stay as-is
- Only a thin adapter per protocol is added

### Layered Abstraction Summary

```
Component -> IInternalBus -> MessageBuffer (payload + metadata)
                                  |
                            [Bus Codec Adapter]
                                  |
                    +-------------+-------------+
                    |                           |
              AprotoCodec                 SppMessagePacker
              (unchanged)                  (unchanged)
                    |                           |
              [Framing Layer]            [Framing Layer]
              SLIP / COBS               SLIP / COBS / TM/TC
                    |                           |
              [Transport]                [Transport]
              TCP / UDP / Serial         TCP / UDP / Serial
```

## Transfer Frame Gap

Apex currently has no transfer frame layer (TM/TC/AOS equivalent). For
ground links over serial or RF requiring sync markers and frame-level error
detection, this is the missing piece. SLIP/COBS provide framing but not the
multiplexing and sync properties of CCSDS transfer frames.

## Open Questions

1. Is the 3-byte savings (14B -> 11B) worth maintaining two header profiles?
   Leaning yes: the real win is not bytes saved but separation of concerns.
   Components should not carry transport/crypto fields they never use.
2. Resolved: compact is a strict subset of APROTO, not a separate codec.
   The Interface wraps/unwraps at the boundary. No second format to maintain.
3. What is the minimum viable bus abstraction that supports codec swapping
   without over-engineering the Interface component?
4. Should transfer frames (TM/TC) be implemented for RF/serial ground links,
   or is SLIP/COBS + APROTO sufficient?
