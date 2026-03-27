#ifndef APEX_HAL_ICAN_HPP
#define APEX_HAL_ICAN_HPP
/**
 * @file ICan.hpp
 * @brief Abstract CAN interface for embedded systems.
 *
 * Platform-agnostic CAN interface designed for MCUs and embedded Linux.
 * Unlike the protocols/fieldbus/can/ library (which uses STL containers
 * and POSIX sockets), this interface has no OS dependencies and can be
 * implemented on bare-metal.
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies (no fd, no SocketCAN)
 *  - Static buffers managed by implementation
 *  - Suitable for interrupt-driven operation
 *  - Frame-oriented (not byte-stream)
 *
 * Implementations:
 *  - Stm32Can (STM32 bxCAN peripheral)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- CanStatus ----------------------------- */

/**
 * @brief Status codes for CAN operations.
 */
enum class CanStatus : uint8_t {
  OK = 0,           ///< Operation succeeded.
  WOULD_BLOCK,      ///< No frame available or TX mailbox full.
  BUSY,             ///< Peripheral busy.
  ERROR_TIMEOUT,    ///< Operation timed out.
  ERROR_OVERRUN,    ///< RX buffer overrun (frame lost).
  ERROR_BUS_OFF,    ///< CAN bus-off state (too many errors).
  ERROR_PASSIVE,    ///< CAN error-passive state.
  ERROR_NOT_INIT,   ///< CAN not initialized.
  ERROR_INVALID_ARG ///< Invalid argument (e.g., DLC > 8).
};

/**
 * @brief Convert CanStatus to string.
 * @param s Status value.
 * @return Human-readable string.
 * @note RT-safe: Returns static string literal.
 */
