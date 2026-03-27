# CRC Library

**Namespace:** `apex::checksums::crc`
**Platform:** Cross-platform
**C++ Standard:** C++17

Header-only CRC library with zero-allocation, CRTP-based implementations for real-time embedded systems.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Module Reference](#3-module-reference)
4. [Requirements](#4-requirements)
5. [Testing](#5-testing)
6. [See Also](#6-see-also)

---

## 1. Quick Reference

### Headers

```cpp
#include "src/utilities/checksums/crc/inc/Crc.hpp"      // All CRC variants
#include "src/utilities/checksums/crc/inc/CrcTable.hpp" // Table-driven only
```

### One-Shot Calculation

```cpp
using apex::checksums::crc::Crc32IsoHdlcTable;

std::vector<uint8_t> data = { /* ... */ };
Crc32IsoHdlcTable calc;
uint32_t result{};
calc.calculate(data, result);
```

### Streaming API

```cpp
using apex::checksums::crc::Crc32IsoHdlcTable;

Crc32IsoHdlcTable calc;
calc.reset();
calc.update(buf1.data(), buf1.size());
calc.update(buf2.data(), buf2.size());
uint32_t result{};
calc.finalize(result);
```

### Implementation Strategies

| Strategy     | Class Suffix | Table Size  | Speed   | Use Case                       |
| ------------ | ------------ | ----------- | ------- | ------------------------------ |
| Table-driven | `Table`      | 256 entries | Fastest | General use, sufficient memory |
| Nibble-table | `Nibble`     | 16 entries  | Medium  | Memory-constrained systems     |
| Bitwise      | `Bitwise`    | None        | Slowest | Minimal footprint, no tables   |
| Hardware     | `Hardware`   | None        | Fastest | CRC-32C only, SSE4.2/ARM CRC32 |

---

## 2. Design Principles

### RT-Safety

| Annotation  | Meaning                                                    |
| ----------- | ---------------------------------------------------------- |
| **RT-safe** | No allocation, bounded execution, safe for real-time loops |

All CRC operations are **RT-safe** after construction:

- No dynamic memory allocation
- O(n) time complexity in input size
- No exceptions
- No blocking operations

### Fixed-Size Data

- Lookup tables are `constexpr` and generated at compile time
- No `std::string` or `std::vector` in hot paths
- State is a single accumulator register

### CRTP Architecture

All implementations inherit from `CrcBase<T, Derived>` providing:

- Uniform API across all strategies
- Zero virtual function overhead
- Compile-time polymorphism

---

## 3. Module Reference

### CrcBase

**Header:** `CrcBase.hpp`
**Purpose:** CRTP base class providing the common CRC interface.

#### Key Types

```cpp
enum class Status : uint8_t {
  SUCCESS = 0,   ///< Computation succeeded.
  ERROR_CALC = 1 ///< Generic error during calculation.
};
```

#### API

```cpp
/// Reset streaming state to initial seed.
/// @note RT-safe: O(1), no allocation.
constexpr void reset() noexcept;

/// Feed raw bytes into the CRC engine.
/// @note RT-safe: O(n), no allocation.
Status update(const uint8_t* data, size_t len) noexcept;

/// Finalize and write the computed CRC.
/// @note RT-safe: O(1), no allocation.
Status finalize(T& out) const noexcept;

/// One-shot CRC calculation.
/// @note RT-safe: O(n), no allocation.
Status calculate(const uint8_t* data, size_t len, T& out) noexcept;
```

### CrcTable

**Header:** `CrcTable.hpp`
**Purpose:** Table-driven CRC with 256-entry lookup table.

**RT-safe:** Yes (compile-time table, no allocation)

Fastest software implementation. Uses slicing-by-1 algorithm with precomputed table.

### CrcNibble

**Header:** `CrcNibble.hpp`
**Purpose:** Nibble-table CRC with 16-entry lookup table.

**RT-safe:** Yes (compile-time table, no allocation)

Balanced speed/size tradeoff. Processes each byte in two 4-bit folds.

### CrcBitwise

**Header:** `CrcBitwise.hpp`
**Purpose:** Table-less bitwise CRC implementation.

**RT-safe:** Yes (no table, no allocation)

Smallest footprint. Processes each bit individually via shift-and-XOR.

### CrcHardware

**Header:** `CrcHardware.hpp`
**Purpose:** Hardware-accelerated CRC-32C using CPU intrinsics.

**RT-safe:** Yes (no allocation)

Supports:

- x86/x64: SSE4.2 `_mm_crc32_u*` instructions
- ARM/ARM64: `__crc32c*` instructions

**Limitation:** Only supports CRC-32C polynomial (iSCSI/SCTP/Btrfs).

#### Detection Macros

```cpp
APEX_CRC_HAS_SSE42     // SSE4.2 available (x86/x64)
APEX_CRC_HAS_ARM_CRC32 // CRC32 extension available (ARM)
APEX_CRC_HAS_HARDWARE  // Any hardware acceleration available
```

### Supported CRC Variants

All variants verified against canonical check values (ASCII "123456789"):

| Variant         | Width |        Check Value | Use Case                          |
| --------------- | :---: | -----------------: | --------------------------------- |
| CRC-8/SMBUS     |   8   |               0xF4 | I2C, SMBus                        |
| CRC-8/MAXIM     |   8   |               0xA1 | 1-Wire, iButton                   |
| CRC-8/AUTOSAR   |   8   |               0xDF | Automotive                        |
| CRC-16/XMODEM   |  16   |             0x31C3 | XMODEM, ZMODEM                    |
| CRC-16/IBM-3740 |  16   |             0x29B1 | Common "CCITT" variant            |
| CRC-16/IBM-SDLC |  16   |             0x906E | X.25, HDLC, PPP                   |
| CRC-16/MODBUS   |  16   |             0x4B37 | Modbus RTU                        |
| CRC-16/USB      |  16   |             0xB4C8 | USB tokens                        |
| CRC-24/OPENPGP  |  24   |           0x21CF02 | PGP, GPG                          |
| CRC-32/ISO-HDLC |  32   |         0xCBF43926 | Ethernet, ZIP, PNG, gzip          |
| CRC-32/ISCSI    |  32   |         0xE3069283 | iSCSI, SCTP, Btrfs, ext4 (CRC32C) |
| CRC-32/MPEG-2   |  32   |         0x0376E6E7 | MPEG-2, DVB, ATSC                 |
| CRC-64/ECMA     |  64   | 0x6C40DF5F0B497347 | ECMA-182                          |
| CRC-64/XZ       |  64   | 0x995DC9BBDF1939FA | XZ Utils, LZMA                    |

---

## 4. Requirements

### Build Requirements

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- No external dependencies

### Optional: Hardware Acceleration

- x86/x64: CPU with SSE4.2 support (Intel Nehalem+, AMD Bulldozer+)
- ARM: CPU with CRC32 extension (ARMv8-A+)

CMake automatically enables `-msse4.2` on supported x86 platforms.

---

## 5. Testing

Run tests using the standard Docker workflow:

```bash
# Build
docker compose run --rm -T dev-cuda make debug

# Run CRC tests only
docker compose run --rm -T dev-cuda ctest --test-dir build/native-linux-debug -L crc

# Run performance tests
docker compose run --rm -T dev-cuda ./build/native-linux-debug/bin/ptests/TestChecksumsCrc_PTEST
```

### Test Organization

| File                 | Tests | Coverage                      |
| -------------------- | ----: | ----------------------------- |
| CrcTable_uTest.cpp   |    15 | Table-driven check values     |
| CrcBitwise_uTest.cpp |    14 | Bitwise check values          |
| CrcNibble_uTest.cpp  |    14 | Nibble-table check values     |
| Crc_pTest.cpp        |     8 | Throughput, scaling, hardware |

### Performance

CRC-32 throughput on x86-64 (4KB payload, 15 repeats):

| Implementation    | Median (us) |  CV% | Throughput |
| ----------------- | ----------: | ---: | ---------: |
| Bitwise           |      158.88 | 1.8% |  24.6 MB/s |
| Nibble-table      |       19.03 | 1.1% | 205.2 MB/s |
| Table-driven      |       10.20 | 6.0% | 383.2 MB/s |
| Hardware (SSE4.2) |        0.65 | 2.6% |   6.0 GB/s |

Streaming and one-shot APIs have identical throughput. Chunked updates
(multiple `update()` calls) add no measurable overhead versus single-call
`calculate()`.

---

## 6. See Also

- [CRC RevEng Catalog](https://reveng.sourceforge.io/crc-catalogue/) - Canonical CRC parameters
- `src/utilities/compatibility/` - Span compatibility shims used by CRC API
- [Vernier](https://github.com/apexedgesystems/vernier) - Performance testing framework

---

## References

- [RFC 1662](https://www.rfc-editor.org/rfc/rfc1662) - PPP/HDLC framing (CRC-16/IBM-SDLC, CRC-32/ISO-HDLC)
- [RFC 4880](https://www.rfc-editor.org/rfc/rfc4880) - OpenPGP (CRC-24/OPENPGP)
- [ECMA-182](https://www.ecma-international.org/publications-and-standards/standards/ecma-182/) - CRC-64/ECMA
