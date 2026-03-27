#ifndef APEX_SYSTEM_CORE_PROTOCOLS_APROTO_CODEC_HPP
#define APEX_SYSTEM_CORE_PROTOCOLS_APROTO_CODEC_HPP
/**
 * @file AprotoCodec.hpp
 * @brief Encode/decode functions for APROTO packets.
 *
 * Provides zero-copy packet viewing, encoding, and validation.
 * All functions are RT-safe with bounded execution time.
 *
 * @note RT-safe: No dynamic allocation, O(1) header operations, O(n) for CRC.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoStatus.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>

namespace system_core {
namespace protocols {
namespace aproto {

/* ----------------------------- Decode API ------------------------------- */

/**
 * @brief Decode header from buffer.
 * @param buf Input buffer (must contain at least APROTO_HEADER_SIZE bytes).
 * @param out Output header structure.
 * @return SUCCESS on valid header, ERROR_* on failure.
 * @note RT-safe: O(1).
 *
 * Validates:
 *   - Buffer size >= APROTO_HEADER_SIZE
 *   - Magic == APROTO_MAGIC
 *   - Version == APROTO_VERSION
 *
 * Does NOT validate payload length or CRC.
 */
[[nodiscard]] Status decodeHeader(apex::compat::rospan<std::uint8_t> buf,
                                  AprotoHeader& out) noexcept;

/**
 * @brief Validate complete packet including optional CRC.
 * @param packet Full packet bytes (header + payload + optional CRC).
 * @return SUCCESS if valid, ERROR_* on failure.
 * @note RT-safe: O(n) where n = packet size (for CRC computation if present).
 *
 * Validates:
 *   - Header magic and version
 *   - Buffer contains full payload (header.payloadLength bytes)
 *   - CRC32 matches (if crcPresent flag is set)
 */
[[nodiscard]] Status validatePacket(apex::compat::rospan<std::uint8_t> packet) noexcept;

/**
 * @brief Get payload span from validated packet.
 * @param packet Full packet bytes (must be validated first).
 * @return Span of payload bytes (may be empty if payloadLength=0).
 * @note RT-safe: O(1).
 * @pre Packet has been validated via validatePacket().
 * @note For encrypted packets, returns ciphertext+authTag. Decrypt before use.
 */
[[nodiscard]] apex::compat::rospan<std::uint8_t>
getPayload(apex::compat::rospan<std::uint8_t> packet) noexcept;

/**
 * @brief Get crypto metadata from encrypted packet.
 * @param packet Full packet bytes (must be validated first).
 * @param meta Output crypto metadata structure.
 * @return SUCCESS if encrypted and metadata extracted, ERROR_MISSING_CRYPTO if not encrypted.
 * @note RT-safe: O(1).
 * @pre Packet has been validated via validatePacket().
 */
[[nodiscard]] Status getCryptoMeta(apex::compat::rospan<std::uint8_t> packet,
                                   CryptoMeta& meta) noexcept;

/* ----------------------------- Encode API ------------------------------- */

/**
 * @brief Build header from parameters.
 * @param fullUid Target component fullUid.
 * @param opcode Operation code.
 * @param sequence Sequence number for correlation.
 * @param payloadLen Payload size in bytes.
 * @param isResponse True for response/telemetry, false for command.
 * @param ackRequested True to request ACK/NAK.
 * @param includeCrc True to indicate CRC32 will follow payload.
 * @return Populated header structure.
 * @note RT-safe: O(1).
 */
[[nodiscard]] AprotoHeader buildHeader(std::uint32_t fullUid, std::uint16_t opcode,
                                       std::uint16_t sequence, std::uint16_t payloadLen,
                                       bool isResponse = false, bool ackRequested = false,
                                       bool includeCrc = false) noexcept;

/**
 * @brief Encode header to buffer.
 * @param hdr Header to encode.
 * @param outBuf Output buffer (must have at least APROTO_HEADER_SIZE bytes).
 * @return SUCCESS on success, ERROR_BUFFER_TOO_SMALL if buffer insufficient.
 * @note RT-safe: O(1).
 */
[[nodiscard]] Status encodeHeader(const AprotoHeader& hdr,
                                  apex::compat::mutable_bytes_span outBuf) noexcept;

/**
 * @brief Encode complete packet (header + payload + optional CRC).
 * @param hdr Packet header (payloadLength must match payload.size()).
 * @param payload Payload bytes.
 * @param outBuf Output buffer for encoded packet.
 * @param bytesWritten Output: total bytes written.
 * @return SUCCESS on success, ERROR_* on failure.
 * @note RT-safe: O(n) where n = payload size (for CRC if enabled).
 *
 * Writes: header (14 bytes) + payload + CRC32 (4 bytes if crcPresent).
 */
