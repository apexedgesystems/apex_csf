/**
 * @file ApexFileSystem.cpp
 * @brief Implementation of the default filesystem layout.
 */

#include "src/system/core/components/filesystem/posix/inc/ApexFileSystem.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <ctime>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <fmt/core.h>

namespace system_core {
namespace filesystem {

ApexFileSystem::ApexFileSystem(std::filesystem::path fs, std::string name) noexcept
    : FileSystemBase(std::move(fs), std::move(name)) {
  const std::filesystem::path ROOT = root();

  // Bank directories.
  bankADir_ = ROOT / BANK_A_DIR;
  bankBDir_ = ROOT / BANK_B_DIR;
  activeBankPath_ = ROOT / ACTIVE_BANK_FILE;

  // Non-banked directories.
  tlmDir_ = ROOT / TLM_DIR;
  logDir_ = ROOT / LOG_DIR;
  coreLogDir_ = logDir_ / LOG_CORE_SUBDIR;
  modelLogDir_ = logDir_ / LOG_MODELS_SUBDIR;
  supportLogDir_ = logDir_ / LOG_SUPPORT_SUBDIR;
  driverLogDir_ = logDir_ / LOG_DRIVERS_SUBDIR;
  dbDir_ = ROOT / DB_DIR;
  swapHistoryDir_ = ROOT / SWAP_HISTORY_DIR;
}

ApexFileSystem::~ApexFileSystem() {
  if (!autoCleanupOnDestroy_) {
    return;
  }

  // Destructor runs after shutdown stages but before logger destruction.
  // This ensures complete logging before filesystem archive/cleanup.
  if (auto* lg = componentLog()) {
    lg->info(label(), "Destructor: Archiving filesystem...");
  }

  const Status FS_STATUS = customArchivePath_.empty() ? cleanup() : cleanup(customArchivePath_);

  if (auto* lg = componentLog()) {
    if (FS_STATUS == Status::SUCCESS) {
      const std::string archLoc =
          customArchivePath_.empty() ? "default location" : customArchivePath_.string();
      lg->info(label(), fmt::format("Filesystem archived to {} successfully", archLoc));
    } else {
      lg->warning(label(), 0,
                  fmt::format("Filesystem archive failed in destructor: {}", toString(FS_STATUS)));
    }
  }
}

void ApexFileSystem::configureShutdownCleanup(bool enabled,
                                              std::filesystem::path customArchivePath) noexcept {
  autoCleanupOnDestroy_ = enabled;
  customArchivePath_ = std::move(customArchivePath);
  // Configuration is logged in main System Configuration section
}

std::uint8_t ApexFileSystem::doInit() noexcept {
  // Create non-banked directory structure (not RT-safe).
  const std::vector<std::filesystem::path> NON_BANKED_DIRS{
      tlmDir_,        logDir_,       coreLogDir_, modelLogDir_,
      supportLogDir_, driverLogDir_, dbDir_,      swapHistoryDir_,
  };

  Status st = createDirectories(NON_BANKED_DIRS);
  if (st != Status::SUCCESS) {
    return static_cast<std::uint8_t>(st);
  }

  // Create bank A and bank B directory structures.
  const std::vector<std::filesystem::path> BANK_DIRS{
      bankADir_ / LIB_DIR, bankADir_ / TPRM_DIR, bankADir_ / BIN_DIR,  bankADir_ / RTS_DIR,
      bankADir_ / ATS_DIR, bankBDir_ / LIB_DIR,  bankBDir_ / TPRM_DIR, bankBDir_ / BIN_DIR,
      bankBDir_ / RTS_DIR, bankBDir_ / ATS_DIR,
  };

  st = createDirectories(BANK_DIRS);
  if (st != Status::SUCCESS) {
    return static_cast<std::uint8_t>(st);
  }

  // Read or initialize active bank marker.
  activeBank_ = readActiveBankMarker();

  // Create dedicated SYNC-mode log for filesystem operations.
  // Lightweight: no I/O thread, minimal memory (~4KB vs ~2.1MB for async).
  fsLogPath_ = coreLogDir_ / FS_LOG_FN;
  fsLog_ = std::make_shared<logs::SystemLog>(fsLogPath_.string());
  fsLog_->setLevel(logs::SystemLog::Level::INFO);

  // Install as component log (base class stores shared_ptr).
  // All subsequent FileSystemBase operations will use this log.
  setComponentLog(fsLog_);

  // Create dedicated SYNC-mode swap log for runtime update traceability.
  swapLogPath_ = coreLogDir_ / SWAP_LOG_FN;
  swapLog_ = std::make_shared<logs::SystemLog>(swapLogPath_.string());
  swapLog_->setLevel(logs::SystemLog::Level::INFO);

  const std::size_t TOTAL_DIRS = NON_BANKED_DIRS.size() + BANK_DIRS.size();
  componentLog()->info(label(),
                       fmt::format("Filesystem initialized (created {} directories)", TOTAL_DIRS));
  componentLog()->info(label(), fmt::format("Dedicated log: {}", fsLogPath_.string()));
  componentLog()->info(label(), fmt::format("Active bank: {}", activeBank_ == Bank::A ? "A" : "B"));

  return static_cast<std::uint8_t>(Status::SUCCESS);
}

std::filesystem::path ApexFileSystem::archivePath() const {
  // Generate timestamp for unique archive naming
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

  // Store archives in logs directory with timestamp
  // Format: <name>_YYYYMMDD-HHMMSS.tar
  const std::string ARCHIVE_NAME = name() + "_" + TIMESTAMP + ".tar";
  return logDir() / ARCHIVE_NAME;
}

/* ----------------------------- Bank A/B ----------------------------- */

std::filesystem::path ApexFileSystem::libDir() const noexcept {
  return bankDir(activeBank_) / LIB_DIR;
}

std::filesystem::path ApexFileSystem::tprmDir() const noexcept {
  return bankDir(activeBank_) / TPRM_DIR;
}

std::filesystem::path ApexFileSystem::binDir() const noexcept {
  return bankDir(activeBank_) / BIN_DIR;
}

std::filesystem::path ApexFileSystem::rtsDir() const noexcept {
  return bankDir(activeBank_) / RTS_DIR;
}

std::filesystem::path ApexFileSystem::atsDir() const noexcept {
  return bankDir(activeBank_) / ATS_DIR;
}

std::filesystem::path ApexFileSystem::inactiveLibDir() const noexcept {
  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  return bankDir(INACTIVE) / LIB_DIR;
}

std::filesystem::path ApexFileSystem::inactiveTprmDir() const noexcept {
  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  return bankDir(INACTIVE) / TPRM_DIR;
}

std::filesystem::path ApexFileSystem::inactiveBinDir() const noexcept {
  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  return bankDir(INACTIVE) / BIN_DIR;
}

std::filesystem::path ApexFileSystem::inactiveRtsDir() const noexcept {
  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  return bankDir(INACTIVE) / RTS_DIR;
}

std::filesystem::path ApexFileSystem::inactiveAtsDir() const noexcept {
  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  return bankDir(INACTIVE) / ATS_DIR;
}

std::filesystem::path ApexFileSystem::bankDir(Bank bank) const noexcept {
  return (bank == Bank::A) ? bankADir_ : bankBDir_;
}

bool ApexFileSystem::swapBankFile(std::string_view subdir, const std::string& filename) noexcept {
  namespace fsns = std::filesystem;
  std::error_code ec;

  const Bank INACTIVE = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  const fsns::path ACTIVE_FILE = bankDir(activeBank_) / subdir / filename;
  const fsns::path INACTIVE_FILE = bankDir(INACTIVE) / subdir / filename;

  const bool ACTIVE_EXISTS = fsns::exists(ACTIVE_FILE, ec);
  const bool INACTIVE_EXISTS = fsns::exists(INACTIVE_FILE, ec);

  if (!ACTIVE_EXISTS && !INACTIVE_EXISTS) {
    return false; // Nothing to swap.
  }

  if (ACTIVE_EXISTS && INACTIVE_EXISTS) {
    // Both exist: swap via temporary.
    const fsns::path TMP = ACTIVE_FILE.parent_path() / (filename + ".swap");
    fsns::rename(ACTIVE_FILE, TMP, ec);
    if (ec) {
      return false;
    }
    fsns::rename(INACTIVE_FILE, ACTIVE_FILE, ec);
    if (ec) {
      // Rollback: restore active from tmp.
      fsns::rename(TMP, ACTIVE_FILE, ec);
      return false;
    }
    fsns::rename(TMP, INACTIVE_FILE, ec);
    if (ec) {
      return false;
    }
  } else if (INACTIVE_EXISTS) {
    // First deploy: move inactive -> active.
    fsns::rename(INACTIVE_FILE, ACTIVE_FILE, ec);
    if (ec) {
      return false;
    }
  } else {
    // Only active exists, nothing to swap in.
    return false;
  }

  if (auto* lg = componentLog()) {
    lg->info(label(), fmt::format("Bank file swap: {}/{}", subdir, filename));
  }

  return true;
}

bool ApexFileSystem::flipActiveBank() noexcept {
  const Bank NEW_BANK = (activeBank_ == Bank::A) ? Bank::B : Bank::A;
  if (!writeActiveBankMarker(NEW_BANK)) {
    return false;
  }
  activeBank_ = NEW_BANK;

  if (auto* lg = componentLog()) {
    lg->info(label(), fmt::format("Active bank flipped to {}", activeBank_ == Bank::A ? "A" : "B"));
  }

  return true;
}

Bank ApexFileSystem::readActiveBankMarker() const noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(activeBankPath_, ec)) {
    // First boot: write default marker.
    std::ofstream ofs(activeBankPath_, std::ios::trunc);
    if (ofs.good()) {
      ofs << "a";
    }
    return Bank::A;
  }

