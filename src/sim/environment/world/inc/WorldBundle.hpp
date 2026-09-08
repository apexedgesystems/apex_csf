#ifndef APEX_SIM_ENVIRONMENT_WORLD_WORLD_BUNDLE_HPP
#define APEX_SIM_ENVIRONMENT_WORLD_WORLD_BUNDLE_HPP
/**
 * @file WorldBundle.hpp
 * @brief World bundle container: one packed, hash-identified file per world.
 *
 * A world bundle collects the content artifacts that construct one
 * simulated world (gravity coefficient table, terrain tiling,
 * atmosphere table) into a single bank-resident binary
 * (<body>.world.tprm). It is the content sibling of the component
 * tprm family: same identity discipline (magic, version, uid, hash),
 * different container because bulk demands it -- the v3 component
 * payload carries a 16-bit size and whole-body reads, while bundle
 * entries need 64-bit offsets and in-place access (a full gravity
 * table is tens of MB; boot must not pay for entries a fidelity never
 * loads).
 *
 * Identity and verification layers:
 *  - bundleContentHash (FNV-1a 64): covers every byte from the end of
 *    the fixed header to end of file -- entry table and all payloads;
 *    the header (and thus the hash field itself) is excluded by
 *    construction. This is the value a consumer tprm PINS; masters
 *    thereby authorize world content transitively.
 *  - Per-entry crc32 (CRC-32 IEEE, the tprm family function): covers
 *    that entry's payload bytes only; verified on every read, so boot
 *    cost scales with the entries actually loaded.
 *  - Per-entry specHash: the inner artifact's self-declared provenance
 *    hash, copied verbatim from its own header at pack time (zero when
 *    the artifact format predates headers). Not recomputed here; the
 *    artifact remains the authority on its own identity.
 *
 * Bundle uids live in the reserved world componentId range
 * [0x0100, 0x01FF] -- above the single-byte space every runtime
 * component allocates from, so collision is structural, not
 * conventional.
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include <vector>

namespace sim {
namespace environment {
namespace world {

/* ----------------------------- Constants ----------------------------- */

/// Canonical file suffix: content sibling of the component tprm family.
inline constexpr std::string_view WORLD_FILE_SUFFIX{".world.tprm"};

/// Magic bytes identifying a world bundle: "APW1".
inline constexpr std::array<char, 4> WORLD_BUNDLE_MAGIC = {'A', 'P', 'W', '1'};

/// Current bundle format version.
inline constexpr std::uint16_t WORLD_BUNDLE_VERSION = 1;

/// Reserved componentId range for world bundles (uids are
/// componentId << 8; the instance byte stays zero for bundles).
inline constexpr std::uint16_t WORLD_COMPONENT_ID_FIRST = 0x0100;
inline constexpr std::uint16_t WORLD_COMPONENT_ID_LAST = 0x01FF;

/// Fixed header size in bytes.
inline constexpr std::size_t WORLD_HEADER_SIZE = 64;

/// Entry record size in bytes.
inline constexpr std::size_t WORLD_ENTRY_SIZE = 48;

/// Payload offsets are aligned to this boundary within the file.
inline constexpr std::uint64_t WORLD_PAYLOAD_ALIGN = 8;

/// Build a bundle fullUid from a world componentId.
[[nodiscard]] inline constexpr std::uint32_t worldFullUid(std::uint16_t componentId) noexcept {
  return static_cast<std::uint32_t>(componentId) << 8;
}

/// True when a fullUid lies in the reserved world range.
[[nodiscard]] inline constexpr bool isWorldUid(std::uint32_t fullUid) noexcept {
  const std::uint32_t ID = fullUid >> 8;
  return ID >= WORLD_COMPONENT_ID_FIRST && ID <= WORLD_COMPONENT_ID_LAST;
}

/* ----------------------------- Enums ----------------------------- */

/// Role an entry plays in constructing the world. The role is the
/// lookup key a consumer uses; exactly one entry per role per bundle.
/// The vocabulary is registered HERE and only here -- manifest rows
/// and tool arguments reference these names, so growing the
/// vocabulary (magnetic and solar are ported next) is one enumerator
/// plus one table row below. Role values stay below
/// WORLD_ROLE_LIMIT; unknown roles in a bundle are ignorable by
/// construction, so grown worlds keep serving old consumers.
enum class WorldEntryRole : std::uint8_t {
  GRAVITY = 0,    ///< Coefficient table (.grav payload).
  TERRAIN = 1,    ///< Terrain tiling (.htile payload).
  ATMOSPHERE = 2, ///< Atmosphere table (.atm payload).
};

/// Ceiling for role values (duplicate detection uses a 64-bit mask).
inline constexpr std::uint8_t WORLD_ROLE_LIMIT = 64;

