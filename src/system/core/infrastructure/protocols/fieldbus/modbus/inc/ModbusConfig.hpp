#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_CONFIG_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_CONFIG_HPP
/**
 * @file ModbusConfig.hpp
 * @brief Configuration structures for Modbus RTU and TCP transports.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTypes.hpp"

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- ModbusRtuConfig ----------------------------- */

/**
 * @struct ModbusRtuConfig
 * @brief Configuration for Modbus RTU (serial) transport.
 *
 * This configures the Modbus-specific parameters. The underlying UART
 * configuration (baud rate, parity, etc.) is handled by UartConfig.
 *
 * Note: Modbus RTU timing is sensitive to baud rate. The inter-frame
 * silence (3.5 character times) is calculated automatically based on
 * the configured baud rate.
 */
struct ModbusRtuConfig {
  /**
   * @brief Response timeout in milliseconds.
   *
   * Time to wait for a response after sending a request.
   * Modbus spec recommends 1000ms as a starting point for slow devices.
   * Fast devices may use 100-500ms.
   */
  std::uint32_t responseTimeoutMs{1000};

  /**
   * @brief Inter-frame delay in microseconds.
   *
   * Modbus RTU requires 3.5 character times of silence between frames.
   * Set to 0 to auto-calculate based on baud rate.
   * At 9600 baud: ~4ms, at 115200 baud: ~0.3ms.
   */
  std::uint32_t interFrameDelayUs{0};

  /**
   * @brief Maximum retries on timeout or CRC error.
   *
   * Set to 0 to disable retries. Each retry uses the same timeout.
   */
  std::uint8_t maxRetries{3};

  /**
   * @brief Enable turnaround delay after transmit.
   *
   * Some RS485 transceivers need time to switch from TX to RX mode.
   * This adds a small delay after sending before reading the response.
   * Set to 0 to disable.
   */
  std::uint32_t turnaroundDelayUs{0};
};

/* ----------------------------- ModbusTcpConfig ----------------------------- */

/**
 * @struct ModbusTcpConfig
 * @brief Configuration for Modbus TCP transport.
 */
struct ModbusTcpConfig {
  /**
   * @brief Response timeout in milliseconds.
   *
   * Time to wait for a response after sending a request.
   * TCP has reliable delivery, so timeouts can be shorter than RTU.
   */
  std::uint32_t responseTimeoutMs{500};

  /**
   * @brief Connection timeout in milliseconds.
   *
   * Time to wait when establishing the TCP connection.
   */
  std::uint32_t connectTimeoutMs{5000};

  /**
   * @brief Maximum retries on timeout.
   *
   * Set to 0 to disable retries. TCP handles packet retransmission,
   * so this is for application-level timeouts.
   */
  std::uint8_t maxRetries{2};

  /**
   * @brief Keep-alive interval in seconds.
   *
   * Set to 0 to disable TCP keep-alive.
   * Recommended: 30-60 seconds for idle connections.
   */
  std::uint32_t keepAliveIntervalSec{30};

  /**
   * @brief Starting transaction ID.
   *
   * Modbus TCP uses a 16-bit transaction ID for request/response matching.
   * This wraps around at 65535.
   */
  std::uint16_t initialTransactionId{1};
};

/* ----------------------------- MasterConfig ----------------------------- */

/**
 * @struct MasterConfig
 * @brief Common configuration for Modbus master operations.
 *
 * This applies to both RTU and TCP modes.
 */
struct MasterConfig {
  /**
   * @brief Default unit address for requests.
   *
   * Valid range: 1-247 for unicast, 0 for broadcast.
   * Can be overridden per-request.
   */
  std::uint8_t defaultUnitAddress{1};

  /**
   * @brief Validate response unit address.
   *
   * When true, the response unit address must match the request.
   * Some devices don't echo the unit address correctly.
   */
  bool validateUnitAddress{true};

  /**
   * @brief Validate response function code.
   *
   * When true, the response function code must match the request
   * (or be an exception response).
   */
  bool validateFunctionCode{true};
};

/* ----------------------------- toString Helpers ----------------------------- */

/**
 * @brief Calculate the inter-frame delay for a given baud rate.
 * @param baudRate UART baud rate.
 * @return Inter-frame delay in microseconds (3.5 character times).
 * @note RT-safe: Simple calculation.
 *
 * One character at 8N1 = 10 bits (start + 8 data + stop).
 * 3.5 characters = 35 bits.
 * Delay = (35 * 1,000,000) / baudRate microseconds.
 */
inline std::uint32_t calculateInterFrameDelay(std::uint32_t baudRate) noexcept {
  if (baudRate == 0) {
    return 0;
  }
  // 35 bits * 1,000,000 us/s / baudRate bits/s
  return (35 * 1000000) / baudRate;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_CONFIG_HPP
