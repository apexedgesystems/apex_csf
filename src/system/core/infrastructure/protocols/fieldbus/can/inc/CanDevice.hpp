#ifndef APEX_PROTOCOLS_FIELDBUS_CAN_CAN_DEVICE_HPP
#define APEX_PROTOCOLS_FIELDBUS_CAN_CAN_DEVICE_HPP
/**
 * @file CanDevice.hpp
 * @brief Abstract CAN device interface (SocketCAN, MCU HAL, etc.) for the fieldbus layer.
 *
 * Purpose
 *  - Define a minimal, frame-oriented contract for CAN backends.
 *  - Keep APIs zero-allocation at call sites; callers provide storage.
 *
 * Notes
 *  - Derived classes implement backend specifics (e.g., SocketCAN, MCU controllers).
 *  - Backends may ignore unsupported configuration fields; such behavior should be documented
 *    by each backend.
 *
 * Timeout parameter
 *  - This interface accepts a @p timeoutMs parameter for recv/send. Its precise behavior may vary
 *    by backend. The recommended interpretation (used by the SocketCAN adapter) is:
 *      - < 0 : block until I/O is ready
 *      - == 0: nonblocking poll (immediate readiness check)
 *      - > 0 : bounded wait (best-effort up to @p timeoutMs)
 *  - Backends should document the exact mapping (e.g., whether elapsed waits yield WOULD_BLOCK
 *    or a specific ERROR_*).
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStatus.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

/* ------------------------------ Data Types ------------------------------ */

/**
 * @brief CAN identifier attributes.
 *
 * Semantics
 *  - @ref id holds either an 11-bit (standard) or 29-bit (extended) identifier in the low bits.
 *  - @ref extended selects 29-bit format when true (otherwise 11-bit).
 *  - @ref remote marks an RTR (remote transmission request) frame.
 *  - @ref error marks a controller/kernel error frame (backend-dependent).
 */
struct CanId {
  std::uint32_t id = 0;  ///< 11-bit (standard) or 29-bit (extended) identifier in low bits.
  bool extended = false; ///< True for 29-bit extended frames.
  bool remote = false;   ///< True for RTR frames.
  bool error = false;    ///< True for error frames (backend-dependent signaling).
};

/**
 * @brief Masked acceptance filter (backend may support a set of filters).
 *
 * Semantics
 *  - If @ref extended == true, @ref id and @ref mask are interpreted as 29-bit.
 *  - Bits set to 1 in @ref mask are compared against @ref id.
 */
struct CanFilter {
  std::uint32_t id = 0;   ///< Identifier to match (width per @ref extended).
  std::uint32_t mask = 0; ///< Bits set to 1 are compared.
  bool extended = false;  ///< True to interpret as 29-bit IDs.
};

/**
 * @brief Classic CAN data frame (DLC <= 8).
 *
 * Semantics
 *  - @ref dlc is the payload length (0..8). Only the first @ref dlc bytes of @ref data are valid.
 *  - @ref timestampNs is optional and backend-defined (e.g., SO_TIMESTAMPING); unset if
 * unavailable.
 */
struct CanFrame {
  CanId canId{};
  std::uint8_t dlc = 0;                     ///< Payload length (0..8) for classic CAN.
  std::array<std::uint8_t, 8> data{{0}};    ///< First @c dlc bytes are valid payload.
  std::optional<std::uint64_t> timestampNs; ///< Monotonic or HW timestamp in ns, if available.
};

/**
 * @brief Device configuration (bitrate, mode, filters).
 *
 * Notes
 *  - Some fields may not be enforceable by certain backends (e.g., raw sockets).
 *    Backends should document which fields are honored or ignored.
 */
struct CanConfig {
  std::uint32_t bitrate = 500000; ///< Nominal bitrate (classic CAN) in bps.
  bool listenOnly = false;        ///< Silent mode (no ACK) when supported.
  bool loopback = false;          ///< Local loopback when supported.
  std::vector<CanFilter> filters; ///< Empty = backend default (often "accept all").
};

/**
 * @brief Fixed-size device configuration (no heap allocation).
 *
 * Alternative to CanConfig that uses a compile-time fixed array for filters.
 * Use this when configure() must be RT-safe or heap allocation is forbidden.
 *
 * @tparam N Maximum number of filters (0 = no filters, accept all).
 *
 * Usage:
 * @code
 *   CanConfigFixed<4> cfg;
 *   cfg.loopback = true;
 *   cfg.addFilter(CanFilter{.id = 0x123, .mask = 0x7FF, .extended = false});
 *   adapter.configure(cfg);
 * @endcode
 *
 * @note RT-safe: No heap allocation.
 */
