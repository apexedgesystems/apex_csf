#ifndef APEX_PROTOCOLS_COMMON_BYTE_TRACE_HPP
#define APEX_PROTOCOLS_COMMON_BYTE_TRACE_HPP
/**
 * @file ByteTrace.hpp
 * @brief Optional byte-level tracing for protocol I/O debugging.
 *
 * Provides a lightweight callback-based tracing mechanism for capturing
 * raw bytes on read/write operations. Designed for:
 *  - Protocol debugging (see exact bytes on the wire)
 *  - Integration testing (capture traffic for replay/analysis)
 *  - Production diagnostics (enable temporarily when issues arise)
 *
 * Design principles:
 *  - Zero overhead when disabled (null pointer check only)
 *  - Callback-based: user controls destination (ring buffer, log, file)
 *  - RT-safe if user provides RT-safe callback
 *  - No dependency on logs library (optional integration)
 *
 * RT-Safety:
 *  - attachTrace()/detachTrace(): NOT RT-safe (setup phase only)
 *  - setTraceEnabled()/traceEnabled(): RT-safe (atomic bool)
 *  - Callback invocation: RT-safe if callback is RT-safe
 *
 * Usage:
 *  Protocol adapters inherit from this class as a mixin to gain tracing.
 *  The trace callback is invoked for each successful I/O operation
 *  when tracing is both attached and enabled.
 *
 * @code
 * class MyAdapter : public SomeDevice, public apex::protocols::ByteTrace {
 * public:
 *   Status write(span<const uint8_t> data) {
 *     // ... perform I/O ...
 *     invokeTrace(TraceDirection::TX, data.data(), data.size());
 *     return Status::SUCCESS;
 *   }
 * };
 *
 * MyAdapter adapter;
 * adapter.attachTrace(myCallback, myContext);
 * adapter.setTraceEnabled(true);
 * adapter.write(data);  // Traces TX
 * @endcode
 */

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {

/* ----------------------------- TraceDirection ----------------------------- */

/**
 * @brief Trace direction indicator.
 */
enum class TraceDirection : std::uint8_t {
  RX = 0, ///< Data received (read/recv)
  TX = 1  ///< Data transmitted (write/send)
};

/**
 * @brief Convert TraceDirection to string.
 * @param dir Direction value.
 * @return "RX" or "TX".
 * @note RT-safe: Returns static string.
 */
inline const char* toString(TraceDirection dir) noexcept {
  return dir == TraceDirection::RX ? "RX" : "TX";
}

/* ----------------------------- TraceCallback ----------------------------- */

/**
 * @brief Callback signature for byte tracing.
 *
 * @param dir Direction (RX for read, TX for write).
 * @param data Pointer to byte data (valid only during callback).
 * @param len Number of bytes.
 * @param userData User-provided context pointer.
 *
 * @note Callback must be noexcept and should complete quickly.
 * @note RT-safe if implementation is RT-safe (no allocation, bounded time).
 */
using TraceCallback = void (*)(TraceDirection dir, const std::uint8_t* data, std::size_t len,
                               void* userData) noexcept;

/* ----------------------------- ByteTrace ----------------------------- */

/**
 * @class ByteTrace
 * @brief Mixin class providing byte-level tracing capability for protocol adapters.
 *
 * Thread Safety:
 *  - attachTrace()/detachTrace() are NOT thread-safe with I/O operations.
 *  - setTraceEnabled() is thread-safe (atomic).
 *  - invokeTrace() is thread-safe with setTraceEnabled().
 */
class ByteTrace {
public:
  ByteTrace() noexcept = default;
  virtual ~ByteTrace() = default;

  ByteTrace(const ByteTrace&) = delete;
  ByteTrace& operator=(const ByteTrace&) = delete;

  // Move operations disabled due to atomic member
  ByteTrace(ByteTrace&&) = delete;
  ByteTrace& operator=(ByteTrace&&) = delete;

  /**
   * @brief Attach a trace callback.
   * @param callback Function to call on each traced operation.
   * @param userData User context passed to callback (may be nullptr).
   * @note NOT RT-safe: Call during setup phase only.
   * @note Replaces any previously attached callback.
   */
  void attachTrace(TraceCallback callback, void* userData = nullptr) noexcept {
    traceCallback_ = callback;
    traceUserData_ = userData;
  }

  /**
   * @brief Detach the trace callback.
   * @note NOT RT-safe: Call during teardown phase only.
   */
  void detachTrace() noexcept {
    traceCallback_ = nullptr;
    traceUserData_ = nullptr;
    traceEnabled_.store(false, std::memory_order_relaxed);
  }

  /**
   * @brief Enable or disable tracing.
   * @param enabled true to enable, false to disable.
   * @note RT-safe: Atomic store.
   * @note Tracing only occurs if both enabled AND callback attached.
   */
  void setTraceEnabled(bool enabled) noexcept {
    traceEnabled_.store(enabled, std::memory_order_relaxed);
  }

  /**
   * @brief Check if tracing is enabled.
   * @return true if tracing is enabled.
   * @note RT-safe: Atomic load.
   */
  [[nodiscard]] bool traceEnabled() const noexcept {
    return traceEnabled_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Check if a trace callback is attached.
   * @return true if callback is attached.
   * @note RT-safe: Pointer check.
   */
  [[nodiscard]] bool traceAttached() const noexcept { return traceCallback_ != nullptr; }

protected:
  /**
   * @brief Invoke the trace callback if tracing is active.
   * @param dir Direction (RX or TX).
   * @param data Pointer to byte data.
   * @param len Number of bytes.
   * @note RT-safe if callback is RT-safe.
   * @note Call from read()/write() implementations after successful I/O.
   */
  void invokeTrace(TraceDirection dir, const std::uint8_t* data, std::size_t len) noexcept {
    // Fast path: skip if no callback or not enabled
    if (traceCallback_ == nullptr || !traceEnabled_.load(std::memory_order_relaxed)) {
      return;
    }
    traceCallback_(dir, data, len, traceUserData_);
  }

private:
  TraceCallback traceCallback_{nullptr};
  void* traceUserData_{nullptr};
  std::atomic<bool> traceEnabled_{false};
};

/* ----------------------------- Format Helpers ----------------------------- */

/**
 * @brief Format bytes as hex string into a fixed buffer.
 * @param data Source bytes.
 * @param len Number of bytes.
 * @param out Output buffer.
 * @param outSize Size of output buffer.
 * @param maxBytes Maximum bytes to format (rest shown as "...").
 * @return Number of characters written (excluding null terminator).
 * @note RT-safe: No allocation, bounded time.
 *
 * Output format: "DE AD BE EF" (space-separated hex pairs)
 * If len > maxBytes, appends " ..." at the end.
 */
inline std::size_t formatBytesHex(const std::uint8_t* data, std::size_t len, char* out,
                                  std::size_t outSize, std::size_t maxBytes = 32) noexcept {
  if (out == nullptr || outSize == 0) {
    return 0;
  }

  static constexpr char HEX_CHARS[] = "0123456789ABCDEF";
  const std::size_t BYTES_TO_FORMAT = (len < maxBytes) ? len : maxBytes;

  std::size_t pos = 0;
  for (std::size_t i = 0; i < BYTES_TO_FORMAT && pos + 3 < outSize; ++i) {
    if (i > 0) {
      out[pos++] = ' ';
    }
    out[pos++] = HEX_CHARS[(data[i] >> 4) & 0x0F];
    out[pos++] = HEX_CHARS[data[i] & 0x0F];
  }

  // Add ellipsis if truncated
  if (len > maxBytes && pos + 4 < outSize) {
    out[pos++] = ' ';
    out[pos++] = '.';
    out[pos++] = '.';
    out[pos++] = '.';
  }

  out[pos] = '\0';
  return pos;
}

/**
 * @brief Format a trace message into a buffer.
 * @param dir Trace direction.
 * @param data Byte data.
 * @param len Number of bytes.
 * @param out Output buffer.
 * @param outSize Size of output buffer.
 * @param prefix Protocol prefix (e.g., "TCP", "UDP", "CAN").
 * @param maxBytes Maximum bytes to format in hex output.
 * @return Number of characters written (excluding null terminator).
 * @note RT-safe: No allocation, bounded time.
 *
 * Output format: "[TCP] TX (4 bytes): DE AD BE EF"
 */
inline std::size_t formatTraceMessage(TraceDirection dir, const std::uint8_t* data, std::size_t len,
                                      char* out, std::size_t outSize, const char* prefix,
                                      std::size_t maxBytes = 32) noexcept {
  if (out == nullptr || outSize == 0) {
    return 0;
  }

  char* p = out;
  const char* end = out + outSize - 1;

  // "[PREFIX] "
  if (p < end) {
    *p++ = '[';
  }
  while (prefix != nullptr && *prefix != '\0' && p < end) {
    *p++ = *prefix++;
  }
  if (p < end) {
    *p++ = ']';
  }
  if (p < end) {
    *p++ = ' ';
  }

  // Direction
  const char* dirStr = toString(dir);
  while (*dirStr != '\0' && p < end) {
    *p++ = *dirStr++;
  }

  // " ("
  if (p < end) {
    *p++ = ' ';
  }
  if (p < end) {
    *p++ = '(';
  }

  // Length as decimal (manual to avoid snprintf overhead)
  char lenBuf[16];
  int lenPos = 0;
  std::size_t n = len;
  if (n == 0) {
    lenBuf[lenPos++] = '0';
  } else {
    char tmp[16];
    int tmpPos = 0;
    while (n > 0) {
      tmp[tmpPos++] = static_cast<char>('0' + (n % 10));
      n /= 10;
    }
    while (tmpPos > 0) {
      lenBuf[lenPos++] = tmp[--tmpPos];
    }
  }
  for (int i = 0; i < lenPos && p < end; ++i) {
    *p++ = lenBuf[i];
  }

  // " bytes): "
  const char* SUFFIX = " bytes): ";
  while (*SUFFIX != '\0' && p < end) {
    *p++ = *SUFFIX++;
  }

  // Hex content (use remaining buffer space)
  std::size_t hexSpace = static_cast<std::size_t>(end - p);
  if (hexSpace > 0) {
    std::size_t hexLen = formatBytesHex(data, len, p, hexSpace, maxBytes);
    p += hexLen;
  }

  *p = '\0';
  return static_cast<std::size_t>(p - out);
}

} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_COMMON_BYTE_TRACE_HPP
