#ifndef APEX_PROTOCOLS_CCSDS_EPP_VIEWER_HPP
#define APEX_PROTOCOLS_CCSDS_EPP_VIEWER_HPP
/**
 * @file CcsdsEppViewer.hpp
 * @brief Lightweight zero-copy viewer for CCSDS EPP packets.
 *
 * Design goals:
 *  - Zero-copy viewing (holds spans, not copies)
 *  - Parse-on-demand (only parse fields when accessed)
 *  - RT-friendly (no exceptions, no heap allocations)
 *  - Clean API with method notation
 *  - Fast-path helpers for packet routing
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 *
 * RT-Safety: All functions are RT-safe (no allocation, no exceptions).
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppCommonDefs.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace protocols {
namespace ccsds {
namespace epp {

/* ============================= Validation Errors =========================== */

/**
 * @enum ValidationError
 * @brief Detailed validation error codes.
 */
enum class ValidationError : std::uint8_t {
  OK = 0,           ///< Validation passed
  PACKET_TOO_SMALL, ///< Packet smaller than minimum (1 byte)
  INVALID_VERSION,  ///< Version is not 7 (EPP_VALID_VERSION)
  INVALID_LOL,      ///< Length of Length field is invalid
  LENGTH_MISMATCH,  ///< Packet size doesn't match header's packet length
  LENGTH_OVER_MAX,  ///< Packet length exceeds MAX_EPP_PACKET_LENGTH
  HEADER_INCOMPLETE ///< Not enough bytes for header variant
};

/**
 * @brief Convert validation error to human-readable string.
 * @param err Error code.
 * @return String literal describing the error.
 * @note RT-safe: Returns static string literal.
 */
const char* toString(ValidationError err) noexcept;

/* ========================== Header View (Zero-Copy) ========================= */

/**
 * @struct EppHeaderView
 * @brief Zero-copy view of EPP header fields with parse-on-demand.
 *
 * Per CCSDS 133.1-B-3 Section 4.1.2, the EPP header has variable length
 * (1, 2, 4, or 8 octets) determined by the Length of Length (LoL) field.
 *
 * @note RT-safe: No heap allocation. Holds span reference only.
 */
struct EppHeaderView {
  apex::compat::bytes_span raw; ///< Header bytes (1, 2, 4, or 8 bytes)

