#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_EXCEPTION_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_EXCEPTION_HPP
/**
 * @file ModbusException.hpp
 * @brief Modbus exception codes as defined by the Modbus specification.
 *
 * When a Modbus slave cannot process a request, it returns an exception response
 * with the function code's high bit set (0x80 | functionCode) followed by an
 * exception code. These codes indicate the reason for the failure.
 *
 * Reference: Modbus Application Protocol Specification V1.1b3
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- ExceptionCode ----------------------------- */

/**
 * @enum ExceptionCode
 * @brief Standard Modbus exception codes (1-12).
 *
 * Exception codes 0x05-0x08 are defined but commonly unused.
 * Codes 0x0A-0x0B are gateway-specific.
 */
enum class ExceptionCode : std::uint8_t {
  NONE = 0x00, ///< No exception (internal use only, not a valid Modbus exception).

  // Standard exception codes (Modbus spec):
  ILLEGAL_FUNCTION = 0x01,     ///< Function code not supported by slave.
  ILLEGAL_DATA_ADDRESS = 0x02, ///< Data address not allowed (out of range).
  ILLEGAL_DATA_VALUE = 0x03,   ///< Value in request data field is invalid.
  SLAVE_DEVICE_FAILURE = 0x04, ///< Unrecoverable error in slave device.
  ACKNOWLEDGE = 0x05,          ///< Request accepted, processing takes time.
  SLAVE_DEVICE_BUSY = 0x06,    ///< Slave is busy, retry later.
  NEGATIVE_ACKNOWLEDGE = 0x07, ///< Slave cannot perform requested action.
  MEMORY_PARITY_ERROR = 0x08,  ///< Parity error in extended memory.

  // Gateway exception codes:
  GATEWAY_PATH_UNAVAILABLE = 0x0A, ///< Gateway path to target not available.
  GATEWAY_TARGET_FAILED = 0x0B     ///< Target device failed to respond.
};

/**
 * @brief Human-readable string for ExceptionCode (cold path, no allocation).
 * @param code Exception code value.
 * @return Static string literal describing the exception.
 * @note RT-safe: Returns pointer to static string.
 */
inline const char* toString(ExceptionCode code) noexcept {
  switch (code) {
  case ExceptionCode::NONE:
    return "NONE";
  case ExceptionCode::ILLEGAL_FUNCTION:
    return "ILLEGAL_FUNCTION";
  case ExceptionCode::ILLEGAL_DATA_ADDRESS:
    return "ILLEGAL_DATA_ADDRESS";
  case ExceptionCode::ILLEGAL_DATA_VALUE:
    return "ILLEGAL_DATA_VALUE";
  case ExceptionCode::SLAVE_DEVICE_FAILURE:
    return "SLAVE_DEVICE_FAILURE";
  case ExceptionCode::ACKNOWLEDGE:
    return "ACKNOWLEDGE";
  case ExceptionCode::SLAVE_DEVICE_BUSY:
    return "SLAVE_DEVICE_BUSY";
  case ExceptionCode::NEGATIVE_ACKNOWLEDGE:
    return "NEGATIVE_ACKNOWLEDGE";
  case ExceptionCode::MEMORY_PARITY_ERROR:
    return "MEMORY_PARITY_ERROR";
  case ExceptionCode::GATEWAY_PATH_UNAVAILABLE:
    return "GATEWAY_PATH_UNAVAILABLE";
  case ExceptionCode::GATEWAY_TARGET_FAILED:
    return "GATEWAY_TARGET_FAILED";
  }
  return "UNKNOWN";
}

/**
 * @brief Human-readable description for ExceptionCode (cold path, no allocation).
 * @param code Exception code value.
 * @return Static string literal with a brief description.
 * @note RT-safe: Returns pointer to static string.
 */
inline const char* toDescription(ExceptionCode code) noexcept {
  switch (code) {
  case ExceptionCode::NONE:
    return "No exception";
  case ExceptionCode::ILLEGAL_FUNCTION:
    return "Function code not supported by slave";
  case ExceptionCode::ILLEGAL_DATA_ADDRESS:
    return "Data address not allowed or out of range";
  case ExceptionCode::ILLEGAL_DATA_VALUE:
    return "Value in request data field is invalid";
  case ExceptionCode::SLAVE_DEVICE_FAILURE:
    return "Unrecoverable error in slave device";
  case ExceptionCode::ACKNOWLEDGE:
    return "Request accepted, processing takes time";
  case ExceptionCode::SLAVE_DEVICE_BUSY:
    return "Slave is busy processing another request";
  case ExceptionCode::NEGATIVE_ACKNOWLEDGE:
    return "Slave cannot perform requested action";
  case ExceptionCode::MEMORY_PARITY_ERROR:
    return "Parity error detected in extended memory";
  case ExceptionCode::GATEWAY_PATH_UNAVAILABLE:
    return "Gateway path to target device not available";
  case ExceptionCode::GATEWAY_TARGET_FAILED:
    return "Target device failed to respond via gateway";
  }
  return "Unknown exception code";
}

/**
 * @brief Check if a raw byte is a valid Modbus exception code.
 * @param code Raw exception code byte.
 * @return true if the code is a standard Modbus exception (0x01-0x0B).
 * @note RT-safe: Simple range check.
 */
inline bool isValidExceptionCode(std::uint8_t code) noexcept {
  return (code >= 0x01 && code <= 0x08) || (code >= 0x0A && code <= 0x0B);
}

/**
 * @brief Check if a function code indicates an exception response.
 * @param functionCode The function code byte from a Modbus response.
 * @return true if the high bit is set (0x80), indicating an exception.
 * @note RT-safe: Simple bit check.
 *
 * In Modbus, an exception response has function code = 0x80 | originalFunctionCode.
 */
inline bool isExceptionResponse(std::uint8_t functionCode) noexcept {
  return (functionCode & 0x80) != 0;
}

/**
 * @brief Extract the original function code from an exception response.
 * @param exceptionFunctionCode The function code from an exception response.
 * @return The original function code (with high bit cleared).
 * @note RT-safe: Simple bit operation.
 */
inline std::uint8_t extractOriginalFunctionCode(std::uint8_t exceptionFunctionCode) noexcept {
  return exceptionFunctionCode & 0x7F;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_EXCEPTION_HPP