/// Registered vocabulary: canonical name and inner-format suffix per
/// role, in enumerator order.
struct WorldRoleInfo {
  WorldEntryRole role;
  std::string_view name;
  std::string_view suffix;
};

inline constexpr WorldRoleInfo WORLD_ROLE_TABLE[] = {
    {WorldEntryRole::GRAVITY, "gravity", ".grav"},
    {WorldEntryRole::TERRAIN, "terrain", ".htile"},
    {WorldEntryRole::ATMOSPHERE, "atmosphere", ".atm"},
};

/// Canonical name for a role ("?" for values outside the table).
[[nodiscard]] inline constexpr std::string_view worldRoleName(std::uint8_t role) noexcept {
  for (const auto& r : WORLD_ROLE_TABLE) {
    if (static_cast<std::uint8_t>(r.role) == role) {
      return r.name;
    }
  }
  return "?";
}

/// Resolve a registered role by name.
/// @return true and sets out on a vocabulary hit.
[[nodiscard]] inline constexpr bool worldRoleFromName(std::string_view name,
                                                      WorldRoleInfo& out) noexcept {
  for (const auto& r : WORLD_ROLE_TABLE) {
    if (r.name == name) {
      out = r;
      return true;
    }
  }
  return false;
}

/// Distinct verdict per check; loaders report these as fault detail.
enum class WorldBundleCheck : std::uint8_t {
  OK = 0,
  FILE_ERROR = 1,       ///< Open/read/stat failed.
  BAD_MAGIC = 2,        ///< Not a world bundle.
  BAD_VERSION = 3,      ///< Format version mismatch.
  BAD_UID = 4,          ///< fullUid outside the reserved world range.
  SIZE_MISMATCH = 5,    ///< Header totalSize disagrees with the file.
  TABLE_INVALID = 6,    ///< Entry table malformed (offsets/sizes/roles).
  ENTRY_CRC = 7,        ///< Entry payload failed its crc32 on read.
  CONTENT_HASH = 8,     ///< bundleContentHash mismatch (full verify).
  ENTRY_NOT_FOUND = 9,  ///< No entry carries the requested role.
  BUFFER_TOO_SMALL = 10 ///< Caller buffer shorter than entry size.
};

/// Human-readable check name (static string, no allocation).
[[nodiscard]] const char* toString(WorldBundleCheck c) noexcept;

/* ----------------------------- On-disk layout ----------------------------- */

#pragma pack(push, 1)
/// 64-byte fixed header. Multi-byte fields are host-endian, matching
/// the artifact formats this container carries. Field order keeps
/// every 64-bit member on a natural 8-byte boundary so access through
/// the packed struct never forms a misaligned reference (pack(1) plus
/// the static_asserts lock the on-disk layout regardless).
struct WorldBundleHeader {
  std::array<char, 4> magic{};        ///< "APW1" (offset 0)
  std::uint16_t version{0};           ///< Format version (offset 4).
  std::uint16_t entryCount{0};        ///< Entry records (offset 6).
  std::uint32_t fullUid{0};           ///< World-range uid (offset 8).
  std::uint8_t reserved0[4]{};        ///< Zero (offset 12).
  char body[16]{};                    ///< World name (offset 16).
  std::uint64_t bundleContentHash{0}; ///< FNV-1a 64 [64,EOF) (offset 32).
  std::uint64_t totalSize{0};         ///< Whole-file bytes (offset 40).
  std::uint8_t reserved1[16]{};       ///< Zero (offset 48).
};

/// 48-byte entry record. The table sits immediately after the header;
/// payloads follow, offset-aligned to WORLD_PAYLOAD_ALIGN. Same
/// natural-alignment rule as the header.
struct WorldEntryRecord {
  std::uint8_t role{0};        ///< WorldEntryRole (offset 0).
  std::uint8_t reserved0[3]{}; ///< Zero (offset 1).
  std::uint32_t crc32{0};      ///< Payload CRC-32 (offset 4).
  char suffix[8]{};            ///< Inner format suffix (offset 8).
  std::uint64_t offset{0};     ///< Payload file offset (offset 16).
  std::uint64_t size{0};       ///< Payload byte length (offset 24).
  std::uint64_t specHash{0};   ///< Inner provenance hash (offset 32).
  std::uint8_t reserved1[8]{}; ///< Zero (offset 40).
};
#pragma pack(pop)

static_assert(sizeof(WorldBundleHeader) == WORLD_HEADER_SIZE, "WorldBundleHeader must be 64 bytes");
static_assert(sizeof(WorldEntryRecord) == WORLD_ENTRY_SIZE, "WorldEntryRecord must be 48 bytes");

/* ----------------------------- Hash ----------------------------- */

/// FNV-1a 64 running hash; seed with FNV1A64_INIT, fold bytes in any
/// chunking (streamable -- the writer hashes as it copies payloads).
inline constexpr std::uint64_t FNV1A64_INIT = 0xcbf29ce484222325ull;

