#ifndef APEX_PROTOCOLS_CCSDS_SPP_MUTABLE_MESSAGE_HPP
#define APEX_PROTOCOLS_CCSDS_SPP_MUTABLE_MESSAGE_HPP
/**
 * @file CcsdsSppMutableMessage.hpp
 * @brief Mutable, typed facade for assembling CCSDS SPP packets.
 *
 * RT-Safety:
 *  - No heap allocation (all templates use fixed-size arrays).
 *  - No std::function.
 *  - No exceptions in hot paths; factories, pack(), and packInto() return std::optional.
 *  - C++17-compatible via apex::compat::bytes_span / apex::compat::rospan.
 *  - Packet Data Field may be secondary-only, user-only, or both (>=1 octet total).
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppMessagePacker.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring> // std::memcpy
#include <optional>
#include <type_traits>

namespace protocols {
namespace ccsds {
namespace spp {

/* ------------------------- MutableSppPrimaryHeader ------------------------- */

/**
 * @struct MutableSppPrimaryHeader
 * @brief Field-only primary header; serialize via MutableSppMessageT::pack()/packInto().
 *
 * Note: The effective Secondary Header flag in the final packet is derived from
 * the presence of secondary bytes (timeCode/ancillary) at pack time.
 *
 * @note RT-safe: POD struct.
 */
struct MutableSppPrimaryHeader {
  std::uint8_t version = 0;         ///< 3-bit (0..7)
  bool type = false;                ///< 0=TM, 1=TC
  bool secondaryHeaderFlag = false; ///< Informational only; final flag follows actual bytes
  std::uint16_t apid = 0;           ///< 11-bit (0..2047)
  std::uint8_t seqFlags = 0;        ///< 2-bit (0..3)
  std::uint16_t seqCount = 0;       ///< 14-bit (0..16383)
};

/* ------------------------ MutableSppSecondaryHeader ------------------------ */

/**
 * @struct MutableSppSecondaryHeader
 * @brief RT-safe storage for Time Code and Ancillary secondary fields.
 *
 * @tparam MaxTimeCode Maximum capacity for Time Code field (bytes).
 * @tparam MaxAncillary Maximum capacity for Ancillary Data field (bytes).
 *
 * @note RT-safe: No heap allocation. Fixed-size arrays.
 */
template <std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64>
struct MutableSppSecondaryHeader {
  std::array<std::uint8_t, MaxTimeCode> timeCode{};
  std::size_t timeCodeLen = 0;
  std::array<std::uint8_t, MaxAncillary> ancillaryData{};
  std::size_t ancillaryLen = 0;

  /// @note RT-safe.
  [[nodiscard]] std::size_t length() const noexcept { return timeCodeLen + ancillaryLen; }

  /// @note RT-safe.
  [[nodiscard]] apex::compat::bytes_span timeCodeSpan() const noexcept {
    return apex::compat::bytes_span{timeCode.data(), timeCodeLen};
  }

  /// @note RT-safe.
  [[nodiscard]] apex::compat::bytes_span ancillarySpan() const noexcept {
    return apex::compat::bytes_span{ancillaryData.data(), ancillaryLen};
  }

  /**
   * @brief Set time code from span.
   * @param src Source bytes.
   * @return true on success, false if exceeds capacity.
   * @note RT-safe.
   */
  [[nodiscard]] bool setTimeCode(apex::compat::bytes_span src) noexcept {
    if (src.size() > MaxTimeCode)
      return false;
    if (src.size() > 0) {
      std::memcpy(timeCode.data(), src.data(), src.size());
    }
    timeCodeLen = src.size();
    return true;
  }

  /**
   * @brief Set ancillary data from span.
   * @param src Source bytes.
   * @return true on success, false if exceeds capacity.
   * @note RT-safe.
   */
  [[nodiscard]] bool setAncillary(apex::compat::bytes_span src) noexcept {
    if (src.size() > MaxAncillary)
      return false;
    if (src.size() > 0) {
      std::memcpy(ancillaryData.data(), src.data(), src.size());
    }
    ancillaryLen = src.size();
    return true;
  }

  /// @note RT-safe.
  [[nodiscard]] static constexpr std::size_t maxTimeCodeCapacity() noexcept { return MaxTimeCode; }

  /// @note RT-safe.
  [[nodiscard]] static constexpr std::size_t maxAncillaryCapacity() noexcept {
    return MaxAncillary;
  }
};

/// Default secondary header type (16B time code, 64B ancillary).
using MutableSppSecondaryHeaderDefault = MutableSppSecondaryHeader<16, 64>;

/* ---------------------------- MutableSppMessageT --------------------------- */

/**
 * @struct MutableSppMessageT
 * @brief Mutable descriptor that packs into a caller-provided buffer or fixed SppMsg.
 *
 * @tparam T Payload element type (trivially copyable, standard layout, contiguous).
 * @tparam MaxPacketSize Maximum packet size for pack() output.
 * @tparam MaxTimeCode Maximum time code capacity.
 * @tparam MaxAncillary Maximum ancillary capacity.
 *
 * @note RT-safe: No heap allocation.
 */
