/**
 * @file HtileTile.cpp
 * @brief Implementation of the HtileTile terrain consumer.
 */

#include "src/utilities/math/vecmat/inc/Angles.hpp"
#include "src/sim/environment/terrain/inc/HtileTile.hpp"

#include "src/sim/environment/terrain/inc/Htile.hpp"
#include "src/sim/environment/terrain/inc/TerrainModelBase.hpp"
#include "src/sim/environment/terrain/inc/TerrainStatus.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- Constants ----------------------------- */

namespace {
inline constexpr double K_DEG_PER_RAD = apex::math::vecmat::RAD_TO_DEG;

/// Normalize `lonDeg` into the tile's declared `[lonMin, lonMax]` window by
/// shifting it by whole turns of 360 deg so it lands in the half-open band
/// `[lonMin, lonMin + 360)`. htile tiles may declare a 0..360 east-longitude
/// window (LOLA convention) while `elevationAtEcef`'s atan2 produces a
/// normalized [-180, 180] longitude; this brings the query into the tile's
/// frame before any bounds comparison. A longitude genuinely outside a
/// sub-360 window stays outside (one canonical shift cannot bridge the gap).
/// Degrees in, degrees out.
inline double normalizeLonDeg(double lonDeg, double lonMin, double /*lonMax*/) noexcept {
  // Reduce (lonDeg - lonMin) into [0, 360) via a single fmod-based shift.
  double offset = std::fmod(lonDeg - lonMin, 360.0);
  if (offset < 0.0) {
    offset += 360.0;
  }
  return lonMin + offset;
}
} // namespace

/* ----------------------------- HtileTile Methods ----------------------------- */

Status HtileTile::load(const std::string& path) noexcept {
  close();
  HtileReader r;
  if (!r.open(path.c_str())) {
    // HtileReader::open fails on both a missing/unreadable path and a
    // structurally invalid header. Distinguish by re-probing the path:
    // if it cannot be opened for reading at all, it's a path error;
    // otherwise the header/body failed validation.
    std::FILE* probe = std::fopen(path.c_str(), "rb");
    if (probe == nullptr) {
      return Status::ERROR_DATA_PATH_INVALID;
    }
    std::fclose(probe);
    return Status::ERROR_FILE_FORMAT_INVALID;
  }
  // Currently support int16 only; htile v1's other supported type
  // (float32) needs a separate code path that we don't need yet.
  if (r.header().sample_type != static_cast<std::uint8_t>(HtileSampleType::kInt16)) {
    return Status::ERROR_SAMPLE_TYPE_UNSUPPORTED;
  }
  // B2: the on-disk void_value is int32; samples are int16. A void_value
  // outside the int16 range can never match any sample, silently disabling
  // void rejection. Reject such a tile as a format error at load time.
  if (r.header().void_value < std::numeric_limits<std::int16_t>::min() ||
      r.header().void_value > std::numeric_limits<std::int16_t>::max()) {
    return Status::ERROR_FILE_FORMAT_INVALID;
  }
  const std::size_t COUNT = static_cast<std::size_t>(r.header().dim_lat) * r.header().dim_lon;
  std::vector<std::int16_t> samples;
  try {
    samples.resize(COUNT);
  } catch (...) {
    return Status::ERROR_ALLOC_FAIL;
  }
  if (!r.readAllSamples(samples.data(), COUNT * sizeof(std::int16_t))) {
    return Status::ERROR_FILE_FORMAT_INVALID;
  }
  header_ = r.header();
  samples_ = std::move(samples);

  // Approximate per-cell ground spacing in meters. Use the body's
  // reference radius and the lat span; longitude span at the equator
  // collapses to the same arc length per degree as latitude.
  const double LAT_SPAN_DEG = header_.lat_max_deg - header_.lat_min_deg;
  const double METERS_PER_DEG = (header_.ref_radius_m * 2.0 * 3.141592653589793) / 360.0;
  resolution_m_ = (LAT_SPAN_DEG * METERS_PER_DEG) / static_cast<double>(header_.dim_lat);
  return Status::SUCCESS;
}

void HtileTile::close() noexcept {
  header_ = HtileHeader{};
  samples_.clear();
  samples_.shrink_to_fit();
  resolution_m_ = 0.0;
}

