# Protocol Common Utilities

Header-only mixin library providing byte-level I/O tracing and formatting for protocol adapters.

**Library:** `system_core_protocols_common`
**Namespace:** `apex::protocols`
**Header:** `inc/ByteTrace.hpp`

---

## 1. Quick Reference

| Component                  | Type          | Purpose                                             | RT-Safe                 |
| -------------------------- | ------------- | --------------------------------------------------- | ----------------------- |
| `TraceDirection`           | Enum          | RX/TX direction indicator                           | Yes                     |
| `TraceCallback`            | Type alias    | Callback signature for byte tracing                 | -                       |
| `ByteTrace`                | Mixin class   | Attach/detach/enable byte-level tracing             | Setup: No, Runtime: Yes |
| `formatBytesHex`           | Free function | Format bytes as hex string into fixed buffer        | Yes                     |
| `formatTraceMessage`       | Free function | Format full trace message with prefix/direction/hex | Yes                     |
| `toString(TraceDirection)` | Free function | Convert direction to "RX"/"TX" string               | Yes                     |

---

## 2. When to Use

| Scenario                               | Use This Library?                     |
| -------------------------------------- | ------------------------------------- |
| Protocol adapter needs debug tracing   | Yes -- inherit from `ByteTrace`       |
| Format raw bytes as hex for logging    | Yes -- `formatBytesHex`               |
| Format complete trace line with prefix | Yes -- `formatTraceMessage`           |
| High-level structured logging          | No -- use `system_core_logs`          |
| Binary protocol parsing                | No -- use protocol-specific libraries |

**Design intent:** Zero-overhead tracing for protocol I/O debugging. When tracing is disabled, the only cost is a null pointer check per I/O call. When enabled, the callback-based design lets the caller control the destination (ring buffer, log file, console).

---

## 3. Performance

Measured on x86_64 (clang-21, -O2), Docker container, 15 repeats per data point.

### Format Throughput

| Function             | Payload               | Median (us) | Calls/s | CV%   |
| -------------------- | --------------------- | ----------- | ------- | ----- |
| `formatBytesHex`     | 4B                    | 0.023       | 43.5M   | 2.7%  |
| `formatBytesHex`     | 32B                   | 0.125       | 8.0M    | 13.4% |
| `formatBytesHex`     | 64B                   | 0.247       | 4.1M    | 3.0%  |
| `formatBytesHex`     | 64B (truncated to 8B) | 0.042       | 23.6M   | 5.1%  |
| `formatTraceMessage` | 4B                    | 0.081       | 12.4M   | 5.3%  |
| `formatTraceMessage` | 32B                   | 0.192       | 5.2M    | 2.0%  |

### Invoke Overhead

| Operation                              | Median (us) | Calls/s | CV%  |
| -------------------------------------- | ----------- | ------- | ---- |
| `invokeTrace` (enabled, noop callback) | 0.013       | 75.8M   | 2.4% |

### Profiler Analysis (gperftools)

| Function             | Self-Time | Type                                    |
| -------------------- | --------- | --------------------------------------- |
| `formatBytesHex`     | 77.3%     | CPU-bound (hex lookup loop)             |
| `formatTraceMessage` | 12.0%     | CPU-bound (delegates to formatBytesHex) |
| `invokeTrace`        | 1.3%      | Near-zero overhead                      |

### Memory Footprint

| Component                          | Stack                                 | Heap |
| ---------------------------------- | ------------------------------------- | ---- |
| `ByteTrace` instance               | 24B (pointer + pointer + atomic bool) | 0    |
| `formatBytesHex` (32B payload)     | ~128B output buffer (caller-owned)    | 0    |
| `formatTraceMessage` (32B payload) | ~256B output buffer (caller-owned)    | 0    |

---

## 4. Design Principles

- **Zero overhead when disabled** -- Null pointer check + atomic load per I/O call
- **Callback-based** -- Caller owns the trace destination (no dependency on logging)
- **RT-safe runtime path** -- `setTraceEnabled`, `traceEnabled`, `invokeTrace` use relaxed atomics only
- **No heap allocation** -- All buffers are caller-owned, fixed-size
- **No exceptions** -- All functions are `noexcept`
- **Mixin pattern** -- Protocol adapters inherit from `ByteTrace` to gain tracing capability
- **Freestanding-compatible** -- Uses only `<atomic>`, `<cstddef>`, `<cstdint>` (no OS dependencies)

---

## 5. API Reference

### TraceDirection

