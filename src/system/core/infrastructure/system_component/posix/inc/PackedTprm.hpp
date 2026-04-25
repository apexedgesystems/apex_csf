#ifndef APEX_SYSTEM_CORE_SYSTEM_COMPONENT_PACKED_TPRM_HPP
#define APEX_SYSTEM_CORE_SYSTEM_COMPONENT_PACKED_TPRM_HPP
/**
 * @file PackedTprm.hpp
 * @brief Packed tunable parameter (tprm) file format reader.
 *
 * Reads packed tprm files containing multiple component/model configurations
 * in a single uplink-friendly binary. Executive unpacks to individual files.
 *
 * Format (version 2):
 * ```
 * Header (8 bytes):
 *   magic[4]   = "TPRM"
 *   version[2] = format version (2)
 *   count[2]   = number of entries
 *
 * Index (12 bytes x count):
 *   fullUid[4] = component fullUid (componentId << 8 | instanceIndex)
 *   offset[4]  = byte offset from start of data section
 *   size[4]    = size in bytes
 *
 * Data:
 *   [entry 0 bytes]
 *   [entry 1 bytes]
 *   ...
 * ```
 *
 * fullUid allows per-instance configuration for multi-instance components.
 * Single-instance components use instanceIndex=0 (e.g., Executive=0x000000).
 *
 * @note NOT RT-safe: File I/O operations.
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace system_core {
namespace system_component {

/* ----------------------------- Constants ----------------------------- */

/// Magic bytes for packed tprm files.
inline constexpr std::array<char, 4> TPRM_MAGIC = {'T', 'P', 'R', 'M'};

/// Current format version (2 = fullUid support).
inline constexpr std::uint16_t TPRM_VERSION = 2;

/// Maximum supported entries in a packed tprm.
inline constexpr std::size_t TPRM_MAX_ENTRIES = 256;

/// Reserved fullUid base for RTS sequence entries (0xFF0000 | slot).
inline constexpr std::uint32_t TPRM_RTS_BASE = 0xFF0000;

/// Reserved fullUid base for ATS sequence entries (0xFE0000 | slot).
inline constexpr std::uint32_t TPRM_ATS_BASE = 0xFE0000;

/// Check if a packed entry fullUid represents an RTS sequence.
[[nodiscard]] inline constexpr bool isRtsEntry(std::uint32_t fullUid) noexcept {
  return (fullUid & 0xFFFF00) == TPRM_RTS_BASE;
}

/// Check if a packed entry fullUid represents an ATS sequence.
[[nodiscard]] inline constexpr bool isAtsEntry(std::uint32_t fullUid) noexcept {
  return (fullUid & 0xFFFF00) == TPRM_ATS_BASE;
}

/// Extract the sequence slot index from an RTS/ATS fullUid.
[[nodiscard]] inline constexpr std::uint8_t sequenceSlot(std::uint32_t fullUid) noexcept {
  return static_cast<std::uint8_t>(fullUid & 0xFF);
}

/// Header size in bytes.
inline constexpr std::size_t TPRM_HEADER_SIZE = 8;

/// Index entry size in bytes (fullUid[4] + offset[4] + size[4] = 12).
inline constexpr std::size_t TPRM_INDEX_ENTRY_SIZE = 12;

/* ----------------------------- PackedTprmHeader ----------------------------- */

/**
 * @struct PackedTprmHeader
 * @brief Header of packed tprm file.
 */
struct PackedTprmHeader {
  std::array<char, 4> magic{}; ///< "TPRM"
  std::uint16_t version{0};    ///< Format version.
  std::uint16_t count{0};      ///< Number of entries.

  /** @brief Check if magic bytes are valid. */
  [[nodiscard]] bool isValid() const noexcept {
    return magic == TPRM_MAGIC && version == TPRM_VERSION;
  }
};
static_assert(sizeof(PackedTprmHeader) == TPRM_HEADER_SIZE);

