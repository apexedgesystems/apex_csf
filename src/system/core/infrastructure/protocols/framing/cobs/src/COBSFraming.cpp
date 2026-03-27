/**
 * @file COBSFraming.cpp
 * @brief Implementation of COBS framing encode and decode operations.
 */

#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"

namespace apex {
namespace protocols {
namespace cobs {

/* ----------------------------- File Helpers ----------------------------- */

/**
 * @brief Calculate exact encoded size for COBS framing.
 *
 * COBS encoding replaces each zero byte with a code byte (net 0 overhead) and adds
 * one code byte per 254-byte block boundary (net +1 each). The base encoded size is
 * n + 1 (initial code byte) + blockSplits + optional delimiter.
 *
 * Uses hybrid counting strategy optimized for real-time systems:
 * - Small payloads (<256B): Simple scan provides deterministic timing
 * - Large payloads (>=256B): SIMD-optimized memchr skips clean regions
 */
static inline size_t cobsEncodedSizeExact(const uint8_t* p, size_t n, bool withDelimiter) {
  if (n == 0) {
    return 1u + (withDelimiter ? 1u : 0u);
  }

  // Count 254-byte block splits (runs of non-zero bytes reaching the COBS block limit).
  // Zeros reset the run but add no overhead (they replace a data byte with a code byte).
  size_t blockSplits = 0;
  uint8_t runLen = 0;

  if (n < 256) {
    // Small payload path: deterministic single-pass scan
    for (size_t i = 0; i < n; ++i) {
      if (p[i] != 0) {
        ++runLen;
        if (runLen == 254) {
          ++blockSplits;
          runLen = 0;
        }
      } else {
        runLen = 0;
      }
    }
  } else {
    // Large payload path: vectorized search for bulk transfers
    const uint8_t* pos = p;
    const uint8_t* const end = p + n;

    while (pos < end) {
      const uint8_t* nextZero =
          static_cast<const uint8_t*>(memchr(pos, 0, static_cast<size_t>(end - pos)));

      const uint8_t* segEnd = nextZero ? nextZero : end;
      size_t segLen = static_cast<size_t>(segEnd - pos);
      blockSplits += segLen / 254;

      if (nextZero == nullptr) {
        break;
      }
      pos = nextZero + 1;
    }
  }

  // Total: n data/code bytes + 1 initial code byte + block split codes + delimiter
  return n + 1 + blockSplits + (withDelimiter ? 1u : 0u);
}

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
  case Status::ERROR_DECODE:
    return "ERROR_DECODE";
  case Status::ERROR_OVERSIZE:
    return "ERROR_OVERSIZE";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Encode payload using COBS framing per IEEE/ACM specification (Cheshire & Baker).
 *
 * Hybrid encoding strategy adapts to payload size for optimal real-time performance:
 * - Small payloads (<256B): Byte-by-byte scan ensures deterministic timing for RT systems
 * - Large payloads (>=256B): Bulk operations with memchr and memcpy for throughput
 *
 * COBS overhead is minimal: maximum 1 byte per 254 bytes (approximately 0.4%).
 * Blocks are closed only when full (code reaches 0xFF) to minimize overhead.
 */
IoResult encode(apex::compat::bytes_span payload, uint8_t* out, size_t outCapacity,
                bool trailingDelimiter) noexcept {
  IoResult r{};

  const uint8_t* p = payload.size() ? payload.data() : nullptr;
  const size_t NEEDED = cobsEncodedSizeExact(p, payload.size(), trailingDelimiter);
  if (outCapacity < NEEDED) {
    r.status = Status::OUTPUT_FULL;
    r.needed = NEEDED;
    return r;
  }

  size_t w = 0;

  size_t codeIndex = w;
  out[w++] = 0;
  uint8_t code = 1;

  if (p == nullptr) {
    out[codeIndex] = 0x01;
  } else {
    const uint8_t* const PEND = p + payload.size();

    if (payload.size() < 256) {
      // Small payload path: deterministic byte-by-byte encoding
      for (const uint8_t* it = p; it < PEND; ++it) {
        const uint8_t B = *it;
        if (COMPAT_LIKELY(B != 0)) {
          out[w++] = B;
          if (COMPAT_UNLIKELY(++code == 0xFF)) {
            out[codeIndex] = code;
            codeIndex = w;
            out[w++] = 0;
            code = 1;
          }
        } else {
          out[codeIndex] = code;
          codeIndex = w;
          out[w++] = 0;
          code = 1;
        }
      }
    } else {
      // Large payload path: bulk operations for efficiency
      const uint8_t* pos = p;

      while (pos < PEND) {
        const uint8_t* nextZero =
            static_cast<const uint8_t*>(memchr(pos, 0, static_cast<size_t>(PEND - pos)));

        if (nextZero == nullptr) {
          nextZero = PEND;
        }

        // Process non-zero runs in 254-byte COBS blocks
        size_t runSize = static_cast<size_t>(nextZero - pos);
        while (runSize > 0) {
          const uint8_t spaceInBlock = static_cast<uint8_t>(0xFF - code);
          uint8_t chunk = (runSize < spaceInBlock) ? static_cast<uint8_t>(runSize) : spaceInBlock;

          memcpy(out + w, pos, chunk);
          w += chunk;
          pos += chunk;
          code += chunk;
          runSize -= chunk;

          // Close block only when full to minimize overhead
          if (code == 0xFF) {
            out[codeIndex] = code;
            codeIndex = w;
            out[w++] = 0;
            code = 1;
          }
        }

        // Handle zero delimiter if present
        if (pos < PEND && *pos == 0) {
          out[codeIndex] = code;
          codeIndex = w;
          out[w++] = 0;
          code = 1;
          ++pos;
        }
      }
    }

    out[codeIndex] = code;
  }

  if (trailingDelimiter) {
    out[w++] = DELIMITER;
  }

  r.status = Status::OK;
  r.bytesProduced = w;
  return r;
}

/**
 * @brief Decode COBS-encoded data with stateful streaming support.
 *
 * Uses bulk copy optimization for runs of 8 or more bytes, reducing finite
 * state machine overhead while maintaining deterministic behavior for smaller runs.
 * The 8-byte threshold is empirically optimal for cache-line efficiency.
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
      if (B == DELIMITER) {
        st.reset();
      }
      continue;
    }
    if (st.mode == DecodeState::Mode::DRAIN_OVERSIZE) {
      ++r.bytesConsumed;
      if (B == DELIMITER) {
        st.reset();
      }
      continue;
    }

    // IDLE: ignore leading delimiters; start frame on first non-delimiter
    if (st.mode == DecodeState::Mode::IDLE) {
      if (B == DELIMITER) {
        ++r.bytesConsumed;
        continue;
      }
      if (COMPAT_UNLIKELY(st.zeroPending)) {
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
        out[w++] = 0;
        ++st.frameLen;
        st.zeroPending = false;
      }
      // Start new run: B is the code byte (must be 1-255)
      if (COMPAT_UNLIKELY(B == 0)) {
        r.status = Status::ERROR_DECODE;
        r.bytesProduced = w;
        if (cfg.dropUntilDelimiter)
          st.mode = DecodeState::Mode::DRAIN_CORRUPT;
        ++r.bytesConsumed;
        return r;
      }
      st.mode = DecodeState::Mode::IN_FRAME;
      st.runRemaining = static_cast<uint8_t>(B - 1);
      st.zeroPending = (B < 0xFF);
      ++r.bytesConsumed;
      continue;
    }

    // IN_FRAME: process run data
    if (st.mode == DecodeState::Mode::IN_FRAME) {
      if (st.runRemaining > 0) {
        // Bulk copy for runs of 8 or more bytes (cache-line efficient)
        constexpr size_t BULK_THRESHOLD = 8;

        if (st.runRemaining >= BULK_THRESHOLD) {
          const size_t inputAvail = static_cast<size_t>(PEND - it);
          const size_t canCopy = (inputAvail < st.runRemaining) ? inputAvail : st.runRemaining;

          // Check limits before copying (ERROR_OVERSIZE before OUTPUT_FULL per specification)
          if (COMPAT_UNLIKELY(st.frameLen + canCopy > cfg.maxFrameSize)) {
            st.mode = DecodeState::Mode::DRAIN_OVERSIZE;
            r.status = Status::ERROR_OVERSIZE;
            r.bytesProduced = w;
            ++r.bytesConsumed;
            return r;
          }
          if (COMPAT_UNLIKELY(w + canCopy > outCapacity)) {
            r.status = Status::OUTPUT_FULL;
            r.bytesProduced = w;
            return r;
          }

          memcpy(out + w, it, canCopy);
          w += canCopy;
          st.frameLen += canCopy;
          st.runRemaining -= static_cast<uint8_t>(canCopy);
          r.bytesConsumed += canCopy;

          it += (canCopy - 1);
          continue;
        }

        // Byte-by-byte for small runs (deterministic for RT)
        if (COMPAT_UNLIKELY(w >= outCapacity)) {
          r.status = Status::OUTPUT_FULL;
          r.bytesProduced = w;
          return r;
        }
        if (COMPAT_UNLIKELY(st.frameLen + 1 > cfg.maxFrameSize)) {
          st.mode = DecodeState::Mode::DRAIN_OVERSIZE;
          r.status = Status::ERROR_OVERSIZE;
          r.bytesProduced = w;
          ++r.bytesConsumed;
          return r;
        }
        out[w++] = B;
        ++st.frameLen;
        --st.runRemaining;
        ++r.bytesConsumed;
        continue;
      }

      // Run completed: check for frame termination
      if (B == DELIMITER) {
        st.reset();
        ++r.bytesConsumed;
        r.bytesProduced = w;
        r.frameCompleted = true;
        r.status = Status::OK;
        return r;
      }

      // Emit pending zero if previous code was less than 0xFF
      if (st.zeroPending) {
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
        out[w++] = 0;
        ++st.frameLen;
        st.zeroPending = false;
      }

      // Process next code byte (must be 1-255)
      if (COMPAT_UNLIKELY(B == 0)) {
        r.status = Status::ERROR_DECODE;
        r.bytesProduced = w;
        ++r.bytesConsumed;
        if (cfg.dropUntilDelimiter)
          st.mode = DecodeState::Mode::DRAIN_CORRUPT;
        else
          st.reset();
        return r;
      }

      st.runRemaining = static_cast<uint8_t>(B - 1);
      st.zeroPending = (B < 0xFF);
      ++r.bytesConsumed;
      continue;
    }
  }

  // End of chunk: check if more data needed
  if (st.mode == DecodeState::Mode::IN_FRAME) {
    r.status = Status::NEED_MORE;
  } else {
    r.status = Status::OK;
  }
  r.bytesProduced = w;
  return r;
}

} // namespace cobs
} // namespace protocols
} // namespace apex