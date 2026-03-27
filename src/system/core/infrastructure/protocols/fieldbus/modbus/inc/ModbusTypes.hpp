#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_TYPES_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_TYPES_HPP
/**
 * @file ModbusTypes.hpp
 * @brief Common Modbus types, function codes, and protocol constants.
 *
 * Reference: Modbus Application Protocol Specification V1.1b3
 */

#include <cstddef>
#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- Protocol Constants ----------------------------- */

/**
 * @brief Modbus protocol constants.
 */
struct Constants {
  // Unit address range
  static constexpr std::uint8_t UNIT_ADDRESS_MIN = 1;       ///< Minimum valid unit address.
  static constexpr std::uint8_t UNIT_ADDRESS_MAX = 247;     ///< Maximum valid unit address.
  static constexpr std::uint8_t UNIT_ADDRESS_BROADCAST = 0; ///< Broadcast address.

  // Register limits
  static constexpr std::uint16_t MAX_READ_COILS = 2000;     ///< Max coils per read (FC01/02).
  static constexpr std::uint16_t MAX_READ_REGISTERS = 125;  ///< Max registers per read (FC03/04).
  static constexpr std::uint16_t MAX_WRITE_COILS = 1968;    ///< Max coils per write (FC15).
  static constexpr std::uint16_t MAX_WRITE_REGISTERS = 123; ///< Max registers per write (FC16).

  // Frame size limits
  static constexpr std::size_t RTU_MIN_FRAME_SIZE = 4;   ///< Unit + FC + CRC (1+1+2).
  static constexpr std::size_t RTU_MAX_FRAME_SIZE = 256; ///< Maximum RTU frame size.
  static constexpr std::size_t TCP_MIN_FRAME_SIZE = 8;   ///< MBAP header + unit + FC.
  static constexpr std::size_t TCP_MAX_FRAME_SIZE = 260; ///< Maximum TCP ADU size.
  static constexpr std::size_t PDU_MAX_SIZE = 253;       ///< Maximum PDU size (FC + data).

  // CRC size
  static constexpr std::size_t CRC_SIZE = 2; ///< CRC-16 is 2 bytes.

  // MBAP header (TCP)
  static constexpr std::size_t MBAP_HEADER_SIZE =
      7; ///< Transaction ID (2) + Protocol ID (2) + Length (2) + Unit ID (1).
  static constexpr std::uint16_t MBAP_PROTOCOL_ID = 0; ///< Modbus protocol identifier (always 0).

  // RTU timing (3.5 character times silence between frames)
  // At 9600 baud: ~4ms, at 115200 baud: ~0.3ms
  static constexpr std::uint32_t RTU_INTER_FRAME_DELAY_US_9600 = 4000;
  static constexpr std::uint32_t RTU_INTER_FRAME_DELAY_US_115200 = 300;
};

/* ----------------------------- FunctionCode ----------------------------- */

/**
 * @enum FunctionCode
 * @brief Standard Modbus function codes.
 *
 * Organized by category:
 *  - Bit access (coils and discrete inputs): 0x01-0x05, 0x0F
 *  - Word access (holding and input registers): 0x03-0x04, 0x06, 0x10
 *  - Diagnostics: 0x07-0x08
 *  - File record access: 0x14-0x15 (not commonly used)
 *  - Device identification: 0x2B
 */
enum class FunctionCode : std::uint8_t {
  // Bit access (coils and discrete inputs):
  READ_COILS = 0x01,           ///< Read 1-2000 coils (read-write bits).
  READ_DISCRETE_INPUTS = 0x02, ///< Read 1-2000 discrete inputs (read-only bits).
  WRITE_SINGLE_COIL = 0x05,    ///< Write a single coil (0x0000 or 0xFF00).
  WRITE_MULTIPLE_COILS = 0x0F, ///< Write 1-1968 coils.

  // Word access (16-bit registers):
  READ_HOLDING_REGISTERS = 0x03,   ///< Read 1-125 holding registers (read-write).
  READ_INPUT_REGISTERS = 0x04,     ///< Read 1-125 input registers (read-only).
  WRITE_SINGLE_REGISTER = 0x06,    ///< Write a single holding register.
  WRITE_MULTIPLE_REGISTERS = 0x10, ///< Write 1-123 holding registers.

  // Read/write multiple registers (combined operation):
  READ_WRITE_MULTIPLE_REGISTERS = 0x17, ///< Read and write in single transaction.

  // Diagnostics:
  READ_EXCEPTION_STATUS = 0x07, ///< Read 8-bit exception status.
  DIAGNOSTICS = 0x08,           ///< Various diagnostic sub-functions.

  // FIFO queue:
  READ_FIFO_QUEUE = 0x18, ///< Read FIFO queue contents.

  // Device identification:
  READ_DEVICE_IDENTIFICATION = 0x2B ///< MEI (Modbus Encapsulated Interface).
};

/**
 * @brief Human-readable string for FunctionCode (cold path, no allocation).
 * @param fc Function code value.
 * @return Static string literal.
 * @note RT-safe: Returns pointer to static string.
 */
