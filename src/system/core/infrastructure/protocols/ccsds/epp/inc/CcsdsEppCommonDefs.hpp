#ifndef APEX_PROTOCOLS_CCSDS_EPP_COMMON_DEFS_HPP
#define APEX_PROTOCOLS_CCSDS_EPP_COMMON_DEFS_HPP
/**
 * @file CcsdsEppCommonDefs.hpp
 * @brief Common definitions for CCSDS Encapsulation Packet Protocol (EPP).
 *
 * Provides:
 *  - Protocol constants (header sizes, packet limits)
 *  - Length of Length (LoL) field values
 *  - Packet Version Number constant
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 *
 * RT-Safety: All constants are constexpr; no allocation.
 */

#include <cstdint>

namespace protocols {
namespace ccsds {
namespace epp {

/* ----------------------------- Constants ----------------------------- */

/// EPP header lengths in octets.
/// Per CCSDS 133.1-B-3 Section 4.1.2.1.1: Header length is 1, 2, 4, or 8 octets.
constexpr std::uint8_t EPP_HEADER_1_OCTET = 1;
constexpr std::uint8_t EPP_HEADER_2_OCTET = 2;
constexpr std::uint8_t EPP_HEADER_4_OCTET = 4;
constexpr std::uint8_t EPP_HEADER_8_OCTET = 8;

/// Maximum allowed CCSDS EPP packet length (in octets).
/// Per CCSDS 133.1-B-3 Section 4.1.1.2: Max packet length is 4,294,967,295 octets.
constexpr std::size_t MAX_EPP_PACKET_LENGTH = 0xFFFFFFFFu;

/// Minimum EPP packet length (1 octet for idle packet).
/// Per CCSDS 133.1-B-3 Section 4.1.1.2: Min packet length is 1 octet.
constexpr std::size_t MIN_EPP_PACKET_LENGTH = 1;

/**
 * @brief Length of Length (LoL) field values.
 *
 * Per CCSDS 133.1-B-3 Section 4.1.2.4 (Table 4-1):
 * The 2-bit LoL field (bits 6-7) determines the length of the Packet Length field:
 *   - 00: 0 octets (1-octet header, Idle Packet)
 *   - 01: 1 octet  (2-octet header)
 *   - 10: 2 octets (4-octet header)
 *   - 11: 4 octets (8-octet header)
 */
constexpr std::uint8_t EPP_LOL_IDLE = 0x00;
constexpr std::uint8_t EPP_LOL_1_OCTET = 0x01;
constexpr std::uint8_t EPP_LOL_2_OCTETS = 0x02;
constexpr std::uint8_t EPP_LOL_4_OCTETS = 0x03;

/// Valid Packet Version Number for EPP.
/// Per CCSDS 133.1-B-3 Section 4.1.2.2.2: PVN must be '111' (binary) = 7 (decimal).
constexpr std::uint8_t EPP_VALID_VERSION = 0x07;

/// Encapsulation Protocol ID for Idle Packets.
/// Per CCSDS 133.1-B-3 Section 4.1.2.3 Note 1: Value '000' indicates an Idle Packet.
constexpr std::uint8_t EPP_PROTOCOL_ID_IDLE = 0x00;

/// Encapsulation Protocol ID indicating Extended Protocol ID is used.
/// Per CCSDS 133.1-B-3 Section 4.1.2.3 Note 2: Value '110' uses the extension field.
constexpr std::uint8_t EPP_PROTOCOL_ID_EXTENDED = 0x06;

/// Encapsulation Protocol ID for mission-specific private data.
/// Per CCSDS 133.1-B-3 Section 4.1.2.3 Note 3: Value '111' = privately defined.
constexpr std::uint8_t EPP_PROTOCOL_ID_PRIVATE = 0x07;

/* ----------------------------- Bit Masks ----------------------------- */

/// Bit masks for first octet field extraction.
/// Per CCSDS 133.1-B-3 Section 4.1.2: First octet layout (MSB first):
///   Bits 0-2: Packet Version Number (3 bits)
///   Bits 3-5: Encapsulation Protocol ID (3 bits)
///   Bits 6-7: Length of Length (2 bits)
constexpr std::uint8_t EPP_VERSION_MASK = 0xE0;      ///< Full mask in octet position
constexpr std::uint8_t EPP_VERSION_MASK_3BIT = 0x07; ///< Mask after shift
constexpr unsigned EPP_VERSION_SHIFT = 5;
constexpr std::uint8_t EPP_PROTOCOL_ID_MASK = 0x1C;      ///< Full mask in octet position
constexpr std::uint8_t EPP_PROTOCOL_ID_MASK_3BIT = 0x07; ///< Mask after shift
constexpr unsigned EPP_PROTOCOL_ID_SHIFT = 2;
constexpr std::uint8_t EPP_LOL_MASK = 0x03; ///< Bits 6-7 (no shift needed)

/// Bit masks for second octet (4- and 8-octet headers).
/// Per CCSDS 133.1-B-3 Section 4.1.2.5-4.1.2.6:
///   Bits 0-3: User Defined Field (4 bits)
///   Bits 4-7: Protocol ID Extension (4 bits)
constexpr std::uint8_t EPP_USER_DEFINED_MASK = 0xF0;      ///< Full mask in octet position
constexpr std::uint8_t EPP_USER_DEFINED_MASK_4BIT = 0x0F; ///< Mask after shift
constexpr unsigned EPP_USER_DEFINED_SHIFT = 4;
constexpr std::uint8_t EPP_PROTOCOL_IDE_MASK = 0x0F; ///< Lower 4 bits (no shift needed)

/* ------------------------- Practical Limits --------------------------- */

/// Practical maximum packet length for RT systems.
/// While the spec allows 4GB packets, this provides a reasonable default.
constexpr std::size_t EPP_DEFAULT_MAX_PACKET_LENGTH = 65536;

} // namespace epp
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_EPP_COMMON_DEFS_HPP
