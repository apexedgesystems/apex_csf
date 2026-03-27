#ifndef APEX_SYSTEM_CORE_PROTOCOLS_APROTO_MUTABLE_MESSAGE_HPP
#define APEX_SYSTEM_CORE_PROTOCOLS_APROTO_MUTABLE_MESSAGE_HPP
/**
 * @file AprotoMutableMessage.hpp
 * @brief Mutable, typed facade for assembling APROTO packets.
 *
 * RT-Safety:
 *  - No heap allocation (all templates use fixed-size arrays).
 *  - No std::function.
 *  - No exceptions in hot paths; pack() and packInto() return std::optional.
 *  - C++17-compatible via apex::compat::bytes_span / apex::compat::rospan.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace system_core {
namespace protocols {
namespace aproto {

/* ----------------------- MutableAprotoHeader ------------------------------ */

/**
 * @struct MutableAprotoHeader
 * @brief Field-only header descriptor; serialize via MutableAprotoMessageT::pack()/packInto().
 *
 * Provides named fields matching the wire format without requiring the caller
 * to manually compose AprotoFlags or call buildHeader().
 *
 * @note RT-safe: POD struct.
 */
struct MutableAprotoHeader {
  std::uint32_t fullUid = 0;  ///< Target component fullUid.
  std::uint16_t opcode = 0;   ///< Operation code.
  std::uint16_t sequence = 0; ///< Sequence number for ACK correlation.
  bool isResponse = false;    ///< 0=command, 1=response/telemetry.
  bool ackRequested = false;  ///< 1=expects ACK/NAK.
  bool includeCrc = false;    ///< 1=append CRC32 trailer.
  bool encrypted = false;     ///< 1=payload is AEAD encrypted.
};

/* ----------------------- MutableAprotoCryptoMeta -------------------------- */

/**
 * @struct MutableAprotoCryptoMeta
 * @brief RT-safe storage for encryption metadata fields.
 *
 * @note RT-safe: POD struct, fixed-size array.
 */
struct MutableAprotoCryptoMeta {
  std::uint8_t keyIndex = 0;                           ///< Key table index (0-255).
  std::array<std::uint8_t, APROTO_NONCE_SIZE> nonce{}; ///< 12-byte AEAD nonce/IV.

  /**
   * @brief Set nonce from span.
   * @param src Source bytes (must be exactly APROTO_NONCE_SIZE).
   * @return true on success, false if wrong size.
   * @note RT-safe.
   */
  [[nodiscard]] bool setNonce(apex::compat::bytes_span src) noexcept {
    if (src.size() != APROTO_NONCE_SIZE)
      return false;
    std::memcpy(nonce.data(), src.data(), APROTO_NONCE_SIZE);
    return true;
  }
};

/* ----------------------- Fixed-Size Packet Storage ------------------------ */

/**
 * @struct AprotoMsg
 * @brief Fixed-size packet storage for RT-safe packet assembly.
 *
 * @tparam MaxSize Maximum packet size in bytes.
 *
 * @note RT-safe: No heap allocation.
 */
template <std::size_t MaxSize = APROTO_HEADER_SIZE + APROTO_MAX_PAYLOAD + APROTO_CRC_SIZE>
struct AprotoMsg {
  std::array<std::uint8_t, MaxSize> data{};
  std::size_t length = 0;

  /**
   * @brief Get read-only span of the encoded packet.
   * @return Span over valid bytes.
   * @note RT-safe.
   */
  [[nodiscard]] apex::compat::bytes_span span() const noexcept {
    return apex::compat::bytes_span{data.data(), length};
  }

  /**
   * @brief Get buffer capacity.
   * @return Maximum packet size.
   * @note RT-safe.
   */
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MaxSize; }
};

/* ----------------------- MutableAprotoMessageT ---------------------------- */

/**
 * @struct MutableAprotoMessageT
 * @brief Mutable descriptor that packs into a caller-provided buffer or fixed AprotoMsg.
 *
 * Assembles a complete APROTO packet from typed fields. The payload is referenced
 * by pointer (zero-copy); the actual encoding happens in pack()/packInto().
 *
 * @tparam T Payload element type (trivially copyable, standard layout).
 * @tparam MaxPacketSize Maximum packet size for pack() output.
 *
 * @note RT-safe: No heap allocation.
 */
template <typename T,
          std::size_t MaxPacketSize = APROTO_HEADER_SIZE + APROTO_MAX_PAYLOAD + APROTO_CRC_SIZE>
struct MutableAprotoMessageT {
  static_assert(std::is_trivially_copyable<T>::value,
                "MutableAprotoMessageT<T> requires T to be trivially copyable.");
  static_assert(std::is_standard_layout<T>::value,
                "MutableAprotoMessageT<T> requires T to be standard-layout.");

