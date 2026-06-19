#ifndef APEX_SIM_ENVIRONMENT_ATMOSPHERE_ATM_HPP
#define APEX_SIM_ENVIRONMENT_ATMOSPHERE_ATM_HPP
/**
 * @file Atm.hpp
 * @brief `.atm` binary file format primitives -- header struct + reader/writer.
 *
 * The `.atm` file format is a portable self-describing atmosphere
 * parameter file: 64-byte header followed by N 32-byte parameter
 * records. The header carries:
 *   - identity (magic, version, body name)
 *   - model fidelity discriminator (constant, exponential, layered, empirical)
 *   - thermodynamic constants common to every model (R_specific, gamma, g0)
 *   - record count + provenance hash
 *
 * The payload schema depends on `model_type`:
 *   CONSTANT    : 1 record  { rho0, T0, P0, _pad }
 *   EXPONENTIAL : 1 record  { rho0, T0, scale_height_m, _pad }
 *   LAYERED     : N records { base_alt_m, base_T_K, base_P_Pa, lapse_K_per_m }
 *   EMPIRICAL   : 1 record  { model_id_hash, _pad, _pad, _pad }
 *
 * The format is a documented wire spec: any producer can emit it, and
 * any consumer can read it, by implementing the byte layout below.
 * This file is apex_csf's reader/writer implementation; producer-side
 * tools live independently. (E.g. horizon's `horizon_world` CLI emits
 * `.atm` files via its own implementation of the same spec.)
 *
 * Header layout (64 bytes, packed, host-endian):
 *
 *   Identity (24 B):
 *     char     magic[4]        "ATM\0"
 *     uint32_t version         kAtmVersion (currently 1)
 *     char     body[16]        NUL-padded body name
 *   Discriminator + count (8 B):
 *     uint8_t  model_type      AtmModelType
 *     uint8_t  reserved_disc[3]
 *     uint16_t n_records       Number of 32-byte payload records
 *     uint16_t reserved_n
 *   Thermodynamics (24 B):
 *     double   R_specific      [J/(kg*K)]
 *     double   gamma           cp/cv
 *     double   g0              [m/s^2] surface gravity (for hydrostatic)
 *   Provenance (8 B):
 *     uint64_t spec_hash       0 for converted-from-paper (USSA76), else seed
 *
 * Body: n_records * 32 bytes. Each record is 4 doubles, schema as above.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace sim {
namespace environment {
namespace atmosphere {

/* ----------------------------- Constants ----------------------------- */

/// Magic bytes identifying an atm file: "ATM\0".
inline constexpr char kAtmMagic[4] = {'A', 'T', 'M', '\0'};

/// Current atm format version.
inline constexpr std::uint32_t kAtmVersion = 1u;

/// Header size in bytes.
inline constexpr std::size_t kAtmHeaderSize = 64u;

/// Per-record size in bytes (always 4 doubles regardless of model_type).
inline constexpr std::size_t kAtmRecordSize = 32u;

/* ----------------------------- Enums ----------------------------- */

/// Atmosphere model fidelity. The byte stored in the header `model_type`
/// field; consumers branch on this to choose the correct payload-record
/// interpretation and runtime model.
enum class AtmModelType : std::uint8_t {
  kConstant = 0,    ///< rho/T/P held constant (vacuum when rho == 0).
  kExponential = 1, ///< rho(h) = rho0 * exp(-h/H), isothermal.
  kLayered = 2,     ///< Hydrostatic piecewise-linear-T layers (USSA76-class).
  kEmpirical = 3,   ///< Curve-fit-tabulated (NRLMSISE-00 et al). Pointer record only.
};

/* ----------------------------- AtmHeader ----------------------------- */

/**
 * @brief 64-byte packed on-disk header for an atm file.
 *
 * Multi-byte fields are host-endian. The thermodynamic constants
 * (R_specific, gamma, g0) are model-agnostic per-body data; every model
 * type uses them when computing derived quantities (e.g., speed of sound,
 * or hydrostatic pressure for the layered model).
 */
#pragma pack(push, 1)
struct AtmHeader {
  /* Identity (24 B) */
  char magic[4];
  std::uint32_t version;
  char body[16];

  /* Discriminator + count (8 B) */
  std::uint8_t model_type;
  std::uint8_t reserved_disc[3];
  std::uint16_t n_records;
  std::uint16_t reserved_n;

  /* Thermodynamics (24 B) */
  double R_specific;
  double gamma;
  double g0;

  /* Provenance (8 B) */
  std::uint64_t spec_hash;
};
#pragma pack(pop)

static_assert(sizeof(AtmHeader) == kAtmHeaderSize, "AtmHeader must be 64 bytes");

/* ----------------------------- AtmRecord ----------------------------- */

/**
 * @brief 32-byte parameter record. Four doubles whose meaning depends on
 *        the parent header's model_type. Use the named accessors below
 *        rather than reading f0..f3 directly.
 */
