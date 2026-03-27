/**
 * @file FileSystemBase.cpp
 * @brief Implementation of FileSystemBase operations.
 */

#include "src/system/core/components/filesystem/apex/inc/FileSystemBase.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <cstdlib>
#include <ctime>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_set>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <fmt/core.h>

namespace system_core {
namespace filesystem {

FileSystemBase::FileSystemBase(std::filesystem::path fs, std::string name) noexcept
    : fs_(std::move(fs)), fsName_(std::move(name)) {
  setConfigured(true); // Simple component - no params file needed
  // Cache canonical root for fast path checks (error_code path to avoid throws).
  std::error_code ec;
  fsCanon_ = std::filesystem::weakly_canonical(fs_, ec);
  if (ec) {
    ec.clear();
    fsCanon_ = fs_.lexically_normal();
  }
  // Cache string representation to avoid allocation in isUnderRoot() hot path.
  fsCanonStr_ = fsCanon_.generic_string();
}

std::filesystem::path FileSystemBase::archivePath() const {
  // Default: <root>/<name>.tar
  return fs_ / (fsName_ + ".tar");
}

// Not RT-safe: shells out + heavy I/O.
Status FileSystemBase::cleanup(const std::filesystem::path& customArchivePath) {
  namespace fsns = std::filesystem;
  std::error_code ec;

  if (!fsns::exists(fs_, ec) || !fsns::is_directory(fs_, ec)) {
    setLastError("Invalid filesystem root");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_INVALID_FS));
    return Status::ERROR_INVALID_FS;
  }

  if (preCleanupHook() != Status::SUCCESS) {
    // Assume preCleanupHook set status; if not, preserve SUCCESS.
    return static_cast<Status>(status());
  }

  // Determine archive path: use custom if provided, otherwise use archivePath()
  fsns::path TAR_PATH;
  if (!customArchivePath.empty()) {
    // Custom path provided - ensure directory exists and append timestamped filename
    fsns::create_directories(customArchivePath, ec);
    if (ec) {
      setLastError("Failed to create custom archive directory");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
      return Status::ERROR_FS_CREATION_FAIL;
    }

    // Generate timestamped filename
    const auto NOW = std::chrono::system_clock::now();
    const std::time_t T = std::chrono::system_clock::to_time_t(NOW);
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &T);
#else
    localtime_r(&T, &tmLocal);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmLocal, "%Y%m%d-%H%M%S");
    const std::string TIMESTAMP = oss.str();
    const std::string ARCHIVE_NAME = fsName_ + "_" + TIMESTAMP + ".tar";

    TAR_PATH = customArchivePath / ARCHIVE_NAME;
  } else {
    // Use default path from archivePath()
    TAR_PATH = archivePath();
  }

  // Archive via shell (best-effort).
  const std::string TAR_CMD = fmt::format("tar -cf {} -C {} .", TAR_PATH.string(), fs_.string());

  // Log the tar command before execution
  if (auto* lg = componentLog()) {
    lg->info(label(), fmt::format("tar: {}", TAR_CMD));
  }

  const int RC = std::system(TAR_CMD.c_str());
  if (RC != 0) {
    setLastError("Failed to create tarball");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_TAR_CREATE_FAIL));
    return Status::ERROR_FS_TAR_CREATE_FAIL;
  }

  // Remove all contents
  for (fsns::directory_iterator it(fs_, ec), end; !ec && it != end; it.increment(ec)) {
    const fsns::path CUR = it->path();
    // Skip tar only if it's inside the filesystem root
    if (CUR == TAR_PATH)
      continue;

    fsns::remove_all(CUR, ec);
    if (ec) {
      setLastError("Remove failed during cleanup");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_TAR_MOVE_FAIL));
      return Status::ERROR_FS_TAR_MOVE_FAIL;
    }
  }

  // If using custom archive path, remove entire filesystem root directory
  // (archive is external, so we can delete everything)
  if (!customArchivePath.empty()) {
    fsns::remove_all(fs_, ec);
    ec.clear(); // Non-fatal
  }

  (void)postCleanupHook();

  setLastError(nullptr);
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

