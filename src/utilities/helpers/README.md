# Apex Helpers Library

**Namespace:** `apex::helpers`
**Platform:** Cross-platform (Linux primary)
**C++ Standard:** C++17
**Library:** `utilities_helpers`

A collection of low-level helper utilities for embedded and real-time systems. Provides bit manipulation, byte ordering, CPU primitives, file descriptor management, and socket utilities with clear RT-safety annotations.

---

## Table of Contents

1. [Quick Reference](#1-quick-reference)
2. [Design Principles](#2-design-principles)
3. [Domains](#3-domains)
4. [RT-Safety Guide](#4-rt-safety-guide)
5. [Building](#5-building)
6. [Testing](#6-testing)

---

## 1. Quick Reference

| Domain  | Header        | RT-Safety       | Purpose                                         |
| ------- | ------------- | --------------- | ----------------------------------------------- |
| args    | `Args.hpp`    | RT-UNSAFE       | CLI argument parsing and usage printing         |
| bits    | `Bits.hpp`    | RT-SAFE         | Bit manipulation (set, clear, flip, test)       |
| bytes   | `Bytes.hpp`   | RT-SAFE         | Byte extraction, LE/BE load/store, endian names |
| cpu     | `Cpu.hpp`     | RT-SAFE         | CPU relax/pause hints, exponential backoff      |
| fd      | `Fd.hpp`      | RT-CAUTION      | RAII file descriptor wrapper                    |
| files   | `Files.hpp`   | RT-SAFE/CAUTION | File I/O, path utilities, binary struct loading |
| format  | `Format.hpp`  | RT-UNSAFE       | Human-readable formatting (bytes, Hz, counts)   |
| net     | `Net.hpp`     | RT-UNSAFE       | Socket utilities and TCP options                |
| strings | `Strings.hpp` | RT-SAFE         | String manipulation, search, prefix/suffix      |

**Convenience Header:** `Utilities.hpp` includes all domain headers plus `runningAverage()`.

---

## 2. Design Principles

### No Exceptions

All APIs are `noexcept`. No dynamic allocation in RT-safe functions:

```cpp
// RT-SAFE: Pure constexpr, no allocation
apex::helpers::bits::set(byte, 3);
auto val = apex::helpers::bytes::loadLe<uint32_t>(data);
```

### Hardened Inputs

Functions handle out-of-range inputs safely without UB:

```cpp
// Bit index is masked to [0,7] - never UB
apex::helpers::bits::set(byte, 255);  // Same as bit 7
```

### Explicit Endianness

No implicit byte order assumptions. Use explicit LE/BE functions:

```cpp
// Little-endian: LSB first
auto le = apex::helpers::bytes::loadLe<uint16_t>(data);

// Big-endian: MSB first
auto be = apex::helpers::bytes::loadBe<uint16_t>(data);
```

---

## 3. Domains

### args - CLI Argument Parsing (`apex::helpers::args`)

Fixed-arity argument parsing for CLI tools:

```cpp
#include "src/utilities/helpers/inc/Args.hpp"

using namespace apex::helpers::args;

// Define accepted flags
ArgMap map = {
  {0, {"--input", 1, true, "Input file path"}},
  {1, {"--output", 1, false, "Output file path"}},
  {2, {"--verbose", 0, false, "Enable verbose output"}},
};

// Parse arguments
ParsedArgs parsed;
std::string error;
if (!parseArgs(args, map, parsed, error)) {
  printUsage(argv[0], "My CLI tool", map);
  return 1;
}
```

### bits - Bit Manipulation (`apex::helpers::bits`)

Safe bit operations with masked indices:

```cpp
#include "src/utilities/helpers/inc/Bits.hpp"

std::uint8_t flags = 0;
apex::helpers::bits::set(flags, 3);    // Set bit 3
apex::helpers::bits::clear(flags, 3);  // Clear bit 3
apex::helpers::bits::flip(flags, 3);   // Toggle bit 3
bool isSet = apex::helpers::bits::test(flags, 3);
```

### bytes - Byte Ordering (`apex::helpers::bytes`)

Portable LE/BE operations using memcpy (optimizer-friendly):

```cpp
#include "src/utilities/helpers/inc/Bytes.hpp"

// Extract individual bytes
auto lsb = apex::helpers::bytes::extractLe(0x12345678u, 0);  // 0x78
auto msb = apex::helpers::bytes::extractBe(0x12345678u, 0);  // 0x12

// Load/store with explicit byte order
uint32_t val = apex::helpers::bytes::loadLe<uint32_t>(buffer);
apex::helpers::bytes::storeBe(val, buffer);

// Endianness names
const char* name = apex::helpers::bytes::nativeEndianName();  // "little-endian"

// Convert to byte array
auto bytes = apex::helpers::bytes::toBytes(myStruct);
```

### cpu - CPU Primitives (`apex::helpers::cpu`)

Architecture-specific pause hints and backoff:

```cpp
#include "src/utilities/helpers/inc/Cpu.hpp"

// Single relax hint (PAUSE on x86, YIELD on ARM)
apex::helpers::cpu::relax();

// Exponential backoff for contested locks
apex::helpers::cpu::ExponentialBackoff backoff;
while (condition) {
  backoff.spinOnce();  // 1, 2, 4, ... up to 4096 iterations
}
```

### fd - File Descriptor RAII (`apex::helpers::fd`)

Move-only RAII wrapper for POSIX file descriptors:

```cpp
#include "src/utilities/helpers/inc/Fd.hpp"

using apex::helpers::fd::UniqueFd;

UniqueFd sock(::socket(AF_INET, SOCK_STREAM, 0));
if (!sock) { /* error */ }

int raw = sock.get();       // Access raw fd
int owned = sock.release(); // Transfer ownership
sock.reset(new_fd);         // Replace (closes previous)
// Destructor calls close() automatically
```

### net - Socket Utilities (`apex::helpers::net`)

Nonblocking setup and TCP options (RT-UNSAFE):

```cpp
#include "src/utilities/helpers/inc/Net.hpp"

// Create socket with CLOEXEC + NONBLOCK atomically
int fd = apex::helpers::net::socketCloexecNonblock(AF_INET, SOCK_STREAM, 0);

// Set common TCP options
apex::helpers::net::setNoDelay(fd, true);   // TCP_NODELAY
apex::helpers::net::setReuseAddr(fd, true); // SO_REUSEADDR

// Linux-specific
apex::helpers::net::setQuickAck(fd, true);  // TCP_QUICKACK
apex::helpers::net::setBusyPoll(fd, 50);    // SO_BUSY_POLL
```

### strings - String Manipulation (`apex::helpers::strings`)

Safe string operations with bounds checking:

```cpp
#include "src/utilities/helpers/inc/Strings.hpp"

// Copy to fixed-size array
std::array<char, 64> buf;
apex::helpers::strings::copyToFixedArray(buf, "hello");

// Prefix/suffix matching
bool isNode = apex::helpers::strings::startsWith("node0", "node");
bool isLog = apex::helpers::strings::endsWith("app.log", ".log");

// Parse index from name
int idx = apex::helpers::strings::parseIndexFromName("cpu3", "cpu");  // 3

// Search in string collection
std::vector<std::string> vec = {"foo", "bar", "baz"};
bool found = apex::helpers::strings::containsString(vec, "bar");  // true
```

### files - File I/O (`apex::helpers::files`)

RT-safe file operations using C-style I/O:

```cpp
#include "src/utilities/helpers/inc/Files.hpp"

// Read file contents
char buf[256];
size_t len = apex::helpers::files::readFileToBuffer("/proc/uptime", buf, sizeof(buf));

// Read integers from sysfs
int64_t freq = apex::helpers::files::readFileInt64("/sys/devices/.../scaling_cur_freq");

// Path utilities
if (apex::helpers::files::pathExists("/dev/sda")) { /* ... */ }
if (apex::helpers::files::isDirectory("/proc/1")) { /* ... */ }

// Check file exists (string_view overload - allocates for null-termination)
if (apex::helpers::files::checkFileExists(pathView)) { /* ... */ }

// Load binary file into struct
MyPackedStruct data;
std::string error;
if (!apex::helpers::files::hex2cpp(filename, data, error)) {
  // Handle error
}
```

### format - Human-Readable Formatting (`apex::helpers::format`)

Formatting for CLI output (NOT RT-safe):

```cpp
#include "src/utilities/helpers/inc/Format.hpp"

// Byte formatting
std::string s1 = apex::helpers::format::bytesBinary(1536);    // "1.5 KiB"
std::string s2 = apex::helpers::format::bytesDecimal(1500);   // "1.5 KB"

// Frequency formatting
std::string s3 = apex::helpers::format::frequencyHz(2400000000);  // "2.40 GHz"

// Count formatting
std::string s4 = apex::helpers::format::count(1234567);  // "1.2M"

// Debug output - print byte span as hex
apex::helpers::format::printSpan(byteSpan);  // "0x12 0x34 0x56\n"
```

---

## 4. RT-Safety Guide

Each function is annotated with its RT-safety level:

| Level      | Meaning                                    | Use In RT Path?     |
| ---------- | ------------------------------------------ | ------------------- |
| RT-SAFE    | Pure constexpr, no syscalls, no allocation | Yes                 |
| RT-CAUTION | May syscall on construction/destruction    | Setup/teardown only |
| RT-UNSAFE  | Always syscalls, non-deterministic latency | Never               |

### Summary by Header

| Header        | RT-Safety       | Notes                                   |
| ------------- | --------------- | --------------------------------------- |
| Args.hpp      | RT-UNSAFE       | Allocates std::unordered_map            |
| Bits.hpp      | RT-SAFE         | All constexpr                           |
| Bytes.hpp     | RT-SAFE         | memcpy only, optimizer eliminates       |
| Cpu.hpp       | RT-SAFE         | Single CPU instruction                  |
| Fd.hpp        | RT-CAUTION      | close() in destructor                   |
| Files.hpp     | RT-SAFE/CAUTION | C-style I/O; syscall latency variable   |
| Format.hpp    | RT-UNSAFE       | Returns std::string (heap allocation)   |
| Net.hpp       | RT-UNSAFE       | All functions are syscalls              |
| Strings.hpp   | RT-SAFE         | No allocation, bounded operations       |
| Utilities.hpp | (varies)        | Convenience header; imports all domains |

### RT Build Warning

For strict RT builds, define `RT_BUILD` to get compile-time warnings when including RT-UNSAFE headers:

```cpp
#define RT_BUILD
#include "src/utilities/helpers/inc/Net.hpp"  // Warning emitted
```

---

## 5. Building

This is a header-only (interface) library. No separate compilation is required. Simply include the desired headers:

```cpp
#include "src/utilities/helpers/inc/Bits.hpp"
#include "src/utilities/helpers/inc/Bytes.hpp"
```

The main project builds with:

```bash
# Build with CUDA support
docker compose run --rm -T dev-cuda make debug

# Clean build
docker compose run --rm -T dev-cuda make clean debug
```

---

## 6. Testing

Tests are in `tst/` with comprehensive coverage:

```bash
# Run all tests
docker compose run --rm -T dev-cuda make test

# Run helpers tests only
docker compose run --rm -T dev-cuda ctest -R "BitOps|ByteOps"
```

Test coverage includes:

- Bit operations: set/clear/flip/test, index masking
- Byte operations: LE/BE extraction, load/store round-trips
- Edge cases: 8-bit values, boundary conditions, out-of-range indices

### Performance

Throughput on x86-64 (batch of 10,000, 15 repeats):

| Operation                    | Median (us) | Per-Op (ns) | CV%  |
| ---------------------------- | ----------- | ----------- | ---- |
| loadLe(u32)                  | 37.3        | 3.7         | 3.6% |
| loadBe(u32)                  | 64.9        | 6.5         | 1.5% |
| loadLe(u64)                  | 37.1        | 3.7         | 2.6% |
| Hex encode (1KB)             | 8.3         | -           | 5.3% |
| Hex decode (1KB)             | 17.2        | -           | 2.9% |
| BitOps (set+flip+test+clear) | 177.2       | 17.7        | 0.9% |

LE loads are native-endian (no swap). BE loads add a bswap instruction.
Hex encoding uses a 256-entry lookup table.