  /**
   * @brief Get header length in octets.
   * @return Header size (1, 2, 4, or 8).
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t headerLength() const noexcept { return raw.size(); }

  /**
   * @brief Get Packet Version Number (3 bits).
   * Per CCSDS 133.1-B-3 Section 4.1.2.2.2: Must be '111' = 7.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint8_t version() const noexcept {
    return (raw[0] >> EPP_VERSION_SHIFT) & EPP_VERSION_MASK_3BIT;
  }

  /**
   * @brief Get Encapsulation Protocol ID (3 bits).
   * Per CCSDS 133.1-B-3 Section 4.1.2.3.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint8_t protocolId() const noexcept {
    return (raw[0] >> EPP_PROTOCOL_ID_SHIFT) & EPP_PROTOCOL_ID_MASK_3BIT;
  }

  /**
   * @brief Get Length of Length field (2 bits).
   * Per CCSDS 133.1-B-3 Section 4.1.2.4.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint8_t lengthOfLength() const noexcept { return raw[0] & EPP_LOL_MASK; }

  /**
   * @brief Get User Defined field (4 bits, 4/8-octet headers only).
   * Per CCSDS 133.1-B-3 Section 4.1.2.5.
   * @return 0 for 1/2-octet headers.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint8_t userDefined() const noexcept {
    if (raw.size() < EPP_HEADER_4_OCTET)
      return 0;
    return (raw[1] >> EPP_USER_DEFINED_SHIFT) & EPP_USER_DEFINED_MASK_4BIT;
  }

  /**
   * @brief Get Protocol ID Extension field (4 bits, 4/8-octet headers only).
   * Per CCSDS 133.1-B-3 Section 4.1.2.6.
   * @return 0 for 1/2-octet headers.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint8_t protocolIdExtension() const noexcept {
    if (raw.size() < EPP_HEADER_4_OCTET)
      return 0;
    return raw[1] & EPP_PROTOCOL_IDE_MASK;
  }

  /**
   * @brief Get CCSDS Defined field (16 bits, 8-octet header only).
   * Per CCSDS 133.1-B-3 Section 4.1.2.7.
   * @return 0 for 1/2/4-octet headers.
   * @note RT-safe.
   */
  [[nodiscard]] std::uint16_t ccsdsDefined() const noexcept {
    if (raw.size() < EPP_HEADER_8_OCTET)
      return 0;
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(raw[2]) << 8) | raw[3]);
  }

  /**
   * @brief Get Packet Length field value (raw value from header).
   * Per CCSDS 133.1-B-3 Section 4.1.2.8.
   * For LoL=00 (idle), returns 0 (no length field present).
   * @note RT-safe.
   */
  [[nodiscard]] std::uint32_t packetLengthField() const noexcept {
    switch (raw.size()) {
    case EPP_HEADER_1_OCTET:
      return 0; // Idle packet has no length field
    case EPP_HEADER_2_OCTET:
      return raw[1];
    case EPP_HEADER_4_OCTET:
      return static_cast<std::uint16_t>((static_cast<std::uint16_t>(raw[2]) << 8) | raw[3]);
    case EPP_HEADER_8_OCTET:
      return (static_cast<std::uint32_t>(raw[4]) << 24) |
             (static_cast<std::uint32_t>(raw[5]) << 16) |
             (static_cast<std::uint32_t>(raw[6]) << 8) | raw[7];
    default:
      return 0;
    }
  }

  /**
   * @brief Check if this is an idle packet (LoL=00, 1-octet header).
   * Per CCSDS 133.1-B-3 Section 4.1.2.3 Note 1.
   * @note RT-safe.
   */
  [[nodiscard]] bool isIdle() const noexcept {
    return lengthOfLength() == EPP_LOL_IDLE && protocolId() == EPP_PROTOCOL_ID_IDLE;
  }

  /**
   * @brief Create header view from packet bytes.
   * @param data Packet bytes (at least 1 byte required).
   * @return Header view on success, nullopt if insufficient data.
   * @note RT-safe.
   */
  static std::optional<EppHeaderView> create(apex::compat::bytes_span data) noexcept;
};

/* ============================ Packet Viewer ================================ */

/**
 * @class PacketViewer
 * @brief Zero-copy viewer for complete EPP packets.
 *
 * Provides structured access to all packet components per CCSDS 133.1-B-3:
 *  - Header fields (viewer.hdr.version(), viewer.hdr.protocolId(), etc.)
 *  - Encapsulated data (payload)
 *
 * @note RT-safe: No heap allocation. Holds span references only.
 */
class PacketViewer {
public:
  // ========================== Nested Views ===================================

  EppHeaderView hdr; ///< Header view (always present)

  // ========================== Raw Packet Access ==============================

  apex::compat::bytes_span raw; ///< Full packet bytes (zero-copy reference)

  // ========================== Payload Access =================================

  /**
   * @brief Get encapsulated data (payload after header).
   * Per CCSDS 133.1-B-3 Section 4.1.3.
   * @return Payload bytes view (empty for idle packets).
   * @note RT-safe.
   */
  [[nodiscard]] apex::compat::bytes_span encapsulatedData() const noexcept COMPAT_HOT {
    const std::size_t hdrLen = hdr.headerLength();
    if (raw.size() <= hdrLen)
      return {};
    return raw.subspan(hdrLen);
  }

