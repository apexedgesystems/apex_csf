/**
 * @file WorldBundle.cpp
 * @brief World bundle writer/reader implementation.
 */

#include "src/sim/environment/world/inc/WorldBundle.hpp"

#include <cstring>

namespace sim {
namespace environment {
namespace world {

namespace {

/// Streaming copy chunk. Large enough to amortize syscalls against
/// multi-MB gravity payloads, small enough to stay stack-friendly off
/// the heap via a single vector.
constexpr std::size_t K_CHUNK = 1u << 20;

/// Round up to the payload alignment boundary.
[[nodiscard]] constexpr std::uint64_t alignUp(std::uint64_t v) noexcept {
  return (v + (WORLD_PAYLOAD_ALIGN - 1)) & ~(WORLD_PAYLOAD_ALIGN - 1);
}

} // namespace

/* ----------------------------- Bundle kinds ----------------------------- */

namespace {
/// The registered kinds. A new content-bundle kind adds a row here
/// (plus its vocabulary table); everything downstream reads this.
constexpr BundleKindInfo K_KINDS[] = {
    {"world", WORLD_COMPONENT_ID_FIRST, WORLD_COMPONENT_ID_LAST, WORLD_FILE_SUFFIX,
     WORLD_ROLE_TABLE, sizeof(WORLD_ROLE_TABLE) / sizeof(WORLD_ROLE_TABLE[0])},
};
} // namespace

const BundleKindInfo* bundleKindByName(std::string_view name) noexcept {
  for (const auto& k : K_KINDS) {
    if (k.name == name) {
      return &k;
    }
  }
  return nullptr;
}

const BundleKindInfo* bundleKindByUid(std::uint32_t fullUid) noexcept {
  const std::uint32_t ID = fullUid >> 8;
  for (const auto& k : K_KINDS) {
    if (ID >= k.componentIdFirst && ID <= k.componentIdLast) {
      return &k;
    }
  }
  return nullptr;
}

bool bundleRoleFromName(const BundleKindInfo& kind, std::string_view name,
                        WorldRoleInfo& out) noexcept {
  for (std::size_t i = 0; i < kind.roleCount; ++i) {
    if (kind.roles[i].name == name) {
      out = kind.roles[i];
      return true;
    }
  }
  return false;
}

const char* toString(WorldBundleCheck c) noexcept {
  switch (c) {
  case WorldBundleCheck::OK:
    return "OK";
  case WorldBundleCheck::FILE_ERROR:
    return "FILE_ERROR";
  case WorldBundleCheck::BAD_MAGIC:
    return "BAD_MAGIC";
  case WorldBundleCheck::BAD_VERSION:
    return "BAD_VERSION";
  case WorldBundleCheck::BAD_UID:
    return "BAD_UID";
  case WorldBundleCheck::SIZE_MISMATCH:
    return "SIZE_MISMATCH";
  case WorldBundleCheck::TABLE_INVALID:
    return "TABLE_INVALID";
  case WorldBundleCheck::ENTRY_CRC:
    return "ENTRY_CRC";
  case WorldBundleCheck::CONTENT_HASH:
    return "CONTENT_HASH";
  case WorldBundleCheck::ENTRY_NOT_FOUND:
    return "ENTRY_NOT_FOUND";
  case WorldBundleCheck::BUFFER_TOO_SMALL:
    return "BUFFER_TOO_SMALL";
  }
  return "UNKNOWN";
}

/* ----------------------------- Writer ----------------------------- */

WorldBundleCheck WorldBundleWriter::write(const std::filesystem::path& outPath,
                                          std::uint32_t fullUid, std::string_view body,
                                          const std::vector<WorldEntrySource>& sources) noexcept {
  if (bundleKindByUid(fullUid) == nullptr) {
    return WorldBundleCheck::BAD_UID;
  }
  // A bundle with no entries is a manifest error: an empty world
  // means the consumer config should be unbound, not bound to
  // nothing.
  if (sources.empty()) {
    return WorldBundleCheck::TABLE_INVALID;
  }
  // Unique roles below the role ceiling; sources must exist.
  std::uint64_t seenRoles = 0;
  for (const auto& s : sources) {
    if (static_cast<std::uint8_t>(s.role) >= WORLD_ROLE_LIMIT) {
      return WorldBundleCheck::TABLE_INVALID;
    }
    const std::uint64_t BIT = 1ull << static_cast<std::uint8_t>(s.role);
    if ((seenRoles & BIT) != 0) {
      return WorldBundleCheck::TABLE_INVALID;
    }
    seenRoles |= BIT;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(s.srcPath, ec)) {
      return WorldBundleCheck::FILE_ERROR;
    }
  }