  std::ifstream ifs(activeBankPath_);
  char ch = 'a';
  ifs.get(ch);
  return (ch == 'b' || ch == 'B') ? Bank::B : Bank::A;
}

bool ApexFileSystem::writeActiveBankMarker(Bank bank) noexcept {
  std::ofstream ofs(activeBankPath_, std::ios::trunc);
  if (!ofs.good()) {
    return false;
  }
  ofs << (bank == Bank::A ? 'a' : 'b');
  ofs.flush();
  return ofs.good();
}

/* ----------------------------- Swap Traceability ----------------------------- */

std::filesystem::path ApexFileSystem::archiveComponentLog(const std::filesystem::path& logFile,
                                                          std::string_view componentName,
                                                          std::uint8_t instanceIndex,
                                                          std::size_t maxArchives) noexcept {
  namespace fsns = std::filesystem;
  std::error_code ec;

  if (!fsns::exists(logFile, ec) || fsns::file_size(logFile, ec) == 0) {
    return {};
  }

  // Generate timestamped archive subdirectory.
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

  const std::string COMPONENT_TAG = fmt::format("{}_{}", componentName, instanceIndex);
  const std::string DIR_NAME = fmt::format("{}_{}", TIMESTAMP, COMPONENT_TAG);
  const fsns::path ARCHIVE_DIR = swapHistoryDir_ / DIR_NAME;

  fsns::create_directories(ARCHIVE_DIR, ec);
  if (ec) {
    return {};
  }

  // Copy the log file into the archive directory.
  const fsns::path DEST = ARCHIVE_DIR / logFile.filename();
  fsns::copy_file(logFile, DEST, fsns::copy_options::overwrite_existing, ec);
  if (ec) {
    return {};
  }

  // Prune: collect all archives for this component, sort by name (timestamp), FIFO.
  const std::string PREFIX_SUFFIX = "_" + COMPONENT_TAG;
  std::vector<fsns::path> archives;

  for (fsns::directory_iterator it(swapHistoryDir_, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }
    const std::string ENTRY_NAME = it->path().filename().string();
    if (ENTRY_NAME.size() > PREFIX_SUFFIX.size() &&
        ENTRY_NAME.compare(ENTRY_NAME.size() - PREFIX_SUFFIX.size(), PREFIX_SUFFIX.size(),
                           PREFIX_SUFFIX) == 0) {
      archives.push_back(it->path());
    }
  }

  if (archives.size() > maxArchives) {
    std::sort(archives.begin(), archives.end());
    const std::size_t TO_REMOVE = archives.size() - maxArchives;
    for (std::size_t i = 0; i < TO_REMOVE; ++i) {
      fsns::remove_all(archives[i], ec);
      ec.clear();
    }
  }

  if (auto* lg = componentLog()) {
    lg->info(label(), fmt::format("Archived component log: {} -> {}", logFile.filename().string(),
                                  DIR_NAME));
  }

  return DEST;
}

} // namespace filesystem
} // namespace system_core