  /**
   * @brief Get total packet length.
   * For idle packets (LoL=00), this is 1.
   * For other packets, this is the Packet Length field value.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t packetLength() const noexcept {
    if (hdr.lengthOfLength() == EPP_LOL_IDLE)
      return 1;
    return hdr.packetLengthField();
  }

  // ========================== Construction ===================================

  /**
   * @brief Create packet viewer with validation.
   *
   * Validates per CCSDS 133.1-B-3:
   *  - Section 4.1.2.2.2: Version must be 7
   *  - Section 4.1.2.4: LoL determines header size
   *  - Section 4.1.2.8: Packet length matches actual size
   *
   * @param bytes Full packet bytes (non-owning view).
   * @return Viewer on success, nullopt on validation failure.
   * @note RT-safe.
   */
  static std::optional<PacketViewer> create(apex::compat::bytes_span bytes) noexcept COMPAT_HOT;

  /**
   * @brief Detailed validation (diagnostic use).
   * @param bytes Full packet bytes.
   * @return OK on success, specific error code on failure.
   * @note RT-safe.
   */
  static ValidationError validate(apex::compat::bytes_span bytes) noexcept;

  // ========================== Fast-Path Helpers ==============================

  /**
   * @brief Peek Protocol ID without full packet parsing.
   *
   * Extracts Protocol ID from first octet with minimal overhead.
   * Validates minimum packet size (1 byte) only.
   *
   * @param bytes Packet bytes.
   * @return Protocol ID (3 bits, 0x00 to 0x07) if valid, nullopt if empty.
   * @note RT-safe.
   */
  static std::optional<std::uint8_t>
  peekProtocolId(apex::compat::bytes_span bytes) noexcept COMPAT_HOT;

  /**
   * @brief Peek Length of Length without full packet parsing.
   * @param bytes Packet bytes.
   * @return LoL (2 bits, 0x00 to 0x03) if valid, nullopt if empty.
   * @note RT-safe.
   */
  static std::optional<std::uint8_t> peekLoL(apex::compat::bytes_span bytes) noexcept COMPAT_HOT;

  /**
   * @brief Compute expected header length from LoL value.
   * @param lol Length of Length field (0-3).
   * @return Header size in octets (1, 2, 4, or 8).
   * @note RT-safe.
   */
  static constexpr std::size_t headerLengthFromLoL(std::uint8_t lol) noexcept {
    switch (lol) {
    case EPP_LOL_IDLE:
      return EPP_HEADER_1_OCTET;
    case EPP_LOL_1_OCTET:
      return EPP_HEADER_2_OCTET;
    case EPP_LOL_2_OCTETS:
      return EPP_HEADER_4_OCTET;
    case EPP_LOL_4_OCTETS:
      return EPP_HEADER_8_OCTET;
    default:
      return 0; // Invalid
    }
  }

private:
  // Private constructor (use factories)
  PacketViewer() noexcept = default;
};

/* ==================== Hot-Path Inline Implementations ====================== */

inline std::optional<EppHeaderView> EppHeaderView::create(apex::compat::bytes_span data) noexcept {
  if (data.empty())
    return std::nullopt;

  // Determine header length from LoL field
  const std::uint8_t lol = data[0] & EPP_LOL_MASK;
  const std::size_t hdrLen = PacketViewer::headerLengthFromLoL(lol);
  if (hdrLen == 0 || data.size() < hdrLen)
    return std::nullopt;

  return EppHeaderView{data.subspan(0, hdrLen)};
}

inline std::optional<std::uint8_t>
PacketViewer::peekProtocolId(apex::compat::bytes_span bytes) noexcept {
  if (bytes.empty())
    return std::nullopt;
  return static_cast<std::uint8_t>((bytes[0] >> EPP_PROTOCOL_ID_SHIFT) & EPP_PROTOCOL_ID_MASK_3BIT);
}

inline std::optional<std::uint8_t> PacketViewer::peekLoL(apex::compat::bytes_span bytes) noexcept {
  if (bytes.empty())
    return std::nullopt;
  return static_cast<std::uint8_t>(bytes[0] & EPP_LOL_MASK);
}

} // namespace epp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_EPP_VIEWER_HPP