/* ----------------------------- PackedTprmIndexEntry ----------------------------- */

/**
 * @struct PackedTprmIndexEntry
 * @brief Index entry for a single tprm within packed file.
 *
 * Uses fullUid (24-bit: componentId << 8 | instanceIndex) stored in 32-bit field.
 * This allows per-instance configuration for multi-instance components.
 */
#pragma pack(push, 1)
struct PackedTprmIndexEntry {
  std::uint32_t fullUid{0}; ///< Full UID (componentId << 8 | instanceIndex).
  std::uint32_t offset{0};  ///< Byte offset from start of data section.
  std::uint32_t size{0};    ///< Size in bytes.
};
#pragma pack(pop)
static_assert(sizeof(PackedTprmIndexEntry) == TPRM_INDEX_ENTRY_SIZE);

/* ----------------------------- PackedTprmReader ----------------------------- */

/**
 * @class PackedTprmReader
 * @brief Reads and unpacks packed tprm files.
 *
 * Usage:
 * @code
 *   PackedTprmReader reader;
 *   std::string error;
 *   if (!reader.load("master.tprm", error)) {
 *     // handle error
 *   }
 *
 *   // Extract all entries to directory
 *   if (!reader.extractAll(".apex_fs/tprm", error)) {
 *     // handle error
 *   }
 * @endcode
 *
 * @note NOT RT-safe: File I/O.
 */
class PackedTprmReader {
public:
  PackedTprmReader() = default;
  ~PackedTprmReader() = default;

  // Non-copyable, movable
  PackedTprmReader(const PackedTprmReader&) = delete;
  PackedTprmReader& operator=(const PackedTprmReader&) = delete;
  PackedTprmReader(PackedTprmReader&&) = default;
  PackedTprmReader& operator=(PackedTprmReader&&) = default;

  /**
   * @brief Load a packed tprm file into memory.
   * @param path Path to packed tprm file.
   * @param error Error message on failure.
   * @return true on success.
   */
  [[nodiscard]] bool load(const std::filesystem::path& path, std::string& error) noexcept {
    path_ = path;
    entries_.clear();
    data_.clear();

    // Open file
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      error = "Failed to open packed tprm: " + path.string();
      return false;
    }

    // Get file size
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
      error = "Failed to stat packed tprm";
      ::close(fd);
      return false;
    }
    const auto fileSize = static_cast<std::size_t>(st.st_size);

    // Read header
    PackedTprmHeader header{};
    if (!readExact(fd, &header, sizeof(header))) {
      error = "Failed to read header";
      ::close(fd);
      return false;
    }

    if (!header.isValid()) {
      error = "Invalid packed tprm header (bad magic or version)";
      ::close(fd);
      return false;
    }

    if (header.count > TPRM_MAX_ENTRIES) {
      error = "Too many entries in packed tprm";
      ::close(fd);
      return false;
    }

    // Read index
    entries_.resize(header.count);
    const std::size_t indexSize = header.count * sizeof(PackedTprmIndexEntry);
    if (!readExact(fd, entries_.data(), indexSize)) {
      error = "Failed to read index";
      ::close(fd);
      return false;
    }

    // Calculate data section offset and size
    const std::size_t dataOffset = TPRM_HEADER_SIZE + indexSize;
    if (dataOffset > fileSize) {
      error = "Invalid packed tprm: truncated";
      ::close(fd);
      return false;
    }
    const std::size_t dataSize = fileSize - dataOffset;

    // Read data section
    data_.resize(dataSize);
    if (dataSize > 0 && !readExact(fd, data_.data(), dataSize)) {
      error = "Failed to read data section";
      ::close(fd);
      return false;
    }

    ::close(fd);
    return true;
  }

  /**
   * @brief Extract all entries to individual files.
   * @param outDir Directory to write extracted tprm files.
   * @param error Error message on failure.
   * @return true on success.
   *
   * Files are named: {uid:03d}_{label}.tprm
   * Labels: 000=executive, 001-100=component_{uid}, 101+=model_{uid}
   */
  [[nodiscard]] bool extractAll(const std::filesystem::path& outDir, std::string& error) noexcept {
    // Create output directory
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
      error = "Failed to create output directory: " + ec.message();
      return false;
    }

    // Extract each entry
    for (const auto& entry : entries_) {
      if (!extractEntry(entry, outDir, error)) {
        return false;
      }
    }

    return true;
  }

  /**
   * @brief Get entry by fullUid.
   * @param fullUid Component/model fullUid to find.
   * @return Pointer to entry or nullptr if not found.
   */
  [[nodiscard]] const PackedTprmIndexEntry* findEntry(std::uint32_t fullUid) const noexcept {
    for (const auto& entry : entries_) {
      if (entry.fullUid == fullUid) {
        return &entry;
      }
    }
    return nullptr;
  }

  /**
   * @brief Get data for an entry.
   * @param entry Index entry.
   * @return Pointer to data or nullptr if invalid.
   */
  [[nodiscard]] const std::uint8_t* getData(const PackedTprmIndexEntry& entry) const noexcept {
    if (entry.offset + entry.size > data_.size()) {
      return nullptr;
    }
    return data_.data() + entry.offset;
  }

  /** @brief Number of entries in loaded file. */
  [[nodiscard]] std::size_t count() const noexcept { return entries_.size(); }

  /** @brief Get all entries. */
  [[nodiscard]] const std::vector<PackedTprmIndexEntry>& entries() const noexcept {
    return entries_;
  }

