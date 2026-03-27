#ifndef APEX_PROTOCOLS_CCSDS_SPP_VIEWER_HPP
#define APEX_PROTOCOLS_CCSDS_SPP_VIEWER_HPP
/**
 * @file CcsdsSppViewer.hpp
 * @brief Lightweight zero-copy viewer for CCSDS SPP packets.
 *
 * Design goals:
 *  - Zero-copy viewing (holds spans, not copies)
 *  - Parse-on-demand (only parse fields when accessed)
 *  - RT-friendly (no exceptions, no heap allocations)
 *  - Clean API with method notation (viewer.pri.apid(), viewer.sec.timeCode())
 *  - Auto-detect secondary header from packet flag
 *  - Fast-path helper for packet routing (peekAPID)
 *
 * Usage:
 *  - PacketViewer::create() → std::optional (always validates, ~20 cycles)
 *  - PacketViewer::peekAPID() → std::optional<uint16_t> (extract APID only)
 *  - PacketViewer::validate() → ValidationError (detailed diagnostics)
 *
 * Reference: CCSDS 133.0-B-2 "Space Packet Protocol"
 */

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsTimeCode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace protocols {
namespace ccsds {
namespace spp {

/* ==================== Secondary Header Configuration ======================= */

/**
 * @struct SecondaryHeaderConfig
 * @brief User-provided configuration for secondary header structure.
 *
 * The secondary header format is mission-specific. Users must provide:
 *  - Total length of secondary header
 *  - Time code format (if present)
 *  - Time code length (if present)
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.2.1.4: "The format of the secondary header,
 * if present, is managed and mission specific."
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.2.1.5, the secondary header shall consist of either:
 *  a) a Time Code Field (variable length) only;
 *  b) an Ancillary Data Field (variable length) only; or
 *  c) a Time Code Field followed by an Ancillary Data Field.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.2.1.6: "The chosen option shall remain static
 * for a specific managed data path throughout all Mission Phases."
 */
struct SecondaryHeaderConfig {
  std::size_t totalLength = 0; ///< Total secondary header bytes (0 if not present)

  /// Time code format (NONE, CUC, CDS, etc.)
  /// Per CCSDS 133.0-B-2 Section 4.1.4.2.2.2: "The Time Code Field shall consist of
  /// one of the CCSDS segmented binary or unsegmented binary time codes specified
  /// in reference [3]" (CCSDS 301.0-B-4).
  common::TimeCodeFormat timeCodeFormat = common::TimeCodeFormat::NONE;

  /// Time code bytes (0 if not present)
  /// Per CCSDS 133.0-B-2 Section 4.1.4.2.2.1: "If present, the Time Code Field shall
  /// consist of an integral number of octets."
  std::size_t timeCodeLength = 0;

  /**
   * @brief Validate configuration consistency.
   * @return true if configuration is internally consistent.
   */
  constexpr bool isValid() const noexcept {
    // If time code present, length must be non-zero and fit within total
    if (timeCodeFormat != common::TimeCodeFormat::NONE) {
      if (timeCodeLength == 0 || timeCodeLength > totalLength) {
        return false;
      }
    }
    // If time code not present, time code length must be zero
    if (timeCodeFormat == common::TimeCodeFormat::NONE && timeCodeLength != 0) {
      return false;
    }
    return true;
  }
};

/* ============================= Validation Errors =========================== */

/**
 * @enum ValidationError
 * @brief Detailed validation error codes.
 */
enum class ValidationError : std::uint8_t {
  OK = 0,                    ///< Validation passed
  PACKET_TOO_SMALL,          ///< Packet smaller than minimum (6 bytes)
  PD_LENGTH_MISMATCH,        ///< Packet size doesn't match PD+7
  SECONDARY_OVERSIZE,        ///< Secondary header doesn't fit in data field
  TIME_CODE_INVALID,         ///< Time code parsing failed
  TIME_CODE_LENGTH_MISMATCH, ///< Time code length doesn't match expected
  CONFIG_INVALID             ///< Secondary header config is internally inconsistent
};

/**
 * @brief Convert validation error to human-readable string.
 * @param err Error code.
 * @return String literal describing the error.
 */
const char* toString(ValidationError err) noexcept;

/* ========================== Primary Header View ============================ */

/**
 * @struct PrimaryHeaderView
 * @brief Read-only view of primary header fields with parse-on-demand.
 *
 * Fields are parsed only when accessed via methods, minimizing overhead
 * for RT systems that may only need a subset of fields.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.3: "The Packet Primary Header shall be six octets
 * in length" and contains: Version, Type, Secondary Header Flag, APID, Sequence Flags,
 * Sequence Count, and Packet Data Length.
 */
struct PrimaryHeaderView {
  apex::compat::bytes_span raw; ///< Primary header bytes (6 bytes)

  /// Packet Version Number (3 bits, must be 0)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.1.1: "The Packet Version Number defined by
  /// this Recommended Standard shall be set to binary '000'."
  std::uint8_t version() const noexcept {
    return (raw[SPP_VERSION_OCTET] >> SPP_VERSION_SHIFT) & SPP_VERSION_MASK;
  }

  /// Packet Type (1 bit: false=TM, true=TC)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.1.2: "It indicates whether the Packet is
  /// Telemetry (bit=0) or Telecommand (bit=1)."
  bool type() const noexcept { return (raw[SPP_TYPE_OCTET] & SPP_TYPE_BIT_MASK) != 0; }

  /// Secondary Header Flag (1 bit)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.1.3: "It indicates the presence (bit=1) or
  /// absence (bit=0) of the Packet Secondary Header."
  bool hasSecondaryHeader() const noexcept {
    return (raw[SPP_SECHDR_FLAG_OCTET] & SPP_SECHDR_BIT_MASK) != 0;
  }

  /// Application Process Identifier (11 bits, 0x000 to 0x7FF)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.2.1: "The APID is an 11-bit field that shall
  /// provide a naming mechanism for an application process."
  std::uint16_t apid() const noexcept {
    const std::uint8_t apidUpper = raw[SPP_APID_UPPER_OCTET] & SPP_APID_UPPER_MASK3;
    const std::uint8_t apidLower = raw[SPP_APID_LOWER_OCTET];
    return static_cast<std::uint16_t>((apidUpper << 8) | apidLower);
  }

  /// Sequence Flags (2 bits)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.3.1 and Table 4-3:
  /// 00=Continuation, 01=First, 10=Last, 11=Unsegmented
  std::uint8_t sequenceFlags() const noexcept {
    return (raw[SPP_SEQFLAGS_OCTET] >> SPP_SEQFLAGS_SHIFT) & SPP_SEQFLAGS_MASK;
  }

  /// Packet Sequence Count (14 bits, 0x0000 to 0x3FFF)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.3.2: "The Packet Sequence Count is a 14-bit
  /// field that provides a sequential binary count of Space Packets."
  std::uint16_t sequenceCount() const noexcept {
    const std::uint8_t seqCountUpper = raw[SPP_SEQCOUNT_UPPER_OCTET] & SPP_SEQCOUNT_UPPER6_MASK;
    const std::uint8_t seqCountLower = raw[SPP_SEQCOUNT_LOWER_OCTET];
    return static_cast<std::uint16_t>((seqCountUpper << 8) | seqCountLower);
  }

  /// Packet Data Length (16 bits)
  /// Per CCSDS 133.0-B-2 Section 4.1.3.3.4: "The Packet Data Length shall contain
  /// a length count C that equals one fewer than the length of the Packet Data Field."
  /// Note: This is NOT the total packet length. Total length = packetDataLength + 7.
  std::uint16_t packetDataLength() const noexcept {
    const std::uint8_t pdUpper = raw[SPP_PDL_UPPER_OCTET];
    const std::uint8_t pdLower = raw[SPP_PDL_LOWER_OCTET];
    return static_cast<std::uint16_t>((pdUpper << 8) | pdLower);
  }

  /**
   * @brief Create view from packet bytes.
   * @param data Packet bytes (must be at least SPP_HDR_SIZE_BYTES).
   * @return Primary header view.
   */
  static PrimaryHeaderView create(apex::compat::bytes_span data) noexcept {
    return PrimaryHeaderView{data.subspan(0, SPP_HDR_SIZE_BYTES)};
  }
};

/* ========================= Secondary Header View =========================== */

/**
 * @struct SecondaryHeaderView
 * @brief Read-only view of secondary header components with parse-on-demand.
 *
 * Provides access to:
 *  - Raw secondary header bytes
 *  - Time code bytes (if present)
 *  - Parsed time code structure (parsed only when accessed)
 *  - Ancillary data bytes (if present)
 *
 * Time code parsing is deferred until timeCode() is called, minimizing overhead
 * for RT systems that may only need payload or primary header fields.
 *
 * Per CCSDS 133.0-B-2 Section 4.1.4.2.1.5, the secondary header consists of either:
 *  a) Time Code Field only (variable length)
 *  b) Ancillary Data Field only (variable length)
 *  c) Time Code Field followed by Ancillary Data Field
 */
struct SecondaryHeaderView {
  apex::compat::bytes_span raw; ///< Full secondary header bytes

  /// Time code portion (empty if none)
  /// Per CCSDS 133.0-B-2 Section 4.1.4.2.2: If present, follows CCSDS 301.0-B-4
  apex::compat::bytes_span timeCodeBytes;

  /// Ancillary data portion (empty if none)
  /// Per CCSDS 133.0-B-2 Section 4.1.4.2.3: "The content and format of the data
  /// contained in the Ancillary Data Field are not specified in this Recommended
  /// Standard."
  apex::compat::bytes_span ancillaryData;

  /// Time code format (for parsing when accessed)
  common::TimeCodeFormat timeCodeFormat;

  /**
   * @brief Check if time code is present.
   */
  constexpr bool hasTimeCode() const noexcept { return !timeCodeBytes.empty(); }

  /**
   * @brief Check if ancillary data is present.
   */
  constexpr bool hasAncillaryData() const noexcept { return !ancillaryData.empty(); }

  /**
   * @brief Parse time code on-demand.
   * @return Parsed time code structure. Only call if hasTimeCode() is true.
   *
   * Per CCSDS 301.0-B-4, parses CUC, CDS, or CCS formats based on timeCodeFormat.
   * Parsing is done on each call, so cache result if accessed multiple times.
   */
  common::ParsedTimeCode timeCode() const noexcept;

  /**
   * @brief Create secondary header view from packet bytes.
   * @param data Secondary header bytes (from packet data field).
   * @param cfg Secondary header configuration.
   * @return Secondary header view (time code NOT parsed yet).
   */
  static SecondaryHeaderView create(apex::compat::bytes_span data,
                                    const SecondaryHeaderConfig& cfg) noexcept;
};

/* ============================ Packet Viewer ================================ */

/**
 * @class PacketViewer
 * @brief Zero-copy viewer for complete SPP packets.
 *
 * Provides structured access to all packet components per CCSDS 133.0-B-2 Section 4.1:
 *  - Primary header fields (viewer.pri.apid, viewer.pri.sequenceCount, etc.)
 *  - Secondary header (if present) with time code and ancillary data
 *  - User data payload
 *
 * Construction validates the packet structure according to CCSDS 133.0-B-2:
 *  - Section 4.1.3: Primary header format and PD+7 length rule
 *  - Section 4.1.4: Data field structure and secondary header presence
 *  - Section 4.1.4.2: Secondary header format (if present)
 */
class PacketViewer {
public:
  // ========================== Nested Views ===================================

  PrimaryHeaderView pri;   ///< Primary header view (always present)
  SecondaryHeaderView sec; ///< Secondary header view (valid if pri.hasSecondaryHeader)

  // ========================== Raw Packet Access ==============================

  apex::compat::bytes_span raw; ///< Full packet bytes (zero-copy reference)

  // ========================== Payload Access =================================

  /**
   * @brief Get user data payload (after headers).
   *
   * Per CCSDS 133.0-B-2 Section 4.1.4.3.1: "The User Data Field shall follow,
   * without gap, either the Packet Secondary Header (if a Packet Secondary Header
   * is present) or the Packet Primary Header (if a Packet Secondary Header is
   * not present)."
   *
   * Per CCSDS 133.0-B-2 Section 4.1.4.3.2: "The User Data Field shall be mandatory
   * if a Packet Secondary Header is not present; otherwise, it is optional."
   *
   * @return Payload bytes view.
   */
  apex::compat::bytes_span payload() const noexcept COMPAT_HOT;

  // ========================== Construction ===================================

  /**
   * @brief Create packet viewer with validation.
   *
   * Validates per CCSDS 133.0-B-2:
   *  - Section 4.1.3: Minimum packet size (6 bytes for primary header)
   *  - Section 4.1.3.3.4: PD+7 length rule (total length = PD + 7)
   *  - Section 4.1.3.1.3: Secondary header flag in primary header
   *  - Section 4.1.4.2: Secondary header structure (if flag set)
   *  - Section 4.1.4.2.2: Time code format (if configured)
   *
   * Secondary header presence is detected from primary header flag.
   * Config is only needed if you want to parse time code fields.
   *
   * @param bytes Full packet bytes (non-owning view).
   * @param secConfig Optional: secondary header config for time code parsing.
   *                  If omitted, secondary header accessible as raw bytes only.
   * @return Viewer on success, nullopt on validation failure.
   */
  static std::optional<PacketViewer>
  create(apex::compat::bytes_span bytes,
         const SecondaryHeaderConfig& secConfig = {}) noexcept COMPAT_HOT;

  /**
   * @brief Detailed validation (diagnostic use).
   *
   * Performs full validation and returns specific error code.
   * Useful for logging/debugging when create() returns nullopt.
   *
   * @param bytes Full packet bytes.
   * @param secConfig Optional: secondary header configuration.
   * @return OK on success, specific error code on failure.
   */
  static ValidationError validate(apex::compat::bytes_span bytes,
                                  const SecondaryHeaderConfig& secConfig = {}) noexcept;

  // ========================== Fast-Path Helpers ==============================

  /**
   * @brief Peek APID without full packet parsing.
   *
   * Extracts APID from primary header with minimal overhead.
   * Validates minimum packet size (6 bytes) only.
   *
   * Per CCSDS 133.0-B-2 Section 4.1.3.2: APID is in octets 0-1, bits [10:0].
   *
   * @param bytes Packet bytes.
   * @return APID (11 bits, 0x000 to 0x7FF) if valid, nullopt if too small.
   */
  static std::optional<std::uint16_t> peekAPID(apex::compat::bytes_span bytes) noexcept COMPAT_HOT;

  // ========================== Sequence Gap Detection =========================

  /**
   * @brief Check if this packet indicates a sequence gap.
   *
   * Compares this packet's sequence count with the previous packet's,
   * accounting for 14-bit wraparound.
   *
   * Per CCSDS 133.0-B-2 Section 4.1.3.3.2: "The Packet Sequence Count is a 14-bit
   * field...The counter shall be maintained separately for each APID and shall
   * be incremented modulo-16384."
   *
   * @param lastSeqCount Previous packet's sequence count.
   * @return true if gap detected (one or more packets lost).
   */
  bool hasSequenceGap(std::uint16_t lastSeqCount) const noexcept;

private:
  // Private constructor (use factories)
  PacketViewer() noexcept = default;
};

/* ==================== Hot-Path Inline Implementations ====================== */

inline apex::compat::bytes_span PacketViewer::payload() const noexcept {
  // Payload starts after primary header + secondary header (if present)
  const std::size_t headerSize =
      SPP_HDR_SIZE_BYTES + (pri.hasSecondaryHeader() ? sec.raw.size() : 0);
  return raw.subspan(headerSize);
}

inline std::optional<std::uint16_t>
PacketViewer::peekAPID(apex::compat::bytes_span bytes) noexcept {
  // Validate minimum size for primary header
  if (bytes.size() < SPP_HDR_SIZE_BYTES) {
    return std::nullopt;
  }

  // Extract APID without full validation
  // Per CCSDS 133.0-B-2 Section 4.1.3.2: APID is 11 bits in octets 0-1
  const std::uint8_t byte0 = bytes[SPP_APID_UPPER_OCTET];
  const std::uint8_t byte1 = bytes[SPP_APID_LOWER_OCTET];
  const std::uint8_t apidUpper = byte0 & SPP_APID_UPPER_MASK3;
  return static_cast<std::uint16_t>((apidUpper << 8) | byte1);
}

inline bool PacketViewer::hasSequenceGap(std::uint16_t lastSeqCount) const noexcept {
  // Per CCSDS 133.0-B-2 Section 4.1.3.3.2: Counter increments modulo-16384

  // Expected next sequence count (with 14-bit wraparound)
  const std::uint16_t expectedSeqCount = (lastSeqCount + 1) & SPP_SEQCOUNT_MASK;

  // Compare with actual
  return pri.sequenceCount() != expectedSeqCount;
}

} // namespace spp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_SPP_VIEWER_HPP