#ifndef APEX_PROTOCOLS_CCSDS_TIME_CODE_HPP
#define APEX_PROTOCOLS_CCSDS_TIME_CODE_HPP
/**
 * @file CcsdsTimeCode.hpp
 * @brief CCSDS Time Code format definitions per CCSDS 301.0-B-4.
 *
 * Provides:
 *  - Time code format identifiers (P-field values)
 *  - Time code structure definitions
 *  - Length constants for fixed-length time codes
 *  - Epoch definitions
 *
 * This header is shared across CCSDS protocols (SPP, TM, TC, AOS)
 * that use standard CCSDS time codes.
 *
 * Reference: CCSDS 301.0-B-4 "Time Code Formats"
 *            https://public.ccsds.org/Pubs/301x0b4e1.pdf
 *
 * Reference: CCSDS 133.0-B-2 Section 4.1.4.2.2: "The Time Code Field shall consist
 *            of one of the CCSDS segmented binary or unsegmented binary time codes
 *            specified in reference [3]" (CCSDS 301.0-B-4).
 */

#include <cstddef>
#include <cstdint>

namespace protocols {
namespace ccsds {
namespace common {

/* ========================= Time Code Format Identifiers ==================== */

/**
 * @enum TimeCodeFormat
 * @brief CCSDS time code formats (P-field identifiers).
 *
 * Per CCSDS 301.0-B-4 Section 3.3: "The P-Field is an optional leading field in
 * the time code. Its purpose is to allow a priori identification of both the
 * time code and its characteristics."
 *
 * P-field structure (when present):
 *  - Bit 7: Extension bit (0=no extension)
 *  - Bits 6-4: Time code identification (001=CUC L1, 010=CUC L2, 100=CDS, 101=CCS)
 *  - Bits 3-0: Format-specific configuration
 */
enum class TimeCodeFormat : std::uint8_t {
  NONE = 0x00, ///< No time code present

  // ===================== Unsegmented Time Code (CUC) =======================
  // Per CCSDS 301.0-B-4 Section 3.2: "CUC provides a compact binary representation
  // of elapsed time since an epoch."

  /// CUC Level 1: 4 octets coarse, 0 fine
  /// Per CCSDS 301.0-B-4 Table 3-1: P-field 0x1C = 00011100b
  CUC_LEVEL1_4_0 = 0x1C,

  /// CUC Level 1: 4 octets coarse, 1 fine
  /// Per CCSDS 301.0-B-4 Table 3-1: P-field 0x1D = 00011101b
  CUC_LEVEL1_4_1 = 0x1D,

  /// CUC Level 1: 4 octets coarse, 2 fine
  /// Per CCSDS 301.0-B-4 Table 3-1: P-field 0x1E = 00011110b
  CUC_LEVEL1_4_2 = 0x1E,

  /// CUC Level 1: 4 octets coarse, 3 fine
  /// Per CCSDS 301.0-B-4 Table 3-1: P-field 0x1F = 00011111b
  CUC_LEVEL1_4_3 = 0x1F,

  /// CUC Level 2: 4 octets coarse, 0 fine (agency-defined epoch)
  /// Per CCSDS 301.0-B-4 Section 3.2.3: Level 2 allows agency-defined epoch
  /// Per CCSDS 301.0-B-4 Section 3.2.2: P-field bits 4-5 = coarse octets - 1
  /// P-field 0x2C = 00101100b (time code ID=010, coarse=4, fine=0)
  CUC_LEVEL2_4_0 = 0x2C,

  /// CUC Level 2: 4 octets coarse, 1 fine
  /// P-field 0x2D = 00101101b
  CUC_LEVEL2_4_1 = 0x2D,

  /// CUC Level 2: 4 octets coarse, 2 fine
  /// P-field 0x2E = 00101110b
  CUC_LEVEL2_4_2 = 0x2E,

  /// CUC Level 2: 4 octets coarse, 3 fine
  /// P-field 0x2F = 00101111b
  CUC_LEVEL2_4_3 = 0x2F,

  // ==================== Day Segmented Time Code (CDS) ======================
  // Per CCSDS 301.0-B-4 Section 3.3: "CDS is designed to provide a representation
  // of absolute calendar time...using day segmentation."