[[nodiscard]] inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size,
                                           std::uint64_t state = FNV1A64_INIT) noexcept {
  constexpr std::uint64_t PRIME = 0x100000001b3ull;
  for (std::size_t i = 0; i < size; ++i) {
    state ^= static_cast<std::uint64_t>(data[i]);
    state *= PRIME;
  }
  return state;
}

/// CRC-32/ISO-HDLC, the tprm family function (same catalog CRC the
/// v3 payload stamper uses).
[[nodiscard]] inline std::uint32_t worldCrc32(const std::uint8_t* data, std::size_t size) noexcept {
  apex::checksums::crc::Crc32IsoHdlcTable crc;
  std::uint32_t out = 0;
  (void)crc.calculate(data, size, out);
  return out;
}

/* ----------------------------- Writer ----------------------------- */

/// One entry the packer stages: role, inner suffix, source file, and
/// the artifact's self-declared specHash (zero when headerless).
struct WorldEntrySource {
  WorldEntryRole role{WorldEntryRole::GRAVITY};
  std::string_view suffix;       ///< ".grav" / ".htile" / ".atm"
  std::filesystem::path srcPath; ///< Artifact file to embed.
  std::uint64_t specHash{0};     ///< Copied from the artifact header.
};

/**
 * @brief Single-shot bundle writer.
 *
 * Streams each source payload through the entry crc and the bundle
 * content hash, then rewrites the finalized header and table. Output
 * bytes are a pure function of the inputs (no timestamps), so bundle
 * production is deterministic and the content hash is reproducible.
 *
 * @note NOT RT-safe: file I/O. Pack-time tooling only.
 */
class WorldBundleWriter {
public:
  /// Write a complete bundle. Roles must be unique; sources are laid
  /// out in the order given, offsets aligned to WORLD_PAYLOAD_ALIGN.
  /// @return OK on success; the failing check otherwise.
  [[nodiscard]] static WorldBundleCheck
  write(const std::filesystem::path& outPath, std::uint32_t fullUid, std::string_view body,
        const std::vector<WorldEntrySource>& sources) noexcept;
};

/* ----------------------------- Reader ----------------------------- */

/**
 * @brief In-place bundle reader.
 *
 * open() validates the header and entry table eagerly (cheap: 64 B +
 * 48 B per entry) and leaves payloads on disk. readEntry() seeks,
 * reads, and verifies that entry's crc32 -- boot cost scales with the
 * entries a fidelity actually loads. verifyContentHash() is the full
 * pass over [64, EOF) for stage-time verification against a pin.
 *
 * @note NOT RT-safe: file I/O. Load/verify paths only.
 */
class WorldBundleReader {
public:
  WorldBundleReader() noexcept = default;
  ~WorldBundleReader() noexcept;

  WorldBundleReader(const WorldBundleReader&) = delete;
  WorldBundleReader& operator=(const WorldBundleReader&) = delete;

  /// Open and validate header + entry table.
  [[nodiscard]] WorldBundleCheck open(const std::filesystem::path& path) noexcept;

  /// Close the file. Idempotent.
  void close() noexcept;

  [[nodiscard]] bool isOpen() const noexcept { return file_ != nullptr; }
  [[nodiscard]] const WorldBundleHeader& header() const noexcept { return header_; }
  [[nodiscard]] const std::vector<WorldEntryRecord>& entries() const noexcept { return entries_; }

  /// Path this reader opened. Bulk consumers that stream rather than
  /// buffer (the gravity coefficient table reads windowed from disk)
  /// open this path themselves and seek to an entry's offset; the
  /// entry crc then gets verified by the consumer's own read pass or
  /// a prior stage-time verifyContentHash().
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  /// Locate the entry carrying a role.
  /// @return OK and sets outIndex; ENTRY_NOT_FOUND otherwise.
  [[nodiscard]] WorldBundleCheck findEntry(WorldEntryRole role,
                                           std::size_t& outIndex) const noexcept;

  /// Read entry payload into caller storage and verify its crc32.
  /// @param index Entry index (entries() order).
  /// @param out   Resized to the entry size on success.
  [[nodiscard]] WorldBundleCheck readEntry(std::size_t index,
                                           std::vector<std::uint8_t>& out) noexcept;

  /// Full-file content-hash verification against the header field
  /// (stage-time / pin checks; not part of the boot path).
  [[nodiscard]] WorldBundleCheck verifyContentHash() noexcept;

private:
  std::FILE* file_ = nullptr;
  std::filesystem::path path_{};
  WorldBundleHeader header_{};
  std::vector<WorldEntryRecord> entries_{};
};

} // namespace world
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_WORLD_WORLD_BUNDLE_HPP
