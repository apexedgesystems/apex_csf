/**
 * @file LogBase.cpp
 * @brief Implementation of LogBase with explicit flush capability.
 */

#include "src/system/core/infrastructure/logs/inc/LogBase.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace logs {

/* ----------------------------- API (toString) ----------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::OK:
    return "OK";
  case Status::ERROR_OPEN:
    return "ERROR_OPEN";
  case Status::ERROR_SIZE:
    return "ERROR_SIZE";
  case Status::ERROR_ROTATE_RENAME:
    return "ERROR_ROTATE_RENAME";
  case Status::ERROR_ROTATE_REOPEN:
    return "ERROR_ROTATE_REOPEN";
  case Status::ERROR_SYNC:
    return "ERROR_SYNC";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- File Helpers ----------------------------- */

namespace {

inline void toLocalTime(std::time_t t, std::tm& out) noexcept {
#if defined(_WIN32)
  localtime_s(&out, &t);
#else
  localtime_r(&t, &out);
#endif
}

} // namespace

/* ----------------------------- LogBase Methods ----------------------------- */

LogBase::LogBase(const std::string& logPath) noexcept : logPath_(logPath) { openHandleNoThrow(); }

LogBase::~LogBase() noexcept {
  // Flush before closing to ensure all data reaches disk
  (void)flush();

  const int FD = logFd_.load(std::memory_order_acquire);
  if (FD >= 0) {
    (void)::close(FD);
    logFd_.store(-1, std::memory_order_release);
  }
}

void LogBase::openHandleNoThrow() noexcept {
  int fd = ::open(logPath_.c_str(),
                  O_CREAT | O_APPEND | O_WRONLY
#if defined(O_CLOEXEC)
                      | O_CLOEXEC
#endif
                  ,
                  0644);
  if (fd >= 0) {
    logFd_.store(fd, std::memory_order_release);
    openStatus_ = Status::OK;
  } else {
    logFd_.store(-1, std::memory_order_release);
    openStatus_ = Status::ERROR_OPEN;
  }
}

std::string LogBase::getTimestamp() noexcept {
  const auto NOW = std::chrono::system_clock::now();
  const std::time_t T = std::chrono::system_clock::to_time_t(NOW);

  std::tm tmLocal{};
  toLocalTime(T, tmLocal);

  std::ostringstream oss;
  oss << std::put_time(&tmLocal, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::string LogBase::getTimestampForFile() noexcept {
  const auto NOW = std::chrono::system_clock::now();
  const std::time_t T = std::chrono::system_clock::to_time_t(NOW);

  std::tm tmLocal{};
  toLocalTime(T, tmLocal);

  std::ostringstream oss;
  oss << std::put_time(&tmLocal, "%Y%m%d-%H%M%S");
  return oss.str();
}

Status LogBase::appendBytes(const char* data, std::size_t len) noexcept {
  const int FD = logFd_.load(std::memory_order_acquire);
  if (FD < 0 || openStatus_ != Status::OK) {
    return openStatus_;
  }

  const char* p = data;
  std::size_t remaining = len;

  while (remaining > 0) {
    const ssize_t N = ::write(FD, p, remaining);
    if (N < 0) {
      if (errno == EINTR) {
        continue;
      }
      return Status::OK;
    }
    p += static_cast<std::size_t>(N);
    remaining -= static_cast<std::size_t>(N);
  }
  return Status::OK;
}

Status LogBase::write(const std::string& msg) noexcept {
  std::string line;
  line.reserve(msg.size() + 1);
  line.append(msg);
  line.push_back('\n');
  return appendBytes(line.data(), line.size());
}

Status LogBase::flush() noexcept {
  const int FD = logFd_.load(std::memory_order_acquire);
  if (FD < 0) {
    return openStatus_;
  }

  // Force kernel to write buffered data to disk
  if (::fsync(FD) == 0) {
    return Status::OK;
  }

  // fsync failed - may be EINTR (retry once) or real error
  if (errno == EINTR && ::fsync(FD) == 0) {
    return Status::OK;
  }

  return Status::ERROR_SYNC;
}

Status LogBase::size(std::size_t& outBytes) noexcept {
  std::lock_guard<std::mutex> lock(logMutex_);
  return sizeNoLock(outBytes);
}

std::string LogBase::fpath() noexcept {
  std::lock_guard<std::mutex> lock(logMutex_);
  return logPath_.string();
}

Status LogBase::rotate(std::size_t maxSize) noexcept {
  std::lock_guard<std::mutex> lock(logMutex_);

  std::size_t bytes = 0;
  if (sizeNoLock(bytes) != Status::OK || bytes <= maxSize) {
    return Status::OK;
  }

  const std::string BACKUP = logPath_.string() + "_" + getTimestampForFile() + ".backup";

  std::error_code renameEc;
  std::filesystem::rename(logPath_, BACKUP, renameEc);
  if (renameEc) {
    openHandleNoThrow();
    return Status::ERROR_ROTATE_RENAME;
  }

  int newFd = ::open(logPath_.c_str(),
                     O_CREAT | O_APPEND | O_WRONLY
#if defined(O_CLOEXEC)
                         | O_CLOEXEC
#endif
                     ,
                     0644);
  if (newFd < 0) {
    openStatus_ = Status::ERROR_ROTATE_REOPEN;
    return Status::ERROR_ROTATE_REOPEN;
  }

  const int OLD_FD = logFd_.exchange(newFd, std::memory_order_acq_rel);
  openStatus_ = Status::OK;

  if (OLD_FD >= 0) {
    (void)::close(OLD_FD);
  }
  return Status::OK;
}

Status LogBase::sizeNoLock(std::size_t& outBytes) const noexcept {
  std::error_code ec;
  const auto SZ = std::filesystem::file_size(logPath_, ec);
  if (ec) {
    return Status::ERROR_SIZE;
  }
  constexpr auto MAXSZ = static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max());
  const auto CAPPED = SZ > MAXSZ ? MAXSZ : static_cast<unsigned long long>(SZ);
  outBytes = static_cast<std::size_t>(CAPPED);
  return Status::OK;
}

} // namespace logs