inline const char* toString(CanStatus s) noexcept {
  switch (s) {
  case CanStatus::OK:
    return "OK";
  case CanStatus::WOULD_BLOCK:
    return "WOULD_BLOCK";
  case CanStatus::BUSY:
    return "BUSY";
  case CanStatus::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case CanStatus::ERROR_OVERRUN:
    return "ERROR_OVERRUN";
  case CanStatus::ERROR_BUS_OFF:
    return "ERROR_BUS_OFF";
  case CanStatus::ERROR_PASSIVE:
    return "ERROR_PASSIVE";
  case CanStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case CanStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- CanMode ----------------------------- */

/**
 * @brief CAN operating mode.
 */
enum class CanMode : uint8_t {
  NORMAL = 0,     ///< Normal bus operation (TX and RX).
  LOOPBACK,       ///< Internal loopback (TX echoed to RX, no bus activity).
  SILENT,         ///< Listen-only (RX only, no ACK on bus).
  SILENT_LOOPBACK ///< Internal loopback + silent (no bus activity at all).
};

/* ----------------------------- CanId ----------------------------- */

/**
 * @brief CAN frame identifier.
 */
struct CanId {
  uint32_t id = 0;       ///< 11-bit (standard) or 29-bit (extended) identifier.
  bool extended = false; ///< True for 29-bit extended frames.
  bool remote = false;   ///< True for RTR (remote transmission request) frames.
};

/* ----------------------------- CanFrame ----------------------------- */

/**
 * @brief Classic CAN data frame (DLC 0..8).
 *
 * Only the first @c dlc bytes of @c data are valid payload.
 */
struct CanFrame {
  CanId canId{};        ///< Frame identifier.
  uint8_t dlc = 0;      ///< Payload length (0..8).
  uint8_t data[8] = {}; ///< Payload bytes (first dlc bytes valid).
};

/* ----------------------------- CanFilter ----------------------------- */

/**
 * @brief Masked acceptance filter for CAN frames.
 *
 * Bits set to 1 in @c mask are compared against @c id.
 * If @c extended is true, 29-bit comparison is used.
 */
struct CanFilter {
  uint32_t id = 0;       ///< Identifier to match.
  uint32_t mask = 0;     ///< Mask (1 = compare, 0 = don't care).
  bool extended = false; ///< True for 29-bit filter.
};

/* ----------------------------- CanConfig ----------------------------- */

/**
 * @brief CAN configuration parameters.
 */
struct CanConfig {
  uint32_t bitrate = 500000;      ///< Nominal bitrate in bps.
  CanMode mode = CanMode::NORMAL; ///< Operating mode.
  bool autoRetransmit = true;     ///< Auto-retransmit on failure.
};

/* ----------------------------- CanStats ----------------------------- */

/**
 * @brief CAN statistics for monitoring.
 */
struct CanStats {
  uint32_t framesTx = 0;    ///< Frames successfully transmitted.
  uint32_t framesRx = 0;    ///< Frames successfully received.
  uint32_t errorFrames = 0; ///< Error frames detected.
  uint32_t busOffCount = 0; ///< Bus-off transitions.
  uint32_t txOverflows = 0; ///< TX attempts when mailbox full.
  uint32_t rxOverflows = 0; ///< RX frames dropped (buffer full).

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe.
   */
  void reset() noexcept {
    framesTx = 0;
    framesRx = 0;
    errorFrames = 0;
    busOffCount = 0;
    txOverflows = 0;
    rxOverflows = 0;
  }

  /**
   * @brief Get total error count.
   * @return Sum of all error counters.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t totalErrors() const noexcept {
    return errorFrames + busOffCount + txOverflows + rxOverflows;
  }
};

/* ----------------------------- ICan ----------------------------- */

/**
 * @class ICan
 * @brief Abstract CAN interface for embedded systems.
 *
 * Provides a common API for CAN communication across different MCU platforms.
 * Implementations manage their own RX buffers and interrupt configuration.
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific)
 *  2. Optionally call addFilter() to configure acceptance filters
 *  3. Call init() with configuration
 *  4. Call send()/recv() for frame I/O
 *  5. Call deinit() to release peripheral
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - For RTOS use, wrap with mutex or use from single task.
 *
 * RT-Safety:
 *  - init()/deinit()/addFilter()/clearFilters(): NOT RT-safe (peripheral setup)
 *  - send()/recv(): RT-safe after init (no allocation, bounded time)
 *  - txReady()/rxAvailable(): RT-safe (register or buffer reads)
 */
class ICan {
public:
  virtual ~ICan() = default;

  /**
   * @brief Initialize the CAN peripheral.
   * @param config Configuration parameters.
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: Configures peripheral registers.
   */
  [[nodiscard]] virtual CanStatus init(const CanConfig& config) noexcept = 0;

  /**
   * @brief Deinitialize the CAN peripheral.
   * @note NOT RT-safe: Releases peripheral.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check if CAN is initialized and ready.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /**
   * @brief Send a CAN frame (non-blocking).
   * @param frame Frame to transmit (DLC must be 0..8).
   * @return OK if queued for transmission, WOULD_BLOCK if no mailbox free,
   *         ERROR_INVALID_ARG if DLC > 8, ERROR_NOT_INIT if not initialized.
   * @note RT-safe: Returns immediately.
   */
  [[nodiscard]] virtual CanStatus send(const CanFrame& frame) noexcept = 0;

  /**
   * @brief Receive a CAN frame (non-blocking).
   * @param frame Output frame (filled on OK).
   * @return OK if frame received, WOULD_BLOCK if buffer empty,
   *         ERROR_NOT_INIT if not initialized.
   * @note RT-safe: Copies from RX buffer, returns immediately.
   */
  [[nodiscard]] virtual CanStatus recv(CanFrame& frame) noexcept = 0;

  /**
   * @brief Check if TX mailbox is available.
   * @return true if at least one TX mailbox is free.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool txReady() const noexcept = 0;

  /**
   * @brief Get number of frames available to receive.
   * @return Number of frames in RX buffer.
   * @note RT-safe.
   */
  [[nodiscard]] virtual size_t rxAvailable() const noexcept = 0;

  /**
   * @brief Add an acceptance filter.
   * @param filter Filter to add.
   * @return OK if added, ERROR_INVALID_ARG if filter bank full.
   * @note NOT RT-safe: Call before init() or after deinit().
   */
  [[nodiscard]] virtual CanStatus addFilter(const CanFilter& filter) noexcept = 0;

  /**
   * @brief Clear all acceptance filters (revert to accept-all).
   * @note NOT RT-safe: Call before init() or after deinit().
   */
  virtual void clearFilters() noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const CanStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  virtual void resetStats() noexcept = 0;

  /**
   * @brief Check if CAN controller is in bus-off state.
   * @return true if bus-off.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isBusOff() const noexcept = 0;

protected:
  ICan() = default;
  ICan(const ICan&) = delete;
  ICan& operator=(const ICan&) = delete;
  ICan(ICan&&) = default;
  ICan& operator=(ICan&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_ICAN_HPP
