#ifndef APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_READBACK_HPP
#define APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_READBACK_HPP
/**
 * @file TprmReadback.hpp
 * @brief Staged-bank digest assembly for the READBACK_TPRM command.
 *
 * Ground verifies staged parameter payloads before any apply: the
 * digest reports, per staged .tprm file, the identity the v3 prelude
 * claims (fullUid, layoutHash, payloadCrc) plus a header-level
 * verdict, so a corrupted or wrong-target staged payload is visible
 * from the prelude alone -- no payload body is read.
 *
 * Wire format (little-endian, response extra of READBACK_TPRM):
 *   Page header (8 bytes):
 *     0  u16  total     - staged .tprm files in the bank
 *     2  u16  offset    - index of this page's first row
 *     4  u8   count     - rows in this page (<= READBACK_MAX_ROWS)
 *     5  u8[3] reserved - 0
 *   Rows (16 bytes each):
 *     0  u32  fullUid    - target the prelude declares
 *     4  u32  layoutHash - canonical field-spec CRC the prelude declares
 *     8  u32  payloadCrc - body CRC the prelude declares
 *     12 u8   verdict    - TprmPayloadCheck of the header-level checks
 *     13 u8[3] reserved  - 0
 *
 * Rows are ordered by filename, so pagination is stable while the
 * staged set is unchanged. Files whose prelude fails a header check
 * still produce a row (verdict != OK, declared fields as read):
 * an invalid staged file is a finding, never a silent skip.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace system_core {
namespace system_component {

/// Rows per READBACK_TPRM response page.
inline constexpr std::size_t READBACK_MAX_ROWS = 64;

/// Page header size in bytes.
inline constexpr std::size_t READBACK_PAGE_HEADER_SIZE = 8;

/// Row size in bytes.
inline constexpr std::size_t READBACK_ROW_SIZE = 16;

/**
 * @brief Header-level verdict for one staged file (no body read).
 *
 * Checks magic, version, and declared-size consistency against the
 * file length. CRC and layout verification belong to the apply path,
 * which reads the body.
 */
[[nodiscard]] inline TprmPayloadCheck checkStagedPrelude(const std::filesystem::path& file,
                                                         TprmPayloadHeader& header) noexcept {
  std::error_code ec;
  const auto FILE_SIZE = std::filesystem::file_size(file, ec);
  if (ec) {
    return TprmPayloadCheck::FILE_ERROR;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    return TprmPayloadCheck::TOO_SMALL;
  }
  if (std::memcmp(header.magic.data(), "APV3", 4) != 0) {
    return TprmPayloadCheck::BAD_MAGIC;
  }
  if (header.version != 3) {
    return TprmPayloadCheck::BAD_VERSION;
  }
  if (FILE_SIZE != sizeof(header) + header.payloadSize) {
    return TprmPayloadCheck::SIZE_MISMATCH;
  }
  return TprmPayloadCheck::OK;
}

/**
 * @brief Assemble one page of the staged-bank digest into `out`.
 *
 * @param stagedDir Directory holding staged .tprm payloads.
 * @param offset Index of the first row to emit (filename order).
 * @param out Response buffer; page header + rows are appended.
 * @return true on success; false only if the directory is unreadable
 *         (an empty staged bank is a valid page with total = 0).
 * @note NOT RT-safe: File I/O. Command-path (cold) only.
 */
[[nodiscard]] inline bool appendStagedDigest(const std::filesystem::path& stagedDir,
                                             std::uint16_t offset,
                                             std::vector<std::uint8_t>& out) noexcept {
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(stagedDir, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->is_regular_file(ec) && it->path().extension() == ".tprm") {
      files.push_back(it->path());
    }
  }
  if (ec) {
    return false;
  }
  std::sort(files.begin(), files.end());

  const auto TOTAL = static_cast<std::uint16_t>(
      std::min<std::size_t>(files.size(), std::numeric_limits<std::uint16_t>::max()));
  const std::size_t FIRST = std::min<std::size_t>(offset, TOTAL);
  const std::size_t COUNT = std::min<std::size_t>(READBACK_MAX_ROWS, TOTAL - FIRST);

  out.reserve(out.size() + READBACK_PAGE_HEADER_SIZE + COUNT * READBACK_ROW_SIZE);
  const auto put16 = [&out](std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v >> 8U));
  };
  const auto put32 = [&out](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
    }
  };

  put16(TOTAL);
  put16(static_cast<std::uint16_t>(FIRST));
  out.push_back(static_cast<std::uint8_t>(COUNT));
  out.insert(out.end(), 3, 0U);

  for (std::size_t i = FIRST; i < FIRST + COUNT; ++i) {
    TprmPayloadHeader hdr{};
    const TprmPayloadCheck VERDICT = checkStagedPrelude(files[i], hdr);
    put32(hdr.fullUid);
    put32(hdr.layoutHash);
    put32(hdr.payloadCrc);
    out.push_back(toFaultCode(VERDICT));
    out.insert(out.end(), 3, 0U);
  }
  return true;
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_READBACK_HPP