template <std::size_t N> struct CanConfigFixed {
  std::uint32_t bitrate = 500000;     ///< Nominal bitrate (classic CAN) in bps.
  bool listenOnly = false;            ///< Silent mode (no ACK) when supported.
  bool loopback = false;              ///< Local loopback when supported.
  std::array<CanFilter, N> filters{}; ///< Fixed-size filter array.
  std::size_t filterCount = 0;        ///< Number of valid filters (0..N).

  /**
   * @brief Add a filter to the configuration.
   * @param filter Filter to add.
   * @return true if added, false if array is full.
   * @note RT-safe: No allocation.
   */
  bool addFilter(const CanFilter& filter) noexcept {
    if (filterCount >= N)
      return false;
    filters[filterCount++] = filter;
    return true;
  }

  /**
   * @brief Clear all filters.
   * @note RT-safe: Simple counter reset.
   */
  void clearFilters() noexcept { filterCount = 0; }

  /**
   * @brief Convert to CanConfig for API compatibility.
   *
   * @return CanConfig with filters copied to vector.
   * @note NOT RT-safe: Allocates vector.
   */
  [[nodiscard]] CanConfig toCanConfig() const {
    CanConfig cfg;
    cfg.bitrate = bitrate;
    cfg.listenOnly = listenOnly;
    cfg.loopback = loopback;
    cfg.filters.assign(filters.begin(), filters.begin() + filterCount);
    return cfg;
  }
};

/* ------------------------------- CanDevice ------------------------------- */

/**
 * @class CanDevice
 * @brief Abstract interface for sending/receiving CAN frames on a channel (e.g., "can0").
 *
 * Contract
 *  - Implementations should avoid dynamic allocations on hot paths.
 *  - Methods return compact, typed status codes; use @ref toString(Status) for human strings.
 *  - Timeout behavior is backend-defined; follow the recommended interpretation unless strong
 *    reasons dictate otherwise, and document any differences.
 */
class CanDevice {
public:
  virtual ~CanDevice() = default;

  /**
   * @brief Human-friendly label for logs/diagnostics.
   * @note RT-safe: no allocation or I/O
   */
  virtual const std::string& description() const = 0;

  /**
   * @brief Channel name/identifier (e.g., "can0", "fdcan1").
   * @note RT-safe: no allocation or I/O
   */
  virtual const std::string& channel() const = 0;

  /**
   * @brief Configure the device (idempotent). May open, bind, and apply options.
   *
   * @param cfg Desired configuration.
   * @return Status::SUCCESS on success; Status::ERROR_* otherwise.
   * @note NOT RT-safe: performs I/O syscall
   */
  virtual Status configure(const CanConfig& cfg) = 0;

  /**
   * @brief Receive a single classic CAN frame.
   *
   * @param outFrame   Output frame (filled on Status::SUCCESS).
   * @param timeoutMs  Backend-defined timeout policy (see notes at top of file).
   * @return
   *  - Status::SUCCESS      : exactly one frame delivered into @p outFrame.
   *  - Status::WOULD_BLOCK  : not ready now / not ready within the backend's policy.
   *  - Status::ERROR_*      : error condition (e.g., not configured, closed, I/O error).
   * @note NOT RT-safe: performs I/O syscall
   */
  virtual Status recv(CanFrame& outFrame, int timeoutMs) = 0;

  /**
   * @brief Send a single classic CAN frame.
   *
   * @param frame      Frame to transmit (DLC <= 8).
   * @param timeoutMs  Backend-defined timeout policy (see notes at top of file).
   * @return
   *  - Status::SUCCESS           : frame was queued/sent per backend semantics.
   *  - Status::WOULD_BLOCK       : not writable now / not writable within policy.
   *  - Status::ERROR_INVALID_ARG : invalid frame parameters (e.g., DLC > 8).
   *  - Status::ERROR_*           : error condition (e.g., not configured, closed, I/O error).
   * @note NOT RT-safe: performs I/O syscall
   */
  virtual Status send(const CanFrame& frame, int timeoutMs) = 0;

  /**
   * @brief Enable or disable device-level logging.
   * @param enable True to enable logging.
   *
   * Notes
   *  - The granularity and mechanism are backend-defined (may be a no-op).
   * @note RT-safe: no allocation or I/O
   */
  virtual void enableLogging(bool enable) = 0;

  /**
   * @brief Get current frame statistics.
   * @return Copy of current statistics counters.
   * @note RT-safe: Returns by value (no allocation).
   */
  [[nodiscard]] virtual CanStats stats() const noexcept = 0;

  /**
   * @brief Reset all statistics counters to zero.
   * @note RT-safe: Simple counter reset.
   */
  virtual void resetStats() noexcept = 0;

  /**
   * @brief Receive multiple frames in a single call (batch drain).
   *
   * Drains all immediately available frames (up to @p maxFrames) from the
   * receive buffer. More efficient than repeated recv() calls for high-frequency
   * CAN buses.
   *
   * @param frames     Output buffer (caller-provided, must hold @p maxFrames elements).
   * @param maxFrames  Maximum frames to receive (buffer capacity).
   * @param timeoutMs  Timeout for the first frame only:
   *                   - <0 : block until at least one frame arrives
   *                   - ==0: nonblocking poll
   *                   - >0 : wait up to timeoutMs for first frame
   *                   After the first frame, remaining frames are drained nonblocking.
   * @return Number of frames received (0 to maxFrames).
   *         Returns 0 if no frames available within timeout (not an error).
   * @note RT-safe: No allocation (caller provides buffer).
   */
  virtual std::size_t recvBatch(CanFrame* frames, std::size_t maxFrames, int timeoutMs) = 0;
};

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_CAN_CAN_DEVICE_HPP