  MutableAprotoHeader hdr{};
  std::optional<MutableAprotoCryptoMeta> crypto{};
  const T* payload = nullptr;   ///< Base pointer (may be null if payloadCount==0).
  std::size_t payloadCount = 0; ///< Number of T elements.

  /**
   * @brief Pack into an RT-safe AprotoMsg (fixed-size storage).
   * @return std::nullopt on invalid inputs or size violations.
   * @note RT-safe: No heap allocation.
   */
  [[nodiscard]] std::optional<AprotoMsg<MaxPacketSize>> pack() const noexcept COMPAT_HOT {
    AprotoMsg<MaxPacketSize> msg{};
    auto result = packInto(msg.data.data(), msg.data.size());
    if (!result)
      return std::nullopt;
    msg.length = *result;
    return msg;
  }

  /**
   * @brief Zero-alloc writer: serialize into a caller-provided buffer.
   * @param out Destination buffer.
   * @param outLen Capacity of out in bytes.
   * @return Number of bytes written on success; std::nullopt on invalid args or capacity too small.
   *
   * Layout: [Header(14)] [CryptoMeta(13)?] [Payload] [CRC32(4)?]
   *
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] std::optional<std::size_t> packInto(std::uint8_t* out,
                                                    std::size_t outLen) const noexcept {
    if (out == nullptr)
      return std::nullopt;

    const std::size_t payloadBytes = payloadCount * sizeof(T);

    // Require non-null pointer when payload is present.
    if (payloadBytes > 0 && payload == nullptr)
      return std::nullopt;

    // Check payload fits in 16-bit field.
    if (payloadBytes > APROTO_MAX_PAYLOAD)
      return std::nullopt;

    // Build the header using existing codec API.
    AprotoHeader aprotoHdr =
        buildHeader(hdr.fullUid, hdr.opcode, hdr.sequence, static_cast<std::uint16_t>(payloadBytes),
                    hdr.isResponse, hdr.ackRequested, hdr.includeCrc);

    // Set encryption flag if crypto metadata is present.
    if (crypto) {
      aprotoHdr.flags.encryptedPresent = 1;
    }

    // Calculate total size.
    const std::size_t totalSize = packetSize(aprotoHdr);
    if (totalSize > MaxPacketSize || outLen < totalSize)
      return std::nullopt;

    // Write header.
    std::memcpy(out, &aprotoHdr, APROTO_HEADER_SIZE);
    std::size_t offset = APROTO_HEADER_SIZE;

    // Write crypto metadata (if present).
    if (crypto) {
      CryptoMeta meta{};
      meta.keyIndex = crypto->keyIndex;
      std::memcpy(meta.nonce, crypto->nonce.data(), APROTO_NONCE_SIZE);
      std::memcpy(out + offset, &meta, APROTO_CRYPTO_META_SIZE);
      offset += APROTO_CRYPTO_META_SIZE;
    }

    // Write payload.
    if (payloadBytes > 0) {
      std::memcpy(out + offset, reinterpret_cast<const std::uint8_t*>(payload), payloadBytes);
      offset += payloadBytes;
    }

    // Append CRC if requested.
    if (hdr.includeCrc) {
      const std::uint32_t crc = computeCrc(apex::compat::rospan<std::uint8_t>{out, offset});

      out[offset + 0] = static_cast<std::uint8_t>(crc & 0xFF);
      out[offset + 1] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
      out[offset + 2] = static_cast<std::uint8_t>((crc >> 16) & 0xFF);
      out[offset + 3] = static_cast<std::uint8_t>((crc >> 24) & 0xFF);
      offset += APROTO_CRC_SIZE;
    }

    return offset;
  }

  /**
   * @brief Compute required packet size.
   * @return Total size in bytes.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t requiredSize() const noexcept {
    std::size_t size = APROTO_HEADER_SIZE;
    if (crypto) {
      size += APROTO_CRYPTO_META_SIZE;
    }
    size += payloadCount * sizeof(T);
    if (hdr.includeCrc) {
      size += APROTO_CRC_SIZE;
    }
    return size;
  }

  /// Set payload from a read-only span.
  /// @note RT-safe.
  void setPayload(apex::compat::rospan<T> span) noexcept {
    payload = span.data();
    payloadCount = span.size();
  }

  /// Set payload from pointer + count.
  /// @note RT-safe.
  void setPayload(const T* p, std::size_t n) noexcept {
    payload = p;
    payloadCount = n;
  }
};

/* ----------------------- MutableAprotoMessageFactory ----------------------- */

/**
 * @struct MutableAprotoMessageFactory
 * @brief Convenience builders for MutableAprotoMessageT (no exceptions).
 *
 * @note RT-safe: No heap allocation.
 */
struct MutableAprotoMessageFactory {
  /**
   * @brief Build from a single payload instance reference.
   * @param fullUid Target component fullUid.
   * @param opcode Operation code.
   * @param sequence Sequence number.
   * @param payloadInstance Reference to payload data.
   * @param isResponse True for response/telemetry.
   * @param ackRequested True to request ACK/NAK.
   * @param includeCrc True to append CRC32.
   * @return Populated message, or nullopt on invalid inputs.
   * @note RT-safe.
   */
  template <typename T,
            std::size_t MaxPacketSize = APROTO_HEADER_SIZE + APROTO_MAX_PAYLOAD + APROTO_CRC_SIZE>
  [[nodiscard]] static std::optional<MutableAprotoMessageT<T, MaxPacketSize>>
  build(std::uint32_t fullUid, std::uint16_t opcode, std::uint16_t sequence,
        const T& payloadInstance, bool isResponse = false, bool ackRequested = false,
        bool includeCrc = false) noexcept {
    MutableAprotoMessageT<T, MaxPacketSize> msg;
    msg.hdr.fullUid = fullUid;
    msg.hdr.opcode = opcode;
    msg.hdr.sequence = sequence;
    msg.hdr.isResponse = isResponse;
    msg.hdr.ackRequested = ackRequested;
    msg.hdr.includeCrc = includeCrc;
    msg.payload = &payloadInstance;
    msg.payloadCount = 1;
    return msg;
  }

