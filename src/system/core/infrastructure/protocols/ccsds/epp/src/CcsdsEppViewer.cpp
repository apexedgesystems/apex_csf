/**
 * @file CcsdsEppViewer.cpp
 * @brief Implementation of cold-path functions for CCSDS EPP Viewer.
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppViewer.hpp"

namespace protocols {
namespace ccsds {
namespace epp {

/* ----------------------------- ValidationError toString ------------------- */

const char* toString(ValidationError err) noexcept {
  switch (err) {
  case ValidationError::OK:
    return "OK";
  case ValidationError::PACKET_TOO_SMALL:
    return "PACKET_TOO_SMALL";
  case ValidationError::INVALID_VERSION:
    return "INVALID_VERSION";
  case ValidationError::INVALID_LOL:
    return "INVALID_LOL";
  case ValidationError::LENGTH_MISMATCH:
    return "LENGTH_MISMATCH";
  case ValidationError::LENGTH_OVER_MAX:
    return "LENGTH_OVER_MAX";
  case ValidationError::HEADER_INCOMPLETE:
    return "HEADER_INCOMPLETE";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- PacketViewer -------------------------------- */

std::optional<PacketViewer> PacketViewer::create(apex::compat::bytes_span bytes) noexcept {
  ValidationError err = validate(bytes);
  if (err != ValidationError::OK)
    return std::nullopt;

  // Create header view.
  auto hdrOpt = EppHeaderView::create(bytes);
  if (!hdrOpt)
    return std::nullopt;

  PacketViewer pv;
  pv.hdr = *hdrOpt;
  pv.raw = bytes;
  return pv;
}

ValidationError PacketViewer::validate(apex::compat::bytes_span bytes) noexcept {
  // Check minimum size.
  if (bytes.size() < MIN_EPP_PACKET_LENGTH)
    return ValidationError::PACKET_TOO_SMALL;

  // Check version (must be 7).
  const std::uint8_t VERSION = (bytes[0] >> EPP_VERSION_SHIFT) & EPP_VERSION_MASK_3BIT;
  if (VERSION != EPP_VALID_VERSION)
    return ValidationError::INVALID_VERSION;

  // Determine header length from LoL.
  const std::uint8_t LOL = bytes[0] & EPP_LOL_MASK;
  const std::size_t HDR_LEN = headerLengthFromLoL(LOL);
  if (HDR_LEN == 0)
    return ValidationError::INVALID_LOL;

  // Check header completeness.
  if (bytes.size() < HDR_LEN)
    return ValidationError::HEADER_INCOMPLETE;

  // Determine expected packet length.
  std::size_t expectedLength = 0;
  if (LOL == EPP_LOL_IDLE) {
    // Idle packet: total length is 1.
    expectedLength = 1;
  } else {
    // Read packet length field based on header variant.
    switch (HDR_LEN) {
    case EPP_HEADER_2_OCTET:
      expectedLength = bytes[1];
      break;
    case EPP_HEADER_4_OCTET:
      expectedLength =
          static_cast<std::size_t>((static_cast<std::uint16_t>(bytes[2]) << 8) | bytes[3]);
      break;
    case EPP_HEADER_8_OCTET:
      expectedLength =
          static_cast<std::size_t>((static_cast<std::uint32_t>(bytes[4]) << 24) |
                                   (static_cast<std::uint32_t>(bytes[5]) << 16) |
                                   (static_cast<std::uint32_t>(bytes[6]) << 8) | bytes[7]);
      break;
    default:
      return ValidationError::INVALID_LOL;
    }
  }

  // Validate packet length bounds.
  if (expectedLength > MAX_EPP_PACKET_LENGTH)
    return ValidationError::LENGTH_OVER_MAX;

  // Validate actual size matches expected length.
  if (bytes.size() != expectedLength)
    return ValidationError::LENGTH_MISMATCH;

  return ValidationError::OK;
}

} // namespace epp
} // namespace ccsds
} // namespace protocols
