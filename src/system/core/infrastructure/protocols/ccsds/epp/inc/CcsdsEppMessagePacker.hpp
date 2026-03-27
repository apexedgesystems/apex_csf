#ifndef APEX_PROTOCOLS_CCSDS_EPP_PACKER_HPP
#define APEX_PROTOCOLS_CCSDS_EPP_PACKER_HPP
/**
 * @file CcsdsEppMessagePacker.hpp
 * @brief Immutable construction (packing) of CCSDS EPP packets.
 *
 * Layout:
 *   [ Header (1/2/4/8 octets) ] [ Encapsulated Data (0..N) ]
 *   // Note: Idle packets (LoL=00) have no encapsulated data
 *
 * RT-Safety:
 *  - No heap allocation (all templates use fixed-size arrays).
 *  - No std::function (uses caller-provided buffers).
 *  - No exceptions in hot paths; factories return std::optional.
 *  - Validates header variant and packet length.
 *  - C++17-compatible via apex::compat::bytes_span.
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppCommonDefs.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace protocols {
namespace ccsds {
namespace epp {

/* ----------------------------- EppHeader ---------------------------------- */

/**
 * @class EppHeader
 * @brief Builder/serializer for CCSDS EPP header (1, 2, 4, or 8 octets).
 *
 * Per CCSDS 133.1-B-3 Section 4.1.2, the header variant is determined by
 * the Length of Length (LoL) field:
 *  - LoL=00: 1-octet header (Idle Packet)
 *  - LoL=01: 2-octet header (1-byte Packet Length field)
 *  - LoL=10: 4-octet header (2-byte Packet Length field)
 *  - LoL=11: 8-octet header (4-byte Packet Length field)
 *
 * @note RT-safe: No heap allocation.
 */
class EppHeader {
public:
  /**
   * @brief Build an idle packet header (1 octet).
   * Per CCSDS 133.1-B-3 Section 4.1.2.3 Note 1.
   * @param protocolId Encapsulation Protocol ID (should be 0 for idle).
   * @return EppHeader on success; std::nullopt if validation fails.
   * @note RT-safe: No allocation.
   */
  static std::optional<EppHeader>
  buildIdle(std::uint8_t protocolId = EPP_PROTOCOL_ID_IDLE) noexcept {
    if (COMPAT_UNLIKELY(protocolId > EPP_PROTOCOL_ID_MASK_3BIT))
      return std::nullopt;

    EppHeader h;
    h.len_ = EPP_HEADER_1_OCTET;
    // Octet 0: version (7), protocolId, LoL=00
    h.bytes_[0] = static_cast<std::uint8_t>(
        (EPP_VALID_VERSION << EPP_VERSION_SHIFT) |
        ((protocolId & EPP_PROTOCOL_ID_MASK_3BIT) << EPP_PROTOCOL_ID_SHIFT) | EPP_LOL_IDLE);
    return h;
  }

  /**
   * @brief Build a 2-octet header.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param packetLength Total packet length (max 255).
   * @return EppHeader on success; std::nullopt if validation fails.
   * @note RT-safe: No allocation.
   */
  static std::optional<EppHeader> build2Octet(std::uint8_t protocolId,
                                              std::uint8_t packetLength) noexcept {
    if (COMPAT_UNLIKELY(protocolId > EPP_PROTOCOL_ID_MASK_3BIT))
      return std::nullopt;
    if (COMPAT_UNLIKELY(packetLength < EPP_HEADER_2_OCTET))
      return std::nullopt;

    EppHeader h;
    h.len_ = EPP_HEADER_2_OCTET;
    h.bytes_[0] = static_cast<std::uint8_t>(
        (EPP_VALID_VERSION << EPP_VERSION_SHIFT) |
        ((protocolId & EPP_PROTOCOL_ID_MASK_3BIT) << EPP_PROTOCOL_ID_SHIFT) | EPP_LOL_1_OCTET);
    h.bytes_[1] = packetLength;
    return h;
  }