template <typename T, std::size_t MaxPacketSize = MAX_SPP_PACKET_LENGTH,
          std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64>
struct MutableSppMessageT {
  static_assert(std::is_trivially_copyable<T>::value,
                "MutableSppMessageT<T> requires T to be trivially copyable.");
  static_assert(std::is_standard_layout<T>::value,
                "MutableSppMessageT<T> requires T to be standard-layout.");

  MutableSppPrimaryHeader priHdr{};
  std::optional<MutableSppSecondaryHeader<MaxTimeCode, MaxAncillary>> secHdr{};
  const T* payload = nullptr;   ///< base pointer (may be null if payloadCount==0)
  std::size_t payloadCount = 0; ///< number of T elements

  /**
   * @brief Pack into an RT-safe SppMsg (fixed-size storage).
   * @return std::nullopt on invalid inputs or size violations.
   * @note RT-safe: No heap allocation.
   */
  [[nodiscard]] std::optional<SppMsg<MaxPacketSize>> pack() const noexcept COMPAT_HOT {
    const std::size_t secLen = secHdr ? secHdr->length() : 0u;
    const std::size_t payloadBytes = payloadCount * sizeof(T);
    const bool hasUser = (payloadBytes != 0u);
    const bool hasSecondary = (secLen != 0u);

    // Must have at least one octet in the Packet Data Field.
    if (!hasUser && !hasSecondary)
      return std::nullopt;

    // If user data present, require non-null pointer; otherwise allow nullptr.
    if (hasUser && payload == nullptr)
      return std::nullopt;

    const std::size_t dataBytes = secLen + payloadBytes; // >= 1 by above check
    if ((dataBytes - 1u) > SPP_PDL_MAX)
      return std::nullopt;

    const std::size_t total = SPP_HDR_SIZE_BYTES + dataBytes;
    if (total > MaxPacketSize)
      return std::nullopt;

    apex::compat::bytes_span timeCode{};
    apex::compat::bytes_span ancillary{};
    if (hasSecondary) {
      if (secHdr && secHdr->timeCodeLen > 0)
        timeCode = secHdr->timeCodeSpan();
      if (secHdr && secHdr->ancillaryLen > 0)
        ancillary = secHdr->ancillarySpan();
    }

    apex::compat::bytes_span userData{};
    if (hasUser) {
      const auto* userPtr = reinterpret_cast<const std::uint8_t*>(payload);
      userData = apex::compat::bytes_span{userPtr, payloadBytes};
    }

    return SppMsg<MaxPacketSize>::create(priHdr.version, priHdr.type, priHdr.apid, priHdr.seqFlags,
                                         priHdr.seqCount, timeCode, ancillary, userData);
  }

  /**
   * @brief Zero-alloc writer: serialize into a caller-provided buffer.
   * @param out    Destination buffer (must be at least required total bytes).
   * @param outLen Capacity of @p out in bytes.
   * @return Number of bytes written on success; std::nullopt on invalid args or capacity too small.
   *
   * Layout: [Primary(6)] [Secondary?] [User]
   *
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] std::optional<std::size_t> packInto(std::uint8_t* out,
                                                    std::size_t outLen) const noexcept {
    if (out == nullptr)
      return std::nullopt;

    const std::size_t secLen = secHdr ? secHdr->length() : 0u;
    const std::size_t payloadBytes = payloadCount * sizeof(T);
    const bool hasUser = (payloadBytes != 0u);
    const bool hasSecondary = (secLen != 0u);

    if (!hasUser && !hasSecondary)
      return std::nullopt;
    if (hasUser && payload == nullptr)
      return std::nullopt;

    const std::size_t dataBytes = secLen + payloadBytes; // >= 1
    if ((dataBytes - 1u) > SPP_PDL_MAX)
      return std::nullopt;

    const std::size_t total = SPP_HDR_SIZE_BYTES + dataBytes;
    if (total > MAX_SPP_PACKET_LENGTH || outLen < total)
      return std::nullopt;

    // Build primary header with PD = dataBytes - 1
    const std::uint16_t pd = static_cast<std::uint16_t>(dataBytes - 1u);
    auto phOpt = spp::SppPrimaryHeader::build(priHdr.version, priHdr.type,
                                              /*secHdrPresent*/ hasSecondary, priHdr.apid,
                                              priHdr.seqFlags, priHdr.seqCount, pd);
    if (!phOpt)
      return std::nullopt;

    // Write primary
    phOpt->writeTo(out);

    // Write secondary (if any)
    std::size_t w = SPP_HDR_SIZE_BYTES;
    if (hasSecondary) {
      if (secHdr && secHdr->timeCodeLen > 0) {
        std::memcpy(out + w, secHdr->timeCode.data(), secHdr->timeCodeLen);
        w += secHdr->timeCodeLen;
      }
      if (secHdr && secHdr->ancillaryLen > 0) {
        std::memcpy(out + w, secHdr->ancillaryData.data(), secHdr->ancillaryLen);
        w += secHdr->ancillaryLen;
      }
    }

    // Write user (if any)
    if (hasUser) {
      std::memcpy(out + w, reinterpret_cast<const std::uint8_t*>(payload), payloadBytes);
      w += payloadBytes;
    }

    return w; // == total
  }

  /**
   * @brief Compute required packet size.
   * @return Total size in bytes, or 0 if invalid (empty data field).
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t requiredSize() const noexcept {
    const std::size_t secLen = secHdr ? secHdr->length() : 0u;
    const std::size_t payloadBytes = payloadCount * sizeof(T);
    const std::size_t dataBytes = secLen + payloadBytes;
    if (dataBytes == 0)
      return 0;
    return SPP_HDR_SIZE_BYTES + dataBytes;
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

/* ------------------------- MutableSppMessageFactory ------------------------ */

