#ifndef APEX_SYSTEM_CORE_FILESYSTEM_DEFAULT_HPP
#define APEX_SYSTEM_CORE_FILESYSTEM_DEFAULT_HPP
/**
 * @file ApexFileSystem.hpp
 * @brief Default filesystem layout for the system core.
 *
 * Predefines common subdirectories and initializes them under the given root.
 * Real-time note: init() performs filesystem I/O and should run off the hot path.
 */

#include "src/system/core/components/filesystem/apex/inc/FileSystemBase.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace system_core {
namespace filesystem {

/** @brief Default subdirectory names (relative to the filesystem root). */
static constexpr std::string_view LIB_DIR = "libs";
static constexpr std::string_view TLM_DIR = "tlm";
static constexpr std::string_view LOG_DIR = "logs";
static constexpr std::string_view LOG_CORE_SUBDIR = "core";
static constexpr std::string_view LOG_MODELS_SUBDIR = "models";
static constexpr std::string_view LOG_SUPPORT_SUBDIR = "support";
static constexpr std::string_view LOG_DRIVERS_SUBDIR = "drivers";
static constexpr std::string_view TPRM_DIR = "tprm";
static constexpr std::string_view BIN_DIR = "bin";
static constexpr std::string_view RTS_DIR = "rts";
static constexpr std::string_view ATS_DIR = "ats";
static constexpr std::string_view DB_DIR = "db";

/** @brief Bank A/B directory names and marker file. */
static constexpr std::string_view BANK_A_DIR = "bank_a";
static constexpr std::string_view BANK_B_DIR = "bank_b";
static constexpr std::string_view ACTIVE_BANK_FILE = "active_bank";

/** @brief Swap history directory for archived component logs. */
static constexpr std::string_view SWAP_HISTORY_DIR = "swap_history";

/** @brief Dedicated filesystem log filename (created in LOG_DIR/core/). */
static constexpr std::string_view FS_LOG_FN = "filesystem.log";

/** @brief Dedicated swap log filename (created in LOG_DIR/core/). */
static constexpr std::string_view SWAP_LOG_FN = "swap.log";

/** @brief Maximum archived component logs per component (FIFO pruning). */
static constexpr std::size_t MAX_SWAP_ARCHIVES = 5;

/** @brief Identifies which bank is active. */
enum class Bank : std::uint8_t { A = 0, B = 1 };

/**
 * @class ApexFileSystem
 * @brief Default directory and log structure for most deployments.
 *
 * Supports A/B bank switching for runtime updates. Each bank contains its
 * own libs/, tprm/, and bin/ directories. The active bank is persisted in
 * a marker file (active_bank) so it survives executive restarts.
 *
 * Upload workflow:
 *  1. C2 uploads files to the inactive bank via FILE_TRANSFER.
 *  2. RELOAD commands load from the inactive bank.
 *  3. On success, files swap between active and inactive banks.
 *  4. Rollback: reload again (previous version is in the inactive bank).
 */
class ApexFileSystem : public FileSystemBase {
public:
  /**
   * @brief Construct with root path and optional logical name.
   * @param fs     Filesystem root.
   * @param name   Logical filesystem name (used for archive naming). Default: "fs".
   */
  explicit ApexFileSystem(std::filesystem::path fs, std::string name = "fs") noexcept;

  /**
   * @brief Destructor performs cleanup/archive if configured via configureShutdownCleanup().
   *
   * Timing: Runs after shutdown stages but before logger destruction, ensuring complete logs.
   */
  ~ApexFileSystem() override;

protected:
  /**
   * @brief Create the default directory structure and dedicated log.
   * @return system_component::Status as uint8_t (SUCCESS on success).
   *
   * Creates a dedicated SYNC-mode log at logs/filesystem.log for sparse
   * filesystem operations. Lightweight: no I/O thread, minimal memory.
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

public:
  /**
   * @brief Configure automatic cleanup on destruction.
   * @param enabled If true, destructor archives and cleans filesystem.
   * @param customArchivePath Optional custom archive directory (empty = default location).
   *
   * Call during startup to enable RAII-based filesystem cleanup.
   * Ensures complete logging before archive operation.
   */
  void configureShutdownCleanup(bool enabled,
                                std::filesystem::path customArchivePath = {}) noexcept;

  /// Absolute paths to common subdirectories (non-banked).
  [[nodiscard]] std::filesystem::path tlmDir() const noexcept { return tlmDir_; }
  [[nodiscard]] std::filesystem::path logDir() const noexcept { return logDir_; }
  [[nodiscard]] std::filesystem::path dbDir() const noexcept { return dbDir_; }

  /* ----------------------------- Bank A/B ----------------------------- */

  /// Active bank paths (what the executive loads from at runtime).
  [[nodiscard]] std::filesystem::path libDir() const noexcept;
  [[nodiscard]] std::filesystem::path tprmDir() const noexcept;
  [[nodiscard]] std::filesystem::path binDir() const noexcept;
  [[nodiscard]] std::filesystem::path rtsDir() const noexcept;
  [[nodiscard]] std::filesystem::path atsDir() const noexcept;

  /// Inactive bank paths (where C2 uploads new images).
  [[nodiscard]] std::filesystem::path inactiveLibDir() const noexcept;
  [[nodiscard]] std::filesystem::path inactiveTprmDir() const noexcept;
  [[nodiscard]] std::filesystem::path inactiveBinDir() const noexcept;
  [[nodiscard]] std::filesystem::path inactiveRtsDir() const noexcept;
  [[nodiscard]] std::filesystem::path inactiveAtsDir() const noexcept;

