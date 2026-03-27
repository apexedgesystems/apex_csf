#ifndef APEX_UTILITIES_HELPERS_FD_HPP
#define APEX_UTILITIES_HELPERS_FD_HPP
/**
 * @file Fd.hpp
 * @brief RAII wrapper for POSIX file descriptors.
 *
 * Provides automatic close() on destruction with move semantics.
 * Prevents resource leaks from forgotten close() calls.
 *
 * @note RT-CAUTION: Destructor calls close() syscall. Construct/destruct
 *       in setup/teardown code, not in RT hot paths.
 */

#include <unistd.h>

namespace apex {
namespace helpers {
namespace fd {

/* ----------------------------- Types ----------------------------- */

/**
 * @brief RAII wrapper for a POSIX file descriptor.
 *
 * Owns a file descriptor and closes it on destruction. Move-only
 * (non-copyable) to ensure single ownership.
 *
 * @note RT-CAUTION: Destructor calls close() syscall.
 */
class UniqueFd {
public:
  /**
   * @brief Construct with optional file descriptor.
   * @param fd File descriptor to own (-1 for empty).
   */
  explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}

  /**
   * @brief Destructor closes the file descriptor if valid.
   * @note RT-CAUTION: Calls close() syscall.
   */
  ~UniqueFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  // Non-copyable
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  // Movable
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) {
        ::close(fd_);
      }
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  /**
   * @brief Get the raw file descriptor.
   * @return File descriptor value (-1 if empty).
   * @note RT-SAFE: No syscall.
   */
  [[nodiscard]] int get() const noexcept { return fd_; }

  /**
   * @brief Replace the owned file descriptor.
   * @param fd New file descriptor to own (-1 to release).
   * @note RT-CAUTION: Calls close() on previous fd if valid.
   */
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

  /**
   * @brief Release ownership without closing.
   * @return The file descriptor (caller now owns it).
   * @note RT-SAFE: No syscall.
   */
  [[nodiscard]] int release() noexcept {
    int tmp = fd_;
    fd_ = -1;
    return tmp;
  }

  /**
   * @brief Check if the file descriptor is valid.
   * @return True if fd >= 0.
   * @note RT-SAFE: No syscall.
   */
  explicit operator bool() const noexcept { return fd_ >= 0; }

private:
  int fd_{-1};
};

} // namespace fd
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_FD_HPP