private:
  std::filesystem::path path_;
  std::vector<PackedTprmIndexEntry> entries_;
  std::vector<std::uint8_t> data_;

  /** @brief Read exact number of bytes from fd. */
  static bool readExact(int fd, void* buf, std::size_t size) noexcept {
    auto* p = static_cast<std::uint8_t*>(buf);
    std::size_t remaining = size;
    while (remaining > 0) {
      const ssize_t n = ::read(fd, p, remaining);
      if (n <= 0) {
        return false;
      }
      p += n;
      remaining -= static_cast<std::size_t>(n);
    }
    return true;
  }

  /** @brief Generate filename for fullUid. */
  [[nodiscard]] static std::string fullUidToFilename(std::uint32_t fullUid) noexcept {
    char buf[32];
    // Hex format: 6 digits for 24-bit fullUid.
    // Executive instance 0: 000000.tprm
    // Scheduler instance 0: 000100.tprm (componentId=1, instance=0)
    // PolynomialModel instance 0: 006600.tprm (componentId=102, instance=0)
    // PolynomialModel instance 1: 006601.tprm (componentId=102, instance=1)
    std::snprintf(buf, sizeof(buf), "%06x.tprm", fullUid);
    return buf;
  }

  /** @brief Extract single entry to file. */
  [[nodiscard]] bool extractEntry(const PackedTprmIndexEntry& entry,
                                  const std::filesystem::path& outDir,
                                  std::string& error) noexcept {
    const std::uint8_t* data = getData(entry);
    if (data == nullptr) {
      error = "Invalid entry offset/size for fullUid 0x" + std::to_string(entry.fullUid);
      return false;
    }

    std::filesystem::path outPath = outDir / fullUidToFilename(entry.fullUid);

    int fd = ::open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
      error = "Failed to create output file: " + outPath.string();
      return false;
    }

    const std::uint8_t* p = data;
    std::size_t remaining = entry.size;
    while (remaining > 0) {
      const ssize_t n = ::write(fd, p, remaining);
      if (n <= 0) {
        error = "Failed to write output file: " + outPath.string();
        ::close(fd);
        return false;
      }
      p += n;
      remaining -= static_cast<std::size_t>(n);
    }

    ::close(fd);
    return true;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SYSTEM_COMPONENT_PACKED_TPRM_HPP