#pragma pack(push, 1)
struct AtmRecord {
  double f0;
  double f1;
  double f2;
  double f3;
};
#pragma pack(pop)

static_assert(sizeof(AtmRecord) == kAtmRecordSize, "AtmRecord must be 32 bytes");

/* ----------------------------- API ----------------------------- */

/// Returns the on-disk payload body size in bytes:
/// `n_records * kAtmRecordSize`.
std::size_t atmBodySize(const AtmHeader& h) noexcept;

/// Returns true iff `h.magic` equals "ATM\0".
bool atmMagicValid(const AtmHeader& h) noexcept;

/// Returns true iff `h` passes structural validation:
/// magic correct, version supported, model_type in range, n_records
/// matches the model_type's expected count (1 for non-layered),
/// R_specific > 0, gamma > 1, g0 > 0.
bool atmHeaderValid(const AtmHeader& h) noexcept;

/// Returns the expected number of records for a given model_type:
///   CONSTANT, EXPONENTIAL, EMPIRICAL: 1
///   LAYERED: any positive count (must be set by caller)
/// For layered, returns 0 to mean "any positive value is OK".
std::uint16_t atmExpectedRecordCount(AtmModelType type) noexcept;

/// Initialize a header to defaults: magic, version, model_type =
/// kConstant, n_records = 1, R_specific = 287.058 (Earth dry air),
/// gamma = 1.4, g0 = 9.80665. All other fields zeroed.
/// Caller must still populate body name and any per-model fields.
void atmHeaderInit(AtmHeader& h) noexcept;

/* ----------------------------- Record helpers ----------------------------- */

/// Build a CONSTANT record from (rho0, T0, P0). f3 is reserved.
AtmRecord atmMakeConstant(double rho0, double T0, double P0) noexcept;

/// Build an EXPONENTIAL record from (rho0, T0, scale_height_m). f3 reserved.
AtmRecord atmMakeExponential(double rho0, double T0, double H_m) noexcept;

/// Build a LAYERED record from (base_alt_m, base_T_K, base_P_Pa, lapse_K_per_m).
AtmRecord atmMakeLayer(double base_alt_m, double base_T_K, double base_P_Pa,
                       double lapse_K_per_m) noexcept;

/* ----------------------------- AtmWriter ----------------------------- */

/**
 * @brief Streaming writer for an atm file.
 *
 * Usage:
 *   AtmWriter w;
 *   if (!w.open("foo.atm", header)) { ... handle error ... }
 *   if (!w.writeAllRecords(records.data(), records.size())) { ... }
 *   w.close();
 *
 * @note NOT RT-safe: file I/O.
 */
class AtmWriter {
public:
  AtmWriter() noexcept = default;
  ~AtmWriter() noexcept;

  AtmWriter(const AtmWriter&) = delete;
  AtmWriter& operator=(const AtmWriter&) = delete;

  /// Open `path` for writing and emit the header.
  /// Returns false if `header` is invalid or the file can't be opened.
  bool open(const char* path, const AtmHeader& header) noexcept;

  /// Write the record payload. `nRecords` must equal `header().n_records`.
  /// Returns false on size mismatch or I/O error.
  bool writeAllRecords(const AtmRecord* src, std::size_t nRecords) noexcept;

  /// Close the file. Idempotent.
  void close() noexcept;

  bool isOpen() const noexcept { return out_ != nullptr; }
  const AtmHeader& header() const noexcept { return header_; }

private:
  std::FILE* out_ = nullptr;
  AtmHeader header_{};
};

/* ----------------------------- AtmReader ----------------------------- */

/**
 * @brief Streaming reader for an atm file.
 *
 * Usage:
 *   AtmReader r;
 *   if (!r.open("foo.atm")) { ... handle error ... }
 *   const AtmHeader& h = r.header();
 *   std::vector<AtmRecord> recs(h.n_records);
 *   if (!r.readAllRecords(recs.data(), recs.size())) { ... }
 *   r.close();
 *
 * @note NOT RT-safe: file I/O.
 */
class AtmReader {
public:
  AtmReader() noexcept = default;
  ~AtmReader() noexcept;

  AtmReader(const AtmReader&) = delete;
  AtmReader& operator=(const AtmReader&) = delete;

  /// Open `path`, read and validate the header.
  /// Returns false on I/O error, truncated header, bad magic, unsupported
  /// version, or any other structural validation failure.
  bool open(const char* path) noexcept;

  /// Read the record payload into `dst`. `nRecords` must equal
  /// `header().n_records`. Returns false on size mismatch or I/O error.
  bool readAllRecords(AtmRecord* dst, std::size_t nRecords) noexcept;

  /// Close the file. Idempotent.
  void close() noexcept;

  bool isOpen() const noexcept { return in_ != nullptr; }
  const AtmHeader& header() const noexcept { return header_; }

private:
  std::FILE* in_ = nullptr;
  AtmHeader header_{};
};

} // namespace atmosphere
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_ATMOSPHERE_ATM_HPP