  /// CDS Short: 16-bit days, 32-bit ms, optional 16-bit submillisecond (6 or 8 octets)
  /// Per CCSDS 301.0-B-4 Section 3.3.2 and Table 3-3: P-field 0x41 = 01000001b
  CDS_SHORT = 0x41,

  /// CDS Long: 24-bit days, 32-bit ms, 16-bit submillisecond (10 octets)
  /// Per CCSDS 301.0-B-4 Section 3.3.2 and Table 3-3: P-field 0x42 = 01000010b
  CDS_LONG = 0x42,

  // ================= Calendar Segmented Time Code (CCS) =====================
  // Per CCSDS 301.0-B-4 Section 3.4: "CCS is designed to provide a representation
  // of absolute calendar time using ASCII characters."

  /// CCS Minimal: Year, Month, Day, Hour, Minute, Second (7 octets)
  /// Per CCSDS 301.0-B-4 Section 3.4.2
  CCS_MINIMAL = 0x51,

  /// CCS Extended: + subsecond fields (variable length)
  /// Per CCSDS 301.0-B-4 Section 3.4.2
  CCS_EXTENDED = 0x52,
};

/* ======================== Time Code Length Constants ======================= */

// CUC Lengths (when P-field not included)
constexpr std::size_t CUC_LEVEL1_4_0_LENGTH = 4;
constexpr std::size_t CUC_LEVEL1_4_1_LENGTH = 5;
constexpr std::size_t CUC_LEVEL1_4_2_LENGTH = 6;
constexpr std::size_t CUC_LEVEL1_4_3_LENGTH = 7;

constexpr std::size_t CUC_LEVEL2_4_0_LENGTH = 4;
constexpr std::size_t CUC_LEVEL2_4_1_LENGTH = 5;
constexpr std::size_t CUC_LEVEL2_4_2_LENGTH = 6;
constexpr std::size_t CUC_LEVEL2_4_3_LENGTH = 7;

// CDS Lengths
constexpr std::size_t CDS_SHORT_LENGTH = 8; ///< 2 days + 4 ms + 2 submillisecond
constexpr std::size_t CDS_LONG_LENGTH = 10; ///< 3 days + 4 ms + 2 submillisecond + 1 microsecond

// CCS Lengths
constexpr std::size_t CCS_MINIMAL_LENGTH = 7; ///< Year(2) + Month + Day + Hour + Minute + Second

/* ============================ Epoch Definitions ============================ */

/**
 * @enum TimeEpoch
 * @brief Standard CCSDS time epochs.
 *
 * Per CCSDS 301.0-B-4 Section 2.2: "The origin of the time codes is defined by
 * the epoch...The CCSDS epoch is defined as 1 January 1958 00:00:00 TAI."
 */
enum class TimeEpoch : std::uint8_t {
  /// 1958-01-01 00:00:00 TAI
  /// Per CCSDS 301.0-B-4 Section 2.2.2: Standard CCSDS epoch for Level 1 CUC
  CCSDS = 0,

  /// 1970-01-01 00:00:00 UTC
  /// Common Unix/POSIX epoch
  UNIX = 1,

  /// 1980-01-06 00:00:00 UTC
  /// GPS system epoch
  GPS = 2,

  /// 2000-01-01 12:00:00 TT
  /// J2000.0 epoch used in astronomy
  J2000 = 3,

