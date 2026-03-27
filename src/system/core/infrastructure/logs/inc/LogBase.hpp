#ifndef APEX_SYSTEM_LOGS_LOG_BASE_HPP
#define APEX_SYSTEM_LOGS_LOG_BASE_HPP
/**
 * @file LogBase.hpp
 * @brief Base class for file-backed logging with rotation and size queries (no-throw).
 *
 * Design:
 *  - Real-time friendly: no exceptions, compact typed status codes.
 *  - Writers avoid locks: atomic append via O_APPEND file descriptors.
 *  - Rotation and size management serialized under a single mutex.
 *
 * RT Lifecycle Constraints:
 *  - write() is RT-safe (lock-free, single syscall).
 *  - rotate(), size(), fpath() are NOT RT-safe (acquire mutex).
 *  - flush() blocks on fsync (use sparingly in RT context).
 *  - Construct and destroy outside RT phase.
 */

#include <fmt/core.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace logs {

/**
 * @enum Status
 * @brief Status codes for file operations.
 */
enum class Status : std::uint8_t {
  OK = 0,
  ERROR_OPEN,          ///< Failed to open or create the log file.
  ERROR_SIZE,          ///< Failed to query file size.
  ERROR_ROTATE_RENAME, ///< Failed to rename current log to backup.
  ERROR_ROTATE_REOPEN, ///< Failed to reopen a fresh log after rotation.
  ERROR_SYNC           ///< Failed to sync file to disk.
};

/**
 * @brief Human-readable string for a status code. Intended for diagnostics only.
 */
const char* toString(Status s) noexcept;

/**
 * @class LogBase
 * @brief Provides file management and atomic append writes for logging.
 *
 * Notes:
 *  - Non-copyable and non-movable.
 *  - Timestamps for log lines use "YYYY-MM-DD HH:MM:SS".
 *  - Rotation backups use a file-safe timestamp: "YYYYMMDD-HHMMSS".
 */
class LogBase {
public:
  /**
   * @brief Construct and attempt to open the log file.
   * @param logPath Path to the log file.
   *
   * If opening fails, @ref lastOpenStatus() is set accordingly. Writes become
   * no-ops that return the stored status.
   */
  explicit LogBase(const std::string& logPath) noexcept;

  /// Destructor closes the file descriptor.
  virtual ~LogBase() noexcept;

  LogBase(const LogBase&) = delete;
  LogBase& operator=(const LogBase&) = delete;
  LogBase(LogBase&&) = delete;
  LogBase& operator=(LogBase&&) = delete;

  /**
   * @brief Append a message to the log, followed by '\n'.
   * @param msg Message to write.
   * @return Status::OK on success, otherwise the last open status.
   * @note RT-safe: Lock-free, single syscall via O_APPEND.
   */
  Status write(const std::string& msg) noexcept;

  /**
   * @brief Force buffered data to disk.
   * @return Status::OK on success; Status::ERROR_SYNC on failure.
   * @note NOT RT-safe: Blocks on fsync(). Call from non-RT context only.
   */
  Status flush() noexcept;

  /**
   * @brief Query the current size of the log file.
   * @param outBytes Populated with file size on success, unchanged on failure.
   * @return Status::OK on success; Status::ERROR_SIZE otherwise.
   * @note NOT RT-safe: Acquires mutex. Call from non-RT context.
   */
  Status size(std::size_t& outBytes) noexcept;

  /**
   * @brief Return the log file path.
   * @return Path as a string.
   * @note NOT RT-safe: Acquires mutex. Call from non-RT context.
   */
  std::string fpath() noexcept;

  /**
   * @brief Rotate the log file if its size exceeds the limit.
   * @param maxSize Maximum allowed size in bytes.
   * @return Status::OK if rotation was not required or succeeded.
   *         Status::ERROR_ROTATE_RENAME if backup rename failed.
   *         Status::ERROR_ROTATE_REOPEN if reopening failed.
   * @note NOT RT-safe: Acquires mutex, performs file I/O. Call from non-RT context.
   */
  Status rotate(std::size_t maxSize) noexcept;

  /**
   * @brief Retrieve the result of the most recent open or reopen attempt.
   */
  Status lastOpenStatus() const noexcept { return openStatus_; }

protected:
  /**
   * @brief Generate a timestamp string for log lines.
   * @return "YYYY-MM-DD HH:MM:SS".
   */
  std::string getTimestamp() noexcept;

  /**
   * @brief Generate a timestamp string suitable for filenames.
   * @return "YYYYMMDD-HHMMSS".
   */
  std::string getTimestampForFile() noexcept;

  /**
   * @brief Append raw bytes to the log.
   * @param data Pointer to data.
   * @param len Number of bytes to write.
   * @return Status::OK on success; otherwise the last open status.
   *
   * Performed with a single system call loop, no locks.
   */
  Status appendBytes(const char* data, std::size_t len) noexcept;

  /**
   * @brief Attempt to (re)open the file descriptor.
   *
   * Updates @ref openStatus_.
   */
  void openHandleNoThrow() noexcept;

private:
  /**
   * @brief Helper to query file size without acquiring the mutex.
   * @param outBytes Populated with file size on success.
   * @return Status::OK on success; Status::ERROR_SIZE otherwise.
   *
   * Call only with @ref logMutex_ already held.
   */
  Status sizeNoLock(std::size_t& outBytes) const noexcept;

protected:
  std::filesystem::path logPath_;         ///< Path to the log file.
  std::atomic<int> logFd_{-1};            ///< File descriptor for O_APPEND writes.
  std::mutex logMutex_;                   ///< Guards open/rotate/size operations.
  Status openStatus_{Status::ERROR_OPEN}; ///< Sticky status from last open.
};

} // namespace logs

#endif // APEX_SYSTEM_LOGS_LOG_BASE_HPP