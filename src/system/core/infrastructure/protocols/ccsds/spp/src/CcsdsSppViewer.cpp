/**
 * @file CcsdsSppViewer.cpp
 * @brief Implementation of CCSDS SPP PacketViewer.
 *
 * Reference: CCSDS 133.0-B-2 "Space Packet Protocol"
 */

#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppViewer.hpp"
#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppCommonDefs.hpp"

#include <algorithm>

namespace protocols {
namespace ccsds {
namespace spp {

/* ------------------------- Validation Error Strings ----------------------- */

const char* toString(ValidationError err) noexcept {
  switch (err) {
  case ValidationError::OK:
    return "OK";
  case ValidationError::PACKET_TOO_SMALL:
    return "Packet too small (< 6 bytes)";
  case ValidationError::PD_LENGTH_MISMATCH:
    return "Packet size doesn't match PD+7 rule";
  case ValidationError::SECONDARY_OVERSIZE:
    return "Secondary header doesn't fit in data field";
  case ValidationError::TIME_CODE_INVALID:
    return "Time code parsing failed";
  case ValidationError::TIME_CODE_LENGTH_MISMATCH:
    return "Time code length doesn't match expected";
  case ValidationError::CONFIG_INVALID:
    return "Secondary header config is invalid";
  default:
    return "Unknown error";
  }
}

/* ------------------------- Time Code Parsing Helpers ---------------------- */

// Forward declarations
static void parseCucTimeCode(apex::compat::bytes_span data, common::TimeCodeFormat format,
                             common::CucTimeCode& result) noexcept;

static void parseCdsTimeCode(apex::compat::bytes_span data, common::TimeCodeFormat format,
                             common::CdsTimeCode& result) noexcept;

static void parseTimeCode(apex::compat::bytes_span data, common::TimeCodeFormat format,
                          common::ParsedTimeCode& result) noexcept {

  using common::TimeCodeFormat;

  if (data.empty()) {
    return;
  }

  switch (format) {
  case TimeCodeFormat::CUC_LEVEL1_4_0:
  case TimeCodeFormat::CUC_LEVEL1_4_1:
  case TimeCodeFormat::CUC_LEVEL1_4_2:
  case TimeCodeFormat::CUC_LEVEL1_4_3:
  case TimeCodeFormat::CUC_LEVEL2_4_0:
  case TimeCodeFormat::CUC_LEVEL2_4_1:
  case TimeCodeFormat::CUC_LEVEL2_4_2:
  case TimeCodeFormat::CUC_LEVEL2_4_3:
    parseCucTimeCode(data, format, result.cuc);
    break;

  case TimeCodeFormat::CDS_SHORT:
  case TimeCodeFormat::CDS_LONG:
    parseCdsTimeCode(data, format, result.cds);
    break;

  default:
    break;
  }
}

static void parseCucTimeCode(apex::compat::bytes_span data, common::TimeCodeFormat format,
                             common::CucTimeCode& result) noexcept {

  using common::TimeCodeFormat;

  // Determine coarse and fine octets from format
  // Per CCSDS 301.0-B-4 Section 3.2.2: P-field bits 4-5 = coarse octets - 1
  switch (format) {
  case TimeCodeFormat::CUC_LEVEL1_4_0:
  case TimeCodeFormat::CUC_LEVEL2_4_0:
    result.coarseOctets = 4;
    result.fineOctets = 0;
    break;
  case TimeCodeFormat::CUC_LEVEL1_4_1:
  case TimeCodeFormat::CUC_LEVEL2_4_1:
    result.coarseOctets = 4;
    result.fineOctets = 1;
    break;
  case TimeCodeFormat::CUC_LEVEL1_4_2:
  case TimeCodeFormat::CUC_LEVEL2_4_2:
    result.coarseOctets = 4;
    result.fineOctets = 2;
    break;
  case TimeCodeFormat::CUC_LEVEL1_4_3:
  case TimeCodeFormat::CUC_LEVEL2_4_3:
    result.coarseOctets = 4;
    result.fineOctets = 3;
    break;
  default:
    return;
  }

  // Set epoch based on level
  // Per CCSDS 301.0-B-4 Section 3.2.3: Level 2 uses agency-defined epoch
  switch (format) {
  case TimeCodeFormat::CUC_LEVEL2_4_0:
  case TimeCodeFormat::CUC_LEVEL2_4_1:
  case TimeCodeFormat::CUC_LEVEL2_4_2:
  case TimeCodeFormat::CUC_LEVEL2_4_3:
    result.epoch = common::TimeEpoch::MISSION;
    break;
  default:
    result.epoch = common::TimeEpoch::CCSDS;
    break;
  }

  if (data.size() < result.coarseOctets + result.fineOctets) {
    return;
  }

  // Parse coarse time (big-endian)
  result.coarse = 0;
  for (std::uint8_t i = 0; i < result.coarseOctets; ++i) {
    result.coarse = (result.coarse << 8) | data[i];
  }

  // Parse fine time (big-endian)
  result.fine = 0;
  for (std::uint8_t i = 0; i < result.fineOctets; ++i) {
    result.fine = (result.fine << 8) | data[result.coarseOctets + i];
  }
}

static void parseCdsTimeCode(apex::compat::bytes_span data, common::TimeCodeFormat format,
                             common::CdsTimeCode& result) noexcept {

  using common::TimeCodeFormat;

  result.has24BitDays = (format == TimeCodeFormat::CDS_LONG);
  const std::size_t DAY_OCTETS = result.has24BitDays ? 3 : 2;
  const std::size_t MIN_LEN = DAY_OCTETS + 4; // Days + milliseconds (submilliseconds optional)

  if (data.size() < MIN_LEN) {
    return;
  }

  // Parse days (big-endian)
  result.days = 0;
  for (std::size_t i = 0; i < DAY_OCTETS; ++i) {
    result.days = (result.days << 8) | data[i];
  }

  // Parse milliseconds (32-bit big-endian)
  const std::size_t MS_OFFSET = DAY_OCTETS;
  result.milliseconds = (static_cast<std::uint32_t>(data[MS_OFFSET + 0]) << 24) |
                        (static_cast<std::uint32_t>(data[MS_OFFSET + 1]) << 16) |
                        (static_cast<std::uint32_t>(data[MS_OFFSET + 2]) << 8) |
                        (static_cast<std::uint32_t>(data[MS_OFFSET + 3]));

  // Parse submilliseconds if present (16-bit big-endian, optional per CCSDS 301.0-B-4)
  if (data.size() >= MS_OFFSET + 4 + 2) {
    result.submilliseconds = (static_cast<std::uint16_t>(data[MS_OFFSET + 4]) << 8) |
                             (static_cast<std::uint16_t>(data[MS_OFFSET + 5]));
  }

  result.epoch = common::TimeEpoch::CCSDS;
}

/* -------------------- SecondaryHeaderView Implementation ------------------ */

SecondaryHeaderView SecondaryHeaderView::create(apex::compat::bytes_span data,
                                                const SecondaryHeaderConfig& cfg) noexcept {
  SecondaryHeaderView view{};

  if (cfg.totalLength == 0) {
    return view;
  }

  view.raw = data;
  view.timeCodeFormat = cfg.timeCodeFormat;

  if (cfg.timeCodeLength > 0) {
    view.timeCodeBytes = data.subspan(0, cfg.timeCodeLength);

    if (cfg.timeCodeLength < cfg.totalLength) {
      view.ancillaryData = data.subspan(cfg.timeCodeLength);
    }
  } else {
    view.ancillaryData = data;
  }

  return view;
}

common::ParsedTimeCode SecondaryHeaderView::timeCode() const noexcept {
  common::ParsedTimeCode result{};
  result.format = timeCodeFormat;

  if (timeCodeBytes.empty()) {
    return result;
  }

  parseTimeCode(timeCodeBytes, timeCodeFormat, result);
  return result;
}

/* ----------------------- PacketViewer Implementation ---------------------- */

std::optional<PacketViewer> PacketViewer::create(apex::compat::bytes_span bytes,
                                                 const SecondaryHeaderConfig& secConfig) noexcept {

  // Validate first
  ValidationError err = validate(bytes, secConfig);
  if (err != ValidationError::OK) {
    return std::nullopt;
  }

  // Construct viewer
  PacketViewer view{};
  view.raw = bytes;

  // Create primary header view (just stores span, no parsing)
  view.pri = PrimaryHeaderView::create(bytes);

  // Create secondary header view if present (detected from flag)
  if (view.pri.hasSecondaryHeader()) {
    const std::size_t secHdrStart = SPP_HDR_SIZE_BYTES;
    // Calculate secondary header length from packet structure
    const std::size_t dataFieldLen = view.pri.packetDataLength() + 1;
    const std::size_t secHdrLen = secConfig.totalLength > 0
                                      ? secConfig.totalLength
                                      : dataFieldLen; // Entire data field if no config

    apex::compat::bytes_span secData = bytes.subspan(secHdrStart, secHdrLen);
    view.sec = SecondaryHeaderView::create(secData, secConfig);
  }

  return view;
}

ValidationError PacketViewer::validate(apex::compat::bytes_span bytes,
                                       const SecondaryHeaderConfig& secConfig) noexcept {

  // 1. Check minimum packet size
  // Per CCSDS 133.0-B-2 Section 4.1.3: Primary header is 6 octets
  if (bytes.size() < SPP_HDR_SIZE_BYTES) {
    return ValidationError::PACKET_TOO_SMALL;
  }

  // 2. Parse primary header to get PD length and flags
  const std::uint8_t BYTE0 = bytes[SPP_VERSION_OCTET];
  const std::uint8_t PD_UPPER = bytes[SPP_PDL_UPPER_OCTET];
  const std::uint8_t PD_LOWER = bytes[SPP_PDL_LOWER_OCTET];
  const std::uint16_t PD = static_cast<std::uint16_t>((PD_UPPER << 8) | PD_LOWER);

  // Per CCSDS 133.0-B-2 Section 4.1.3.3.4: Total length = PD + 7
  const std::size_t EXPECTED_LENGTH = static_cast<std::size_t>(PD) + SPP_TOTAL_OVERHEAD;

  if (bytes.size() != EXPECTED_LENGTH) {
    return ValidationError::PD_LENGTH_MISMATCH;
  }

  // 3. Check secondary header presence from primary header flag
  // Per CCSDS 133.0-B-2 Section 4.1.3.1.3: Bit 3 indicates presence
  const bool HAS_SEC_HDR = (BYTE0 & SPP_SECHDR_BIT_MASK) != 0;
  const std::size_t DATA_FIELD_LENGTH = static_cast<std::size_t>(PD) + 1;

  // 4. If secondary header is present, validate config (if provided)
  if (HAS_SEC_HDR && secConfig.totalLength > 0) {
    // Config provided - validate it
    if (!secConfig.isValid()) {
      return ValidationError::CONFIG_INVALID;
    }

    // Check secondary header fits in data field
    if (secConfig.totalLength > DATA_FIELD_LENGTH) {
      return ValidationError::SECONDARY_OVERSIZE;
    }
  }

  // Note: If hasSecHdrFlag is true but no config, that's OK - raw bytes accessible
  // Note: If hasSecHdrFlag is false, config is ignored

  return ValidationError::OK;
}

} // namespace spp
} // namespace ccsds
} // namespace protocols