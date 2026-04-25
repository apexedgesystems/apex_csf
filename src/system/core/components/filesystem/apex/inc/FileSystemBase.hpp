#ifndef APEX_SYSTEM_CORE_FILESYSTEM_BASE_HPP
#define APEX_SYSTEM_CORE_FILESYSTEM_BASE_HPP
/**
 * @file FileSystemBase.hpp
 * @brief Base interface for filesystem management (C++17-compatible).
 *
 * Notes:
 * - Doxygen in headers only; sources keep brief intent comments.
 * - Returns typed Status; internal ops use error_code to avoid exceptions.
 * - Logger is optional and checked before use.
 * - cleanup() performs heavy I/O and is not real-time safe.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/system/core/components/filesystem/apex/inc/FileSystemStatus.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace system_core {
namespace filesystem {

/**
 * @class FileSystemBase
 * @brief Base class for filesystem operations (creation, maintenance, teardown).
 *
 * Inherits lifecycle/state/logging from SystemComponentBase.
 * Derived classes implement init() via the SystemComponentBase contract.
 */
class FileSystemBase : public system_component::CoreComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (2 = FileSystem, system component range 1-100).
  static constexpr std::uint16_t COMPONENT_ID = 2;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "FileSystem";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Durability policy for write operations.
   */
  enum class FsyncPolicy : std::uint8_t { NEVER = 0, ON_DEMAND, ALWAYS };

  /**
   * @brief Construct with filesystem root and name.
   * @param fs     Root directory for operations.
   * @param name   Logical filesystem name (used for archive naming); defaults to "fs".
   *
   * Implementation caches a canonicalized root for fast path checks.
   */
  explicit FileSystemBase(std::filesystem::path fs, std::string name = "fs") noexcept;

  /** @brief Virtual destructor. */
  ~FileSystemBase() override = default;

  /** @brief Module label (string literal). */
  [[nodiscard]] const char* label() const noexcept override { return "FILE_SYSTEM"; }

  /** @brief Filesystem-typed view of the last status (casts base status()). */
  [[nodiscard]] Status fsStatus() const noexcept { return static_cast<Status>(status()); }

  /** @brief Root filesystem path. */
  [[nodiscard]] const std::filesystem::path& root() const noexcept { return fs_; }

  /** @brief Logical filesystem name. */
  [[nodiscard]] const std::string& name() const noexcept { return fsName_; }

  /** @brief Set / get write durability policy. */
  void setFsyncPolicy(FsyncPolicy p) noexcept { fsyncPolicy_ = p; }
  [[nodiscard]] FsyncPolicy fsyncPolicy() const noexcept { return fsyncPolicy_; }

  // ---------------------------------------------------------------------------
  // Operations
  // ---------------------------------------------------------------------------

  /**
   * @brief Create an archive of the root directory and remove the directory contents.
   * @param customArchivePath Optional custom directory for archive (uses archivePath() if empty).
   * @return Status result.
   * @note Not real-time safe.
   */
  [[nodiscard]] virtual Status cleanup(const std::filesystem::path& customArchivePath = {});

  /**
   * @brief Create multiple directories.
   * @param dirs Directory paths to create.
   * @return Status result.
   */
  [[nodiscard]] virtual Status createDirectories(const std::vector<std::filesystem::path>& dirs);

  /**
   * @brief Ensure the filesystem root exists (creates it if missing).
   * @return Status result.
   */
  [[nodiscard]] Status ensureFsExists();

  /**
   * @brief Remove all entries under the root, except the provided keep-list.
   * @param keep Absolute or relative (to root) paths to preserve; must resolve under root.
   * @return Status result.
   */
  [[nodiscard]] Status clearContentsExcept(const std::vector<std::filesystem::path>& keep);

  /**
   * @brief Lightweight existence probe for the filesystem root.
   * @return true if the root exists and is a directory.
   */
  [[nodiscard]] bool exists() const noexcept;

  /**
   * @brief Query available space and optionally enforce a minimum threshold.
   * @param minBytes Minimum required free bytes; if > 0 and not met, returns
   * ERROR_FS_CREATION_FAIL.
   * @param outAvail Optional; receives available bytes.
   * @return Status result.
   */
  [[nodiscard]] Status checkSpace(std::uintmax_t minBytes = 0,
                                  std::uintmax_t* outAvail = nullptr) noexcept;

  /**
   * @brief Returns true if a path resolves inside the filesystem root.
   * Uses cached canonical root for fast comparison.
   * @param p Path to test (absolute or relative to root).
   */
  [[nodiscard]] bool isUnderRoot(const std::filesystem::path& p) const noexcept;

  /**
   * @brief Atomically write bytes to a file using temp + rename.
   * @param target Destination file under root.
   * @param bytes  Data to write.
   * @param doFsync If true, fsync according to policy ON_DEMAND.
   * @return Status result.
   */
  [[nodiscard]] Status writeFileAtomic(const std::filesystem::path& target,
                                       const std::vector<std::uint8_t>& bytes,
                                       bool doFsync = false) noexcept;

  /**
   * @brief Delete files older than maxAgeSec in a directory (non-recursive).
   * @param dir Directory to prune.
   * @param maxAgeSec Maximum age in seconds.
   * @return Status result.
   */
  [[nodiscard]] Status pruneByAge(const std::filesystem::path& dir,
                                  std::uint64_t maxAgeSec) noexcept;

  /**
   * @brief Keep total size in a directory under maxBytes by deleting oldest first.
   * @param dir Directory to prune (non-recursive).
   * @param maxBytes Maximum total bytes to retain.
   * @return Status result.
   */
  [[nodiscard]] Status pruneBySize(const std::filesystem::path& dir,
                                   std::uintmax_t maxBytes) noexcept;

protected:
  /** @brief Customizable archive target for cleanup() (default: `<root>/<name>.tar`). */
  [[nodiscard]] virtual std::filesystem::path archivePath() const;

  /**
   * @brief Hooks around cleanup(); default no-op.
   * @return Status::SUCCESS to proceed; any error to abort cleanup().
   */
  [[nodiscard]] virtual Status preCleanupHook() { return Status::SUCCESS; }
  [[nodiscard]] virtual Status postCleanupHook() { return Status::SUCCESS; }

private:
  std::filesystem::path fs_{};
  std::filesystem::path fsCanon_{}; ///< Cached canonical root for fast isUnderRoot().
  std::string fsCanonStr_{};        ///< Cached string of fsCanon_ (avoids allocation in hot path).
  std::string fsName_{"fs"};
  FsyncPolicy fsyncPolicy_{FsyncPolicy::NEVER};
};

} // namespace filesystem
} // namespace system_core

#endif // APEX_SYSTEM_CORE_FILESYSTEM_BASE_HPP