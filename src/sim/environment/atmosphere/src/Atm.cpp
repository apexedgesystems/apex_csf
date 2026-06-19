/**
 * @file Atm.cpp
 * @brief Implementation of `.atm` file format primitives. See Atm.hpp
 *        for the wire-spec description.
 */

#include "src/sim/environment/atmosphere/inc/Atm.hpp"

#include <cmath>
#include <cstring>

namespace sim {
namespace environment {
namespace atmosphere {

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

std::size_t atmBodySize(const AtmHeader& h) noexcept {
  return static_cast<std::size_t>(h.n_records) * kAtmRecordSize;
}

bool atmMagicValid(const AtmHeader& h) noexcept {
  return h.magic[0] == kAtmMagic[0] && h.magic[1] == kAtmMagic[1] && h.magic[2] == kAtmMagic[2] &&
         h.magic[3] == kAtmMagic[3];
}

std::uint16_t atmExpectedRecordCount(AtmModelType type) noexcept {
  switch (type) {
  case AtmModelType::kConstant:
  case AtmModelType::kExponential:
  case AtmModelType::kEmpirical:
    return 1u;
  case AtmModelType::kLayered:
    // Layered models accept any positive count; caller decides.
    return 0u;
  }
  return 0u;
}

bool atmHeaderValid(const AtmHeader& h) noexcept {
  if (!atmMagicValid(h)) {
    return false;
  }
  if (h.version != kAtmVersion) {
    return false;
  }
  if (h.model_type > static_cast<std::uint8_t>(AtmModelType::kEmpirical)) {
    return false;
  }
  // Per-model_type record-count check.
  const AtmModelType TYPE = static_cast<AtmModelType>(h.model_type);
  const std::uint16_t EXPECTED = atmExpectedRecordCount(TYPE);
  if (EXPECTED == 0u) {
    // Layered: must be at least 1.
    if (h.n_records == 0u) {
      return false;
    }
  } else {
    if (h.n_records != EXPECTED) {
      return false;
    }
  }
  // Thermodynamic invariants.
  if (!std::isfinite(h.R_specific) || h.R_specific <= 0.0) {
    return false;
  }
  if (!std::isfinite(h.gamma) || h.gamma <= 1.0) {
    return false;
  }
  if (!std::isfinite(h.g0) || h.g0 <= 0.0) {
    return false;
  }
  return true;
}

void atmHeaderInit(AtmHeader& h) noexcept {
  std::memset(&h, 0, sizeof(AtmHeader));
  std::memcpy(h.magic, kAtmMagic, sizeof(kAtmMagic));
  h.version = kAtmVersion;
  h.model_type = static_cast<std::uint8_t>(AtmModelType::kConstant);
  h.n_records = 1u;
  h.R_specific = 287.058; // Earth dry air; caller may override per body
  h.gamma = 1.4;          // diatomic gas
  h.g0 = 9.80665;         // Earth surface gravity
}

/* ----------------------------- Record helpers ----------------------------- */

AtmRecord atmMakeConstant(double rho0, double T0, double P0) noexcept {
  return AtmRecord{rho0, T0, P0, 0.0};
}

AtmRecord atmMakeExponential(double rho0, double T0, double H_m) noexcept {
  return AtmRecord{rho0, T0, H_m, 0.0};
}

AtmRecord atmMakeLayer(double base_alt_m, double base_T_K, double base_P_Pa,
                       double lapse_K_per_m) noexcept {
  return AtmRecord{base_alt_m, base_T_K, base_P_Pa, lapse_K_per_m};
}

/* ----------------------------- AtmWriter Methods ----------------------------- */

AtmWriter::~AtmWriter() noexcept { close(); }

bool AtmWriter::open(const char* path, const AtmHeader& header) noexcept {
  if (out_ != nullptr) {
    return false;
  }
  if (!atmHeaderValid(header)) {
    return false;
  }
  out_ = std::fopen(path, "wb");
  if (out_ == nullptr) {
    return false;
  }
  if (!writeExact(out_, &header, sizeof(AtmHeader))) {
    std::fclose(out_);
    out_ = nullptr;
    return false;
  }
  header_ = header;
  return true;
}

bool AtmWriter::writeAllRecords(const AtmRecord* src, std::size_t nRecords) noexcept {
  if (out_ == nullptr || src == nullptr) {
    return false;
  }
  if (nRecords != header_.n_records) {
    return false;
  }
  return writeExact(out_, src, nRecords * sizeof(AtmRecord));
}

void AtmWriter::close() noexcept {
  if (out_ != nullptr) {
    std::fclose(out_);
    out_ = nullptr;
  }
}

/* ----------------------------- AtmReader Methods ----------------------------- */

AtmReader::~AtmReader() noexcept { close(); }

bool AtmReader::open(const char* path) noexcept {
  if (in_ != nullptr) {
    return false;
  }
  in_ = std::fopen(path, "rb");
  if (in_ == nullptr) {
    return false;
  }
  AtmHeader hdr{};
  if (!readExact(in_, &hdr, sizeof(AtmHeader))) {
    std::fclose(in_);
    in_ = nullptr;
    return false;
  }
  if (!atmHeaderValid(hdr)) {
    std::fclose(in_);
    in_ = nullptr;
    return false;
  }
  header_ = hdr;
  return true;
}

bool AtmReader::readAllRecords(AtmRecord* dst, std::size_t nRecords) noexcept {
  if (in_ == nullptr || dst == nullptr) {
    return false;
  }
  if (nRecords != header_.n_records) {
    return false;
  }
  return readExact(in_, dst, nRecords * sizeof(AtmRecord));
}

void AtmReader::close() noexcept {
  if (in_ != nullptr) {
    std::fclose(in_);
    in_ = nullptr;
  }
}

} // namespace atmosphere
} // namespace environment
} // namespace sim