  /**
   * @brief Build a 4-octet header.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param userDefined User Defined field (4 bits).
   * @param protocolIde Protocol ID Extension (4 bits, must be 0 unless protocolId==0x06).
   * @param packetLength Total packet length (max 65535).
   * @return EppHeader on success; std::nullopt if validation fails.
   * @note RT-safe: No allocation.
   * @note Per CCSDS 133.1-B-3 Section 4.1.2.6.3: If protocolId != '110', protocolIde must be 0.
   */
  static std::optional<EppHeader> build4Octet(std::uint8_t protocolId, std::uint8_t userDefined,
                                              std::uint8_t protocolIde,
                                              std::uint16_t packetLength) noexcept {
    if (COMPAT_UNLIKELY(protocolId > EPP_PROTOCOL_ID_MASK_3BIT))
      return std::nullopt;
    if (COMPAT_UNLIKELY(userDefined > EPP_USER_DEFINED_MASK_4BIT))
      return std::nullopt;
    if (COMPAT_UNLIKELY(protocolIde > EPP_PROTOCOL_IDE_MASK))
      return std::nullopt;
    // Per CCSDS 133.1-B-3 Section 4.1.2.6.3: protocolIde must be 0 if protocolId != '110'.
    if (COMPAT_UNLIKELY(protocolId != EPP_PROTOCOL_ID_EXTENDED && protocolIde != 0))
      return std::nullopt;
    if (COMPAT_UNLIKELY(packetLength < EPP_HEADER_4_OCTET))
      return std::nullopt;

    EppHeader h;
    h.len_ = EPP_HEADER_4_OCTET;
    h.bytes_[0] = static_cast<std::uint8_t>(
        (EPP_VALID_VERSION << EPP_VERSION_SHIFT) |
        ((protocolId & EPP_PROTOCOL_ID_MASK_3BIT) << EPP_PROTOCOL_ID_SHIFT) | EPP_LOL_2_OCTETS);
    h.bytes_[1] = static_cast<std::uint8_t>(
        ((userDefined & EPP_USER_DEFINED_MASK_4BIT) << EPP_USER_DEFINED_SHIFT) |
        (protocolIde & EPP_PROTOCOL_IDE_MASK));
    h.bytes_[2] = static_cast<std::uint8_t>((packetLength >> 8) & 0xFF);
    h.bytes_[3] = static_cast<std::uint8_t>(packetLength & 0xFF);
    return h;
  }

  /**
   * @brief Build an 8-octet header.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param userDefined User Defined field (4 bits).
   * @param protocolIde Protocol ID Extension (4 bits, must be 0 unless protocolId==0x06).
   * @param ccsdsDefined CCSDS Defined field (16 bits).
   * @param packetLength Total packet length (max 4,294,967,295).
   * @return EppHeader on success; std::nullopt if validation fails.
   * @note RT-safe: No allocation.
   * @note Per CCSDS 133.1-B-3 Section 4.1.2.6.3: If protocolId != '110', protocolIde must be 0.
   */
  static std::optional<EppHeader> build8Octet(std::uint8_t protocolId, std::uint8_t userDefined,
                                              std::uint8_t protocolIde, std::uint16_t ccsdsDefined,
                                              std::uint32_t packetLength) noexcept {
    if (COMPAT_UNLIKELY(protocolId > EPP_PROTOCOL_ID_MASK_3BIT))
      return std::nullopt;
    if (COMPAT_UNLIKELY(userDefined > EPP_USER_DEFINED_MASK_4BIT))
      return std::nullopt;
    if (COMPAT_UNLIKELY(protocolIde > EPP_PROTOCOL_IDE_MASK))
      return std::nullopt;
    // Per CCSDS 133.1-B-3 Section 4.1.2.6.3: protocolIde must be 0 if protocolId != '110'.
    if (COMPAT_UNLIKELY(protocolId != EPP_PROTOCOL_ID_EXTENDED && protocolIde != 0))
      return std::nullopt;
    if (COMPAT_UNLIKELY(packetLength < EPP_HEADER_8_OCTET))
      return std::nullopt;

    EppHeader h;
    h.len_ = EPP_HEADER_8_OCTET;
    h.bytes_[0] = static_cast<std::uint8_t>(
        (EPP_VALID_VERSION << EPP_VERSION_SHIFT) |
        ((protocolId & EPP_PROTOCOL_ID_MASK_3BIT) << EPP_PROTOCOL_ID_SHIFT) | EPP_LOL_4_OCTETS);
    h.bytes_[1] = static_cast<std::uint8_t>(
        ((userDefined & EPP_USER_DEFINED_MASK_4BIT) << EPP_USER_DEFINED_SHIFT) |
        (protocolIde & EPP_PROTOCOL_IDE_MASK));
    h.bytes_[2] = static_cast<std::uint8_t>((ccsdsDefined >> 8) & 0xFF);
    h.bytes_[3] = static_cast<std::uint8_t>(ccsdsDefined & 0xFF);
    h.bytes_[4] = static_cast<std::uint8_t>((packetLength >> 24) & 0xFF);
    h.bytes_[5] = static_cast<std::uint8_t>((packetLength >> 16) & 0xFF);
    h.bytes_[6] = static_cast<std::uint8_t>((packetLength >> 8) & 0xFF);
    h.bytes_[7] = static_cast<std::uint8_t>(packetLength & 0xFF);
    return h;
  }