  // Layout pass: sizes and aligned offsets.
  std::vector<WorldEntryRecord> table(sources.size());
  std::uint64_t cursor = alignUp(WORLD_HEADER_SIZE + sources.size() * WORLD_ENTRY_SIZE);
  for (std::size_t i = 0; i < sources.size(); ++i) {
    std::error_code ec;
    const std::uint64_t SIZE = std::filesystem::file_size(sources[i].srcPath, ec);
    if (ec) {
      return WorldBundleCheck::FILE_ERROR;
    }
    auto& r = table[i];
    r.role = static_cast<std::uint8_t>(sources[i].role);
    const std::size_t SUFFIX_LEN = sources[i].suffix.size() < sizeof(r.suffix)
                                       ? sources[i].suffix.size()
                                       : sizeof(r.suffix) - 1;
    std::memcpy(r.suffix, sources[i].suffix.data(), SUFFIX_LEN);
    r.offset = cursor;
    r.size = SIZE;
    r.specHash = sources[i].specHash;
    cursor = alignUp(cursor + SIZE);
  }
  const std::uint64_t TOTAL = cursor;

  std::FILE* out = std::fopen(outPath.string().c_str(), "wb+");
  if (out == nullptr) {
    return WorldBundleCheck::FILE_ERROR;
  }

  // Placeholder header + table; finalized after the payload pass.
  WorldBundleHeader hdr{};
  hdr.magic = WORLD_BUNDLE_MAGIC;
  hdr.version = WORLD_BUNDLE_VERSION;
  hdr.entryCount = static_cast<std::uint16_t>(sources.size());
  hdr.fullUid = fullUid;
  const std::size_t BODY_LEN = body.size() < sizeof(hdr.body) ? body.size() : sizeof(hdr.body) - 1;
  std::memcpy(hdr.body, body.data(), BODY_LEN);
  hdr.totalSize = TOTAL;

  auto fail = [&out, &outPath]() {
    std::fclose(out);
    std::error_code ec;
    std::filesystem::remove(outPath, ec);
    return WorldBundleCheck::FILE_ERROR;
  };

  if (std::fwrite(&hdr, 1, sizeof(hdr), out) != sizeof(hdr)) {
    return fail();
  }
  if (!table.empty() &&
      std::fwrite(table.data(), sizeof(WorldEntryRecord), table.size(), out) != table.size()) {
    return fail();
  }

  // Payload pass: copy sources, computing per-entry crc32; zero-fill
  // alignment gaps so the byte stream (and the content hash) is a pure
  // function of the inputs.
  std::vector<std::uint8_t> buf(K_CHUNK);
  for (std::size_t i = 0; i < sources.size(); ++i) {
    const std::uint64_t POS = static_cast<std::uint64_t>(std::ftell(out));
    for (std::uint64_t p = POS; p < table[i].offset; ++p) {
      if (std::fputc(0, out) == EOF) {
        return fail();
      }
    }
    // Pack-time tool, hosted only: whole-source reads keep the crc a
    // single catalog-CRC call (no streaming surface on CrcTable).
    std::FILE* in = std::fopen(sources[i].srcPath.string().c_str(), "rb");
    if (in == nullptr) {
      return fail();
    }
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(table[i].size));
    if (!payload.empty() && std::fread(payload.data(), 1, payload.size(), in) != payload.size()) {
      std::fclose(in);
      return fail();
    }
    std::fclose(in);
    table[i].crc32 = payload.empty() ? 0 : worldCrc32(payload.data(), payload.size());
    if (!payload.empty() && std::fwrite(payload.data(), 1, payload.size(), out) != payload.size()) {
      return fail();
    }
  }
  // Trailing alignment pad to totalSize.
  for (std::uint64_t p = static_cast<std::uint64_t>(std::ftell(out)); p < TOTAL; ++p) {
    if (std::fputc(0, out) == EOF) {
      return fail();
    }
  }

