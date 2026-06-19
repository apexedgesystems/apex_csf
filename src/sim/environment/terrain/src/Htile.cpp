/**
 * @file Htile.cpp
 * @brief Implementation of `.htile` file format primitives. See Htile.hpp
 *        for the wire-spec description.
 */

#include "src/sim/environment/terrain/inc/Htile.hpp"

#include <cmath>
#include <cstring>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/// Read exactly N bytes from `f` into `dst`. Returns false on EOF or error.
bool readExact(std::FILE* f, void* dst, std::size_t bytes) noexcept {
  if (bytes == 0) {
    return true;
  }
  const std::size_t GOT = std::fread(dst, 1, bytes, f);
  return GOT == bytes;
}

/// Write exactly N bytes from `src` to `f`. Returns false on error.
bool writeExact(std::FILE* f, const void* src, std::size_t bytes) noexcept {
  if (bytes == 0) {
    return true;
  }
  const std::size_t WROTE = std::fwrite(src, 1, bytes, f);
  return WROTE == bytes;
}

} // namespace

/* ----------------------------- API ----------------------------- */

std::size_t htileSampleSize(HtileSampleType type) noexcept {
  switch (type) {
  case HtileSampleType::kInt16:
    return 2u;
  case HtileSampleType::kFloat32:
    return 4u;
  }
  return 0u;
}

std::size_t htileBodySize(const HtileHeader& h) noexcept {
  const std::size_t SAMPLE_SIZE = htileSampleSize(static_cast<HtileSampleType>(h.sample_type));
  if (SAMPLE_SIZE == 0u) {
    return 0u;
  }
  // Use 64-bit math; dim_lat and dim_lon are uint32_t so product fits in 64-bit.
  return static_cast<std::size_t>(h.dim_lat) * static_cast<std::size_t>(h.dim_lon) * SAMPLE_SIZE;
}

bool htileMagicValid(const HtileHeader& h) noexcept {
  return h.magic[0] == kHtileMagic[0] && h.magic[1] == kHtileMagic[1] &&
         h.magic[2] == kHtileMagic[2] && h.magic[3] == kHtileMagic[3];
}

bool htileHeaderValid(const HtileHeader& h) noexcept {
  if (!htileMagicValid(h)) {
    return false;
  }
  if (h.version != kHtileVersion) {
    return false;
  }
  if (h.dim_lat == 0u || h.dim_lon == 0u) {
    return false;
  }
  if (h.sample_type > static_cast<std::uint8_t>(HtileSampleType::kFloat32)) {
    return false;
  }
  if (h.row_order > static_cast<std::uint8_t>(HtileRowOrder::kSouthToNorth)) {
    return false;
  }
  if (h.endianness > static_cast<std::uint8_t>(HtileEndian::kBig)) {
    return false;
  }
  if (!std::isfinite(h.lat_min_deg) || !std::isfinite(h.lat_max_deg) ||
      !std::isfinite(h.lon_min_deg) || !std::isfinite(h.lon_max_deg)) {
    return false;
  }
  if (h.lat_max_deg <= h.lat_min_deg || h.lon_max_deg <= h.lon_min_deg) {
    return false;
  }
  if (!std::isfinite(h.ref_radius_m) || h.ref_radius_m <= 0.0) {
    return false;
  }
  if (!std::isfinite(h.scale_m_per_dn) || h.scale_m_per_dn <= 0.0) {
    return false;
  }
  return true;
}

void htileHeaderInit(HtileHeader& h) noexcept {
  std::memset(&h, 0, sizeof(HtileHeader));
  std::memcpy(h.magic, kHtileMagic, sizeof(kHtileMagic));
  h.version = kHtileVersion;
  h.row_order = static_cast<std::uint8_t>(HtileRowOrder::kNorthToSouth);
  h.sample_type = static_cast<std::uint8_t>(HtileSampleType::kInt16);
  h.endianness = static_cast<std::uint8_t>(HtileEndian::kLittle);
  h.void_value = kHtileVoidInt16;
  h.scale_m_per_dn = 1.0;
}

/* ----------------------------- HtileWriter Methods ----------------------------- */

HtileWriter::~HtileWriter() noexcept { close(); }

bool HtileWriter::open(const char* path, const HtileHeader& header) noexcept {
  if (out_ != nullptr) {
    return false;
  }
  if (!htileHeaderValid(header)) {
    return false;
  }
  out_ = std::fopen(path, "wb");
  if (out_ == nullptr) {
    return false;
  }
  if (!writeExact(out_, &header, sizeof(HtileHeader))) {
    std::fclose(out_);
    out_ = nullptr;
    return false;
  }
  header_ = header;
  return true;
}

bool HtileWriter::writeAllSamples(const void* src, std::size_t srcBytes) noexcept {
  if (out_ == nullptr || src == nullptr) {
    return false;
  }
  if (srcBytes != htileBodySize(header_)) {
    return false;
  }
  return writeExact(out_, src, srcBytes);
}

void HtileWriter::close() noexcept {
  if (out_ != nullptr) {
    std::fclose(out_);
    out_ = nullptr;
  }
}

/* ----------------------------- HtileReader Methods ----------------------------- */

HtileReader::~HtileReader() noexcept { close(); }

bool HtileReader::open(const char* path) noexcept {
  if (in_ != nullptr) {
    return false;
  }
  in_ = std::fopen(path, "rb");
  if (in_ == nullptr) {
    return false;
  }
  HtileHeader hdr{};
  if (!readExact(in_, &hdr, sizeof(HtileHeader))) {
    std::fclose(in_);
    in_ = nullptr;
    return false;
  }
  if (!htileHeaderValid(hdr)) {
    std::fclose(in_);
    in_ = nullptr;
    return false;
  }
  header_ = hdr;
  return true;
}

bool HtileReader::readAllSamples(void* dst, std::size_t dstBytes) noexcept {
  if (in_ == nullptr || dst == nullptr) {
    return false;
  }
  if (dstBytes != htileBodySize(header_)) {
    return false;
  }
  return readExact(in_, dst, dstBytes);
}

void HtileReader::close() noexcept {
  if (in_ != nullptr) {
    std::fclose(in_);
    in_ = nullptr;
  }
}

} // namespace terrain
} // namespace environment
} // namespace sim