  /// Get header length.
  /// @note RT-safe.
  [[nodiscard]] std::size_t length() const noexcept { return len_; }

  /// Write header bytes to caller-provided buffer.
  /// @note RT-safe: No allocation.
  void writeTo(std::uint8_t* out) const noexcept { std::memcpy(out, bytes_.data(), len_); }

  /// Get header as array (for fixed-size usage).
  /// @note RT-safe.
  [[nodiscard]] std::array<std::uint8_t, EPP_HEADER_8_OCTET> toArray() const noexcept {
    return bytes_;
  }

private:
  EppHeader() = default;
  std::array<std::uint8_t, EPP_HEADER_8_OCTET> bytes_{};
  std::size_t len_ = 0;
};

/* --------------------------------- EppMsg --------------------------------- */

/**
 * @class EppMsg
 * @brief RT-safe EPP packet with fixed-size storage.
 *
 * @tparam MaxPacketSize Maximum packet size (default EPP_DEFAULT_MAX_PACKET_LENGTH).
 *
 * @note RT-safe: No heap allocation. Fixed-size array.
 */
template <std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH> class EppMsg {
  static_assert(MaxPacketSize >= EPP_HEADER_1_OCTET,
                "MaxPacketSize must hold at least idle packet");

public:
  /**
   * @brief Create an idle packet (1-octet header, no payload).
   * @param protocolId Protocol ID (should be 0 for idle).
   * @return EppMsg on success; std::nullopt on failure.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] static std::optional<EppMsg>
  createIdle(std::uint8_t protocolId = EPP_PROTOCOL_ID_IDLE) noexcept {
    auto hdr = EppHeader::buildIdle(protocolId);
    if (!hdr)
      return std::nullopt;

    EppMsg msg;
    hdr->writeTo(msg.bytes_.data());
    msg.len_ = EPP_HEADER_1_OCTET;
    return msg;
  }

  /**
   * @brief Create a 2-octet header packet.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param payload Encapsulated data.
   * @return EppMsg on success; std::nullopt on failure.
   * @note RT-safe: No allocation.
   * @note Per CCSDS 133.1-B-3 Section 4.1.3.1.5: If payload is empty, protocolId must be '000'.
   */
  [[nodiscard]] static std::optional<EppMsg>
  create2Octet(std::uint8_t protocolId, apex::compat::bytes_span payload) noexcept COMPAT_HOT {
    // Per CCSDS 133.1-B-3 Section 4.1.3.1.5: empty payload requires protocolId == 0.
    if (COMPAT_UNLIKELY(payload.empty() && protocolId != EPP_PROTOCOL_ID_IDLE))
      return std::nullopt;
    const std::size_t totalLength = EPP_HEADER_2_OCTET + payload.size();
    if (totalLength > 0xFF || totalLength > MaxPacketSize)
      return std::nullopt;

    auto hdr = EppHeader::build2Octet(protocolId, static_cast<std::uint8_t>(totalLength));
    if (!hdr)
      return std::nullopt;

    EppMsg msg;
    hdr->writeTo(msg.bytes_.data());
    if (!payload.empty()) {
      std::memcpy(msg.bytes_.data() + EPP_HEADER_2_OCTET, payload.data(), payload.size());
    }
    msg.len_ = totalLength;
    return msg;
  }

  /**
   * @brief Create a 4-octet header packet.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param userDefined User Defined field (4 bits).
   * @param protocolIde Protocol ID Extension (4 bits).
   * @param payload Encapsulated data.
   * @return EppMsg on success; std::nullopt on failure.
   * @note RT-safe: No allocation.
   * @note Per CCSDS 133.1-B-3 Section 4.1.3.1.5: If payload is empty, protocolId must be '000'.
   */
  [[nodiscard]] static std::optional<EppMsg>
  create4Octet(std::uint8_t protocolId, std::uint8_t userDefined, std::uint8_t protocolIde,
               apex::compat::bytes_span payload) noexcept COMPAT_HOT {
    // Per CCSDS 133.1-B-3 Section 4.1.3.1.5: empty payload requires protocolId == 0.
    if (COMPAT_UNLIKELY(payload.empty() && protocolId != EPP_PROTOCOL_ID_IDLE))
      return std::nullopt;
    const std::size_t totalLength = EPP_HEADER_4_OCTET + payload.size();
    if (totalLength > 0xFFFF || totalLength > MaxPacketSize)
      return std::nullopt;

    auto hdr = EppHeader::build4Octet(protocolId, userDefined, protocolIde,
                                      static_cast<std::uint16_t>(totalLength));
    if (!hdr)
      return std::nullopt;

    EppMsg msg;
    hdr->writeTo(msg.bytes_.data());
    if (!payload.empty()) {
      std::memcpy(msg.bytes_.data() + EPP_HEADER_4_OCTET, payload.data(), payload.size());
    }
    msg.len_ = totalLength;
    return msg;
  }

  /**
   * @brief Create an 8-octet header packet.
   * @param protocolId Encapsulation Protocol ID (3 bits).
   * @param userDefined User Defined field (4 bits).
   * @param protocolIde Protocol ID Extension (4 bits).
   * @param ccsdsDefined CCSDS Defined field (16 bits).
   * @param payload Encapsulated data.
   * @return EppMsg on success; std::nullopt on failure.
   * @note RT-safe: No allocation.
   * @note Per CCSDS 133.1-B-3 Section 4.1.3.1.5: If payload is empty, protocolId must be '000'.
   */
  [[nodiscard]] static std::optional<EppMsg>
  create8Octet(std::uint8_t protocolId, std::uint8_t userDefined, std::uint8_t protocolIde,
               std::uint16_t ccsdsDefined, apex::compat::bytes_span payload) noexcept COMPAT_HOT {
    // Per CCSDS 133.1-B-3 Section 4.1.3.1.5: empty payload requires protocolId == 0.
    if (COMPAT_UNLIKELY(payload.empty() && protocolId != EPP_PROTOCOL_ID_IDLE))
      return std::nullopt;
    const std::size_t totalLength = EPP_HEADER_8_OCTET + payload.size();
    if (totalLength > MaxPacketSize)
      return std::nullopt;

    auto hdr = EppHeader::build8Octet(protocolId, userDefined, protocolIde, ccsdsDefined,
                                      static_cast<std::uint32_t>(totalLength));
    if (!hdr)
      return std::nullopt;

    EppMsg msg;
    hdr->writeTo(msg.bytes_.data());
    if (!payload.empty()) {
      std::memcpy(msg.bytes_.data() + EPP_HEADER_8_OCTET, payload.data(), payload.size());
    }
    msg.len_ = totalLength;
    return msg;
  }

  /// @return Serialized packet (read-only view).
  /// @note RT-safe.
  [[nodiscard]] apex::compat::bytes_span data() const noexcept {
    return apex::compat::bytes_span{bytes_.data(), len_};
  }

  /// @return Total packet length in bytes.
  /// @note RT-safe.
  [[nodiscard]] std::size_t length() const noexcept { return len_; }

  /// @return Maximum packet capacity.
  /// @note RT-safe.
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MaxPacketSize; }