Status FileSystemBase::createDirectories(const std::vector<std::filesystem::path>& dirs) {
  namespace fsns = std::filesystem;
  std::error_code ec;

  for (const auto& dir : dirs) {
    (void)fsns::create_directories(dir, ec);
    if (ec) {
      setLastError("Failed to create directory");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
      return Status::ERROR_FS_CREATION_FAIL;
    }
  }

  setLastError(nullptr);
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

Status FileSystemBase::ensureFsExists() {
  namespace fsns = std::filesystem;
  std::error_code ec;

  if (fsns::exists(fs_, ec) && fsns::is_directory(fs_, ec)) {
    setLastError(nullptr);
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    return Status::SUCCESS;
  }

  (void)fsns::create_directories(fs_, ec);
  if (ec) {
    setLastError("Failed to create filesystem root");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
    return Status::ERROR_FS_CREATION_FAIL;
  }

  // Refresh canonical cache after creation.
  fsCanon_ = fs_.lexically_normal();
  fsCanonStr_ = fsCanon_.generic_string();

  setLastError(nullptr);
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

Status FileSystemBase::clearContentsExcept(const std::vector<std::filesystem::path>& keep) {
  namespace fsns = std::filesystem;
  std::error_code ec;

  // Normalize keep set; store both exact files and all ancestor dirs.
  std::unordered_set<std::string> keepFiles;
  std::unordered_set<std::string> keepDirs;
  keepFiles.reserve(keep.size());
  keepDirs.reserve(keep.size() * 2);

  const fsns::path ROOT = fsCanon_.empty() ? fs_ : fsCanon_;

  for (const auto& p : keep) {
    const fsns::path ABS = (p.is_absolute() ? p : (fs_ / p)).lexically_normal();
    const std::string ABS_S = ABS.generic_string();
    keepFiles.insert(ABS_S);

    // Insert all ancestors up to and including the immediate child under ROOT.
    for (fsns::path anc = ABS.parent_path(); !anc.empty(); anc = anc.parent_path()) {
      const std::string ANC_S = anc.lexically_normal().generic_string();
      keepDirs.insert(ANC_S);
      if (anc == ROOT)
        break;
    }
  }

  // Only remove immediate children of ROOT; preserve if they are kept or an ancestor of any kept
  // path.
  for (fsns::directory_iterator it(ROOT, ec), end; !ec && it != end; it.increment(ec)) {
    const fsns::path CUR = it->path().lexically_normal();
    const std::string CUR_S = CUR.generic_string();

    if (keepFiles.find(CUR_S) != keepFiles.end())
      continue;
    if (keepDirs.find(CUR_S) != keepDirs.end())
      continue;

    fsns::remove_all(CUR, ec);
    if (ec) {
      setLastError("Failed to clear directory contents");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_TAR_MOVE_FAIL));
      return Status::ERROR_FS_TAR_MOVE_FAIL;
    }
  }

  setLastError(nullptr);
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

bool FileSystemBase::exists() const noexcept {
  namespace fsns = std::filesystem;
  std::error_code ec;
  return fsns::exists(fs_, ec) && fsns::is_directory(fs_, ec);
}

/* ----------------------------- Maintenance Helpers ----------------------------- */

Status FileSystemBase::checkSpace(std::uintmax_t minBytes, std::uintmax_t* outAvail) noexcept {
  std::error_code ec;
  const auto SPACE = std::filesystem::space(fs_, ec);
  if (ec) {
    setStatus(static_cast<std::uint8_t>(Status::ERROR_INVALID_FS));
    return Status::ERROR_INVALID_FS;
  }
  if (outAvail != nullptr) {
    *outAvail = SPACE.available;
  }
  if (minBytes > 0 && SPACE.available < minBytes) {
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
    return Status::ERROR_FS_CREATION_FAIL;
  }
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

bool FileSystemBase::isUnderRoot(const std::filesystem::path& p) const noexcept {
  // Fast path: if path has no ".." components, skip syscall-heavy weakly_canonical().
  // This handles the common case of absolute, normalized paths (~50x faster).
  const auto ABS = p.is_absolute() ? p : (fs_ / p);
  const auto ABS_S = ABS.generic_string();

  // Check for ".." which would require canonicalization to resolve
  bool needsCanon = false;
  for (std::size_t i = 0; i + 1 < ABS_S.size(); ++i) {
    if (ABS_S[i] == '.' && ABS_S[i + 1] == '.') {
      // Check it's actually ".." (not part of "...foo")
      const bool AT_START = (i == 0);
      const bool AFTER_SEP = (i > 0 && ABS_S[i - 1] == '/');
      const bool AT_END = (i + 2 == ABS_S.size());
      const bool BEFORE_SEP = (i + 2 < ABS_S.size() && ABS_S[i + 2] == '/');
      if ((AT_START || AFTER_SEP) && (AT_END || BEFORE_SEP)) {
        needsCanon = true;
        break;
      }
    }
  }

  std::string pathStr;
  if (needsCanon) {
    // Slow path: use weakly_canonical for paths with ".."
    std::error_code ec;
    const auto CAN_P = std::filesystem::weakly_canonical(ABS, ec);
    if (ec)
      return false;
    pathStr = CAN_P.generic_string();
  } else {
    // Fast path: use lexically_normal (no syscalls)
    pathStr = ABS.lexically_normal().generic_string();
  }

  // Use cached string to avoid allocation
  if (pathStr.size() < fsCanonStr_.size())
    return false;
  if (pathStr.compare(0, fsCanonStr_.size(), fsCanonStr_) != 0)
    return false;
  if (pathStr.size() == fsCanonStr_.size())
    return true;
  return pathStr[fsCanonStr_.size()] == '/';
}

Status FileSystemBase::writeFileAtomic(const std::filesystem::path& target,
                                       const std::vector<std::uint8_t>& bytes,
                                       bool doFsync) noexcept {
  if (!isUnderRoot(target)) {
    setStatus(static_cast<std::uint8_t>(Status::ERROR_INVALID_FS));
    return Status::ERROR_INVALID_FS;
  }

  std::error_code ec;
  const auto DIR = target.parent_path();
  if (!DIR.empty()) {
    std::filesystem::create_directories(DIR, ec);
    if (ec) {
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
      return Status::ERROR_FS_CREATION_FAIL;
    }
  }

  const auto TMP_PATH = target.parent_path() / (target.filename().string() + ".tmp");

  {
    std::ofstream ofs(TMP_PATH, std::ios::binary | std::ios::trunc);
    if (!ofs.good()) {
      setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_CREATION_FAIL));
      return Status::ERROR_FS_CREATION_FAIL;
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    ofs.flush();
#if !defined(_WIN32)
    if (doFsync || fsyncPolicy_ == FsyncPolicy::ALWAYS) {
      int fd = ::open(TMP_PATH.c_str(), O_RDONLY);
      if (fd >= 0) {
#if defined(FDATASYNC_PRESENT) || defined(__linux__)
        ::fdatasync(fd);
#else
        ::fsync(fd);
#endif
        ::close(fd);
      }
    }
#else
    (void)doFsync;
#endif
  }

  std::filesystem::rename(TMP_PATH, target, ec);
  if (ec) {
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FS_TAR_MOVE_FAIL));
    std::filesystem::remove(TMP_PATH, ec);
    return Status::ERROR_FS_TAR_MOVE_FAIL;
  }

  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

