#ifndef APEX_PROTOCOLS_CCSDS_EPP_MUTABLE_MESSAGE_HPP
#define APEX_PROTOCOLS_CCSDS_EPP_MUTABLE_MESSAGE_HPP
/**
 * @file CcsdsEppMutableMessage.hpp
 * @brief Mutable, typed facade for assembling CCSDS EPP packets.
 *
 * RT-Safety:
 *  - No heap allocation (all templates use fixed-size arrays).
 *  - No std::function.
 *  - No exceptions in hot paths; factories, pack(), and packInto() return std::optional.
 *  - C++17-compatible via apex::compat::bytes_span.
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppMessagePacker.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace protocols {
namespace ccsds {
namespace epp {

/* ------------------------- MutableEppHeader -------------------------------- */

/**
 * @struct MutableEppHeader
 * @brief Field-only EPP header; serialize via MutableEppMessageT::pack()/packInto().
 *
 * @note RT-safe: POD struct.
 */
struct MutableEppHeader {
  std::uint8_t headerVariant = EPP_HEADER_2_OCTET; ///< 1, 2, 4, or 8
  std::uint8_t protocolId = 0;                     ///< 3-bit (0..7)
  std::uint8_t userDefined = 0;                    ///< 4-bit (0..15, for 4/8-octet headers)
  std::uint8_t protocolIde = 0;                    ///< 4-bit (0..15, for 4/8-octet headers)
  std::uint16_t ccsdsDefined = 0;                  ///< 16-bit (for 8-octet header only)
};

/* ---------------------------- MutableEppMessageT --------------------------- */

/**
 * @struct MutableEppMessageT
 * @brief Mutable descriptor that packs into a caller-provided buffer or fixed EppMsg.
 *
 * @tparam T Payload element type (trivially copyable, standard layout, contiguous).
 * @tparam MaxPacketSize Maximum packet size for pack() output.
 *
 * @note RT-safe: No heap allocation.
 */
template <typename T, std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH>
struct MutableEppMessageT {
  static_assert(std::is_trivially_copyable<T>::value,
                "MutableEppMessageT<T> requires T to be trivially copyable.");
  static_assert(std::is_standard_layout<T>::value,
                "MutableEppMessageT<T> requires T to be standard-layout.");

  MutableEppHeader hdr{};
  const T* payload = nullptr;   ///< base pointer (may be null if payloadCount==0)
  std::size_t payloadCount = 0; ///< number of T elements

  /**
   * @brief Pack into an RT-safe EppMsg (fixed-size storage).
   * @return std::nullopt on invalid inputs or size violations.
   * @note RT-safe: No heap allocation.
   */
  [[nodiscard]] std::optional<EppMsg<MaxPacketSize>> pack() const noexcept COMPAT_HOT {
    const std::size_t payloadBytes = payloadCount * sizeof(T);

    // Idle packets have no payload.
    if (hdr.headerVariant == EPP_HEADER_1_OCTET) {
      if (payloadBytes != 0)
        return std::nullopt;
      return EppMsg<MaxPacketSize>::createIdle(hdr.protocolId);
    }

    // For other variants, payload may be empty.
    apex::compat::bytes_span payloadSpan{};
    if (payloadBytes > 0) {
      if (payload == nullptr)
        return std::nullopt;
      payloadSpan =
          apex::compat::bytes_span{reinterpret_cast<const std::uint8_t*>(payload), payloadBytes};
    }

    switch (hdr.headerVariant) {
    case EPP_HEADER_2_OCTET:
      return EppMsg<MaxPacketSize>::create2Octet(hdr.protocolId, payloadSpan);
    case EPP_HEADER_4_OCTET:
      return EppMsg<MaxPacketSize>::create4Octet(hdr.protocolId, hdr.userDefined, hdr.protocolIde,
                                                 payloadSpan);
    case EPP_HEADER_8_OCTET:
      return EppMsg<MaxPacketSize>::create8Octet(hdr.protocolId, hdr.userDefined, hdr.protocolIde,
                                                 hdr.ccsdsDefined, payloadSpan);
    default:
      return std::nullopt;
    }
  }