private:
  EppMsg() = default;
  std::array<std::uint8_t, MaxPacketSize> bytes_{};
  std::size_t len_ = 0;
};

/// Default EppMsg type (64KB capacity).
using EppMsgDefault = EppMsg<EPP_DEFAULT_MAX_PACKET_LENGTH>;

/// Small EppMsg type (256B capacity).
using EppMsgSmall = EppMsg<256>;

/// Large EppMsg type (1MB capacity).
using EppMsgLarge = EppMsg<1048576>;

/* ----------------------------- RT-Safe Packer ----------------------------- */

/**
 * @brief RT-safe packet packing directly into caller buffer.
 *
 * @param headerVariant Header variant (1, 2, 4, or 8).
 * @param protocolId Encapsulation Protocol ID (3 bits).
 * @param userDefined User Defined field (4 bits, for 4/8-octet headers).
 * @param protocolIde Protocol ID Extension (4 bits, for 4/8-octet headers).
 * @param ccsdsDefined CCSDS Defined field (16 bits, for 8-octet header).
 * @param payload Encapsulated data.
 * @param outBuf Destination buffer.
 * @param outCapacity Capacity of outBuf.
 * @param bytesWritten Output: number of bytes written on success.
 * @return true on success, false on invalid input or insufficient capacity.
 * @note RT-safe: No allocation. Zero-copy write.
 */
