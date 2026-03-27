#ifndef APEX_PROTOCOLS_CCSDS_SPP_COMMON_DEFS_HPP
#define APEX_PROTOCOLS_CCSDS_SPP_COMMON_DEFS_HPP
/**
 * @file CcsdsSppCommonDefs.hpp
 * @brief Common definitions for CCSDS Space Packet Protocol (SPP).
 *
 * Provides:
 *  - Protocol constants (header sizes, packet limits)
 *  - Primary header bit masks and shifts
 *  - Octet position constants
 *  - PD+7 relationship constants
 *  - Validation error codes
 *
 * Design goals:
 *  - Zero magic numbers in implementation code
 *  - Clear, self-documenting constant names
 *  - Compile-time computation where possible
 *  - Full traceability to CCSDS 133.0-B-2 specification
 *
 * Reference: CCSDS 133.0-B-2 "Space Packet Protocol"
 *            https://public.ccsds.org/Pubs/133x0b2e1.pdf
 */

#include <cstddef>
#include <cstdint>

namespace protocols {
namespace ccsds {
namespace spp {

/* ========================== Packet Structure Bounds ======================== */

/// Fixed primary header length for a CCSDS SPP packet (in bytes).
/// Per CCSDS 133.0-B-2 Section 4.1.3: "The Packet Primary Header shall be six octets in length."
constexpr std::size_t SPP_HDR_SIZE_BYTES = 6;

/// Minimum valid packet length (primary header only).
/// Per CCSDS 133.0-B-2 Section 4.1.4.3.2: "The User Data Field shall be mandatory
/// if a Packet Secondary Header is not present" - so minimum is 6-byte header only
/// when secondary header is present.
constexpr std::size_t SPP_MIN_PACKET_LENGTH = SPP_HDR_SIZE_BYTES;

/// Maximum allowed CCSDS SPP packet length (in bytes).
/// Note: This is an implementation-defined limit. Per CCSDS 133.0-B-2 Section 5.2,
/// "Maximum Packet Length" is a managed parameter. Common values: 4096 (default),
/// 65542 (max based on 16-bit PD field), or mission-specific.
constexpr std::size_t MAX_SPP_PACKET_LENGTH = 4096;

/// Maximum data field length (packet length - primary header).
constexpr std::size_t SPP_MAX_DATA_FIELD_LENGTH = MAX_SPP_PACKET_LENGTH - SPP_HDR_SIZE_BYTES;

/* ======================== PD+7 Relationship Constants ====================== */

/// Packet Data Length (PD) offset: PD = (data_field_length - 1).
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.4: "The Packet Data Length shall contain
/// a length count C that equals one fewer than the length of the Packet Data Field."
constexpr std::size_t SPP_PD_OFFSET = 1;

/// Total packet overhead: primary header + PD offset = 7 bytes.
/// Total packet length = PD + 7.
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.4: "The value of the Length Count C shall be
/// (total number of octets in the Packet Data Field) - 1." Therefore, total packet
/// length in octets = C + 1 + 6 = PD + 7.
constexpr std::size_t SPP_TOTAL_OVERHEAD = SPP_HDR_SIZE_BYTES + SPP_PD_OFFSET;

/// Maximum value for Packet Data Length field (16-bit, 0x0000 to 0xFFFF).
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.4: "The Packet Data Length is 16 bits."
constexpr std::uint16_t SPP_PDL_MAX = 0xFFFFu;

/// Maximum data field length based on PD field limit.
/// Theoretical maximum is 65536 octets (PD=0xFFFF means 65536 data field octets).
constexpr std::size_t SPP_MAX_PDL_DATA_FIELD = SPP_PDL_MAX + SPP_PD_OFFSET;

/* ======================= Primary Header Octet Positions ==================== */

/// Octet 0: Version (bits 7-5), Type (bit 4), Sec Hdr Flag (bit 3), APID upper 3 bits (bits 2-0).
/// Per CCSDS 133.0-B-2 Figure 4-1 and Section 4.1.3.
constexpr std::size_t SPP_VERSION_OCTET = 0;
constexpr std::size_t SPP_TYPE_OCTET = 0;
constexpr std::size_t SPP_SECHDR_FLAG_OCTET = 0;
constexpr std::size_t SPP_APID_UPPER_OCTET = 0;

/// Octet 1: APID lower 8 bits.
/// Per CCSDS 133.0-B-2 Section 4.1.3.2: "The APID is an 11-bit field...occupying
/// the least significant three bits of the first octet...and all eight bits of
/// the second octet."
constexpr std::size_t SPP_APID_LOWER_OCTET = 1;

/// Octet 2: Sequence Flags (bits 7-6), Sequence Count upper 6 bits (bits 5-0).
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.1 and 4.1.3.3.2.
constexpr std::size_t SPP_SEQFLAGS_OCTET = 2;
constexpr std::size_t SPP_SEQCOUNT_UPPER_OCTET = 2;

/// Octet 3: Sequence Count lower 8 bits.
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.2: "The Packet Sequence Count is a 14-bit
/// field...occupying the least significant six bits of the third octet and all
/// eight bits of the fourth octet."
constexpr std::size_t SPP_SEQCOUNT_LOWER_OCTET = 3;

/// Octet 4: Packet Data Length (PD) upper 8 bits.
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.4: "The Packet Data Length is 16 bits and
/// comprises the fifth and sixth octets of the Packet Primary Header."
constexpr std::size_t SPP_PDL_UPPER_OCTET = 4;

/// Octet 5: Packet Data Length (PD) lower 8 bits.
constexpr std::size_t SPP_PDL_LOWER_OCTET = 5;

/* ===================== Primary Header Bitfields (Octet 0) ================== */

/// Version is 3 bits; stored at bits [7:5] of octet 0.
/// Per CCSDS 133.0-B-2 Section 4.1.3.1.1: "The Packet Version Number is a 3-bit field
/// and occupies the three most significant bits of the first octet."
constexpr std::uint8_t SPP_VERSION_MASK = 0x07u;
constexpr unsigned SPP_VERSION_SHIFT = 5;

/// Type flag in octet 0, bit 4 (0=TM, 1=TC).
/// Per CCSDS 133.0-B-2 Section 4.1.3.1.2: "The Packet Type is a 1-bit field and
/// occupies the fourth most significant bit of the first octet. It indicates whether
/// the Packet is Telemetry (bit=0) or Telecommand (bit=1)."
constexpr std::uint8_t SPP_TYPE_BIT_MASK = 0x10u;
constexpr unsigned SPP_TYPE_SHIFT = 4;

/// Secondary Header flag in octet 0, bit 3.
/// Per CCSDS 133.0-B-2 Section 4.1.3.1.3: "The Secondary Header Flag is a 1-bit field
/// and occupies the fifth most significant bit of the first octet. It indicates the
/// presence (bit=1) or absence (bit=0) of the Packet Secondary Header."
constexpr std::uint8_t SPP_SECHDR_BIT_MASK = 0x08u;
constexpr unsigned SPP_SECHDR_SHIFT = 3;

/// APID is 11 bits across octets 0..1 (lower 8 in octet 1, upper 3 in octet 0).
/// Per CCSDS 133.0-B-2 Section 4.1.3.2.1: "The APID is an 11-bit field. It occupies
/// the least significant three bits of the first octet and all eight bits of the
/// second octet."
constexpr std::uint16_t SPP_APID_MASK = 0x07FFu;
constexpr std::uint8_t SPP_APID_UPPER_MASK3 = 0x07u; // Upper 3 bits in octet 0

/* ==================== Primary Header Bitfields (Octet 2) =================== */

/// Sequence Flags are 2 bits at octet 2 bits [7:6].
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.1: "The Sequence Flags are a 2-bit field and
/// occupy the two most significant bits of the third octet."
constexpr std::uint8_t SPP_SEQFLAGS_MASK = 0x03u;
constexpr unsigned SPP_SEQFLAGS_SHIFT = 6;

/// Sequence Count is 14 bits across octets 2..3.
/// Octet 2 carries the upper 6 bits of the count in its low bits [5:0].
/// Per CCSDS 133.0-B-2 Section 4.1.3.3.2: "The Packet Sequence Count is a 14-bit field
/// and occupies the least significant six bits of the third octet and all eight bits
/// of the fourth octet."
constexpr std::uint16_t SPP_SEQCOUNT_MASK = 0x3FFFu;
constexpr std::uint8_t SPP_SEQCOUNT_UPPER6_MASK = 0x3Fu; // For octet 2 low 6 bits

/* ======================== Sequence Flags Values ============================ */

/// Sequence flags: Continuation segment (00).
/// Per CCSDS 133.0-B-2 Table 4-3: "00 = Continuation segment of User Data."
constexpr std::uint8_t SPP_SEQFLAG_CONTINUATION = 0x00u;

/// Sequence flags: First segment (01).
/// Per CCSDS 133.0-B-2 Table 4-3: "01 = First segment of User Data."
constexpr std::uint8_t SPP_SEQFLAG_FIRST = 0x01u;

/// Sequence flags: Last segment (10).
/// Per CCSDS 133.0-B-2 Table 4-3: "10 = Last segment of User Data."
constexpr std::uint8_t SPP_SEQFLAG_LAST = 0x02u;

/// Sequence flags: Unsegmented (11) - most common.
/// Per CCSDS 133.0-B-2 Table 4-3: "11 = Unsegmented User Data (the Space Packet
/// contains a complete message or Data Unit)."
constexpr std::uint8_t SPP_SEQFLAG_UNSEGMENTED = 0x03u;

/* ============================= Version Values ============================== */

/// Protocol version 0 (only valid version per CCSDS 133.0-B-2).
/// Per CCSDS 133.0-B-2 Section 4.1.3.1.1: "The Packet Version Number defined by this
/// Recommended Standard shall be set to binary '000'."
constexpr std::uint8_t SPP_VERSION_0 = 0x00u;

} // namespace spp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_SPP_COMMON_DEFS_HPP