  /**
   * @brief Zero-alloc writer: serialize into a caller-provided buffer.
   * @param out    Destination buffer (must be at least required total bytes).
   * @param outLen Capacity of @p out in bytes.
   * @return Number of bytes written on success; std::nullopt on invalid args or capacity too small.
   *
   * Layout: [Header(1/2/4/8)] [Payload]
   *
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] std::optional<std::size_t> packInto(std::uint8_t* out,
                                                    std::size_t outLen) const noexcept {
    if (out == nullptr)
      return std::nullopt;

    const std::size_t payloadBytes = payloadCount * sizeof(T);

    // Idle packets have no payload.
    if (hdr.headerVariant == EPP_HEADER_1_OCTET) {
      if (payloadBytes != 0)
        return std::nullopt;
      if (outLen < EPP_HEADER_1_OCTET)
        return std::nullopt;

      auto h = EppHeader::buildIdle(hdr.protocolId);
      if (!h)
        return std::nullopt;
      h->writeTo(out);
      return EPP_HEADER_1_OCTET;
    }

    // For other variants, we need the payload span.
    apex::compat::bytes_span payloadSpan{};
    if (payloadBytes > 0) {
      if (payload == nullptr)
        return std::nullopt;
      payloadSpan =
          apex::compat::bytes_span{reinterpret_cast<const std::uint8_t*>(payload), payloadBytes};
    }

    std::size_t bytesWritten = 0;
    const bool ok = packPacket(hdr.headerVariant, hdr.protocolId, hdr.userDefined, hdr.protocolIde,
                               hdr.ccsdsDefined, payloadSpan, out, outLen, bytesWritten);
    if (!ok)
      return std::nullopt;
    return bytesWritten;
  }

  /**
   * @brief Compute required packet size.
   * @return Total size in bytes, or 0 if invalid.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t requiredSize() const noexcept {
    const std::size_t payloadBytes = payloadCount * sizeof(T);
    return requiredPacketSize(hdr.headerVariant, payloadBytes);
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

/* ------------------------- MutableEppMessageFactory ------------------------ */

/**
 * @struct MutableEppMessageFactory
 * @brief Convenience builders for MutableEppMessageT (no exceptions).
 *
 * @note RT-safe: No heap allocation.
 */
struct MutableEppMessageFactory {
  /**
   * @brief Build from a single payload instance reference.
   */
  template <typename T, std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH>
  [[nodiscard]] static std::optional<MutableEppMessageT<T, MaxPacketSize>>
  build(std::uint8_t headerVariant, std::uint8_t protocolId, std::uint8_t userDefined,
        std::uint8_t protocolIde, std::uint16_t ccsdsDefined, const T& payloadInstance) noexcept {
    MutableEppMessageT<T, MaxPacketSize> msg;
    msg.hdr.headerVariant = headerVariant;
    msg.hdr.protocolId = protocolId;
    msg.hdr.userDefined = userDefined;
    msg.hdr.protocolIde = protocolIde;
    msg.hdr.ccsdsDefined = ccsdsDefined;

    msg.payload = &payloadInstance;
    msg.payloadCount = 1;

    return msg;
  }

  /**
   * @brief Build from a payload span.
   */
  template <typename T, std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH>
  [[nodiscard]] static std::optional<MutableEppMessageT<T, MaxPacketSize>>
  build(std::uint8_t headerVariant, std::uint8_t protocolId, std::uint8_t userDefined,
        std::uint8_t protocolIde, std::uint16_t ccsdsDefined,
        apex::compat::rospan<T> payloadSpan) noexcept {
    MutableEppMessageT<T, MaxPacketSize> msg;
    msg.hdr.headerVariant = headerVariant;
    msg.hdr.protocolId = protocolId;
    msg.hdr.userDefined = userDefined;
    msg.hdr.protocolIde = protocolIde;
    msg.hdr.ccsdsDefined = ccsdsDefined;

    msg.setPayload(payloadSpan);

    return msg;
  }

  /**
   * @brief Build from pointer + count.
   */
  template <typename T, std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH>
  [[nodiscard]] static std::optional<MutableEppMessageT<T, MaxPacketSize>>
  build(std::uint8_t headerVariant, std::uint8_t protocolId, std::uint8_t userDefined,
        std::uint8_t protocolIde, std::uint16_t ccsdsDefined, const T* payloadPtr,
        std::size_t payloadCount) noexcept {
    MutableEppMessageT<T, MaxPacketSize> msg;
    msg.hdr.headerVariant = headerVariant;
    msg.hdr.protocolId = protocolId;
    msg.hdr.userDefined = userDefined;
    msg.hdr.protocolIde = protocolIde;
    msg.hdr.ccsdsDefined = ccsdsDefined;

    msg.setPayload(payloadPtr, payloadCount);

    return msg;
  }

  /**
   * @brief Build an idle packet (no payload).
   */
  template <typename T = std::uint8_t, std::size_t MaxPacketSize = EPP_DEFAULT_MAX_PACKET_LENGTH>
  [[nodiscard]] static std::optional<MutableEppMessageT<T, MaxPacketSize>>
  buildIdle(std::uint8_t protocolId = EPP_PROTOCOL_ID_IDLE) noexcept {
    MutableEppMessageT<T, MaxPacketSize> msg;
    msg.hdr.headerVariant = EPP_HEADER_1_OCTET;
    msg.hdr.protocolId = protocolId;
    msg.payload = nullptr;
    msg.payloadCount = 0;

    return msg;
  }
};

} // namespace epp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_EPP_MUTABLE_MESSAGE_HPP