  /// Bank directory root for a specific bank.
  [[nodiscard]] std::filesystem::path bankDir(Bank bank) const noexcept;

  /// Current active bank.
  [[nodiscard]] Bank activeBank() const noexcept { return activeBank_; }

  /**
   * @brief Swap a single file between active and inactive banks.
   * @param subdir Subdirectory within bank (LIB_DIR, TPRM_DIR, or BIN_DIR).
   * @param filename Filename to swap.
   * @return true on success. On failure, both banks are unchanged.
   *
   * Moves active/subdir/filename -> inactive/subdir/filename and vice versa.
   * If only the inactive side has the file, it moves to active (first deploy).
   */
  [[nodiscard]] bool swapBankFile(std::string_view subdir, const std::string& filename) noexcept;

  /**
   * @brief Flip the active bank marker (A->B or B->A).
   * @return true if marker was written successfully.
   *
   * Used for full-bank swap (all libs + tprm + bin at once).
   * After calling, all active accessors resolve to the other bank.
   */
  [[nodiscard]] bool flipActiveBank() noexcept;

  /* ----------------------------- Logs ----------------------------- */

  /// Component log subdirectories.
  [[nodiscard]] std::filesystem::path coreLogDir() const noexcept { return coreLogDir_; }
  [[nodiscard]] std::filesystem::path modelLogDir() const noexcept { return modelLogDir_; }
  [[nodiscard]] std::filesystem::path supportLogDir() const noexcept { return supportLogDir_; }
  [[nodiscard]] std::filesystem::path driverLogDir() const noexcept { return driverLogDir_; }

  /// Path to dedicated filesystem log (valid after init()).
  [[nodiscard]] std::filesystem::path fsLogPath() const noexcept { return fsLogPath_; }

  /* ----------------------------- Swap Traceability ----------------------------- */

  /// Swap history directory (archived component logs from hot-swaps).
  [[nodiscard]] std::filesystem::path swapHistoryDir() const noexcept { return swapHistoryDir_; }

  /// Path to dedicated swap log (valid after init()).
  [[nodiscard]] std::filesystem::path swapLogPath() const noexcept { return swapLogPath_; }

  /// Dedicated swap log for runtime update traceability. NOT RT-safe.
  [[nodiscard]] logs::SystemLog* swapLog() const noexcept { return swapLog_.get(); }

  /**
   * @brief Archive a component log before hot-swap. NOT RT-safe.
   * @param logFile    Path to the component's current log file.
   * @param componentName  Component name (e.g. "PolynomialModel").
   * @param instanceIndex  Component instance index.
   * @param maxArchives    Maximum archives to keep per component (FIFO). Default:
   * MAX_SWAP_ARCHIVES.
   * @return Archived path on success, empty path on failure.
   *
   * Copies the log to swap_history/{timestamp}_{name}_{index}/ and prunes
   * oldest archives beyond maxArchives for this component.
   */
  [[nodiscard]] std::filesystem::path
  archiveComponentLog(const std::filesystem::path& logFile, std::string_view componentName,
                      std::uint8_t instanceIndex,
                      std::size_t maxArchives = MAX_SWAP_ARCHIVES) noexcept;

protected:
  /**
   * @brief Optionally customize archive location/name for this layout.
   * Default inherits FileSystemBase behavior: [root]/[name].tar
   */
  [[nodiscard]] std::filesystem::path archivePath() const override;

private:
  /** @brief Read active bank from marker file. Returns Bank::A if file missing/corrupt. */
  [[nodiscard]] Bank readActiveBankMarker() const noexcept;

  /** @brief Write active bank to marker file. */
  [[nodiscard]] bool writeActiveBankMarker(Bank bank) noexcept;

  /// Bank directories.
  std::filesystem::path bankADir_;
  std::filesystem::path bankBDir_;
  std::filesystem::path activeBankPath_; ///< Path to active_bank marker file.
  Bank activeBank_{Bank::A};

  /// Non-banked directories.
  std::filesystem::path tlmDir_;
  std::filesystem::path logDir_;
  std::filesystem::path coreLogDir_;    ///< logs/core/ for system components.
  std::filesystem::path modelLogDir_;   ///< logs/models/ for simulation models.
  std::filesystem::path supportLogDir_; ///< logs/support/ for support components.
  std::filesystem::path driverLogDir_;  ///< logs/drivers/ for driver components.
  std::filesystem::path dbDir_;

  std::filesystem::path swapHistoryDir_;       ///< swap_history/ for archived component logs.
  std::filesystem::path fsLogPath_;            ///< Path to dedicated log file.
  std::shared_ptr<logs::SystemLog> fsLog_{};   ///< Dedicated SYNC-mode log.
  std::filesystem::path swapLogPath_;          ///< Path to dedicated swap log.
  std::shared_ptr<logs::SystemLog> swapLog_{}; ///< Dedicated SYNC-mode swap log.

  bool autoCleanupOnDestroy_{false};
  std::filesystem::path customArchivePath_{};
};

} // namespace filesystem
} // namespace system_core

#endif // APEX_SYSTEM_CORE_FILESYSTEM_DEFAULT_HPP
