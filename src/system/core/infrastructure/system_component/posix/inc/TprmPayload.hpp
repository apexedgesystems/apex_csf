#ifndef APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_PAYLOAD_HPP
#define APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_PAYLOAD_HPP
/**
 * @file TprmPayload.hpp
 * @brief Format A v3 payload prelude: verified ingest for component payloads.
 *
 * Every component payload carries a 20-byte little-endian prelude:
 * ```
 *   magic[4]       = "APV3"
 *   version[2]     = 3
 *   payloadSize[2] = byte length of the payload that follows
 *   fullUid[4]     = (componentId << 8) | instanceIndex the payload targets
 *   layoutHash[4]  = CRC-32 of the canonical field spec the tools emitted
 *   payloadCrc[4]  = CRC-32 (IEEE) of the payload bytes
 * ```
 *
 * The prelude exists so a reader can refuse, loudly and specifically,
 * anything that is not the payload it expects: wrong file (magic), wrong
 * era (version), wrong target (fullUid), truncation (payloadSize),
 * corruption (payloadCrc). layoutHash is carried for tooling and
 * audit: verifying it on board needs a per-component expected value,
 * which no reader here holds, so the checks stop at identity and
 * integrity.
 *
 * Every check maps to a distinct TprmPayloadCheck so the reject reason
 * reaches health telemetry as itself, not as a generic load failure. A
 * rejected payload leaves the component on its compiled defaults: boot
 * continues degraded-but-commandable rather than refusing outright.
 *
 * @note NOT RT-safe: file I/O; control-plane only.
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace system_core {
namespace system_component {

/* ----------------------------- Constants ----------------------------- */

/// Magic bytes for v3 component payloads (distinct from archive "TPRM").
inline constexpr std::array<char, 4> TPRM_PAYLOAD_MAGIC = {'A', 'P', 'V', '3'};

/// Payload format version the reader requires.
inline constexpr std::uint16_t TPRM_PAYLOAD_VERSION = 3;

/// Prelude size in bytes.
inline constexpr std::size_t TPRM_PAYLOAD_HEADER_SIZE = 20;

/* ----------------------------- Header ----------------------------- */

#pragma pack(push, 1)
struct TprmPayloadHeader {
  std::array<char, 4> magic{};  ///< "APV3"
  std::uint16_t version{0};     ///< Format version (3).
  std::uint16_t payloadSize{0}; ///< Byte length of the payload body.
  std::uint32_t fullUid{0};     ///< Target (componentId << 8) | instance.
  std::uint32_t layoutHash{0};  ///< CRC-32 of the canonical field spec.
  std::uint32_t payloadCrc{0};  ///< CRC-32 (IEEE) of the payload body.
};
#pragma pack(pop)
static_assert(sizeof(TprmPayloadHeader) == TPRM_PAYLOAD_HEADER_SIZE);

/* ----------------------------- Checks ----------------------------- */

/**
 * @enum TprmPayloadCheck
 * @brief Distinct verdict per prelude check; values are the fault codes
 *        the loadTprm paths report.
 */
enum class TprmPayloadCheck : std::uint8_t {
  OK = 0,
  FILE_ERROR = 1,         ///< Open/read failed.
  TOO_SMALL = 2,          ///< File smaller than the prelude.
  BAD_MAGIC = 3,          ///< Not a v3 payload.
  BAD_VERSION = 4,        ///< Version other than 3 (v3-only reader).
  SIZE_MISMATCH = 5,      ///< Body length differs from header payloadSize.
  UID_MISMATCH = 6,       ///< Payload targets a different fullUid.
  CRC_MISMATCH = 7,       ///< Body bytes fail the header CRC.
  BODY_SIZE_MISMATCH = 8, ///< Body length differs from the caller's TParams.
};

/// Fault-code form of a check verdict for log/telemetry surfaces.
[[nodiscard]] inline constexpr std::uint8_t toFaultCode(TprmPayloadCheck check) noexcept {
  return static_cast<std::uint8_t>(check);
}

/// Human-readable reject reason for logs.
[[nodiscard]] inline const char* toString(TprmPayloadCheck check) noexcept {
  switch (check) {
  case TprmPayloadCheck::OK:
    return "ok";
  case TprmPayloadCheck::FILE_ERROR:
    return "file open/read failed";
  case TprmPayloadCheck::TOO_SMALL:
    return "smaller than the v3 prelude";
  case TprmPayloadCheck::BAD_MAGIC:
    return "bad payload magic (want APV3)";
  case TprmPayloadCheck::BAD_VERSION:
    return "payload version not 3";
  case TprmPayloadCheck::SIZE_MISMATCH:
    return "body length differs from header";
  case TprmPayloadCheck::UID_MISMATCH:
    return "payload targets another fullUid";
  case TprmPayloadCheck::CRC_MISMATCH:
    return "payload CRC mismatch";
  case TprmPayloadCheck::BODY_SIZE_MISMATCH:
    return "body size differs from TParams";
  }
  return "unknown";
}

