#ifndef APEX_PROTOCOLS_CCSDS_SPP_MESSAGE_PACKER_HPP
#define APEX_PROTOCOLS_CCSDS_SPP_MESSAGE_PACKER_HPP
/**
 * @file CcsdsSppMessagePacker.hpp
 * @brief Immutable construction (packing) of CCSDS SPP packets.
 *
 * Layout:
 *   [ Primary Header (6B) ] [ Secondary Header? (0..N) ] [ User Data (0..N) ]
 *   // Constraint: (Secondary + User) >= 1 octet
 *
 * RT-Safety:
 *  - No heap allocation (all templates use fixed-size arrays).
 *  - No std::function (uses caller-provided buffers).
 *  - No exceptions in hot paths; factories return std::optional.
 *  - Validates Packet Data Length (PD) and total size.
 *  - C++17-compatible via apex::compat::bytes_span.
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring> // std::memcpy
#include <optional>

namespace protocols {
namespace ccsds {
namespace spp {

/* ----------------------------- SppPrimaryHeader ----------------------------- */

/**
 * @class SppPrimaryHeader
 * @brief Builder/serializer for the CCSDS SPP primary header (6 octets).
 *
 * For parsing, prefer SppPrimaryHeaderView from the viewer header.
 *
 * @note RT-safe: No heap allocation.
 */
class SppPrimaryHeader {
public:
  /**
   * @brief Build a primary header from field values.
   * @param version    3-bit version (0..7).
   * @param type       Packet type flag (0=telemetry, 1=telecommand).
   * @param secHdr     Secondary header present flag.
   * @param apid       11-bit APID (0..2047).
   * @param seqFlags   2-bit sequence flags (0..3).
   * @param seqCount   14-bit sequence count (0..16383).
   * @param pdLength   Packet Data Length field = (Data Field bytes) - 1 (0..65535).
   * @return SppPrimaryHeader on success; std::nullopt if a field is out of range.
   * @note RT-safe: No allocation.
   */
  static std::optional<SppPrimaryHeader> build(std::uint8_t version, bool type, bool secHdr,
                                               std::uint16_t apid, std::uint8_t seqFlags,
                                               std::uint16_t seqCount,
                                               std::uint16_t pdLength) noexcept COMPAT_HOT {
    if (COMPAT_UNLIKELY(version > SPP_VERSION_MASK))
      return std::nullopt;
    if (COMPAT_UNLIKELY(apid > SPP_APID_MASK))
      return std::nullopt;
    if (COMPAT_UNLIKELY(seqFlags > SPP_SEQFLAGS_MASK))
      return std::nullopt;
    if (COMPAT_UNLIKELY(seqCount > SPP_SEQCOUNT_MASK))
      return std::nullopt;
    // pdLength is 16-bit; any std::uint16_t fits 0..0xFFFF

    SppPrimaryHeader h;

    // Octet 0: version (bits 7:5), type (bit 4), secHdr (bit 3), APID[10:8] (bits 2:0)
    h.bytes_[0] = static_cast<std::uint8_t>(
        ((version & SPP_VERSION_MASK) << SPP_VERSION_SHIFT) | ((type ? 1u : 0u) << SPP_TYPE_SHIFT) |
        ((secHdr ? 1u : 0u) << SPP_SECHDR_SHIFT) | ((apid >> 8) & SPP_APID_UPPER_MASK3));

    // Octet 1: APID[7:0]
    h.bytes_[1] = static_cast<std::uint8_t>(apid & 0xFFu);

    // Octet 2: seqFlags (bits 7:6), seqCount[13:8] (bits 5:0)
    h.bytes_[2] = static_cast<std::uint8_t>(((seqFlags & SPP_SEQFLAGS_MASK) << SPP_SEQFLAGS_SHIFT) |
                                            ((seqCount >> 8) & SPP_SEQCOUNT_UPPER6_MASK));

    // Octet 3: seqCount[7:0]
    h.bytes_[3] = static_cast<std::uint8_t>(seqCount & 0xFFu);

    // Octets 4-5: Packet Data Length (big-endian)
    h.bytes_[4] = static_cast<std::uint8_t>((pdLength >> 8) & 0xFFu);
    h.bytes_[5] = static_cast<std::uint8_t>(pdLength & 0xFFu);

    return h;
  }

  /// Serialize into a 6-octet array.
  /// @note RT-safe: No allocation.
  [[nodiscard]] std::array<std::uint8_t, SPP_HDR_SIZE_BYTES> toArray() const noexcept {
    return bytes_;
  }