Status FileSystemBase::pruneByAge(const std::filesystem::path& dir,
                                  std::uint64_t maxAgeSec) noexcept {
  namespace fsns = std::filesystem;
  std::error_code ec;

  const auto NOW_F = fsns::file_time_type::clock::now();

  for (fsns::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto P = it->path();
    const auto TS = fsns::last_write_time(P, ec);
    if (ec) {
      ec.clear();
      continue;
    }

    const auto AGE = NOW_F - TS;
    if (std::chrono::duration_cast<std::chrono::seconds>(AGE).count() >
        static_cast<std::int64_t>(maxAgeSec)) {
      fsns::remove_all(P, ec);
      ec.clear();
    }
  }

  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

Status FileSystemBase::pruneBySize(const std::filesystem::path& dir,
                                   std::uintmax_t maxBytes) noexcept {
  namespace fsns = std::filesystem;
  std::error_code ec;

  struct Entry {
    fsns::path path;
    std::uintmax_t size;
    fsns::file_time_type ts;
  };
  std::vector<Entry> entries;
  std::uintmax_t total = 0;

  for (fsns::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const auto P = it->path();
    if (fsns::is_directory(P, ec)) {
      ec.clear();
      continue;
    }
    const auto SZ = fsns::file_size(P, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    const auto TS = fsns::last_write_time(P, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    entries.push_back({P, SZ, TS});
    total += SZ;
  }

  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.ts < b.ts; });

  for (const auto& e : entries) {
    if (total <= maxBytes)
      break;
    fsns::remove(e.path, ec);
    if (!ec)
      total -= e.size;
    ec.clear();
  }

  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

} // namespace filesystem
} // namespace system_core