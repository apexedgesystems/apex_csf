/**
 * @file COBSFraming.hpp
 * @brief Real-time COBS framing with zero-allocation streaming decode and stateless encode.
 *
 * Design goals:
 *  - Zero heap allocation in hot paths (caller-owned buffers)
 *  - Stateful streaming decode with NEED_MORE and OUTPUT_FULL flow control
 *  - Clear policy configuration for resync and maximum frame size
 *  - Optimized for real-time performance with SIMD-friendly bulk operations
 */

#ifndef APEX_PROTOCOLS_COBS_FRAMING_HPP
#define APEX_PROTOCOLS_COBS_FRAMING_HPP

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace apex {
namespace protocols {
namespace cobs {

/* ----------------------------- Constants ----------------------------- */

constexpr uint8_t DELIMITER = 0x00;             ///< Frame delimiter (zero byte).
constexpr size_t DEFAULT_MAX_FRAME_SIZE = 4096; ///< Default maximum frame size in bytes.

/* ----------------------------- Status ----------------------------- */

/**
 * @enum Status
 * @brief Strongly-typed status codes for COBS operations.
 */
enum class Status : uint8_t {
  OK = 0,                  ///< Operation completed successfully.
  NEED_MORE,               ///< Input chunk ended mid-run; provide more bytes to continue.
  OUTPUT_FULL,             ///< Output buffer full; resume with same input position.
  ERROR_MISSING_DELIMITER, ///< Strict mode: frame ended without trailing delimiter.
  ERROR_DECODE,            ///< Decode error (invalid code byte).
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
  bool dropUntilDelimiter = true;       ///< Resync by draining until next delimiter on errors.
  bool requireTrailingDelimiter = true; ///< Frame completes only on delimiter (standard COBS).
};

/* ----------------------------- DecodeState ----------------------------- */

/**
 * @struct DecodeState
 * @brief Minimal finite state machine for streaming COBS decode.
 *
 * COBS frames are terminated by zero delimiters. Each code byte (1-255) indicates
 * a run length: copy (code-1) bytes, then insert zero if code < 255 and not at frame end.
 */
struct DecodeState {
  enum class Mode : uint8_t {
    IDLE,          ///< No active frame.
    IN_FRAME,      ///< Currently decoding a frame.
    DRAIN_CORRUPT, ///< Draining corrupt data until next delimiter.
    DRAIN_OVERSIZE ///< Draining oversized frame until next delimiter.
  };

  Mode mode = Mode::IDLE;   ///< Current decoder mode.
  uint8_t runRemaining = 0; ///< Bytes remaining in current run.
  bool zeroPending = false; ///< Zero byte pending insertion before next code.
  size_t frameLen = 0;      ///< Decoded bytes in current frame.

  /**
   * @brief Reset decoder to idle state.
   */
  void reset() noexcept {
    mode = Mode::IDLE;
    runRemaining = 0;
    zeroPending = false;
    frameLen = 0;
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
 * @brief Stateless COBS encoder with hybrid optimization for real-time systems.
 *
 * Encodes payload into COBS format with optional trailing delimiter. Returns OUTPUT_FULL
 * if output buffer is insufficient, providing exact required size in result.needed.
 *
 * Optimization strategy:
 * - Small payloads (<256B): Simple scan for deterministic real-time behavior
 * - Large payloads (>=256B): SIMD-optimized memchr and memcpy for throughput
 * - Clean data: Near-memcpy performance for payloads with few zeros
 *
 * Performance characteristics:
 * - Small frames: Deterministic single-pass encoding
 * - Large frames: 50-90% speedup over byte-by-byte for clean data
 * - Branch hints optimize common fast paths
 *
 * @param payload           Payload to encode (read-only).
 * @param out               Destination buffer (raw pointer).
 * @param outCapacity       Size of destination buffer in bytes.
 * @param trailingDelimiter Append trailing delimiter (default true).
 * @return IoResult with status, bytes consumed/produced, and needed size if OUTPUT_FULL.
 * @note RT-safe: No allocation, bounded execution.
 */
IoResult encode(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity,
                bool trailingDelimiter = true) noexcept COMPAT_HOT;

/**
 * @brief Streaming COBS decoder with bulk operation optimization.
 *
 * Consumes input chunk and writes decoded payload bytes into output buffer. Returns
 * when input exhausted, output full, or frame completed. On delimiter, returns OK
 * with frameCompleted=true and resets state for next frame.
 *
 * Optimization strategy:
 * - Bulk memcpy for runs of 8+ bytes (reduces FSM overhead)
 * - Branch hints for common error-free paths
 * - Efficient state machine with minimal branching
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

} // namespace cobs
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_COBS_FRAMING_HPP