  /**
   * @brief Build from a payload span.
   * @param fullUid Target component fullUid.
   * @param opcode Operation code.
   * @param sequence Sequence number.
   * @param payloadSpan Read-only span of payload elements.
   * @param isResponse True for response/telemetry.
   * @param ackRequested True to request ACK/NAK.
   * @param includeCrc True to append CRC32.
   * @return Populated message, or nullopt on invalid inputs.
   * @note RT-safe.
   */
  template <typename T,
            std::size_t MaxPacketSize = APROTO_HEADER_SIZE + APROTO_MAX_PAYLOAD + APROTO_CRC_SIZE>
  [[nodiscard]] static std::optional<MutableAprotoMessageT<T, MaxPacketSize>>
  build(std::uint32_t fullUid, std::uint16_t opcode, std::uint16_t sequence,
        apex::compat::rospan<T> payloadSpan, bool isResponse = false, bool ackRequested = false,
        bool includeCrc = false) noexcept {
    if (payloadSpan.empty())
      return std::nullopt;
    MutableAprotoMessageT<T, MaxPacketSize> msg;
    msg.hdr.fullUid = fullUid;
    msg.hdr.opcode = opcode;
    msg.hdr.sequence = sequence;
    msg.hdr.isResponse = isResponse;
    msg.hdr.ackRequested = ackRequested;
    msg.hdr.includeCrc = includeCrc;
    msg.setPayload(payloadSpan);
    return msg;
  }

  /**
   * @brief Build from pointer + count.
   * @param fullUid Target component fullUid.
   * @param opcode Operation code.
   * @param sequence Sequence number.
   * @param payloadPtr Pointer to payload data.
   * @param payloadCount Number of T elements.
   * @param isResponse True for response/telemetry.
   * @param ackRequested True to request ACK/NAK.
   * @param includeCrc True to append CRC32.
   * @return Populated message, or nullopt on invalid inputs.
   * @note RT-safe.
   */
  template <typename T,
            std::size_t MaxPacketSize = APROTO_HEADER_SIZE + APROTO_MAX_PAYLOAD + APROTO_CRC_SIZE>
  [[nodiscard]] static std::optional<MutableAprotoMessageT<T, MaxPacketSize>>
  build(std::uint32_t fullUid, std::uint16_t opcode, std::uint16_t sequence, const T* payloadPtr,
        std::size_t payloadCount, bool isResponse = false, bool ackRequested = false,
        bool includeCrc = false) noexcept {
    if (payloadCount > 0 && payloadPtr == nullptr)
      return std::nullopt;
    if (payloadCount == 0)
      return std::nullopt;
    MutableAprotoMessageT<T, MaxPacketSize> msg;
    msg.hdr.fullUid = fullUid;
    msg.hdr.opcode = opcode;
    msg.hdr.sequence = sequence;
    msg.hdr.isResponse = isResponse;
    msg.hdr.ackRequested = ackRequested;
    msg.hdr.includeCrc = includeCrc;
    msg.setPayload(payloadPtr, payloadCount);
    return msg;
  }
};

} // namespace aproto
} // namespace protocols
} // namespace system_core

#endif // APEX_SYSTEM_CORE_PROTOCOLS_APROTO_MUTABLE_MESSAGE_HPP
