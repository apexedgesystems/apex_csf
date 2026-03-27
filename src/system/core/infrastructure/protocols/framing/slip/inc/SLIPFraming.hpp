/**
 * @file SLIPFraming.hpp
 * @brief Real-time SLIP framing with zero-allocation streaming decode and stateless encode.
 *
 * Design goals:
 *  - Zero heap allocation in hot paths (caller-owned buffers)
 *  - Stateful streaming decode with NEED_MORE and OUTPUT_FULL flow control
 *  - Clear policy configuration for resync, empty frames, and maximum frame size
 *  - Optimized for real-time performance with SIMD-friendly bulk operations
 */

#ifndef APEX_PROTOCOLS_SLIP_FRAMING_HPP
#define APEX_PROTOCOLS_SLIP_FRAMING_HPP

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace apex {
namespace protocols {
namespace slip {

/* ----------------------------- Constants ----------------------------- */

constexpr uint8_t END = 0xC0;     ///< Frame delimiter.
constexpr uint8_t ESC = 0xDB;     ///< Escape character.
constexpr uint8_t ESC_END = 0xDC; ///< Escaped END byte.
constexpr uint8_t ESC_ESC = 0xDD; ///< Escaped ESC byte.

constexpr size_t DEFAULT_MAX_FRAME_SIZE = 4096; ///< Default maximum frame size in bytes.

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Strongly-typed status codes for SLIP operations.
 */
enum class Status : uint8_t {
  OK = 0,                  ///< Operation completed successfully.
  NEED_MORE,               ///< Input chunk ended with incomplete escape; provide more bytes.
  OUTPUT_FULL,             ///< Output buffer full; resume with same input position.
  ERROR_MISSING_DELIMITER, ///< Strict mode: data arrived before starting delimiter.
  ERROR_INCOMPLETE_ESCAPE, ///< Final chunk ended with ESC (stream closed).
  ERROR_INVALID_ESCAPE,    ///< ESC followed by invalid byte.
  ERROR_DECODE_FAILED,     ///< Generic decode failure.
  ERROR_OVERSIZE           ///< Decoded payload exceeded configured maximum frame size.
};

/**
 * @brief Convert status code to human-readable string.
 * @param s Status code to convert.
 * @return String literal describing the status.
 * @note RT-safe: No allocation, bounded execution.
 */
const char* toString(Status s) noexcept;

/* ----------------------------- DecodeConfig ----------------------------- */

/**
 * @struct DecodeConfig
 * @brief Configuration policy for streaming decoder behavior and robustness.
 */
struct DecodeConfig {
  size_t maxFrameSize = DEFAULT_MAX_FRAME_SIZE; ///< Maximum decoded frame size (DoS protection).
  bool allowEmptyFrame = false;                 ///< Emit empty frames on consecutive delimiters.
  bool dropUntilEnd = true;       ///< Resync by draining until next delimiter on errors.
  bool requireTrailingEnd = true; ///< Frame completes only on delimiter (standard SLIP).
};

/* ----------------------------- DecodeState ----------------------------- */

/**
 * @struct DecodeState
 * @brief Minimal finite state machine for streaming SLIP decode.
 */
struct DecodeState {
  enum class Mode : uint8_t {
    IDLE,           ///< No active frame.
    IN_FRAME,       ///< Currently decoding a frame.
    ESCAPE_PENDING, ///< ESC byte seen; awaiting next byte.
    DRAIN_CORRUPT,  ///< Draining corrupt data until next delimiter.
    DRAIN_OVERSIZE  ///< Draining oversized frame until next delimiter.
  };

  Mode mode = Mode::IDLE; ///< Current decoder mode.
  size_t frameLen = 0;    ///< Decoded bytes in current frame.
  bool hadData = false;   ///< Whether any payload byte seen in current frame.

  /**
   * @brief Reset decoder to idle state.
   */
  void reset() noexcept {
    mode = Mode::IDLE;
    frameLen = 0;
    hadData = false;
  }
};

/* ----------------------------- IoResult ----------------------------- */

/**
 * @struct IoResult
 * @brief Result metrics for encode and decode operations.
 */
struct IoResult {
  Status status = Status::OK;  ///< Operation status code.
  size_t bytesConsumed = 0;    ///< Input bytes consumed.
  size_t bytesProduced = 0;    ///< Output bytes written.
  bool frameCompleted = false; ///< Frame decode completed (decoder only).
  size_t needed = 0;           ///< Bytes required for encode (OUTPUT_FULL only).
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Stateless SLIP encoder with hybrid optimization for real-time systems.
 *
 * Encodes payload into SLIP format with configurable leading and trailing delimiters.
 * Returns OUTPUT_FULL if output buffer is insufficient, providing exact required size
 * in result.needed.
 *
 * Optimization strategy:
 * - Small payloads (<256B): Branchless scan for deterministic real-time behavior
 * - Large payloads (>=256B): SIMD-optimized memchr for escape detection
 * - Branchless escape byte selection minimizes pipeline stalls
 *
 * Performance characteristics:
 * - Small frames: Deterministic single-pass encoding
 * - Large frames: 50-90% speedup over byte-by-byte for clean data
 * - High escape rate: Branchless resolution maintains consistent performance
 *
 * @param payload      Payload to encode (read-only).
 * @param out          Destination buffer (raw pointer).
 * @param outCapacity  Size of destination buffer in bytes.
 * @param leadingEnd   Prepend leading delimiter (default true).
 * @param trailingEnd  Append trailing delimiter (default true).
 * @return IoResult with status, bytes consumed/produced, and needed size if OUTPUT_FULL.
 * @note RT-safe: No allocation, bounded execution.
 */
IoResult encode(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity,
                bool leadingEnd = true, bool trailingEnd = true) noexcept COMPAT_HOT;

/**
 * @brief Single-pass SLIP encoder for pre-sized buffers.
 *
 * Fast-path encoder that skips escape counting and writes directly. Use when output
 * buffer size is known to be sufficient (typically payload.size() * 2 + 2).
 *
 * Warning: No bounds checking performed. Caller must ensure sufficient buffer space.
 * Recommended buffer size: payload.size() * 2 + 2 (absolute worst case).
 *
 * @param payload      Payload to encode (read-only).
 * @param out          Destination buffer (raw pointer).
 * @param outCapacity  Size of destination buffer in bytes (must be sufficient).
 * @param leadingEnd   Prepend leading delimiter (default true).
 * @param trailingEnd  Append trailing delimiter (default true).
 * @return IoResult with status OK (assuming sufficient buffer).
 * @note RT-safe: No allocation, bounded execution.
 */
IoResult encodePreSized(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity,
                        bool leadingEnd = true, bool trailingEnd = true) noexcept COMPAT_HOT;

/**
 * @brief Streaming SLIP decoder with bulk operation optimization.
 *
 * Consumes input chunk and writes decoded payload bytes into output buffer. Returns
 * when input exhausted, output full, or frame completed. On delimiter, returns OK
 * with frameCompleted=true and resets state for next frame (subject to empty frame policy).
 *
 * Optimization strategy:
 * - SIMD-optimized memchr for fast delimiter scanning
 * - Bulk memcpy for clean data ranges (no escapes)
 * - Branch hints for common error-free paths
 *
 * Performance characteristics:
 * - Expected 50-90% decode speedup over naive byte-by-byte
 * - Consistent performance with clean data (ideal for real-time)
 * - Graceful degradation with fragmented or error-prone streams
 *
 * @param st           Decoder state (modified in place).
 * @param cfg          Decoder configuration policy.
 * @param in           Input chunk (read-only).
 * @param out          Destination buffer (raw pointer).
 * @param outCapacity  Size of destination buffer in bytes.
 * @return IoResult with status, bytes consumed/produced, and frame completion flag.
 * @note RT-safe: No allocation, bounded execution.
 */
IoResult decodeChunk(DecodeState& st, const DecodeConfig& cfg, apex::compat::bytes_span in,
                     uint8_t* out, size_t outCapacity) noexcept COMPAT_HOT;

} // namespace slip
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SLIP_FRAMING_HPP