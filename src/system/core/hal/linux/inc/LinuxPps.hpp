#ifndef APEX_HAL_LINUX_PPS_HPP
#define APEX_HAL_LINUX_PPS_HPP
/**
 * @file LinuxPps.hpp
 * @brief Linux /dev/pps[N] implementation of IPps.
 *
 * Captures PPS edges by issuing the PPS_FETCH ioctl on a Linux PPS device
 * and immediately reading CLOCK_MONOTONIC for the local timestamp. The
 * kernel latches a precise edge timestamp inside the ISR (within ~1-10us
 * of the physical edge); userspace then samples its own monotonic clock
 * within a few microseconds of consuming the latch. The returned
 * timestamp is in the same monotonic domain as ::clock_gettime
 * (CLOCK_MONOTONIC), so TimeServer can interpolate against it directly.
 *
 * The /dev/pps[N] device is created by the kernel PPS subsystem
 * (CONFIG_PPS) when a board's device tree (or kernel command line)
 * declares a GPIO line as a PPS source. The user-facing API is identical
 * across boards; only the path differs. If the device does not exist,
 * init() returns ERROR_DEVICE and the caller is expected to degrade
 * gracefully.
 *
 * Edge polarity:
 *  - PpsEdge::RISING  -> reads assert_* fields of pps_kinfo
 *  - PpsEdge::FALLING -> reads clear_*  fields of pps_kinfo
 *  Whether the rising edge is the assert depends on the kernel module
 *  configuration; this class assumes the conventional GPS receiver
 *  arrangement (rising = top of second = assert).
 *
 * Test seams:
 *  Subclass and override sysOpen / sysClose / sysIoctl / sysClockGettime
 *  to substitute fake implementations for unit tests. The seams are
 *  noexcept and have C-style signatures so they can be redirected to a
 *  fake function pointer if desired.
 */

#include "src/system/core/hal/base/IPps.hpp"

#include <fcntl.h>
#include <linux/pps.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cstring>

namespace apex {
namespace hal {

/* ----------------------------- LinuxPps ----------------------------- */

/**
 * @class LinuxPps
 * @brief IPps backed by /dev/pps[N] + PPS_FETCH ioctl.
 */
class LinuxPps : public IPps {
public:
  /**
   * @brief Construct with a device path (e.g. "/dev/pps0").
   * @param devicePath Null-terminated path. Stored by pointer; must
   *                   remain valid for the lifetime of LinuxPps.
   */
  explicit LinuxPps(const char* devicePath = "/dev/pps0") noexcept : devicePath_(devicePath) {}

  ~LinuxPps() override { closeFdIfOpen(); }

  LinuxPps(const LinuxPps&) = delete;
  LinuxPps& operator=(const LinuxPps&) = delete;

  /* ----------------------------- IPps overrides ----------------------------- */

  [[nodiscard]] PpsStatus init(const PpsConfig& config) noexcept override {
    if (initialized_) {
      return PpsStatus::OK;
    }
    if (devicePath_ == nullptr) {
      return PpsStatus::ERROR_INVALID_ARG;
    }

    const int fd = sysOpen(devicePath_, O_RDWR);
    if (fd < 0) {
      return PpsStatus::ERROR_DEVICE;
    }

    fd_ = fd;
    config_ = config;
    initialized_ = true;
    lastSequence_ = 0;
    haveLastSequence_ = false;
    pulseCount_ = 0;
    stats_.reset();
    return PpsStatus::OK;
  }

  void deinit() noexcept override { closeFdIfOpen(); }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] PpsStatus readCapture(int64_t& timestampNs) noexcept override {
    if (!initialized_) {
      return PpsStatus::ERROR_NOT_INIT;
    }

    pps_fdata fdata{};
    fdata.timeout.sec = 0;
    fdata.timeout.nsec = 0;
    if (sysIoctl(fd_, PPS_FETCH, &fdata) < 0) {
      ++stats_.errorCount;
      return PpsStatus::ERROR_DEVICE;
    }

    const uint32_t seq = (config_.edge == PpsEdge::RISING) ? fdata.info.assert_sequence
                                                           : fdata.info.clear_sequence;

    // First call after init: prime the sequence baseline without reporting
    // an edge so we don't synthesize one from whatever the kernel happened
    // to have latched before init().
    if (!haveLastSequence_) {
      lastSequence_ = seq;
      haveLastSequence_ = true;
      return PpsStatus::NO_NEW_EDGE;
    }
    if (seq == lastSequence_) {
      return PpsStatus::NO_NEW_EDGE;
    }

    // New edge(s) observed. Update the running pulse counter by the delta
    // (handles dropped/missed reads where >1 edge accumulated) and grab
    // CLOCK_MONOTONIC immediately for the local-domain timestamp.
    pulseCount_ += static_cast<uint32_t>(seq - lastSequence_);
    lastSequence_ = seq;

    struct timespec ts{};
    if (sysClockGettime(CLOCK_MONOTONIC, &ts) < 0) {
      ++stats_.errorCount;
      return PpsStatus::ERROR_DEVICE;
    }
    timestampNs = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(ts.tv_nsec);
    ++stats_.captureCount;
    return PpsStatus::OK;
  }

  [[nodiscard]] uint32_t pulseCount() const noexcept override { return pulseCount_; }

  [[nodiscard]] const PpsStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- Introspection ----------------------------- */

  /** @brief Underlying fd, or -1 if not initialized. For diagnostics. */
  [[nodiscard]] int fd() const noexcept { return fd_; }

  /** @brief Configured device path. */
  [[nodiscard]] const char* devicePath() const noexcept { return devicePath_; }

  /** @brief Most recently observed sequence number from the kernel. */
  [[nodiscard]] uint32_t lastSequence() const noexcept { return lastSequence_; }

protected:
  /* ----------------------------- Test seams ----------------------------- */

  /**
   * @brief Open a device. Defaults to ::open. Override for tests.
   * @return File descriptor on success, -1 on failure.
   */
  virtual int sysOpen(const char* path, int flags) noexcept { return ::open(path, flags); }

  /**
   * @brief Close a device. Defaults to ::close.
   */
  virtual int sysClose(int fd) noexcept { return ::close(fd); }

  /**
   * @brief Issue an ioctl. Defaults to ::ioctl.
   * @note The third argument is `void*` rather than the C variadic form
   *       so that test overrides can inspect or mutate it without
   *       wrestling with va_list.
   */
  virtual int sysIoctl(int fd, unsigned long request, void* argp) noexcept {
    return ::ioctl(fd, request, argp);
  }

  /**
   * @brief Read the monotonic clock. Defaults to ::clock_gettime.
   */
  virtual int sysClockGettime(clockid_t clkId, struct timespec* tsOut) noexcept {
    return ::clock_gettime(clkId, tsOut);
  }

private:
  void closeFdIfOpen() noexcept {
    if (fd_ >= 0) {
      sysClose(fd_);
      fd_ = -1;
    }
    initialized_ = false;
  }

  const char* devicePath_ = nullptr;
  int fd_ = -1;
  bool initialized_ = false;
  PpsConfig config_{};
  PpsStats stats_{};

  uint32_t lastSequence_ = 0;
  bool haveLastSequence_ = false;
  uint32_t pulseCount_ = 0;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_LINUX_PPS_HPP
