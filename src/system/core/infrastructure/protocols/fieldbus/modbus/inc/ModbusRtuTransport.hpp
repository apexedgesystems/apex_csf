#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_RTU_TRANSPORT_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_RTU_TRANSPORT_HPP
/**
 * @file ModbusRtuTransport.hpp
 * @brief Modbus RTU transport over serial (UART).
 *
 * Implements the ModbusTransport interface for Modbus RTU protocol over
 * serial connections. Handles frame timing, CRC verification, and
 * inter-frame delays.
 *
 * Features:
 *  - Uses UartDevice interface (works with any UART implementation)
 *  - Automatic inter-frame delay calculation based on baud rate
 *  - CRC-16/MODBUS validation on receive
 *  - Statistics tracking
 *
 * RT-Safety:
 *  - I/O operations are RT-safe (bounded syscalls via UartDevice)
 *  - Inter-frame timing uses nanosleep (bounded, deterministic)
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusConfig.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTransport.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartDevice.hpp"

#include <cstdint>
#include <string>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

// Namespace alias for UART types
namespace uart = apex::protocols::serial::uart;

/* ----------------------------- ModbusRtuTransport ----------------------------- */

/**
 * @class ModbusRtuTransport
 * @brief Modbus RTU transport implementation over UART.
 *
 * Lifecycle:
 *  1. Construct with UartDevice pointer and RTU config
 *  2. Call open() to configure the UART
 *  3. Use sendRequest()/receiveResponse() for transactions
 *  4. Call close() to release (does NOT close the underlying UART)
 *
 * Ownership:
 *  - Does NOT own the UartDevice. Caller must ensure device outlives transport.
 *  - UART configuration is applied during open().
 *
 * Thread Safety:
 *  - NOT thread-safe. External synchronization required.
 */
class ModbusRtuTransport final : public ModbusTransport, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct RTU transport.
   * @param device Pointer to UART device (must outlive transport).
   * @param config RTU configuration.
   * @param baudRate Baud rate for inter-frame delay calculation.
   * @note NOT RT-safe: allocates internal description string.
   */
  ModbusRtuTransport(uart::UartDevice* device, const ModbusRtuConfig& config,
                     std::uint32_t baudRate);

  ~ModbusRtuTransport() override;

  ModbusRtuTransport(const ModbusRtuTransport&) = delete;
  ModbusRtuTransport& operator=(const ModbusRtuTransport&) = delete;
  ModbusRtuTransport(ModbusRtuTransport&&) = delete;
  ModbusRtuTransport& operator=(ModbusRtuTransport&&) = delete;

  /* ----------------------------- ModbusTransport Interface ----------------------------- */

  /** @note NOT RT-safe: performs I/O syscall. */
  [[nodiscard]] Status open() noexcept override;
  /** @note NOT RT-safe: performs I/O syscall. */
  [[nodiscard]] Status close() noexcept override;
  /** @note RT-safe: no allocation or I/O. */
  [[nodiscard]] bool isOpen() const noexcept override;

  /** @note RT-safe: bounded syscalls, no allocation. */
  [[nodiscard]] Status sendRequest(const FrameBuffer& frame, int timeoutMs) noexcept override;
  /** @note RT-safe: bounded syscalls, no allocation. */
  [[nodiscard]] Status receiveResponse(FrameBuffer& frame, int timeoutMs) noexcept override;
  /** @note NOT RT-safe: performs I/O syscall. */
  [[nodiscard]] Status flush() noexcept override;

  /** @note RT-safe: no allocation or I/O. */
  [[nodiscard]] ModbusStats stats() const noexcept override;
  /** @note RT-safe: no allocation or I/O. */
  void resetStats() noexcept override;

  /** @note RT-safe: no allocation or I/O. */
  [[nodiscard]] const char* description() const noexcept override;

  /* ----------------------------- RTU-Specific ----------------------------- */

  /**
   * @brief Get the inter-frame delay in microseconds.
   * @return Delay value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint32_t interFrameDelayUs() const noexcept { return interFrameDelayUs_; }

  /**
   * @brief Get the underlying UART device.
   * @return Pointer to UART device.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] uart::UartDevice* device() const noexcept { return device_; }

private:
  uart::UartDevice* device_;
  ModbusRtuConfig config_;
  std::uint32_t interFrameDelayUs_;
  bool isOpen_;
  ModbusStats stats_;
  std::string description_;

  /**
   * @brief Wait for inter-frame silence.
   * @note RT-safe: Uses nanosleep with bounded time.
   */
  void waitInterFrameDelay() noexcept;

  /**
   * @brief Read bytes with timeout, accumulating into buffer.
   * @param buf Destination buffer.
   * @param offset Current offset in buffer.
   * @param needed Bytes still needed.
   * @param timeoutMs Timeout in milliseconds.
   * @return Status code.
   */
  Status readWithTimeout(std::uint8_t* buf, std::size_t& offset, std::size_t needed,
                         int timeoutMs) noexcept;
};

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_RTU_TRANSPORT_HPP