  /// Write the 6 octets into caller-provided buffer (must be >= 6).
  /// @note RT-safe: No allocation.
  void writeTo(std::uint8_t* out) const noexcept {
    out[0] = bytes_[0];
    out[1] = bytes_[1];
    out[2] = bytes_[2];
    out[3] = bytes_[3];
    out[4] = bytes_[4];
    out[5] = bytes_[5];
  }

  // Accessors (handy when packing)
  /// @note RT-safe.
  [[nodiscard]] std::uint8_t version() const noexcept {
    return static_cast<std::uint8_t>((bytes_[0] >> SPP_VERSION_SHIFT) & SPP_VERSION_MASK);
  }
  /// @note RT-safe.
  [[nodiscard]] bool type() const noexcept { return (bytes_[0] & SPP_TYPE_BIT_MASK) != 0; }
  /// @note RT-safe.
  [[nodiscard]] bool hasSecondaryHeader() const noexcept {
    return (bytes_[0] & SPP_SECHDR_BIT_MASK) != 0;
  }
  /// @note RT-safe.
  [[nodiscard]] std::uint16_t apid() const noexcept {
    return static_cast<std::uint16_t>(((bytes_[0] & SPP_APID_UPPER_MASK3) << 8) | bytes_[1]);
  }
  /// @note RT-safe.
  [[nodiscard]] std::uint8_t sequenceFlags() const noexcept {
    return static_cast<std::uint8_t>((bytes_[2] >> SPP_SEQFLAGS_SHIFT) & SPP_SEQFLAGS_MASK);
  }
  /// @note RT-safe.
  [[nodiscard]] std::uint16_t sequenceCount() const noexcept {
    return static_cast<std::uint16_t>(((bytes_[2] & SPP_SEQCOUNT_UPPER6_MASK) << 8) | bytes_[3]);
  }
  /// @note RT-safe.
  [[nodiscard]] std::uint16_t packetDataLength() const noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes_[4]) << 8) | bytes_[5]);
  }

private:
  SppPrimaryHeader() = default;
  std::array<std::uint8_t, SPP_HDR_SIZE_BYTES> bytes_{};
};

/* ---------------------------- SppSecondaryHeader ---------------------------- */

/**
 * @class SppSecondaryHeader
 * @brief RT-safe container for secondary header fields (Time Code, Ancillary).
 *
 * @tparam MaxTimeCode Maximum capacity for Time Code field (bytes).
 * @tparam MaxAncillary Maximum capacity for Ancillary Data field (bytes).
 *
 * @note RT-safe: No heap allocation. Fixed-size arrays.
 */
