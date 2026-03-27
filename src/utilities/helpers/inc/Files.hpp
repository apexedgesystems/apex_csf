#ifndef APEX_UTILITIES_HELPERS_FILES_HPP
#define APEX_UTILITIES_HELPERS_FILES_HPP
/**
 * @file Files.hpp
 * @brief File I/O and path utilities for embedded/RT systems.
 *
 * Provides safe file operations using C-style I/O (open/read/close) to avoid
 * heap allocations. All functions work with fixed-size buffers.
 *
 * @note RT-SAFE: Uses C-style I/O, no heap allocation in core functions.
 *       Path checking uses stat()/access() syscalls.
 */

#include "src/utilities/helpers/inc/Strings.hpp"

#include <fcntl.h>    // open, O_RDONLY, O_CLOEXEC
#include <sys/stat.h> // stat, S_ISDIR, S_ISREG
#include <unistd.h>   // read, close, access

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib> // strtol, strtoll, strtoull
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace apex {
namespace helpers {
namespace files {

/* ----------------------------- Constants ----------------------------- */

/// Default buffer size for file reads.
inline constexpr std::size_t FILE_READ_BUFFER_SIZE = 256;

/// Default buffer size for path construction.
inline constexpr std::size_t PATH_BUFFER_SIZE = 256;

/// Size for small integer file reads.
inline constexpr std::size_t INT_READ_BUFFER_SIZE = 64;

/* ----------------------------- File Reading ----------------------------- */

/**
 * @brief Read file contents into buffer using C-style I/O.
 * @param path File path to read.
 * @param buf Output buffer.
 * @param bufSize Size of output buffer.
 * @return Number of bytes read (excluding null terminator), 0 on error.
 * @note RT-SAFE: Uses open/read/close, no heap allocation.
 *
 * Strips trailing newlines and carriage returns. Always null-terminates.
 */
[[nodiscard]] inline std::size_t readFileToBuffer(const char* path, char* buf,
                                                  std::size_t bufSize) noexcept {
  if (path == nullptr || buf == nullptr || bufSize == 0) {
    if (buf != nullptr && bufSize > 0) {
      buf[0] = '\0';
    }
    return 0;
  }

  buf[0] = '\0';

  const int FD = ::open(path, O_RDONLY | O_CLOEXEC);
  if (FD < 0) {
    return 0;
  }

  std::size_t total = 0;
  while (total < bufSize - 1) {
    const ssize_t N = ::read(FD, buf + total, bufSize - 1 - total);
    if (N <= 0) {
      break;
    }
    total += static_cast<std::size_t>(N);
  }

  ::close(FD);
  buf[total] = '\0';

  apex::helpers::strings::stripTrailingWhitespace(buf, total);

  return total;
}

/**
 * @brief Read first line from file into fixed array.
 * @tparam N Array size.
 * @param path File path to read.
 * @param out Output array.
 * @return Number of characters read (excluding null), 0 on error.
 * @note RT-SAFE: Uses open/read/close, no heap allocation.
 */
template <std::size_t N>
[[nodiscard]] inline std::size_t readFileLine(const char* path, std::array<char, N>& out) noexcept {
  out[0] = '\0';

  std::array<char, N> buf{};
  const std::size_t LEN = readFileToBuffer(path, buf.data(), buf.size());
  if (LEN == 0) {
    return 0;
  }

  std::size_t copyLen = 0;
  while (copyLen < LEN && copyLen < N - 1 && buf[copyLen] != '\n' && buf[copyLen] != '\0') {
    out[copyLen] = buf[copyLen];
    ++copyLen;
  }
  out[copyLen] = '\0';

  return copyLen;
}

/**
 * @brief Read signed 32-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::int32_t readFileInt(const char* path,
                                              std::int32_t defaultVal = -1) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const long VAL = std::strtol(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::int32_t>(VAL);
}

/**
 * @brief Read signed 64-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::int64_t readFileInt64(const char* path,
                                                std::int64_t defaultVal = -1) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const long long VAL = std::strtoll(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::int64_t>(VAL);
}

/**
 * @brief Read unsigned 64-bit integer from file.
 * @param path File path to read.
 * @param defaultVal Value to return on error.
 * @return Parsed integer or defaultVal on failure.
 * @note RT-SAFE: No heap allocation.
 */
[[nodiscard]] inline std::uint64_t readFileUint64(const char* path,
                                                  std::uint64_t defaultVal = 0) noexcept {
  std::array<char, INT_READ_BUFFER_SIZE> buf{};
  if (readFileToBuffer(path, buf.data(), buf.size()) == 0) {
    return defaultVal;
  }

  char* end = nullptr;
  const unsigned long long VAL = std::strtoull(buf.data(), &end, 10);
  if (end == buf.data()) {
    return defaultVal;
  }

  return static_cast<std::uint64_t>(VAL);
}

/* ----------------------------- Path Utilities ----------------------------- */

/**
 * @brief Check if path exists (file or directory).
 * @param path Path to check.
 * @return true if path exists.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool pathExists(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  return ::stat(path, &st) == 0;
}

/**
 * @brief Check if path is a directory.
 * @param path Path to check.
 * @return true if path exists and is a directory.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isDirectory(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

/**
 * @brief Check if path is a regular file.
 * @param path Path to check.
 * @return true if path exists and is a regular file.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isRegularFile(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

/**
 * @brief Check if file is readable by current process.
 * @param path Path to check.
 * @return true if file can be opened for reading.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isReadable(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  return ::access(path, R_OK) == 0;
}

/**
 * @brief Check if path is a character device.
 * @param path Path to check.
 * @return true if path exists and is a character device.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isCharDevice(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISCHR(st.st_mode);
}

/**
 * @brief Check if path is a block device.
 * @param path Path to check.
 * @return true if path exists and is a block device.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isBlockDevice(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::stat(path, &st) != 0) {
    return false;
  }
  return S_ISBLK(st.st_mode);
}

/**
 * @brief Check if path is a symbolic link.
 * @param path Path to check.
 * @return true if path is a symbolic link.
 * @note RT-CAUTION: Syscall latency depends on filesystem/cache state.
 */
[[nodiscard]] inline bool isSymlink(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  struct stat st{};
  if (::lstat(path, &st) != 0) {
    return false;
  }
  return S_ISLNK(st.st_mode);
}

/* ----------------------------- String View Overloads ----------------------------- */

/**
 * @brief Check whether a path exists on the filesystem.
 *
 * Accepts files or directories. Returns false for empty paths.
 * Allocates a temporary string for null-termination.
 *
 * @param fpath Path to check (non-owning view).
 * @return true if the path exists; false otherwise.
 * @note Cold-path: Allocates temporary string for null-termination.
 * @see pathExists(const char*) for RT-safe variant.
 */
[[nodiscard]] inline bool checkFileExists(std::string_view fpath) noexcept {
  if (fpath.empty()) {
    return false;
  }
  // Delegate to pathExists for null-terminated path.
  const std::string PATH{fpath};
  return pathExists(PATH.c_str());
}

/* ----------------------------- Binary File Loading ----------------------------- */

namespace detail {

inline void appendUnsignedDec(std::string& s, std::size_t x) {
  std::array<char, 32> buf{};
  char* p = buf.data() + buf.size();
  *--p = '\0';
  do {
    *--p = static_cast<char>('0' + (x % 10U));
    x /= 10U;
  } while (x != 0U);
  s.append(p);
}

inline void setHex2cppError(std::optional<std::reference_wrapper<std::string>>& err,
                            const char* msgPrefix, const std::string& path) {
  if (!err)
    return;
  std::string& e = err->get();
  e.assign(msgPrefix);
  e.append(": path='");
  e.append(path);
  e.push_back('\'');
}

} // namespace detail

/**
 * @brief Load a raw binary file directly into a trivially copyable struct.
 *
 * Validates exact size match before reading.
 *
 * @tparam T Trivially copyable destination type.
 * @param fpath Path to the binary file.
 * @param packedStruct Destination object to fill.
 * @param error Optional error message target (set on failure when provided).
 * @return true on success; false on error (and sets error if provided).
 * @note Cold-path: File I/O.
 */
template <typename T>
[[nodiscard]] inline bool
hex2cpp(const std::string& fpath, T& packedStruct,
        std::optional<std::reference_wrapper<std::string>> error = std::nullopt) noexcept {
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

  if (fpath.empty()) [[unlikely]] {
    if (error)
      error->get() = "Empty path";
    return false;
  }

  int fd = ::open(fpath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) [[unlikely]] {
    detail::setHex2cppError(error, "open failed", fpath);
    return false;
  }

  struct stat st{};
  if (::fstat(fd, &st) != 0) [[unlikely]] {
    detail::setHex2cppError(error, "fstat failed", fpath);
    (void)::close(fd);
    return false;
  }

  const std::size_t EXPECTED_SIZE = sizeof(T);
  const auto FILE_SIZE = static_cast<std::size_t>(st.st_size);
  if (FILE_SIZE != EXPECTED_SIZE) [[unlikely]] {
    if (error) {
      std::string& e = error->get();
      e.assign("size mismatch: file=");
      detail::appendUnsignedDec(e, FILE_SIZE);
      e.append(" struct=");
      detail::appendUnsignedDec(e, EXPECTED_SIZE);
    }
    (void)::close(fd);
    return false;
  }

  auto* dst = reinterpret_cast<std::uint8_t*>(&packedStruct);
  std::size_t remaining = EXPECTED_SIZE;
  while (remaining > 0U) {
    const ssize_t N = ::read(fd, dst, remaining);
    if (N <= 0) [[unlikely]] {
      detail::setHex2cppError(error, "read failed", fpath);
      (void)::close(fd);
      return false;
    }
    const auto STEP = static_cast<std::size_t>(N);
    dst += STEP;
    remaining -= STEP;
  }

  (void)::close(fd);
  return true;
}

} // namespace files
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_FILES_HPP
