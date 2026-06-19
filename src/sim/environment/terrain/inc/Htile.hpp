#ifndef APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_HPP
#define APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_HPP
/**
 * @file Htile.hpp
 * @brief `.htile` binary file format primitives -- header struct + reader/writer.
 *
 * `.htile` = self-describing terrain tile. 128-byte header followed by
 * a row-major grid of int16 (or float32) elevation samples. Heights
 * are derived as `sample * scale_m_per_dn`, measured against the
 * body's declared reference surface.
 *
 * The format is a documented wire spec: any producer can emit it, and
 * any consumer can read it, by implementing the byte layout below.
 * This file is apex_csf's reader/writer implementation; producer-side
 * tools live independently. (E.g. horizon's `horizon_world` CLI emits
 * `.htile` files via its own implementation of the same spec; SRTM
 * `.hgt` and PDS IMG+LBL terrain data are losslessly converted into
 * the format upstream.)
 *
 * Header layout (128 bytes, packed, host-endian):
 *
 *   Identity (32 B):
 *     char     magic[4]          "HTIL"
 *     uint32_t version           kHtileVersion (currently 1)
 *     char     body[16]          NUL-padded body name
 *     uint32_t lod               0 = lowest res, +1 per doubling step
 *     uint32_t reserved_id
 *   Reference surface (24 B):
 *     char     ref_surface[16]   "egm96", "sphere", "ellipsoid"
 *     double   ref_radius_m
 *   Geographic extent (32 B):
 *     double   lat_min_deg
 *     double   lat_max_deg
 *     double   lon_min_deg
 *     double   lon_max_deg
 *   Sample grid (16 B):
 *     uint32_t dim_lat           rows
 *     uint32_t dim_lon           cols
 *     uint8_t  row_order         HtileRowOrder
 *     uint8_t  sample_type       HtileSampleType
 *     uint8_t  endianness        HtileEndian (sample byte order)
 *     uint8_t  reserved_grid[5]
 *   Sample interpretation (16 B):
 *     int32_t  void_value
 *     uint32_t reserved_void
 *     double   scale_m_per_dn    height_m = sample * scale_m_per_dn
 *   Provenance (8 B):
 *     uint64_t spec_hash         0 for converted real data
 *
 * Body: dim_lat * dim_lon samples in row-major order, sample_type-sized.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace sim {
namespace environment {
namespace terrain {

/* ----------------------------- Constants ----------------------------- */

/// Magic bytes identifying an htile file: "HTIL".
inline constexpr char kHtileMagic[4] = {'H', 'T', 'I', 'L'};

/// Current htile format version.
inline constexpr std::uint32_t kHtileVersion = 1u;

/// Header size in bytes.
inline constexpr std::size_t kHtileHeaderSize = 128u;

/// Conventional void value for int16 samples (matches SRTM / LOLA convention).
inline constexpr std::int32_t kHtileVoidInt16 = -32768;

/* ----------------------------- Enums ----------------------------- */

/// Sample storage type. Determines bytes per sample.
enum class HtileSampleType : std::uint8_t {
  kInt16 = 0,   ///< 2-byte signed integer (default for terrain).
  kFloat32 = 1, ///< 4-byte IEEE 754 single-precision.
};

/// Row-major sample storage order.
enum class HtileRowOrder : std::uint8_t {
  kNorthToSouth = 0, ///< Row 0 = north edge. Canonical.
  kSouthToNorth = 1,
};

/// Sample byte order. Header is always host-endian.
enum class HtileEndian : std::uint8_t {
  kLittle = 0,
  kBig = 1,
};

/* ----------------------------- HtileHeader ----------------------------- */

/**
 * @brief 128-byte packed on-disk header for an htile file.
 *
 * Multi-byte fields are host-endian. Sample byte order (for the body that
 * follows the header) is configurable via the `endianness` field.
 */
#pragma pack(push, 1)
struct HtileHeader {
  /* Identity (32 B) */
  char magic[4];
  std::uint32_t version;
  char body[16];
  std::uint32_t lod;
  std::uint32_t reserved_id;

  /* Reference surface (24 B) */
  char ref_surface[16];
  double ref_radius_m;

  /* Geographic extent (32 B) */
  double lat_min_deg;
  double lat_max_deg;
  double lon_min_deg;
  double lon_max_deg;

  /* Sample grid (16 B) */
  std::uint32_t dim_lat;
  std::uint32_t dim_lon;
  std::uint8_t row_order;
  std::uint8_t sample_type;
  std::uint8_t endianness;
  std::uint8_t reserved_grid[5];

