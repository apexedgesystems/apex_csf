#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_CAN_BUS_ADAPTER_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_CAN_BUS_ADAPTER_HPP
/**
 * @file CanBusAdapter.hpp
 * @brief SocketCAN-backed CAN device (PF_CAN/SOCK_RAW) implementing
 *        @ref apex::protocols::fieldbus::can::CanDevice "CanDevice".
 *
 * Design goals
 *  - Frame-oriented API (no byte-stream ambiguity).
 *  - Nonblocking by default with explicit timeout semantics.
 *  - Clean separation from kernel details via
 *    @ref apex::protocols::fieldbus::can::CanDevice "CanDevice".
 *
 * Backend notes
 *  - Filters via CAN_RAW_FILTER; loopback via CAN_RAW_LOOPBACK.
 *  - Bitrate and listen-only typically require `ip link`/netlink; accepted in
 *    @ref apex::protocols::fieldbus::can::CanConfig "CanConfig" but not applied by this adapter.
 *  - Timestamps (SO_TIMESTAMPING) are not enabled by default.
 *
 * Timeout semantics
 *  - timeoutMs < 0 : block until I/O is ready.
 *  - timeoutMs == 0: poll now; return Status::WOULD_BLOCK if not ready.
 *  - timeoutMs > 0 : wait up to timeoutMs ms; return Status::WOULD_BLOCK if not ready in time.
 *
 * Limits
 *  - Classic CAN only (DLC <= 8).
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanDevice.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStats.hpp"

#include <string>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* ----------------------------- CANBusAdapter ----------------------------- */

/**
 * @class CANBusAdapter
 * @brief Linux SocketCAN adapter that fulfills the
 *        @ref apex::protocols::fieldbus::can::CanDevice "CanDevice" interface.
 */
class CANBusAdapter final : public CanDevice, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct a SocketCAN adapter bound to a kernel interface.
   * @param description   Human-friendly label for logs/diagnostics.
   * @param interfaceName Kernel interface name (e.g., "can0", "vcan0").
   * @note NOT RT-safe: allocates for string copy
   */
  CANBusAdapter(std::string description, std::string interfaceName);

  /**
   * @brief Close the socket on destruction (if open).
   * @note NOT RT-safe: performs I/O syscall
   */
  ~CANBusAdapter() override;

  /* ----------------------- CanDevice API ----------------------- */

  /**
   * @brief Human-friendly description (as provided at construction).
   * @note RT-safe: no allocation or I/O
   */
  const std::string& description() const override { return desc_; }

  /**
   * @brief Channel / interface name (e.g., "can0").
   * @note RT-safe: no allocation or I/O
   */
  const std::string& channel() const override { return ifName_; }

  /**
   * @brief Configure the adapter (idempotent). Opens/binds the raw socket if needed.
   *
   * Applies on this backend:
   *  - Kernel loopback (CAN_RAW_LOOPBACK).
   *  - Acceptance filters (CAN_RAW_FILTER). Empty = accept-all.
   *
   * Accepted but not applied here (raw-socket limitation):
   *  - @ref apex::protocols::fieldbus::can::CanConfig::bitrate   "CanConfig::bitrate"
   *  - @ref apex::protocols::fieldbus::can::CanConfig::listenOnly "CanConfig::listenOnly"
   *
   * @param cfg Desired configuration (see
   *  @ref apex::protocols::fieldbus::can::CanConfig "CanConfig").
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   * @note NOT RT-safe: performs I/O syscall
   */
  Status configure(const CanConfig& cfg) override;

  /**
   * @brief Configure with fixed-size configuration (no heap allocation).
   *
   * @tparam N Maximum number of filters in the configuration.
   * @param cfg Fixed-size configuration.
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   * @note Converts to CanConfig internally (allocates vector for filters).
   * @note NOT RT-safe: performs I/O syscall
   */
  template <std::size_t N> Status configure(const CanConfigFixed<N>& cfg) {
    return configure(cfg.toCanConfig());
  }

  /**
   * @brief Receive one classic CAN frame.
   *
   * Behavior:
   *  - Nonblocking by default (see timeout semantics in file header).
   *  - On success, @p outFrame is filled. Timestamp is not set by default.
   *
   * @param outFrame Output frame (valid on Status::SUCCESS).
   * @param timeoutMs <0 block; 0 poll; >0 bounded wait in ms.
   * @return Status::SUCCESS on one frame delivered;
   *         Status::WOULD_BLOCK if not ready within policy;
   *         Status::ERROR_* on error (e.g., closed, not configured).
   * @note NOT RT-safe: performs I/O syscall
   */
  Status recv(CanFrame& outFrame, int timeoutMs) override;

  /**
   * @brief Send one classic CAN frame (DLC <= 8).
   *
   * Behavior:
   *  - Nonblocking by default (see timeout semantics in file header).
   *  - Returns Status::WOULD_BLOCK if not writable within policy.
   *
   * @param frame Frame to transmit.
   * @param timeoutMs <0 block; 0 poll; >0 bounded wait in ms.
   * @return Status::SUCCESS on success;
   *         Status::WOULD_BLOCK if not writable within policy;
   *         Status::ERROR_INVALID_ARG if DLC > 8;
   *         Status::ERROR_* on error (e.g., closed, not configured).
   * @note NOT RT-safe: performs I/O syscall
   */
  Status send(const CanFrame& frame, int timeoutMs) override;

  /**
   * @brief Enable or disable adapter-level logging (granularity is backend-defined).
   * @param enable True to enable logging.
   * @note RT-safe: no allocation or I/O
   */
  void enableLogging(bool enable) override { loggingEnabled_ = enable; }

  /**
   * @brief Get current frame statistics.
   * @return Copy of current statistics counters.
   * @note RT-safe: Returns by value (no allocation).
   */
  [[nodiscard]] CanStats stats() const noexcept override { return stats_; }

  /**
   * @brief Reset all statistics counters to zero.
   * @note RT-safe: Simple counter reset.
   */
  void resetStats() noexcept override { stats_.reset(); }

  /**
   * @brief Receive multiple frames in a single call (batch drain).
   *
   * @param frames     Output buffer (caller-provided).
   * @param maxFrames  Maximum frames to receive.
   * @param timeoutMs  Timeout for first frame only (see CanDevice::recvBatch).
   * @return Number of frames received (0 to maxFrames).
   * @note RT-safe: No allocation.
   */
  std::size_t recvBatch(CanFrame* frames, std::size_t maxFrames, int timeoutMs) override;

  /**
   * @brief Get the underlying socket file descriptor.
   *
   * Useful for advanced use cases like integrating with epoll event loops.
   *
   * @return Socket fd, or -1 if not configured.
   * @note RT-safe: Simple accessor.
   */
  [[nodiscard]] int socketFd() const noexcept { return sockfd_; }

