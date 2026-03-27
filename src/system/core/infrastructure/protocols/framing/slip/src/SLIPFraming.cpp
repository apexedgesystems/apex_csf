/**
 * @file SLIPFraming.cpp
 * @brief Implementation of SLIP framing encode and decode operations.
 */

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"

namespace apex {
namespace protocols {
namespace slip {

/* ----------------------------- API ----------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::OK:
    return "OK";
  case Status::NEED_MORE:
    return "NEED_MORE";
  case Status::OUTPUT_FULL:
    return "OUTPUT_FULL";
  case Status::ERROR_MISSING_DELIMITER:
    return "ERROR_MISSING_DELIMITER";
  case Status::ERROR_INCOMPLETE_ESCAPE:
    return "ERROR_INCOMPLETE_ESCAPE";
  case Status::ERROR_INVALID_ESCAPE:
    return "ERROR_INVALID_ESCAPE";
  case Status::ERROR_DECODE_FAILED:
    return "ERROR_DECODE_FAILED";
  case Status::ERROR_OVERSIZE:
    return "ERROR_OVERSIZE";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Encode payload using SLIP framing with hybrid escape counting.
 *
 * Uses size-adaptive strategy optimized for real-time systems:
 * - Small payloads (<256B): Branchless scan provides deterministic timing
 * - Large payloads (>=256B): SIMD-optimized memchr efficiently skips clean regions
 *
 * Branchless escape byte selection minimizes pipeline stalls for consistent
 * real-time performance regardless of escape density.
 */
IoResult encode(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity, bool leadingEnd,
                bool trailingEnd) noexcept {
  IoResult r{};

  // Count escape sequences needed
  size_t escapes = 0;
  const uint8_t* p = payload.size() ? payload.data() : nullptr;
  const uint8_t* const PEND = p ? (p + payload.size()) : nullptr;

  if (p && PEND > p) {
    constexpr size_t SMALL_PAYLOAD_THRESHOLD = 256;

    if (payload.size() < SMALL_PAYLOAD_THRESHOLD) {
      // Small payload path: branchless scan for deterministic timing
      for (const uint8_t* pos = p; pos < PEND; ++pos) {
        const uint8_t b = *pos;
        escapes += ((b == END) | (b == ESC));
      }
    } else {
      // Large payload path: vectorized search for bulk transfers
      const uint8_t* pos = p;
      while (pos < PEND) {
        const uint8_t* nextEnd =
            static_cast<const uint8_t*>(memchr(pos, END, static_cast<size_t>(PEND - pos)));
        const uint8_t* nextEsc =
            static_cast<const uint8_t*>(memchr(pos, ESC, static_cast<size_t>(PEND - pos)));

        if (nextEnd && (!nextEsc || nextEnd < nextEsc)) {
          ++escapes;
          pos = nextEnd + 1;
        } else if (nextEsc) {
          ++escapes;
          pos = nextEsc + 1;
        } else {
          break;
        }
      }
    }
  }

  const size_t NEEDED = (leadingEnd ? 1 : 0) + (trailingEnd ? 1 : 0) + payload.size() + escapes;

  if (outCapacity < NEEDED) {
    r.status = Status::OUTPUT_FULL;
    r.needed = NEEDED;
    return r;
  }

  size_t w = 0;
  if (leadingEnd) {
    out[w++] = END;
  }

  for (const uint8_t* it = p; it && it < PEND; ++it) {
    const uint8_t B = *it;

    // Fast path: no escape needed (most common case)
    if (COMPAT_LIKELY(B != END && B != ESC)) {
      out[w++] = B;
    } else {
      // Escape path: branchless byte selection for deterministic timing
      // ESC_END=0xDC, ESC_ESC=0xDD differ by 1, so ESC_END + (B == ESC) works
      const uint8_t escapedByte = ESC_END + (B == ESC);
      out[w++] = ESC;
      out[w++] = escapedByte;
    }
  }

  if (trailingEnd) {
    out[w++] = END;
  }

  r.status = Status::OK;
  r.bytesConsumed = payload.size();
  r.bytesProduced = w;
  return r;
}

/**
 * @brief Single-pass SLIP encoder for pre-sized buffers.
 *
 * Skips escape counting phase for applications where buffer size is known
 * sufficient (typically payload.size() * 2 + 2 for worst case).
 *
 * Note: Caller must ensure adequate buffer space. Insufficient capacity
 * will be detected but may indicate a programming error.
 */
IoResult encodePreSized(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity,
                        bool leadingEnd, bool trailingEnd) noexcept {
  IoResult r{};

  const uint8_t* p = payload.size() ? payload.data() : nullptr;
  const uint8_t* const PEND = p ? (p + payload.size()) : nullptr;

  size_t w = 0;
  if (leadingEnd) {
    out[w++] = END;
  }

  for (const uint8_t* it = p; it && it < PEND; ++it) {
    const uint8_t B = *it;

    if (COMPAT_LIKELY(B != END && B != ESC)) {
      out[w++] = B;
    } else {
      out[w++] = ESC;
      out[w++] = (B == END) ? ESC_END : ESC_ESC;
    }
  }

  if (trailingEnd) {
    out[w++] = END;
  }

  // Verify buffer was sufficient
  if (w > outCapacity) {
    r.status = Status::ERROR_DECODE_FAILED;
    r.needed = w;
    return r;
  }

  r.status = Status::OK;
  r.bytesConsumed = payload.size();
  r.bytesProduced = w;
  return r;
}

/**
 * @brief Decode SLIP-encoded data with stateful streaming support.
 *
 * Uses escape lookahead and bulk copy optimizations:
 * - Atomic ESC+next byte processing when both available eliminates state transitions
 * - SIMD-optimized memchr for fast delimiter scanning in clean data ranges
 * - Bulk memcpy for ranges without special characters
 *
 * These optimizations maintain deterministic behavior while significantly
 * improving throughput for typical real-time data streams.
 */
IoResult decodeChunk(DecodeState& st, const DecodeConfig& cfg, apex::compat::bytes_span in,
                     uint8_t* out, size_t outCapacity) noexcept {
  IoResult r{};
  size_t w = 0;

  const uint8_t* p = in.size() ? in.data() : nullptr;
  const uint8_t* const PEND = p ? (p + in.size()) : nullptr;

  for (const uint8_t* it = p; it && it < PEND; ++it) {
    const uint8_t B = *it;

    // Drain modes: resync to next delimiter
    if (st.mode == DecodeState::Mode::DRAIN_CORRUPT) {
      ++r.bytesConsumed;
      if (B == END) {
        st.reset();
      }
      continue;
    }
    if (st.mode == DecodeState::Mode::DRAIN_OVERSIZE) {
      ++r.bytesConsumed;
      if (B == END) {
        st.reset();
      }
      continue;
    }

    // ESCAPE_PENDING: resolve escape sequence
    if (st.mode == DecodeState::Mode::ESCAPE_PENDING) {
      if (B == ESC_ESC || B == ESC_END) {
        const uint8_t RESOLVED = (B == ESC_ESC) ? ESC : END;
        if (COMPAT_UNLIKELY(st.frameLen + 1 > cfg.maxFrameSize)) {
          st.mode = DecodeState::Mode::DRAIN_OVERSIZE;
          r.status = Status::ERROR_OVERSIZE;
          r.bytesProduced = w;
          ++r.bytesConsumed;
          return r;
        }
        if (COMPAT_UNLIKELY(w >= outCapacity)) {
          r.status = Status::OUTPUT_FULL;
          r.bytesProduced = w;
          return r;
        }
        out[w++] = RESOLVED;
        ++st.frameLen;
        st.hadData = true;
        st.mode = DecodeState::Mode::IN_FRAME;
        ++r.bytesConsumed;
        continue;
      }
      // Invalid escape sequence
      r.status = Status::ERROR_INVALID_ESCAPE;
      r.bytesProduced = w;
      ++r.bytesConsumed;
      if (cfg.dropUntilEnd) {
        st.mode = DecodeState::Mode::DRAIN_CORRUPT;
      } else {
        st.reset();
      }
      return r;
    }

    // IDLE: wait for frame start delimiter
    if (st.mode == DecodeState::Mode::IDLE) {
      if (B == END) {
        st.mode = DecodeState::Mode::IN_FRAME;
        st.frameLen = 0;
        st.hadData = false;
        ++r.bytesConsumed;
        continue;
      } else {
        ++r.bytesConsumed;
        if (!cfg.dropUntilEnd) {
          r.status = Status::ERROR_MISSING_DELIMITER;
          r.bytesProduced = w;
          return r;
        }
        continue;
      }
    }

    // IN_FRAME: process frame data
    if (st.mode == DecodeState::Mode::IN_FRAME) {
      if (B == END) {
        // Frame termination
        const bool EMIT = (st.hadData || cfg.allowEmptyFrame);
        st.reset();
        ++r.bytesConsumed;
        r.bytesProduced = w;
        r.frameCompleted = EMIT;
        r.status = Status::OK;
        return r;
      }

      // Escape lookahead: process ESC+next atomically when available
      if (B == ESC) {
        if (it + 1 < PEND) {
          const uint8_t NEXT = *(it + 1);
          if (NEXT == ESC_ESC || NEXT == ESC_END) {
            const uint8_t RESOLVED = (NEXT == ESC_ESC) ? ESC : END;

            if (COMPAT_UNLIKELY(st.frameLen + 1 > cfg.maxFrameSize)) {
              st.mode = DecodeState::Mode::DRAIN_OVERSIZE;
              r.status = Status::ERROR_OVERSIZE;
              r.bytesProduced = w;
              r.bytesConsumed += 2;
              return r;
            }
            if (COMPAT_UNLIKELY(w >= outCapacity)) {
              r.status = Status::OUTPUT_FULL;
              r.bytesProduced = w;
              return r;
            }

            out[w++] = RESOLVED;
            ++st.frameLen;
            st.hadData = true;
            ++it;
            r.bytesConsumed += 2;
            continue;
          } else {
            // Invalid escape sequence
            r.status = Status::ERROR_INVALID_ESCAPE;
            r.bytesProduced = w;
            r.bytesConsumed += 2;
            if (cfg.dropUntilEnd) {
              st.mode = DecodeState::Mode::DRAIN_CORRUPT;
            } else {
              st.reset();
            }
            return r;
          }
        } else {
          // ESC at chunk boundary: need more data
          st.mode = DecodeState::Mode::ESCAPE_PENDING;
          ++r.bytesConsumed;
          continue;
        }
      }

      // Bulk copy: find next special character and copy clean range
      const size_t remaining = static_cast<size_t>(PEND - it);
      const uint8_t* nextEnd = static_cast<const uint8_t*>(memchr(it, END, remaining));
      const uint8_t* nextEsc = static_cast<const uint8_t*>(memchr(it, ESC, remaining));

      const uint8_t* nextSpecial = PEND;
      if (nextEnd && nextEnd < nextSpecial) {
        nextSpecial = nextEnd;
      }
      if (nextEsc && nextEsc < nextSpecial) {
        nextSpecial = nextEsc;
      }

      const size_t cleanLen = static_cast<size_t>(nextSpecial - it);

      if (cleanLen > 0) {
        // Check frame size limit
        if (COMPAT_UNLIKELY(st.frameLen + cleanLen > cfg.maxFrameSize)) {
          st.mode = DecodeState::Mode::DRAIN_OVERSIZE;
          r.status = Status::ERROR_OVERSIZE;
          r.bytesProduced = w;
          r.bytesConsumed += cleanLen;
          return r;
        }

        // Check output capacity
        if (COMPAT_UNLIKELY(w + cleanLen > outCapacity)) {
          r.status = Status::OUTPUT_FULL;
          r.bytesProduced = w;
          return r;
        }

        // Bulk copy clean range
        memcpy(out + w, it, cleanLen);
        w += cleanLen;
        st.frameLen += cleanLen;
        st.hadData = true;
        r.bytesConsumed += cleanLen;

        it = nextSpecial - 1;
        continue;
      }

      continue;
    }
  }

  // End of chunk
  if (st.mode == DecodeState::Mode::ESCAPE_PENDING) {
    r.status = Status::NEED_MORE;
  } else {
    r.status = Status::OK;
  }
  r.bytesProduced = w;
  return r;
}

} // namespace slip
} // namespace protocols
} // namespace apex