  /* Sample interpretation (16 B) */
  std::int32_t void_value;
  std::uint32_t reserved_void;
  double scale_m_per_dn;

  /* Provenance (8 B) */
  std::uint64_t spec_hash;
};
#pragma pack(pop)

static_assert(sizeof(HtileHeader) == kHtileHeaderSize, "HtileHeader must be 128 bytes");

/* ----------------------------- API ----------------------------- */

/// Returns size in bytes of a single sample for the given type.
/// Returns 0 for unknown types.
std::size_t htileSampleSize(HtileSampleType type) noexcept;

/// Returns the on-disk sample body size in bytes for `h`:
/// `dim_lat * dim_lon * sampleSize(sample_type)`.
/// Returns 0 if the sample type is unknown.
std::size_t htileBodySize(const HtileHeader& h) noexcept;

/// Returns true iff `h.magic` equals "HTIL".
bool htileMagicValid(const HtileHeader& h) noexcept;

/// Returns true iff `h` passes structural validation:
/// magic correct, version supported, dims non-zero, enums in range,
/// bounds finite + ordered, ref_radius_m > 0, scale_m_per_dn > 0.
bool htileHeaderValid(const HtileHeader& h) noexcept;

/// Initialize a header to defaults: magic, version, scale = 1.0,
/// row_order = kNorthToSouth, sample_type = kInt16, endianness = kLittle,
/// void_value = kHtileVoidInt16. All other fields zeroed.
/// Caller must still populate body name, dims, bounds, ref surface, radius.
void htileHeaderInit(HtileHeader& h) noexcept;

/* ----------------------------- HtileWriter ----------------------------- */

/**
 * @brief Streaming writer for an htile file.
 *
 * Usage:
 *   HtileWriter w;
 *   if (!w.open("foo.htile", header)) { ... handle error ... }
 *   if (!w.writeAllSamples(buffer, htileBodySize(header))) { ... }
 *   w.close();
 *
 * @note NOT RT-safe: file I/O.
 */
class HtileWriter {
public:
  HtileWriter() noexcept = default;
  ~HtileWriter() noexcept;

  HtileWriter(const HtileWriter&) = delete;
  HtileWriter& operator=(const HtileWriter&) = delete;

  /// Open `path` for writing and emit the header.
  /// Returns false if `header` is invalid or the file can't be opened.
  bool open(const char* path, const HtileHeader& header) noexcept;

  /// Write the sample body. `srcBytes` must equal `htileBodySize(header())`.
  /// Returns false on size mismatch or I/O error.
  bool writeAllSamples(const void* src, std::size_t srcBytes) noexcept;

  /// Close the file. Idempotent.
  void close() noexcept;

  bool isOpen() const noexcept { return out_ != nullptr; }
  const HtileHeader& header() const noexcept { return header_; }

private:
  std::FILE* out_ = nullptr;
  HtileHeader header_{};
};

/* ----------------------------- HtileReader ----------------------------- */

/**
 * @brief Streaming reader for an htile file.
 *
 * Usage:
 *   HtileReader r;
 *   if (!r.open("foo.htile")) { ... handle error ... }
 *   const HtileHeader& h = r.header();
 *   std::vector<std::int16_t> buf(h.dim_lat * h.dim_lon);
 *   if (!r.readAllSamples(buf.data(), buf.size() * sizeof(std::int16_t))) { ... }
 *   r.close();
 *
 * @note NOT RT-safe: file I/O.
 */
class HtileReader {
public:
  HtileReader() noexcept = default;
  ~HtileReader() noexcept;

  HtileReader(const HtileReader&) = delete;
  HtileReader& operator=(const HtileReader&) = delete;

  /// Open `path`, read and validate the header.
  /// Returns false on I/O error, truncated header, bad magic, unsupported
  /// version, or any other structural validation failure.
  bool open(const char* path) noexcept;

  /// Read the sample body into `dst`. `dstBytes` must equal
  /// `htileBodySize(header())`. Returns false on size mismatch or I/O error.
  bool readAllSamples(void* dst, std::size_t dstBytes) noexcept;

  /// Close the file. Idempotent.
  void close() noexcept;

  bool isOpen() const noexcept { return in_ != nullptr; }
  const HtileHeader& header() const noexcept { return header_; }

private:
  std::FILE* in_ = nullptr;
  HtileHeader header_{};
};

} // namespace terrain
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_TERRAIN_HTILE_HPP