  /// Mission-specific epoch
  /// Per CCSDS 301.0-B-4 Section 3.2.3: Level 2 CUC allows agency-defined epoch
  MISSION = 4
};

/* ========================= Parsed Time Code Structures ===================== */

/**
 * @struct CucTimeCode
 * @brief Parsed CUC (Unsegmented) time code.
 *
 * Per CCSDS 301.0-B-4 Section 3.2: "The CCSDS unsegmented time code (CUC)
 * provides a compact binary representation of elapsed time since an epoch."
 *
 * Structure (per Section 3.2.2):
 *  - Coarse time: 1-8 octets (seconds or agency-defined unit)
 *  - Fine time: 0-3 octets (fractional seconds, binary fraction)
 */
struct CucTimeCode {
  std::uint64_t coarse = 0;      ///< Coarse time (seconds or mission-specific unit)
  std::uint32_t fine = 0;        ///< Fine time (fractional, resolution depends on format)
  std::uint8_t coarseOctets = 0; ///< Number of coarse octets (1-8)
  std::uint8_t fineOctets = 0;   ///< Number of fine octets (0-3)
  TimeEpoch epoch = TimeEpoch::CCSDS;
};

/**
 * @struct CdsTimeCode
 * @brief Parsed CDS (Day Segmented) time code.
 *
 * Per CCSDS 301.0-B-4 Section 3.3: "The CCSDS day segmented time code (CDS) is
 * designed to provide a representation of absolute calendar time."
 *
 * Structure (per Section 3.3.2):
 *  - Day segment: 16 or 24 bits (days since epoch)
 *  - Milliseconds of day: 32 bits
 *  - Submilliseconds: 16 bits (optional, only if P-field bit 2 = 1)
 *
 * Per Section 3.3.3: Default epoch is CCSDS epoch (1958-01-01).
 */
struct CdsTimeCode {
  std::uint32_t days = 0;            ///< Days since epoch (16 or 24 bits)
  std::uint32_t milliseconds = 0;    ///< Milliseconds of day (0-86400999)
  std::uint16_t submilliseconds = 0; ///< Submilliseconds (optional, picoseconds/2^16)
  bool has24BitDays = false;         ///< True if 24-bit day field
  TimeEpoch epoch = TimeEpoch::CCSDS;
};

/**
 * @struct CcsTimeCode
 * @brief Parsed CCS (Calendar Segmented) time code.
 *
 * Per CCSDS 301.0-B-4 Section 3.4: "The CCSDS calendar segmented time code (CCS)
 * is designed to provide a representation of absolute calendar time using ASCII
 * characters."
 *
 * Structure (per Section 3.4.2):
 *  - Year: 16 bits (0-65535)
 *  - Month: 8 bits (1-12)
 *  - Day: 8 bits (1-31)
 *  - Hour: 8 bits (0-23)
 *  - Minute: 8 bits (0-59)
 *  - Second: 8 bits (0-60, allows leap second)
 *  - Subsecond: Variable length (optional)
 */
struct CcsTimeCode {
  std::uint16_t year = 0;
  std::uint8_t month = 0;
  std::uint8_t day = 0;
  std::uint8_t hour = 0;
  std::uint8_t minute = 0;
  std::uint8_t second = 0;
  std::uint32_t subsecond = 0; ///< Fractional seconds (resolution depends on format)
};

/**
 * @struct ParsedTimeCode
 * @brief Union of all time code formats.
 */
struct ParsedTimeCode {
  TimeCodeFormat format = TimeCodeFormat::NONE;

  union {
    CucTimeCode cuc;
    CdsTimeCode cds;
    CcsTimeCode ccs;
  };

  ParsedTimeCode() : cuc{} {}
};

/* ======================== P-Field Detection Constants ====================== */

/// P-field extension bit (bit 7 of P-field octet 1).
/// Per CCSDS 301.0-B-4 Section 3.3.1: "Bit 7 is an extension flag...If the
/// extension flag = 0, there are no extension octets."
constexpr std::uint8_t PFIELD_EXTENSION_BIT = 0x80u;

/// P-field time code identification bits (bits 6-4 of P-field octet 1).
/// Per CCSDS 301.0-B-4 Section 3.3.1 and Table 3-1.
constexpr std::uint8_t PFIELD_TIMECODE_ID_MASK = 0x70u;
constexpr unsigned PFIELD_TIMECODE_ID_SHIFT = 4;

/// Time code type identifiers (3 bits).
/// Per CCSDS 301.0-B-4 Table 3-1: Bits 6-4 identify the time code format.
constexpr std::uint8_t TIMECODE_TYPE_CUC_LEVEL1 = 0x01u; ///< 001b = CUC Level 1
constexpr std::uint8_t TIMECODE_TYPE_CUC_LEVEL2 = 0x02u; ///< 010b = CUC Level 2
constexpr std::uint8_t TIMECODE_TYPE_CDS = 0x04u;        ///< 100b = CDS
constexpr std::uint8_t TIMECODE_TYPE_CCS = 0x05u;        ///< 101b = CCS

} // namespace common
} // namespace ccsds
} // namespace protocols

#endif // APEX_PROTOCOLS_CCSDS_TIME_CODE_HPP