  // Rewrite the finalized table, then hash [WORLD_HEADER_SIZE, EOF)
  // and rewrite the finalized header.
  if (std::fseek(out, static_cast<long>(WORLD_HEADER_SIZE), SEEK_SET) != 0) {
    return fail();
  }
  if (!table.empty() &&
      std::fwrite(table.data(), sizeof(WorldEntryRecord), table.size(), out) != table.size()) {
    return fail();
  }
  std::fflush(out);
  if (std::fseek(out, static_cast<long>(WORLD_HEADER_SIZE), SEEK_SET) != 0) {
    return fail();
  }
  std::uint64_t hash = FNV1A64_INIT;
  std::uint64_t remaining = TOTAL - WORLD_HEADER_SIZE;
  while (remaining > 0) {
    const std::size_t WANT = remaining < K_CHUNK ? static_cast<std::size_t>(remaining) : K_CHUNK;
    if (std::fread(buf.data(), 1, WANT, out) != WANT) {
      return fail();
    }
    hash = fnv1a64(buf.data(), WANT, hash);
    remaining -= WANT;
  }
  hdr.bundleContentHash = hash;
  if (std::fseek(out, 0, SEEK_SET) != 0 || std::fwrite(&hdr, 1, sizeof(hdr), out) != sizeof(hdr)) {
    return fail();
  }
  std::fclose(out);
  return WorldBundleCheck::OK;
}

/* ----------------------------- Reader ----------------------------- */

WorldBundleReader::~WorldBundleReader() noexcept { close(); }

void WorldBundleReader::close() noexcept {
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  entries_.clear();
  header_ = WorldBundleHeader{};
  path_.clear();
}

WorldBundleCheck WorldBundleReader::open(const std::filesystem::path& path) noexcept {
  close();
  file_ = std::fopen(path.string().c_str(), "rb");
  if (file_ == nullptr) {
    return WorldBundleCheck::FILE_ERROR;
  }
  path_ = path;
  if (std::fread(&header_, 1, sizeof(header_), file_) != sizeof(header_)) {
    close();
    return WorldBundleCheck::FILE_ERROR;
  }
  if (header_.magic != WORLD_BUNDLE_MAGIC) {
    close();
    return WorldBundleCheck::BAD_MAGIC;
  }
  if (header_.version != WORLD_BUNDLE_VERSION) {
    close();
    return WorldBundleCheck::BAD_VERSION;
  }
  if (bundleKindByUid(header_.fullUid) == nullptr) {
    close();
    return WorldBundleCheck::BAD_UID;
  }
  std::error_code ec;
  const std::uint64_t FSIZE = std::filesystem::file_size(path, ec);
  if (ec || FSIZE != header_.totalSize) {
    close();
    return WorldBundleCheck::SIZE_MISMATCH;
  }
  entries_.resize(header_.entryCount);
  if (header_.entryCount > 0 && std::fread(entries_.data(), sizeof(WorldEntryRecord),
                                           entries_.size(), file_) != entries_.size()) {
    close();
    return WorldBundleCheck::FILE_ERROR;
  }
  // Table sanity: payloads inside the file, past the table, no role
  // duplicates.
  const std::uint64_t PAYLOAD_FLOOR = WORLD_HEADER_SIZE + header_.entryCount * WORLD_ENTRY_SIZE;
  // Roles above the ceiling are refused (mask safety); roles below it
  // but outside this build's vocabulary are tolerated -- findEntry
  // simply never matches them, so grown worlds serve old consumers.
  std::uint64_t seenRoles = 0;
  for (const auto& e : entries_) {
    if (e.role >= WORLD_ROLE_LIMIT) {
      close();
      return WorldBundleCheck::TABLE_INVALID;
    }
    const std::uint64_t BIT = 1ull << e.role;
    const bool ROLE_DUP = (seenRoles & BIT) != 0;
    seenRoles |= BIT;
    // Overflow-safe span check: offset + size must fit inside the file
    // even when a doctored offset is near the uint64 ceiling.
    if (ROLE_DUP || e.offset < PAYLOAD_FLOOR || e.size > header_.totalSize ||
        e.offset > header_.totalSize - e.size) {
      close();
      return WorldBundleCheck::TABLE_INVALID;
    }
  }
  return WorldBundleCheck::OK;
}

WorldBundleCheck WorldBundleReader::findEntry(WorldEntryRole role,
                                              std::size_t& outIndex) const noexcept {
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].role == static_cast<std::uint8_t>(role)) {
      outIndex = i;
      return WorldBundleCheck::OK;
    }
  }
  return WorldBundleCheck::ENTRY_NOT_FOUND;
}

WorldBundleCheck WorldBundleReader::readEntry(std::size_t index,
                                              std::vector<std::uint8_t>& out) noexcept {
  if (file_ == nullptr || index >= entries_.size()) {
    return WorldBundleCheck::FILE_ERROR;
  }
  const auto& e = entries_[index];
  out.resize(static_cast<std::size_t>(e.size));
  if (std::fseek(file_, static_cast<long>(e.offset), SEEK_SET) != 0) {
    return WorldBundleCheck::FILE_ERROR;
  }
  if (e.size > 0 && std::fread(out.data(), 1, out.size(), file_) != out.size()) {
    return WorldBundleCheck::FILE_ERROR;
  }
  if (e.size > 0 && worldCrc32(out.data(), out.size()) != e.crc32) {
    return WorldBundleCheck::ENTRY_CRC;
  }
  return WorldBundleCheck::OK;
}

WorldBundleCheck WorldBundleReader::verifyContentHash() noexcept {
  if (file_ == nullptr) {
    return WorldBundleCheck::FILE_ERROR;
  }
  if (std::fseek(file_, static_cast<long>(WORLD_HEADER_SIZE), SEEK_SET) != 0) {
    return WorldBundleCheck::FILE_ERROR;
  }
  std::vector<std::uint8_t> buf(K_CHUNK);
  std::uint64_t hash = FNV1A64_INIT;
  std::uint64_t remaining = header_.totalSize - WORLD_HEADER_SIZE;
  while (remaining > 0) {
    const std::size_t WANT = remaining < K_CHUNK ? static_cast<std::size_t>(remaining) : K_CHUNK;
    if (std::fread(buf.data(), 1, WANT, file_) != WANT) {
      return WorldBundleCheck::FILE_ERROR;
    }
    hash = fnv1a64(buf.data(), WANT, hash);
    remaining -= WANT;
  }
  return hash == header_.bundleContentHash ? WorldBundleCheck::OK : WorldBundleCheck::CONTENT_HASH;
}

} // namespace world
} // namespace environment
} // namespace sim