Status HtileTile::elevationAt(double latRad, double lonRad, double& H) const noexcept {
  if (samples_.empty()) {
    return Status::ERROR_NOT_INITIALIZED;
  }
  const double LAT_DEG = latRad * K_DEG_PER_RAD;
  // B4: bring the query longitude into the tile's declared window (which may
  // be 0..360) before the bounds comparison, so a [-180, 180] longitude from
  // elevationAtEcef is accepted by a 0..360 tile.
  const double LON_DEG =
      normalizeLonDeg(lonRad * K_DEG_PER_RAD, header_.lon_min_deg, header_.lon_max_deg);
  if (LAT_DEG < header_.lat_min_deg || LAT_DEG > header_.lat_max_deg ||
      LON_DEG < header_.lon_min_deg || LON_DEG > header_.lon_max_deg) {
    return Status::WARN_OUTSIDE_COVERAGE;
  }
  // Continuous (col, row) into the tile. row 0 = lat_max (htile canonical N->S).
  const double LAT_SPAN = header_.lat_max_deg - header_.lat_min_deg;
  const double LON_SPAN = header_.lon_max_deg - header_.lon_min_deg;
  const double FRAC_LAT = (header_.lat_max_deg - LAT_DEG) / LAT_SPAN; // 0 at north, 1 at south
  const double FRAC_LON = (LON_DEG - header_.lon_min_deg) / LON_SPAN;
  const double CY = FRAC_LAT * static_cast<double>(header_.dim_lat - 1);
  const double CX = FRAC_LON * static_cast<double>(header_.dim_lon - 1);

  const std::uint32_t Y0 = static_cast<std::uint32_t>(std::floor(CY));
  const std::uint32_t X0 = static_cast<std::uint32_t>(std::floor(CX));
  const std::uint32_t Y1 = std::min<std::uint32_t>(Y0 + 1, header_.dim_lat - 1);
  const std::uint32_t X1 = std::min<std::uint32_t>(X0 + 1, header_.dim_lon - 1);
  const double FY = CY - static_cast<double>(Y0);
  const double FX = CX - static_cast<double>(X0);

  const auto idx = [this](std::uint32_t y, std::uint32_t x) {
    return static_cast<std::size_t>(y) * header_.dim_lon + x;
  };
  const std::int16_t S00 = samples_[idx(Y0, X0)];
  const std::int16_t S01 = samples_[idx(Y0, X1)];
  const std::int16_t S10 = samples_[idx(Y1, X0)];
  const std::int16_t S11 = samples_[idx(Y1, X1)];
  // B2: void_value is int32 on disk but samples are int16; compare against
  // the int16-narrowed marker. load() has already guaranteed void_value is
  // representable as int16, so this narrowing is lossless.
  const std::int16_t VOID16 = static_cast<std::int16_t>(header_.void_value);
  if (S00 == VOID16 || S01 == VOID16 || S10 == VOID16 || S11 == VOID16) {
    return Status::WARN_VOID_DATA;
  }

  const double H00 = static_cast<double>(S00) * header_.scale_m_per_dn;
  const double H01 = static_cast<double>(S01) * header_.scale_m_per_dn;
  const double H10 = static_cast<double>(S10) * header_.scale_m_per_dn;
  const double H11 = static_cast<double>(S11) * header_.scale_m_per_dn;

  H = H00 * (1.0 - FX) * (1.0 - FY) + H01 * FX * (1.0 - FY) + H10 * (1.0 - FX) * FY + H11 * FX * FY;
  return Status::SUCCESS;
}

Status HtileTile::elevationAtEcef(const double ecef[3], double& H) const noexcept {
  if (ecef == nullptr) {
    return Status::ERROR_PARAM_BUFFER_NULL;
  }
  // Spherical approximation: lat = atan2(z, sqrt(x^2 + y^2)), lon = atan2(y, x).
  const double R_HORIZ = std::sqrt(ecef[0] * ecef[0] + ecef[1] * ecef[1]);
  const double LAT = std::atan2(ecef[2], R_HORIZ);
  const double LON = std::atan2(ecef[1], ecef[0]);
  return elevationAt(LAT, LON, H);
}

bool HtileTile::isInCoverage(double latRad, double lonRad) const noexcept {
  const double LAT_DEG = latRad * K_DEG_PER_RAD;
  // B4: normalize into the tile's (possibly 0..360) longitude window first.
  const double LON_DEG =
      normalizeLonDeg(lonRad * K_DEG_PER_RAD, header_.lon_min_deg, header_.lon_max_deg);
  return LAT_DEG >= header_.lat_min_deg && LAT_DEG <= header_.lat_max_deg &&
         LON_DEG >= header_.lon_min_deg && LON_DEG <= header_.lon_max_deg;
}

double HtileTile::minLatRad() const noexcept { return header_.lat_min_deg / K_DEG_PER_RAD; }
double HtileTile::maxLatRad() const noexcept { return header_.lat_max_deg / K_DEG_PER_RAD; }
double HtileTile::minLonRad() const noexcept { return header_.lon_min_deg / K_DEG_PER_RAD; }
double HtileTile::maxLonRad() const noexcept { return header_.lon_max_deg / K_DEG_PER_RAD; }

} // namespace terrain
} // namespace environment
} // namespace sim