/**
 * @struct MutableSppMessageFactory
 * @brief Convenience builders for MutableSppMessageT (no exceptions).
 *
 * Note: Secondary-only packets are valid. Passing an empty payload with a non-empty
 * secondary header will succeed. Passing both empty will fail.
 *
 * @note RT-safe: No heap allocation.
 */
struct MutableSppMessageFactory {
  /**
   * @brief Build from a single payload instance reference.
   */
  template <typename T, std::size_t MaxPacketSize = MAX_SPP_PACKET_LENGTH,
            std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64>
  [[nodiscard]] static std::optional<
      MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary>>
  build(bool includeSecondary, std::uint8_t version, bool type, std::uint16_t apid,
        std::uint8_t seqFlags, std::uint16_t seqCount, const T& payloadInstance,
        const std::optional<MutableSppSecondaryHeader<MaxTimeCode, MaxAncillary>>& secondaryHeader =
            std::nullopt) noexcept {
    MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary> msg;
    msg.priHdr.version = version;
    msg.priHdr.type = type;
    msg.priHdr.apid = apid;
    msg.priHdr.seqFlags = seqFlags;
    msg.priHdr.seqCount = seqCount;
    msg.priHdr.secondaryHeaderFlag = includeSecondary;

    msg.payload = &payloadInstance;
    msg.payloadCount = 1;

    if (includeSecondary && secondaryHeader)
      msg.secHdr = *secondaryHeader;
    // Payload may be empty only when a secondary header contributes data.
    if (msg.payload == nullptr || msg.payloadCount == 0) {
      const bool hasSec = msg.secHdr && (msg.secHdr->length() != 0u);
      if (!hasSec)
        return std::nullopt;
    }
    return msg;
  }

  /**
   * @brief Build from a payload span.
   */
  template <typename T, std::size_t MaxPacketSize = MAX_SPP_PACKET_LENGTH,
            std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64>
  [[nodiscard]] static std::optional<
      MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary>>
  build(bool includeSecondary, std::uint8_t version, bool type, std::uint16_t apid,
        std::uint8_t seqFlags, std::uint16_t seqCount, apex::compat::rospan<T> payloadSpan,
        const std::optional<MutableSppSecondaryHeader<MaxTimeCode, MaxAncillary>>& secondaryHeader =
            std::nullopt) noexcept {
    MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary> msg;
    msg.priHdr.version = version;
    msg.priHdr.type = type;
    msg.priHdr.apid = apid;
    msg.priHdr.seqFlags = seqFlags;
    msg.priHdr.seqCount = seqCount;
    msg.priHdr.secondaryHeaderFlag = includeSecondary;

    msg.setPayload(payloadSpan); // may be empty
    if (includeSecondary && secondaryHeader)
      msg.secHdr = *secondaryHeader;

    // Accept empty span only if secondary header contributes bytes.
    if ((msg.payloadCount == 0) && !(msg.secHdr && msg.secHdr->length() != 0u)) {
      return std::nullopt;
    }
    return msg;
  }

  /**
   * @brief Build from pointer + count.
   */
  template <typename T, std::size_t MaxPacketSize = MAX_SPP_PACKET_LENGTH,
            std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64>
  [[nodiscard]] static std::optional<
      MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary>>
  build(bool includeSecondary, std::uint8_t version, bool type, std::uint16_t apid,
        std::uint8_t seqFlags, std::uint16_t seqCount, const T* payloadPtr,
        std::size_t payloadCount,
        const std::optional<MutableSppSecondaryHeader<MaxTimeCode, MaxAncillary>>& secondaryHeader =
            std::nullopt) noexcept {
    MutableSppMessageT<T, MaxPacketSize, MaxTimeCode, MaxAncillary> msg;
    msg.priHdr.version = version;
    msg.priHdr.type = type;
    msg.priHdr.apid = apid;
    msg.priHdr.seqFlags = seqFlags;
    msg.priHdr.seqCount = seqCount;
    msg.priHdr.secondaryHeaderFlag = includeSecondary;

    msg.setPayload(payloadPtr, payloadCount); // payloadPtr may be null iff count==0
    if (includeSecondary && secondaryHeader)
      msg.secHdr = *secondaryHeader;

    // Accept empty payload only if secondary header contributes bytes.
    if ((msg.payloadCount == 0) && !(msg.secHdr && msg.secHdr->length() != 0u)) {
      return std::nullopt;
    }
    return msg;
  }
};

} // namespace spp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_SPP_MUTABLE_MESSAGE_HPP
