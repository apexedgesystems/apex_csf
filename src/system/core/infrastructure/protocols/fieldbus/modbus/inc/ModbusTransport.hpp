#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_TRANSPORT_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_TRANSPORT_HPP
/**
 * @file ModbusTransport.hpp
 * @brief Abstract transport interface for Modbus communication.
 *
 * Defines the interface for sending Modbus requests and receiving responses.
 * Implementations handle transport-specific details (RTU framing, TCP/MBAP).
 *
 * Design:
 *  - Caller provides pre-allocated buffers (no allocation on I/O paths)
 *  - Timeout semantics: <0 = block, 0 = poll, >0 = bounded wait
 *  - Statistics tracking for monitoring and diagnostics
 *
 * RT-Safety:
 *  - sendRequest() / receiveResponse(): RT-safe (bounded syscalls, no allocation)
 *  - configure(): NOT RT-safe (may allocate, perform setup)
 *  - stats(): RT-safe (returns copy)
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusConfig.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStatus.hpp"

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- ModbusTransport ----------------------------- */

/**
 * @class ModbusTransport
 * @brief Abstract interface for Modbus transport layer.
 *
 * Implementations:
 *  - ModbusRtuTransport: Serial RTU with CRC-16
 *  - ModbusTcpTransport: TCP with MBAP header
 *
 * Lifecycle:
 *  1. Construct with transport-specific parameters (device path, host:port)
 *  2. Call configure() to open/connect
 *  3. Use sendRequest() / receiveResponse() for transactions
 *  4. Call close() or let destructor clean up
 *
 * Thread Safety:
 *  - NOT thread-safe. External synchronization required for concurrent access.
 *  - For multi-threaded use, use one transport per thread or add mutex.
 */
class ModbusTransport {
public:
  virtual ~ModbusTransport() = default;

  /* ----------------------------- Configuration ----------------------------- */

  /**
   * @brief Open/connect the transport.
   * @return Status code.
   * @note NOT RT-safe: May allocate, perform system calls.
   */
  [[nodiscard]] virtual Status open() noexcept = 0;

  /**
   * @brief Close the transport and release resources.
   * @return Status code.
   * @note NOT RT-safe: Releases system resources.
   */
  [[nodiscard]] virtual Status close() noexcept = 0;

  /**
   * @brief Check if transport is open and ready.
   * @return true if open and configured.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] virtual bool isOpen() const noexcept = 0;

  /* ----------------------------- I/O Operations ----------------------------- */

  /**
   * @brief Send a Modbus request frame.
   * @param frame Complete frame (with CRC for RTU, without MBAP for TCP).
   * @param timeoutMs Timeout in milliseconds (<0 = block, 0 = poll, >0 = wait).
   * @return Status code.
   * @note RT-safe: Bounded syscalls, no allocation.
   *
   * For RTU: Frame must include unit address, function code, data, and CRC.
   * For TCP: Transport adds MBAP header automatically.
   */
  [[nodiscard]] virtual Status sendRequest(const FrameBuffer& frame, int timeoutMs) noexcept = 0;

  /**
   * @brief Receive a Modbus response frame.
   * @param frame Output buffer for received frame.
   * @param timeoutMs Timeout in milliseconds.
   * @return Status code.
   * @note RT-safe: Bounded syscalls, no allocation.
   *
   * For RTU: Returns complete frame including CRC (caller should verify).
   * For TCP: Returns frame without MBAP header.
   *
   * Returns:
   *  - SUCCESS: Frame received and stored in buffer.
   *  - WOULD_BLOCK: No data available (nonblocking mode).
   *  - ERROR_TIMEOUT: Timeout expired before complete frame received.
   *  - ERROR_CRC: CRC validation failed (RTU only).
   *  - ERROR_FRAME: Malformed frame received.
   *  - ERROR_IO: Transport error.
   */
  [[nodiscard]] virtual Status receiveResponse(FrameBuffer& frame, int timeoutMs) noexcept = 0;

  /**
   * @brief Flush any pending input/output data.
   * @return Status code.
   * @note NOT RT-safe: May block briefly.
   */
  [[nodiscard]] virtual Status flush() noexcept = 0;

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get accumulated statistics.
   * @return Copy of statistics structure.
   * @note RT-safe: Returns copy, O(1).
   */
  [[nodiscard]] virtual ModbusStats stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe: O(1).
   */
  virtual void resetStats() noexcept = 0;

  /* ----------------------------- Info ----------------------------- */

  /**
   * @brief Get transport description (e.g., "RTU:/dev/ttyUSB0" or "TCP:192.168.1.10:502").
   * @return Static or stable string (valid for transport lifetime).
   * @note RT-safe: Returns stable pointer.
   */
  [[nodiscard]] virtual const char* description() const noexcept = 0;

protected:
  ModbusTransport() = default;
  ModbusTransport(const ModbusTransport&) = delete;
  ModbusTransport& operator=(const ModbusTransport&) = delete;
  ModbusTransport(ModbusTransport&&) = default;
  ModbusTransport& operator=(ModbusTransport&&) = default;
};

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_TRANSPORT_HPP