private:
  // Construction / teardown
  /**
   * @brief Open PF_CAN/SOCK_RAW and bind to @ref ifName_ (idempotent).
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   */
  Status openAndBindIfNeeded();

  /** @brief Close the socket if open (noexcept). */
  void closeSocket() noexcept;

  // Options
  /**
   * @brief Apply acceptance filters from
   * @ref apex::protocols::fieldbus::can::CanConfig "CanConfig".
   * @param cfg Configuration with desired filters.
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   */
  Status applyFilters(const CanConfig& cfg);

  /**
   * @brief Enable or disable kernel loopback.
   * @param enable True to enable CAN_RAW_LOOPBACK.
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   */
  Status setLoopback(bool enable);

  // Wait helpers (cold-ish path)
  /**
   * @brief Wait for readiness on @p fd using poll(2).
   * @param fd File descriptor.
   * @param events POLLIN/POLLOUT flags.
   * @param timeoutMs <0 block; 0 no-wait; >0 bounded wait in ms.
   * @return >0 if ready; 0 if timed out/no-wait; <0 on error (errno set).
   */
  static int waitFd(int fd, short events, int timeoutMs) noexcept;

private:
  std::string desc_;           ///< Human-friendly label.
  std::string ifName_;         ///< Kernel interface name (e.g., "can0").
  int sockfd_{-1};             ///< Raw CAN socket (PF_CAN/SOCK_RAW), or -1 if closed.
  bool loggingEnabled_{false}; ///< Adapter-level logging toggle.
  bool configured_{false};     ///< True after successful configure().
  CanConfig lastCfg_{};        ///< Last applied config (for diagnostics).
  mutable CanStats stats_{};   ///< Frame statistics (mutable for const stats()).
};

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_CAN_BUS_ADAPTER_HPP