```cpp
enum class TraceDirection : std::uint8_t {
  RX = 0,  // Data received
  TX = 1   // Data transmitted
};

/// @note RT-safe: Returns static string.
const char* toString(TraceDirection dir) noexcept;
```

### TraceCallback

```cpp
/// Callback signature for byte tracing.
/// @note RT-safe if implementation is RT-safe.
using TraceCallback = void (*)(TraceDirection dir, const std::uint8_t* data,
                               std::size_t len, void* userData) noexcept;
```

### ByteTrace

```cpp
class ByteTrace {
public:
  /// @note NOT RT-safe: Call during setup phase only.
  void attachTrace(TraceCallback callback, void* userData = nullptr) noexcept;

  /// @note NOT RT-safe: Call during teardown phase only.
  void detachTrace() noexcept;

  /// @note RT-safe: Atomic store.
  void setTraceEnabled(bool enabled) noexcept;

  /// @note RT-safe: Atomic load.
  [[nodiscard]] bool traceEnabled() const noexcept;

  /// @note RT-safe: Pointer check.
  [[nodiscard]] bool traceAttached() const noexcept;

protected:
  /// @note RT-safe if callback is RT-safe.
  void invokeTrace(TraceDirection dir, const std::uint8_t* data, std::size_t len) noexcept;
};
```

### Format Helpers

```cpp
/// @note RT-safe: No allocation, bounded time.
std::size_t formatBytesHex(const std::uint8_t* data, std::size_t len,
                           char* out, std::size_t outSize,
                           std::size_t maxBytes = 32) noexcept;

/// @note RT-safe: No allocation, bounded time.
std::size_t formatTraceMessage(TraceDirection dir, const std::uint8_t* data,
                               std::size_t len, char* out, std::size_t outSize,
                               const char* prefix, std::size_t maxBytes = 32) noexcept;
```

---

## 6. Usage Examples

### Mixin Pattern

```cpp
class MyAdapter : public SomeDevice, public apex::protocols::ByteTrace {
public:
  Status write(apex::compat::bytes_span data) {
    // ... perform I/O ...
    invokeTrace(TraceDirection::TX, data.data(), data.size());
    return Status::OK;
  }

  Status read(std::uint8_t* buf, std::size_t len, std::size_t& bytesRead) {
    // ... perform I/O ...
    invokeTrace(TraceDirection::RX, buf, bytesRead);
    return Status::OK;
  }
};
```

### Attaching a Trace Callback

```cpp
void myTraceHandler(apex::protocols::TraceDirection dir, const std::uint8_t* data,
                    std::size_t len, void* userData) noexcept {
  char buf[256];
  apex::protocols::formatTraceMessage(dir, data, len, buf, sizeof(buf), "UART");
  // Output: "[UART] TX (4 bytes): DE AD BE EF"
  puts(buf);
}

MyAdapter adapter;
adapter.attachTrace(myTraceHandler);
adapter.setTraceEnabled(true);
// All subsequent I/O calls will invoke myTraceHandler
```

### Format Helpers (Standalone)

```cpp
const std::uint8_t DATA[] = {0xDE, 0xAD, 0xBE, 0xEF};

char hexBuf[64];
apex::protocols::formatBytesHex(DATA, 4, hexBuf, sizeof(hexBuf));
// hexBuf = "DE AD BE EF"

char msgBuf[128];
apex::protocols::formatTraceMessage(
    apex::protocols::TraceDirection::TX, DATA, 4, msgBuf, sizeof(msgBuf), "CAN");
// msgBuf = "[CAN] TX (4 bytes): DE AD BE EF"
```

---

## 7. Testing

### Test Organization

| Directory | Type              | Tests | Runs with `make test` |
| --------- | ----------------- | ----- | --------------------- |
| `utst/`   | Unit tests        | 30    | Yes                   |
| `ptst/`   | Performance tests | 7     | No (manual)           |

### Test Requirements

- All tests are platform-agnostic (no hardware dependencies)
- Tests verify exact string output for format functions
- Tests verify lifecycle semantics (attach, detach, enable, disable)
- Tests verify null-safety edge cases

---

## 8. See Also

- Protocol libraries that use `ByteTrace` as a mixin:
  - `system_core_protocols_serial_uart` -- UART adapter
  - `system_core_protocols_network_tcp` -- TCP socket adapter
  - `system_core_protocols_network_udp` -- UDP socket adapter
  - `system_core_protocols_network_unix` -- Unix socket adapter
  - `system_core_protocols_fieldbus_can` -- CAN bus adapter