/* ----------------------------- CRC-32 ----------------------------- */

/// CRC-32/ISO-HDLC over the payload body, via the apex checksum
/// library (the rust stamper computes the same catalog CRC; the golden
/// vectors pin the two implementations to identical bytes).
[[nodiscard]] inline std::uint32_t tprmCrc32(const std::uint8_t* data, std::size_t size) noexcept {
  apex::checksums::crc::Crc32IsoHdlcTable crc;
  std::uint32_t out = 0;
  (void)crc.calculate(data, size, out);
  return out;
}

/* ----------------------------- Verification ----------------------------- */

/**
 * @brief Verify a v3-stamped buffer against the expected target.
 * @param data        Whole file image (prelude + body).
 * @param size        Image size in bytes.
 * @param expectedUid fullUid the caller expects the payload to target.
 * @param header      Parsed prelude on OK (optional).
 * @return OK or the first failed check.
 */
[[nodiscard]] inline TprmPayloadCheck
verifyTprmPayload(const std::uint8_t* data, std::size_t size, std::uint32_t expectedUid,
                  TprmPayloadHeader* header = nullptr) noexcept {
  if (size < TPRM_PAYLOAD_HEADER_SIZE) {
    return TprmPayloadCheck::TOO_SMALL;
  }
  TprmPayloadHeader hdr{};
  std::memcpy(&hdr, data, sizeof(hdr));
  if (hdr.magic != TPRM_PAYLOAD_MAGIC) {
    return TprmPayloadCheck::BAD_MAGIC;
  }
  if (hdr.version != TPRM_PAYLOAD_VERSION) {
    return TprmPayloadCheck::BAD_VERSION;
  }
  const std::size_t BODY = size - TPRM_PAYLOAD_HEADER_SIZE;
  if (BODY != hdr.payloadSize) {
    return TprmPayloadCheck::SIZE_MISMATCH;
  }
  if (hdr.fullUid != expectedUid) {
    return TprmPayloadCheck::UID_MISMATCH;
  }
  if (tprmCrc32(data + TPRM_PAYLOAD_HEADER_SIZE, BODY) != hdr.payloadCrc) {
    return TprmPayloadCheck::CRC_MISMATCH;
  }
  if (header != nullptr) {
    *header = hdr;
  }
  return TprmPayloadCheck::OK;
}

/**
 * @brief Read and verify a v3 payload file; return the body bytes.
 * @param path        Payload file ({fullUid:06x}.tprm).
 * @param expectedUid fullUid the caller expects.
 * @param body        Receives the verified payload body.
 * @return OK or the first failed check.
 */
[[nodiscard]] inline TprmPayloadCheck readTprmPayload(const std::filesystem::path& path,
                                                      std::uint32_t expectedUid,
                                                      std::vector<std::uint8_t>& body) noexcept {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return TprmPayloadCheck::FILE_ERROR;
  }
  const auto SIZE = static_cast<std::size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> image(SIZE);
  file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(SIZE));
  if (file.gcount() != static_cast<std::streamsize>(SIZE)) {
    return TprmPayloadCheck::FILE_ERROR;
  }
  const TprmPayloadCheck CHECK = verifyTprmPayload(image.data(), SIZE, expectedUid);
  if (CHECK != TprmPayloadCheck::OK) {
    return CHECK;
  }
  body.assign(image.begin() + static_cast<std::ptrdiff_t>(TPRM_PAYLOAD_HEADER_SIZE), image.end());
  return TprmPayloadCheck::OK;
}

/**
 * @brief Read and verify a v3 payload file into a fixed-size TParams.
 *
 * The body must be exactly sizeof(TParams) -- the pre-v3 size check,
 * now applied after the prelude checks.
 */
template <typename TParams>
[[nodiscard]] TprmPayloadCheck readTprmPayload(const std::filesystem::path& path,
                                               std::uint32_t expectedUid, TParams& out) noexcept {
  static_assert(std::is_trivially_copyable_v<TParams>);
  std::vector<std::uint8_t> body;
  const TprmPayloadCheck CHECK = readTprmPayload(path, expectedUid, body);
  if (CHECK != TprmPayloadCheck::OK) {
    return CHECK;
  }
  if (body.size() != sizeof(TParams)) {
    return TprmPayloadCheck::BODY_SIZE_MISMATCH;
  }
  std::memcpy(&out, body.data(), sizeof(TParams));
  return TprmPayloadCheck::OK;
}

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SYSTEM_COMPONENT_TPRM_PAYLOAD_HPP