[[nodiscard]] Status encodePacket(const AprotoHeader& hdr,
                                  apex::compat::rospan<std::uint8_t> payload,
                                  apex::compat::mutable_bytes_span outBuf,
                                  std::size_t& bytesWritten) noexcept;

/**
 * @brief Build and encode ACK/NAK response packet.
 * @param cmdHeader Original command header (for fullUid, sequence).
 * @param statusCode Response status (0=ACK, nonzero=NAK with error code).
 * @param outBuf Output buffer.
 * @param bytesWritten Output: total bytes written.
 * @param includeCrc True to append CRC32.
 * @return SUCCESS on success, ERROR_* on failure.
 * @note RT-safe: O(1) without CRC, O(n) with CRC.
 */
[[nodiscard]] Status encodeAckNak(const AprotoHeader& cmdHeader, std::uint8_t statusCode,
                                  apex::compat::mutable_bytes_span outBuf,
                                  std::size_t& bytesWritten, bool includeCrc = false) noexcept;

/* ---------------------------- CRC Utilities ----------------------------- */

/**
 * @brief Compute CRC32 of packet data.
 * @param data Data to compute CRC over (typically header + payload).
 * @return CRC32 value.
 * @note RT-safe: O(n) where n = data.size().
 * @note Uses hardware-accelerated CRC32 when available.
 */
[[nodiscard]] std::uint32_t computeCrc(apex::compat::rospan<std::uint8_t> data) noexcept;

/**
 * @brief Append CRC32 to buffer.
 * @param data Data over which CRC was computed.
 * @param outBuf Buffer to append CRC (must have 4 bytes available).
 * @param offset Position in outBuf to write CRC.
 * @return SUCCESS on success, ERROR_BUFFER_TOO_SMALL if insufficient space.
 * @note RT-safe: O(n) for CRC computation.
 */
[[nodiscard]] Status appendCrc(apex::compat::rospan<std::uint8_t> data,
                               apex::compat::mutable_bytes_span outBuf,
                               std::size_t offset) noexcept;

/* ---------------------------- Packet Viewer ----------------------------- */

/**
 * @struct PacketView
 * @brief Zero-copy view into a validated APROTO packet.
 *
 * Provides structured access to packet components without copying.
 * All spans reference the original packet buffer.
 *
 * @note RT-safe: All access is O(1).
 */
struct PacketView {
  AprotoHeader header;                        ///< Decoded header
  apex::compat::rospan<std::uint8_t> raw;     ///< Full packet bytes
  apex::compat::rospan<std::uint8_t> payload; ///< Payload bytes (ciphertext if encrypted)

  /**
   * @brief Check if packet has CRC present.
   */
  [[nodiscard]] bool hasCrc() const noexcept { return header.flags.crcPresent != 0; }

  /**
   * @brief Check if packet is a response.
   */
  [[nodiscard]] bool isResponse() const noexcept { return header.flags.isResponse != 0; }

  /**
   * @brief Check if ACK was requested.
   */
  [[nodiscard]] bool ackRequested() const noexcept { return header.flags.ackRequested != 0; }

  /**
   * @brief Check if packet payload is encrypted.
   */
  [[nodiscard]] bool isEncrypted() const noexcept { return header.flags.encryptedPresent != 0; }

  /**
   * @brief Get CRC span from packet (if present).
   * @return CRC bytes (4 bytes) or empty span if no CRC.
   */
  [[nodiscard]] apex::compat::rospan<std::uint8_t> crcBytes() const noexcept {
    if (!hasCrc()) {
      return {};
    }
    std::size_t crcOffset = APROTO_HEADER_SIZE;
    if (isEncrypted()) {
      crcOffset += APROTO_CRYPTO_META_SIZE;
    }
    crcOffset += header.payloadLength;
    return raw.subspan(crcOffset, APROTO_CRC_SIZE);
  }

  /**
   * @brief Get crypto metadata span from packet (if encrypted).
   * @return Crypto metadata bytes (13 bytes) or empty span if not encrypted.
   */
  [[nodiscard]] apex::compat::rospan<std::uint8_t> cryptoMetaBytes() const noexcept {
    if (!isEncrypted()) {
      return {};
    }
    return raw.subspan(APROTO_HEADER_SIZE, APROTO_CRYPTO_META_SIZE);
  }
};

/**
 * @brief Create packet view with validation.
 * @param packet Full packet bytes.
 * @param view Output view structure.
 * @return SUCCESS on valid packet, ERROR_* on failure.
 * @note RT-safe: O(n) if CRC validation enabled, O(1) otherwise.
 */
[[nodiscard]] Status createPacketView(apex::compat::rospan<std::uint8_t> packet,
                                      PacketView& view) noexcept;

} // namespace aproto
} // namespace protocols
} // namespace system_core

#endif // APEX_SYSTEM_CORE_PROTOCOLS_APROTO_CODEC_HPP