template <std::size_t MaxTimeCode = 16, std::size_t MaxAncillary = 64> class SppSecondaryHeader {
public:
  SppSecondaryHeader() = default;

  /**
   * @brief Create from field spans.
   * @param timeCode Time Code bytes (copied if fits).
   * @param ancillary Ancillary Data bytes (copied if fits).
   * @return Populated header, or std::nullopt if either exceeds capacity.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] static std::optional<SppSecondaryHeader>
  fromFields(apex::compat::bytes_span timeCode, apex::compat::bytes_span ancillary) noexcept {
    if (timeCode.size() > MaxTimeCode || ancillary.size() > MaxAncillary) {
      return std::nullopt;
    }
    SppSecondaryHeader h;
    if (timeCode.size() > 0) {
      std::memcpy(h.timeCode_.data(), timeCode.data(), timeCode.size());
      h.timeCodeLen_ = timeCode.size();
    }
    if (ancillary.size() > 0) {
      std::memcpy(h.ancillary_.data(), ancillary.data(), ancillary.size());
      h.ancillaryLen_ = ancillary.size();
    }
    return h;
  }

  /**
   * @brief Create from ancillary data only.
   * @param ancillary Ancillary Data bytes.
   * @return Populated header, or std::nullopt if exceeds capacity.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] static std::optional<SppSecondaryHeader>
  fromAncillaryOnly(apex::compat::bytes_span ancillary) noexcept {
    return fromFields(apex::compat::bytes_span{}, ancillary);
  }

  /// @note RT-safe.
  [[nodiscard]] bool hasTimeCode() const noexcept { return timeCodeLen_ > 0; }
  /// @note RT-safe.
  [[nodiscard]] bool hasAncillaryData() const noexcept { return ancillaryLen_ > 0; }
  /// @note RT-safe.
  [[nodiscard]] std::size_t length() const noexcept { return timeCodeLen_ + ancillaryLen_; }

  /**
   * @brief Write secondary header bytes to caller buffer.
   * @param out Destination buffer (must have capacity >= length()).
   * @return Number of bytes written.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] std::size_t writeTo(std::uint8_t* out) const noexcept {
    std::size_t w = 0;
    if (timeCodeLen_ > 0) {
      std::memcpy(out + w, timeCode_.data(), timeCodeLen_);
      w += timeCodeLen_;
    }
    if (ancillaryLen_ > 0) {
      std::memcpy(out + w, ancillary_.data(), ancillaryLen_);
      w += ancillaryLen_;
    }
    return w;
  }

  /// @note RT-safe.
  [[nodiscard]] apex::compat::bytes_span timeCode() const noexcept {
    return apex::compat::bytes_span{timeCode_.data(), timeCodeLen_};
  }
  /// @note RT-safe.
  [[nodiscard]] apex::compat::bytes_span ancillaryData() const noexcept {
    return apex::compat::bytes_span{ancillary_.data(), ancillaryLen_};
  }

  /// @note RT-safe.
  [[nodiscard]] static constexpr std::size_t maxTimeCodeCapacity() noexcept { return MaxTimeCode; }
  /// @note RT-safe.
  [[nodiscard]] static constexpr std::size_t maxAncillaryCapacity() noexcept {
    return MaxAncillary;
  }

private:
  std::array<std::uint8_t, MaxTimeCode> timeCode_{};
  std::size_t timeCodeLen_ = 0;
  std::array<std::uint8_t, MaxAncillary> ancillary_{};
  std::size_t ancillaryLen_ = 0;
};

/// Default secondary header type (16B time code, 64B ancillary).
using SppSecondaryHeaderDefault = SppSecondaryHeader<16, 64>;

/* --------------------------------- SppMsg --------------------------------- */

/**
 * @class SppMsg
 * @brief RT-safe SPP packet with fixed-size storage.
 *
 * @tparam MaxPacketSize Maximum packet size (default MAX_SPP_PACKET_LENGTH).
 *
 * @note RT-safe: No heap allocation. Fixed-size array.
 */
template <std::size_t MaxPacketSize = MAX_SPP_PACKET_LENGTH> class SppMsg {
  static_assert(MaxPacketSize >= SPP_HDR_SIZE_BYTES + 1, "MaxPacketSize must hold minimum packet");

public:
  /**
   * @brief Create a packet from fields with optional secondary header.
   * @param version     3-bit version (0..7).
   * @param type        Packet type (0 TM, 1 TC).
   * @param apid        11-bit APID.
   * @param seqFlags    2-bit sequence flags.
   * @param seqCount    14-bit sequence count.
   * @param timeCode    Optional Time Code field (secondary header component).
   * @param ancillary   Optional Ancillary Data field (secondary header component).
   * @param userData    User data bytes. May be empty if secondary header contributes data.
   * @return SppMsg on success; std::nullopt on invalid input or size limit exceeded.
   * @note RT-safe: No allocation.
   */
  [[nodiscard]] static std::optional<SppMsg>
  create(std::uint8_t version, bool type, std::uint16_t apid, std::uint8_t seqFlags,
         std::uint16_t seqCount, apex::compat::bytes_span timeCode = {},
         apex::compat::bytes_span ancillary = {},
         apex::compat::bytes_span userData = {}) noexcept COMPAT_HOT {
    const std::size_t secLen = timeCode.size() + ancillary.size();
    const bool hasSec = (secLen != 0u);
    const bool hasUser = (userData.size() != 0u);

    // Packet Data Field must be >= 1 octet total (secondary and/or user).
    if (COMPAT_UNLIKELY(!hasSec && !hasUser))
      return std::nullopt;

    const std::size_t dataBytes = secLen + userData.size(); // >= 1 by check above

    // PD = (dataBytes - 1), must fit 16 bits
    if (COMPAT_UNLIKELY((dataBytes - 1u) > SPP_PDL_MAX))
      return std::nullopt;

    const std::uint16_t pdLength = static_cast<std::uint16_t>(dataBytes - 1u);

    auto ph = SppPrimaryHeader::build(version, type, /*secHdrPresent*/ hasSec, apid, seqFlags,
                                      seqCount, pdLength);
    if (COMPAT_UNLIKELY(!ph.has_value()))
      return std::nullopt;

    const std::size_t totalSize = SPP_HDR_SIZE_BYTES + dataBytes;
    if (COMPAT_UNLIKELY(totalSize > MaxPacketSize))
      return std::nullopt;

    // Assemble final buffer
    SppMsg msg;
    msg.len_ = totalSize;

    // Write primary header
    ph->writeTo(msg.bytes_.data());

    // Write secondary header (if any)
    std::size_t w = SPP_HDR_SIZE_BYTES;
    if (hasSec) {
      if (timeCode.size()) {
        std::memcpy(msg.bytes_.data() + w, timeCode.data(), timeCode.size());
        w += timeCode.size();
      }
      if (ancillary.size()) {
        std::memcpy(msg.bytes_.data() + w, ancillary.data(), ancillary.size());
        w += ancillary.size();
      }
    }

    // Write user data (if any)
    if (hasUser) {
      std::memcpy(msg.bytes_.data() + w, userData.data(), userData.size());
    }

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
  SppMsg() = default;
  std::array<std::uint8_t, MaxPacketSize> bytes_{};
  std::size_t len_ = 0;
};

/// Default SppMsg type (4KB capacity).
using SppMsgDefault = SppMsg<MAX_SPP_PACKET_LENGTH>;

/// Small SppMsg type (256B capacity).
using SppMsgSmall = SppMsg<256>;

/// Large SppMsg type (8KB capacity).
using SppMsgLarge = SppMsg<8192>;

/* ----------------------------- RT-Safe Packer ----------------------------- */

/**
 * @brief RT-safe packet packing directly into caller buffer.
 *
 * @param version     3-bit version (0..7).
 * @param type        Packet type (0 TM, 1 TC).
 * @param apid        11-bit APID.
 * @param seqFlags    2-bit sequence flags.
 * @param seqCount    14-bit sequence count.
 * @param timeCode    Optional Time Code field.
 * @param ancillary   Optional Ancillary Data field.
 * @param userData    User data bytes.
 * @param outBuf      Destination buffer.
 * @param outCapacity Capacity of outBuf.
 * @param bytesWritten Output: number of bytes written on success.
 * @return true on success, false on invalid input or insufficient capacity.
 * @note RT-safe: No allocation. Zero-copy write.
 */
[[nodiscard]] COMPAT_HOT inline bool
packPacket(std::uint8_t version, bool type, std::uint16_t apid, std::uint8_t seqFlags,
           std::uint16_t seqCount, apex::compat::bytes_span timeCode,
           apex::compat::bytes_span ancillary, apex::compat::bytes_span userData,
           std::uint8_t* outBuf, std::size_t outCapacity, std::size_t& bytesWritten) noexcept {
  const std::size_t secLen = timeCode.size() + ancillary.size();
  const bool hasSec = (secLen != 0u);
  const bool hasUser = (userData.size() != 0u);

  // Packet Data Field must be >= 1 octet total
  if (COMPAT_UNLIKELY(!hasSec && !hasUser))
    return false;

  const std::size_t dataBytes = secLen + userData.size();

  // PD = (dataBytes - 1), must fit 16 bits
  if (COMPAT_UNLIKELY((dataBytes - 1u) > SPP_PDL_MAX))
    return false;

  const std::size_t totalSize = SPP_HDR_SIZE_BYTES + dataBytes;
  if (COMPAT_UNLIKELY(totalSize > outCapacity))
    return false;

  const std::uint16_t pdLength = static_cast<std::uint16_t>(dataBytes - 1u);

  auto ph = SppPrimaryHeader::build(version, type, hasSec, apid, seqFlags, seqCount, pdLength);
  if (COMPAT_UNLIKELY(!ph.has_value()))
    return false;

  // Write primary header
  ph->writeTo(outBuf);

  // Write secondary header (if any)
  std::size_t w = SPP_HDR_SIZE_BYTES;
  if (hasSec) {
    if (timeCode.size()) {
      std::memcpy(outBuf + w, timeCode.data(), timeCode.size());
      w += timeCode.size();
    }
    if (ancillary.size()) {
      std::memcpy(outBuf + w, ancillary.data(), ancillary.size());
      w += ancillary.size();
    }
  }

  // Write user data (if any)
  if (hasUser) {
    std::memcpy(outBuf + w, userData.data(), userData.size());
  }

  bytesWritten = totalSize;
  return true;
}

/**
 * @brief Compute required packet size.
 * @param timeCodeLen Length of Time Code field.
 * @param ancillaryLen Length of Ancillary Data field.
 * @param userDataLen Length of User Data field.
 * @return Total packet size in bytes, or 0 if invalid (empty data field).
 * @note RT-safe: Pure computation.
 */
[[nodiscard]] constexpr std::size_t requiredPacketSize(std::size_t timeCodeLen,
                                                       std::size_t ancillaryLen,
                                                       std::size_t userDataLen) noexcept {
  const std::size_t dataBytes = timeCodeLen + ancillaryLen + userDataLen;
  if (dataBytes == 0)
    return 0;
  return SPP_HDR_SIZE_BYTES + dataBytes;
}

} // namespace spp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_SPP_MESSAGE_PACKER_HPP