inline const char* toString(FunctionCode fc) noexcept {
  switch (fc) {
  case FunctionCode::READ_COILS:
    return "READ_COILS";
  case FunctionCode::READ_DISCRETE_INPUTS:
    return "READ_DISCRETE_INPUTS";
  case FunctionCode::WRITE_SINGLE_COIL:
    return "WRITE_SINGLE_COIL";
  case FunctionCode::WRITE_MULTIPLE_COILS:
    return "WRITE_MULTIPLE_COILS";
  case FunctionCode::READ_HOLDING_REGISTERS:
    return "READ_HOLDING_REGISTERS";
  case FunctionCode::READ_INPUT_REGISTERS:
    return "READ_INPUT_REGISTERS";
  case FunctionCode::WRITE_SINGLE_REGISTER:
    return "WRITE_SINGLE_REGISTER";
  case FunctionCode::WRITE_MULTIPLE_REGISTERS:
    return "WRITE_MULTIPLE_REGISTERS";
  case FunctionCode::READ_WRITE_MULTIPLE_REGISTERS:
    return "READ_WRITE_MULTIPLE_REGISTERS";
  case FunctionCode::READ_EXCEPTION_STATUS:
    return "READ_EXCEPTION_STATUS";
  case FunctionCode::DIAGNOSTICS:
    return "DIAGNOSTICS";
  case FunctionCode::READ_FIFO_QUEUE:
    return "READ_FIFO_QUEUE";
  case FunctionCode::READ_DEVICE_IDENTIFICATION:
    return "READ_DEVICE_IDENTIFICATION";
  }
  return "UNKNOWN";
}

/**
 * @brief Check if a function code is a read operation.
 * @param fc Function code value.
 * @return true if the function reads data without writing.
 * @note RT-safe: Simple comparison.
 */
inline bool isReadFunction(FunctionCode fc) noexcept {
  switch (fc) {
  case FunctionCode::READ_COILS:
  case FunctionCode::READ_DISCRETE_INPUTS:
  case FunctionCode::READ_HOLDING_REGISTERS:
  case FunctionCode::READ_INPUT_REGISTERS:
  case FunctionCode::READ_EXCEPTION_STATUS:
  case FunctionCode::READ_FIFO_QUEUE:
  case FunctionCode::READ_DEVICE_IDENTIFICATION:
    return true;
  default:
    return false;
  }
}

/**
 * @brief Check if a function code is a write operation.
 * @param fc Function code value.
 * @return true if the function writes data.
 * @note RT-safe: Simple comparison.
 */
inline bool isWriteFunction(FunctionCode fc) noexcept {
  switch (fc) {
  case FunctionCode::WRITE_SINGLE_COIL:
  case FunctionCode::WRITE_MULTIPLE_COILS:
  case FunctionCode::WRITE_SINGLE_REGISTER:
  case FunctionCode::WRITE_MULTIPLE_REGISTERS:
  case FunctionCode::READ_WRITE_MULTIPLE_REGISTERS:
    return true;
  default:
    return false;
  }
}

/* ----------------------------- CoilValue ----------------------------- */

/**
 * @brief Standard coil values for WRITE_SINGLE_COIL (FC05).
 *
 * In Modbus, a coil is turned ON with 0xFF00 and OFF with 0x0000.
 * Any other value is invalid.
 */
struct CoilValue {
  static constexpr std::uint16_t ON = 0xFF00;
  static constexpr std::uint16_t OFF = 0x0000;

  /**
   * @brief Check if a value is a valid coil value.
   * @param value The value to check.
   * @return true if value is ON (0xFF00) or OFF (0x0000).
   * @note RT-safe: Simple comparison.
   */
  static constexpr bool isValid(std::uint16_t value) noexcept {
    return value == ON || value == OFF;
  }

  /**
   * @brief Convert a boolean to Modbus coil value.
   * @param state true for ON, false for OFF.
   * @return 0xFF00 for true, 0x0000 for false.
   * @note RT-safe: Simple ternary.
   */
  static constexpr std::uint16_t fromBool(bool state) noexcept { return state ? ON : OFF; }

  /**
   * @brief Convert a Modbus coil value to boolean.
   * @param value Modbus coil value (0xFF00 or 0x0000).
   * @return true if ON, false otherwise.
   * @note RT-safe: Simple comparison.
   */
  static constexpr bool toBool(std::uint16_t value) noexcept { return value == ON; }
};

/* ----------------------------- Address Validation ----------------------------- */

/**
 * @brief Check if a unit address is valid for unicast communication.
 * @param address Unit address to validate.
 * @return true if address is in range [1, 247].
 * @note RT-safe: Simple range check.
 */
inline bool isValidUnitAddress(std::uint8_t address) noexcept {
  return address >= Constants::UNIT_ADDRESS_MIN && address <= Constants::UNIT_ADDRESS_MAX;
}

/**
 * @brief Check if a unit address is the broadcast address.
 * @param address Unit address to check.
 * @return true if address is 0.
 * @note RT-safe: Simple comparison.
 */
inline bool isBroadcastAddress(std::uint8_t address) noexcept {
  return address == Constants::UNIT_ADDRESS_BROADCAST;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_TYPES_HPP