[[nodiscard]] COMPAT_HOT inline bool
packPacket(std::uint8_t headerVariant, std::uint8_t protocolId, std::uint8_t userDefined,
           std::uint8_t protocolIde, std::uint16_t ccsdsDefined, apex::compat::bytes_span payload,
           std::uint8_t* outBuf, std::size_t outCapacity, std::size_t& bytesWritten) noexcept {
  if (outBuf == nullptr)
    return false;

  std::optional<EppHeader> hdr;
  std::size_t totalLength = 0;

  switch (headerVariant) {
  case EPP_HEADER_1_OCTET:
    if (!payload.empty())
      return false; // Idle packets have no payload
    hdr = EppHeader::buildIdle(protocolId);
    totalLength = EPP_HEADER_1_OCTET;
    break;
  case EPP_HEADER_2_OCTET:
    totalLength = EPP_HEADER_2_OCTET + payload.size();
    if (totalLength > 0xFF)
      return false;
    hdr = EppHeader::build2Octet(protocolId, static_cast<std::uint8_t>(totalLength));
    break;
  case EPP_HEADER_4_OCTET:
    totalLength = EPP_HEADER_4_OCTET + payload.size();
    if (totalLength > 0xFFFF)
      return false;
    hdr = EppHeader::build4Octet(protocolId, userDefined, protocolIde,
                                 static_cast<std::uint16_t>(totalLength));
    break;
  case EPP_HEADER_8_OCTET:
    totalLength = EPP_HEADER_8_OCTET + payload.size();
    hdr = EppHeader::build8Octet(protocolId, userDefined, protocolIde, ccsdsDefined,
                                 static_cast<std::uint32_t>(totalLength));
    break;
  default:
    return false;
  }

  if (!hdr || totalLength > outCapacity)
    return false;

  hdr->writeTo(outBuf);
  if (!payload.empty()) {
    std::memcpy(outBuf + headerVariant, payload.data(), payload.size());
  }

  bytesWritten = totalLength;
  return true;
}

/**
 * @brief Compute required packet size.
 * @param headerVariant Header variant (1, 2, 4, or 8).
 * @param payloadLen Length of encapsulated data.
 * @return Total packet size in bytes, or 0 if invalid.
 * @note RT-safe: Pure computation.
 */
[[nodiscard]] constexpr std::size_t requiredPacketSize(std::uint8_t headerVariant,
                                                       std::size_t payloadLen) noexcept {
  switch (headerVariant) {
  case EPP_HEADER_1_OCTET:
    return (payloadLen == 0) ? EPP_HEADER_1_OCTET : 0; // Idle has no payload
  case EPP_HEADER_2_OCTET:
    return EPP_HEADER_2_OCTET + payloadLen;
  case EPP_HEADER_4_OCTET:
    return EPP_HEADER_4_OCTET + payloadLen;
  case EPP_HEADER_8_OCTET:
    return EPP_HEADER_8_OCTET + payloadLen;
  default:
    return 0;
  }
}

} // namespace epp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_EPP_PACKER_HPP
