# Data Proxy

**Namespace:** `system_core::data_proxy`
**Library:** `utilities_data_proxy`
**Platform:** Cross-platform
**C++ Standard:** C++23

Composable byte-level transformation proxies for real-time data pipelines.
Each proxy is a standalone building block. Support components compose proxy
stacks from whichever primitives they need.

---

## 1. Quick Reference

| Header                | Purpose                                        | RT-Safe |
| --------------------- | ---------------------------------------------- | ------- |
| `ByteMaskProxy.hpp`   | General-purpose AND/XOR byte mask queue        | Yes     |
| `EndiannessProxy.hpp` | Compile-time byte-swap for cross-platform data | Yes     |
| `MasterDataProxy.hpp` | Convenience compositor (chains proxies)        | Yes     |

| Question                                            | Answer                                                |
| --------------------------------------------------- | ----------------------------------------------------- |
| How do I byte-swap structs for cross-platform data? | `EndiannessProxy` or `MasterDataProxy`                |
| How do I apply AND/XOR masks to data?               | `ByteMaskProxy` or `MasterDataProxy`                  |
| How do I compose multiple proxies?                  | `MasterDataProxy<T, SwapReq, MaskEnabled>`            |
| How do I inject faults at the byte level?           | `ByteMaskProxy` via `DataTransform` support component |

---

## 2. When to Use

| Scenario                                           | Use This Library?        |
| -------------------------------------------------- | ------------------------ |
| Byte-swap for cross-platform telemetry or hardware | Yes                      |
| Byte-level AND/XOR masking (faults, overrides)     | Yes                      |
| Compose swap + mask in one proxy                   | Yes                      |
| Typed data containers with category semantics      | No -- `system_component` |
| Watchpoints, sequences, action engine              | No -- `action`           |

**Design intent:** All RT-safe, no dynamic allocation, all operations bounded
O(sizeof(T)), all functions noexcept. Proxies are mechanism primitives -- they
describe _how_ to transform, not _why_. The _why_ lives in support components
that compose them.

---

## 3. ByteMaskProxy

Static-sized circular queue of AND/XOR byte mask operations.

Mask rule: `byte = (byte & AND[i]) ^ XOR[i]`

Common patterns:

| Pattern | AND  | XOR  | Effect              |
| ------- | ---- | ---- | ------------------- |
| Zero    | 0x00 | 0x00 | Forces byte to 0    |
| High    | 0x00 | 0xFF | Forces byte to 0xFF |
| Flip    | 0xFF | 0xFF | Inverts all bits    |
| Set     | 0x00 | val  | Forces byte to val  |

```cpp
#include "src/utilities/data_proxy/inc/ByteMaskProxy.hpp"

system_core::data_proxy::ByteMaskProxy proxy;
proxy.pushZeroMask(4, 2);  // Zero bytes 4-5

std::array<std::uint8_t, 8> data = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
proxy.apply(data.data(), data.size());
// data[4] == 0x00, data[5] == 0x00, rest unchanged
```

---

## 4. EndiannessProxy

Compile-time byte-swap between input and output buffers.

- Scalar types (integers, floats): automatic via `swapBytes()`
- Struct types: user provides `void endianSwap(const T& in, T& out) noexcept` via ADL

```cpp
#include "src/utilities/data_proxy/inc/EndiannessProxy.hpp"

std::uint32_t in = 0x12345678;
std::uint32_t out = 0;
system_core::data_proxy::EndiannessProxy<std::uint32_t, true> proxy(&in, &out);
proxy.resolve();  // out == 0x78563412
```

---

## 5. MasterDataProxy

Convenience compositor that chains `EndiannessProxy` and `ByteMaskProxy` into
a single interface. Template parameters control which capabilities are active
at compile time -- disabled capabilities have zero overhead.

```cpp
#include "src/utilities/data_proxy/inc/MasterDataProxy.hpp"

struct Packet { std::uint32_t a; std::uint16_t b; };

// Endianness only (no mask API exposed)
MasterDataProxy<Packet, true, false> swapOnly(&input);

// Mask only (no swap)
MasterDataProxy<Packet, false, true> maskOnly(&input);

// Both
MasterDataProxy<Packet, true, true> both(&input);
```

---

## 6. Requirements

| Requirement  | Details                     |
| ------------ | --------------------------- |
| C++ Standard | C++23                       |
| Dependencies | `utilities_compatibility`   |
| OS           | None (no OS-specific calls) |

---

## 7. See Also

- [system_component](../../system/core/infrastructure/system_component/) -- Data primitives (DataCategory, ModelData, DataTarget)
- [data_transform](../../system/core/support/data_transform/) -- DataTransform support component (fault injection via ByteMaskProxy)
- [action](../../system/core/components/action/) -- Action engine (watchpoints, sequences, commands)
