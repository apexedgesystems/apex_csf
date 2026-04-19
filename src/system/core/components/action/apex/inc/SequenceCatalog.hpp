#ifndef APEX_SYSTEM_CORE_ACTION_SEQUENCE_CATALOG_HPP
#define APEX_SYSTEM_CORE_ACTION_SEQUENCE_CATALOG_HPP
/**
 * @file SequenceCatalog.hpp
 * @brief Lightweight catalog of all onboard sequences (RTS + ATS).
 *
 * The catalog stores metadata for every sequence file on the filesystem.
 * No step data is loaded — only the 8-byte header is read per file.
 * This allows hundreds of sequences to be registered with minimal memory
 * (~48 bytes per entry).
 *
 * RTS sequences are loaded on-demand into execution slots when triggered.
 * ATS sequences are pre-loaded at boot into persistent execution slots.
 *
 * RT-safe: Catalog lookups are O(log N) via binary search on sorted IDs.
 * Catalog mutation (scan, add, remove) is NOT RT-safe.
 */

#include "src/system/core/components/action/apex/inc/ActionEngineConfig.hpp"
#include "src/system/core/components/action/apex/inc/DataSequence.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum catalog entries (configurable for memory budget).
/// 256 entries x ~1.4 KB binary = ~360 KB cached data.
constexpr std::size_t CATALOG_MAX_ENTRIES = Config::SEQUENCE_CATALOG_SIZE;

/// Maximum filename length (including null terminator).
constexpr std::size_t CATALOG_FILENAME_MAX = 48;

/// Maximum blocking relationships per entry.
constexpr std::size_t CATALOG_MAX_BLOCKS = 4;

/* ----------------------------- CatalogEntry ----------------------------- */

/**
 * @struct CatalogEntry
 * @brief Lightweight metadata for one onboard sequence.
 *
 * Populated by reading only the 8-byte binary header from each sequence
 * file. No step data is loaded.
 */
struct CatalogEntry {
  std::uint16_t sequenceId{0}; ///< Unique sequence ID (from file header).
  std::uint16_t eventId{0};    ///< Auto-trigger event (0 = manual only).
  SequenceType type{};         ///< RTS or ATS.
  std::uint8_t stepCount{0};   ///< Number of steps (metadata).
  std::uint8_t priority{0};    ///< Execution priority (higher = more important).
  bool armed{false};           ///< Armed flag from file header.

  /// Abort event: fired when this sequence is preempted, stopped, or times out.
  std::uint16_t abortEventId{0}; ///< Event to dispatch on abort (0 = none).

  /// Mutual exclusion group: only one sequence from a group runs at a time.
  /// When a new sequence starts, any running sequence in the same group is stopped.
  /// 0 = no exclusion group (default).
  std::uint8_t exclusionGroup{0};

  /// Blocking relationships: IDs of sequences this entry blocks.
  std::uint8_t blockCount{0};
  std::uint16_t blocks[CATALOG_MAX_BLOCKS]{};

  /// Filesystem path relative to rts/ or ats/ directory.
  char filename[CATALOG_FILENAME_MAX]{};

  /// Full absolute path (resolved at scan time).
  std::filesystem::path absolutePath{};

  /// Cached binary content (header + steps only, variable length).
  /// Size = 8 + stepCount * 64 bytes. Allocated at scan time.
  /// At trigger time, memcpy into execution slot -- no filesystem I/O.
  static constexpr std::size_t HEADER_SIZE = 8;
  static constexpr std::size_t STEP_SIZE = 64;
  std::vector<std::uint8_t> binary{}; ///< Cached binary (heap, allocated once at scan).
  bool binaryLoaded{false};           ///< True if binary was successfully cached.
};

/* ----------------------------- SequenceCatalog ----------------------------- */

/**
 * @class SequenceCatalog
 * @brief Registry of all onboard sequences with O(log N) lookup.
 *
 * Usage:
 * @code
 *   SequenceCatalog catalog;
 *   catalog.scan(rtsDir, SequenceType::RTS);
 *   catalog.scan(atsDir, SequenceType::ATS);
 *
 *   // Lookup by ID
 *   auto* entry = catalog.findById(47);
 *
 *   // Find all entries triggered by an event
 *   catalog.forEachByEvent(eventId, [](const CatalogEntry& e) { ... });
 * @endcode
 */
class SequenceCatalog {
public:
  /**
   * @brief Scan a directory and add all sequence files to the catalog.
   * @param dir Directory to scan (rts/ or ats/).
   * @param type Sequence type to assign (RTS or ATS).
   * @return Number of entries added.
   * @note NOT RT-safe: filesystem I/O, sorts catalog after scan.
   */
  std::size_t scan(const std::filesystem::path& dir, SequenceType type) noexcept;

  /**
   * @brief Add a single entry to the catalog.
   * @param entry Entry to add.
   * @return true if added, false if catalog is full or duplicate ID.
   * @note NOT RT-safe: may re-sort.
   */
  bool add(const CatalogEntry& entry) noexcept;

  /**
   * @brief Find an entry by sequence ID.
   * @param sequenceId ID to find.
   * @return Pointer to entry, or nullptr if not found.
   * @note RT-safe: O(log N) binary search.
   */
  [[nodiscard]] const CatalogEntry* findById(std::uint16_t sequenceId) const noexcept;

  /**
   * @brief Find a mutable entry by sequence ID.
   * @param sequenceId ID to find.
   * @return Pointer to entry, or nullptr if not found.
   * @note NOT RT-safe if used for mutation during execution.
   */
  [[nodiscard]] CatalogEntry* findByIdMut(std::uint16_t sequenceId) noexcept;

  /**
   * @brief Invoke callback for each entry matching an event ID.
   * @param eventId Event to match.
   * @param fn Callback: void(const CatalogEntry&).
   * @note RT-safe if fn is RT-safe: O(N) scan.
   */
  template <typename F> void forEachByEvent(std::uint16_t eventId, F&& fn) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
      if (entries_[i].eventId == eventId) {
        fn(entries_[i]);
      }
    }
  }

  /**
   * @brief Invoke callback for each entry.
   * @param fn Callback: void(const CatalogEntry&).
   * @note RT-safe if fn is RT-safe: O(N).
   */
  template <typename F> void forEach(F&& fn) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
      fn(entries_[i]);
    }
  }

  /**
   * @brief Get total number of catalog entries.
   * @return Entry count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  /**
   * @brief Get number of RTS entries.
   * @return RTS count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t rtsCount() const noexcept { return rtsCount_; }

  /**
   * @brief Get number of ATS entries.
   * @return ATS count.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t atsCount() const noexcept { return atsCount_; }

  /**
   * @brief Clear all entries.
   * @note NOT RT-safe.
   */
  void clear() noexcept {
    count_ = 0;
    rtsCount_ = 0;
    atsCount_ = 0;
  }

private:
  /// Sort entries by sequenceId for binary search.
  void sortById() noexcept;

  CatalogEntry entries_[CATALOG_MAX_ENTRIES]{};
  std::size_t count_{0};
  std::size_t rtsCount_{0};
  std::size_t atsCount_{0};
};

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_SEQUENCE_CATALOG_HPP
