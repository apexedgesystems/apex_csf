/**
 * @file CobsCodec.hpp
 * @brief Templated COBS codec with owned buffers for zero-allocation framing.
 *
 * Convenience wrapper around the stateless COBS encode/decode API. Owns the
 * decode state, decode config, and output buffers -- sized at compile time via
 * a template parameter. The raw API in COBSFraming.hpp stays available for
 * callers who manage their own buffers.
 *
 * Typical usage (bare-metal):
 * @code
 *   CobsCodec<128> codec;           // 128-byte max decoded frame
 *   auto r = codec.feedDecode({rxBuf, rxLen});
 *   if (r.frameCompleted) {
 *     auto payload = codec.decodedPayload();
 *     // ... process payload ...
 *   }
 * @endcode
 *
 * @note RT-safe after construction (no heap, no exceptions).
 */

#ifndef APEX_PROTOCOLS_COBS_CODEC_HPP
#define APEX_PROTOCOLS_COBS_CODEC_HPP

#include "COBSFraming.hpp"

namespace apex {
namespace protocols {
namespace cobs {

/* ----------------------------- CobsCodec ----------------------------- */

/**
 * @class CobsCodec
 * @brief COBS codec with compile-time-sized owned buffers.
 *
 * @tparam MaxFrameSize Maximum decoded frame size in bytes. Controls decode
 *         and encode buffer allocation. Defaults to DEFAULT_MAX_FRAME_SIZE.
 *
 * @note RT-safe: all methods are O(n) in input/payload size with no allocation.
 */
template <size_t MaxFrameSize = DEFAULT_MAX_FRAME_SIZE> class CobsCodec {
public:
  /// Worst-case COBS-encoded size: payload + overhead bytes + trailing delimiter.
  /// COBS overhead is ceil(N/254) + 1 code byte + 1 delimiter.
  static constexpr size_t ENCODE_BUF_SIZE = MaxFrameSize + (MaxFrameSize / 254) + 2;

  /**
   * @brief Construct codec with default decode config.
   *
   * Sets maxFrameSize in the decode config to match the template parameter.
   */
  CobsCodec() noexcept { cfg_.maxFrameSize = MaxFrameSize; }

  /* ----------------------------- Decode ----------------------------- */

  /**
   * @brief Feed raw bytes into the streaming decoder.
   *
   * Wraps decodeChunk() with owned state and decode buffer. On frame
   * completion (result.frameCompleted == true), the decoded payload is
   * available via decodedPayload() until the next feedDecode() call.
   *
   * @param input Raw bytes from the wire.
   * @return IoResult with status, bytes consumed/produced, and frame flag.
   * @note RT-safe.
   */
  IoResult feedDecode(apex::compat::bytes_span input) noexcept {
    auto result = decodeChunk(state_, cfg_, input, decodeBuf_, MaxFrameSize);
    if (result.frameCompleted) {
      lastDecodedLen_ = accumulatedLen_ + result.bytesProduced;
      accumulatedLen_ = 0;
    } else {
      accumulatedLen_ += result.bytesProduced;
    }
    return result;
  }

  /**
   * @brief Get span over the last decoded frame payload.
   *
   * Valid only after feedDecode() returns frameCompleted == true and before
   * the next feedDecode() call.
   *
   * @return Read-only span of decoded bytes.
   * @note RT-safe.
   */
  apex::compat::bytes_span decodedPayload() const noexcept {
    return apex::compat::bytes_span(decodeBuf_, lastDecodedLen_);
  }

  /**
   * @brief Get the raw decode buffer pointer.
   * @return Pointer to the internal decode buffer.
   * @note RT-safe.
   */
  const uint8_t* decodeBuf() const noexcept { return decodeBuf_; }

  /**
   * @brief Get the number of decoded bytes in the current frame.
   * @return Byte count (valid after frameCompleted).
   * @note RT-safe.
   */
  size_t decodedLen() const noexcept { return lastDecodedLen_; }

  /**
   * @brief Reset the decoder to idle state.
   * @note RT-safe.
   */
  void resetDecoder() noexcept {
    state_.reset();
    lastDecodedLen_ = 0;
    accumulatedLen_ = 0;
  }

  /* ----------------------------- Encode ----------------------------- */

  /**
   * @brief COBS-encode a payload into the owned encode buffer.
   *
   * Wraps encode() with the internal encode buffer.
   *
   * @param payload Data to encode.
   * @param trailingDelimiter Append trailing delimiter (default true).
   * @return IoResult with status and bytes produced.
   * @note RT-safe.
   */
  IoResult encode(apex::compat::bytes_span payload, bool trailingDelimiter = true) noexcept {
    return cobs::encode(payload, encodeBuf_, ENCODE_BUF_SIZE, trailingDelimiter);
  }

  /**
   * @brief Get the raw encode buffer pointer.
   *
   * Valid after a successful encode() call. Contains bytesProduced bytes.
   *
   * @return Pointer to the internal encode buffer.
   * @note RT-safe.
   */
  const uint8_t* encodeBuf() const noexcept { return encodeBuf_; }

  /* ----------------------------- Config ----------------------------- */

  /**
   * @brief Access decoder configuration for modification.
   *
   * Modify before first feedDecode() call. Changes take effect immediately
   * on the next feedDecode().
   *
   * @return Mutable reference to decode config.
   * @note RT-safe.
   */
  DecodeConfig& config() noexcept { return cfg_; }

  /**
   * @brief Access decoder configuration (const).
   * @return Const reference to decode config.
   * @note RT-safe.
   */
  const DecodeConfig& config() const noexcept { return cfg_; }

  /**
   * @brief Access decoder state (const).
   * @return Const reference to decode state.
   * @note RT-safe.
   */
  const DecodeState& state() const noexcept { return state_; }

private:
  DecodeState state_{};
  DecodeConfig cfg_{};
  size_t lastDecodedLen_ = 0;
  size_t accumulatedLen_ = 0;
  uint8_t decodeBuf_[MaxFrameSize]{};
  uint8_t encodeBuf_[ENCODE_BUF_SIZE]{};
};

} // namespace cobs
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_COBS_CODEC_HPP
