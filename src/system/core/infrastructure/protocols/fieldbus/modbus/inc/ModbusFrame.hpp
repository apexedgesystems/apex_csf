#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_FRAME_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_FRAME_HPP
/**
 * @file ModbusFrame.hpp
 * @brief Zero-allocation Modbus frame building and parsing.
 *
 * Provides utilities for constructing and parsing Modbus PDUs (Protocol Data Units)
 * and ADUs (Application Data Units) without dynamic memory allocation.
 *
 * Frame structures:
 *  - RTU ADU: [Unit Address (1)] + [PDU] + [CRC-16 (2)]
 *  - TCP ADU: [MBAP Header (7)] + [PDU]
 *  - PDU: [Function Code (1)] + [Data (0-252)]
 *
 * RT-Safety:
 *  - All frame operations use caller-provided buffers.
 *  - No dynamic allocation on any path.
 *  - CRC calculation is O(n) in frame length.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusException.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStatus.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTypes.hpp"
#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- Frame Buffer ----------------------------- */

/**
 * @struct FrameBuffer
 * @brief Fixed-size buffer for Modbus frame construction.
 *
 * Provides a stack-allocated buffer suitable for any Modbus frame.
 * The buffer is sized for the maximum RTU or TCP ADU.
 *
 * RT-Safety: No allocation, fixed size.
 */
struct FrameBuffer {
  static constexpr std::size_t CAPACITY = Constants::RTU_MAX_FRAME_SIZE;

  std::uint8_t data[CAPACITY]{};
  std::size_t length{0};

  /**
   * @brief Reset buffer to empty state.
   * @note RT-safe: O(1).
   */
  void reset() noexcept { length = 0; }

  /**
   * @brief Get pointer to buffer data.
   * @return Pointer to buffer start.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint8_t* ptr() noexcept { return data; }
  [[nodiscard]] const std::uint8_t* ptr() const noexcept { return data; }

  /**
   * @brief Get remaining capacity.
   * @return Bytes available for writing.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t remaining() const noexcept { return CAPACITY - length; }

  /**
   * @brief Append a single byte.
   * @param b Byte to append.
   * @return true if successful, false if buffer full.
   * @note RT-safe: O(1).
   */
  bool append(std::uint8_t b) noexcept {
    if (length >= CAPACITY) {
      return false;
    }
    data[length++] = b;
    return true;
  }

  /**
   * @brief Append a 16-bit value in big-endian order.
   * @param value Value to append.
   * @return true if successful, false if buffer full.
   * @note RT-safe: O(1).
   */
  bool appendU16BE(std::uint16_t value) noexcept {
    if (length + 2 > CAPACITY) {
      return false;
    }
    data[length++] = static_cast<std::uint8_t>(value >> 8);
    data[length++] = static_cast<std::uint8_t>(value & 0xFF);
    return true;
  }

  /**
   * @brief Append a 16-bit value in little-endian order.
   * @param value Value to append.
   * @return true if successful, false if buffer full.
   * @note RT-safe: O(1).
   */
  bool appendU16LE(std::uint16_t value) noexcept {
    if (length + 2 > CAPACITY) {
      return false;
    }
    data[length++] = static_cast<std::uint8_t>(value & 0xFF);
    data[length++] = static_cast<std::uint8_t>(value >> 8);
    return true;
  }

  /**
   * @brief Append multiple bytes.
   * @param src Source data.
   * @param count Number of bytes to append.
   * @return true if successful, false if buffer full.
   * @note RT-safe: O(n) in count.
   */
  bool appendBytes(const std::uint8_t* src, std::size_t count) noexcept {
    if (src == nullptr || length + count > CAPACITY) {
      return false;
    }
    std::memcpy(data + length, src, count);
    length += count;
    return true;
  }
};

/* ----------------------------- CRC Helpers ----------------------------- */

/**
 * @brief Calculate CRC-16/MODBUS for a buffer.
 * @param data Buffer to calculate CRC over.
 * @param len Length of buffer.
 * @return CRC-16 value.
 * @note RT-safe: O(n) in buffer length, no allocation.
 */
inline std::uint16_t calculateCrc(const std::uint8_t* data, std::size_t len) noexcept {
  apex::checksums::crc::Crc16ModbusTable crc;
  std::uint16_t result = 0;
  crc.calculate(data, len, result);
  return result;
}

/**
 * @brief Verify CRC-16/MODBUS of a complete RTU frame.
 * @param frame Complete frame including CRC bytes.
 * @param len Total frame length (must be >= 4).
 * @return true if CRC is valid.
 * @note RT-safe: O(n) in frame length, no allocation.
 *
 * The CRC is stored in little-endian order at the end of the frame.
 */
inline bool verifyCrc(const std::uint8_t* frame, std::size_t len) noexcept {
  if (frame == nullptr || len < Constants::RTU_MIN_FRAME_SIZE) {
    return false;
  }
  const std::size_t DATA_LEN = len - Constants::CRC_SIZE;
  const std::uint16_t CALCULATED = calculateCrc(frame, DATA_LEN);
  const std::uint16_t RECEIVED = static_cast<std::uint16_t>(frame[DATA_LEN]) |
                                 (static_cast<std::uint16_t>(frame[DATA_LEN + 1]) << 8);
  return CALCULATED == RECEIVED;
}

/* ----------------------------- RTU Frame Building ----------------------------- */

/**
 * @brief Build a Read Coils (FC 0x01) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247).
 * @param startAddr Starting coil address.
 * @param quantity Number of coils to read (1-2000).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildReadCoilsRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                    std::uint16_t startAddr, std::uint16_t quantity) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (quantity == 0 || quantity > Constants::MAX_READ_COILS) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::READ_COILS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(quantity);

  // Append CRC in little-endian
  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Read Discrete Inputs (FC 0x02) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247).
 * @param startAddr Starting input address.
 * @param quantity Number of inputs to read (1-2000).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildReadDiscreteInputsRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                             std::uint16_t startAddr,
                                             std::uint16_t quantity) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (quantity == 0 || quantity > Constants::MAX_READ_COILS) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::READ_DISCRETE_INPUTS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(quantity);

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Read Holding Registers (FC 0x03) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247).
 * @param startAddr Starting register address.
 * @param quantity Number of registers to read (1-125).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildReadHoldingRegistersRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                               std::uint16_t startAddr,
                                               std::uint16_t quantity) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (quantity == 0 || quantity > Constants::MAX_READ_REGISTERS) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::READ_HOLDING_REGISTERS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(quantity);

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Read Input Registers (FC 0x04) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247).
 * @param startAddr Starting register address.
 * @param quantity Number of registers to read (1-125).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildReadInputRegistersRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                             std::uint16_t startAddr,
                                             std::uint16_t quantity) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (quantity == 0 || quantity > Constants::MAX_READ_REGISTERS) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::READ_INPUT_REGISTERS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(quantity);

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Write Single Coil (FC 0x05) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247, or 0 for broadcast).
 * @param coilAddr Coil address.
 * @param value true for ON (0xFF00), false for OFF (0x0000).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildWriteSingleCoilRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                          std::uint16_t coilAddr, bool value) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::WRITE_SINGLE_COIL));
  buf.appendU16BE(coilAddr);
  buf.appendU16BE(CoilValue::fromBool(value));

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Write Single Register (FC 0x06) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247, or 0 for broadcast).
 * @param regAddr Register address.
 * @param value Register value.
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 */
inline Status buildWriteSingleRegisterRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                              std::uint16_t regAddr, std::uint16_t value) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::WRITE_SINGLE_REGISTER));
  buf.appendU16BE(regAddr);
  buf.appendU16BE(value);

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Write Multiple Registers (FC 0x10) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247, or 0 for broadcast).
 * @param startAddr Starting register address.
 * @param values Array of register values.
 * @param count Number of registers to write (1-123).
 * @return Status code.
 * @note RT-safe: O(n) in count, no allocation.
 */
inline Status buildWriteMultipleRegistersRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                                 std::uint16_t startAddr,
                                                 const std::uint16_t* values,
                                                 std::uint16_t count) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (values == nullptr || count == 0 || count > Constants::MAX_WRITE_REGISTERS) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::WRITE_MULTIPLE_REGISTERS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(count);
  buf.append(static_cast<std::uint8_t>(count * 2)); // Byte count

  for (std::uint16_t i = 0; i < count; ++i) {
    buf.appendU16BE(values[i]);
  }

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/**
 * @brief Build a Write Multiple Coils (FC 0x0F) request frame.
 * @param buf Output buffer.
 * @param unitAddr Unit address (1-247, or 0 for broadcast).
 * @param startAddr Starting coil address.
 * @param quantity Number of coils to write (1-1968).
 * @param values Packed coil values (LSB-first per byte).
 * @return Status code.
 * @note RT-safe: O(n) in quantity, no allocation.
 */
inline Status buildWriteMultipleCoilsRequest(FrameBuffer& buf, std::uint8_t unitAddr,
                                             std::uint16_t startAddr, std::uint16_t quantity,
                                             const std::uint8_t* values) noexcept {
  if (!isValidUnitAddress(unitAddr) && !isBroadcastAddress(unitAddr)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (values == nullptr || quantity == 0 || quantity > Constants::MAX_WRITE_COILS) {
    return Status::ERROR_INVALID_ARG;
  }

  const std::uint8_t BYTE_COUNT = static_cast<std::uint8_t>((quantity + 7) / 8);

  buf.reset();
  buf.append(unitAddr);
  buf.append(static_cast<std::uint8_t>(FunctionCode::WRITE_MULTIPLE_COILS));
  buf.appendU16BE(startAddr);
  buf.appendU16BE(quantity);
  buf.append(BYTE_COUNT);

  for (std::uint8_t i = 0; i < BYTE_COUNT; ++i) {
    buf.append(values[i]);
  }

  const std::uint16_t CRC = calculateCrc(buf.data, buf.length);
  buf.appendU16LE(CRC);

  return Status::SUCCESS;
}

/* ----------------------------- Response Parsing ----------------------------- */

/**
 * @struct ParsedResponse
 * @brief Result of parsing a Modbus response frame.
 *
 * Contains parsed header fields and a pointer to the data payload.
 * The data pointer references the original frame buffer (no copy).
 */
struct ParsedResponse {
  std::uint8_t unitAddress{0};
  std::uint8_t functionCode{0};
  bool isException{false};
  ExceptionCode exceptionCode{ExceptionCode::NONE};

  const std::uint8_t* data{nullptr}; ///< Pointer to data portion (in original buffer).
  std::size_t dataLength{0};         ///< Length of data portion.
};

/**
 * @brief Parse an RTU response frame.
 * @param frame Complete RTU frame including CRC.
 * @param frameLen Length of frame.
 * @param result Output: parsed response fields.
 * @return Status code.
 * @note RT-safe: O(n) for CRC verification, no allocation.
 *
 * Validates CRC, extracts header fields, and provides pointer to data.
 * Does NOT validate that the response matches a specific request.
 */
inline Status parseRtuResponse(const std::uint8_t* frame, std::size_t frameLen,
                               ParsedResponse& result) noexcept {
  if (frame == nullptr) {
    return Status::ERROR_INVALID_ARG;
  }
  if (frameLen < Constants::RTU_MIN_FRAME_SIZE) {
    return Status::ERROR_FRAME;
  }

  // Verify CRC
  if (!verifyCrc(frame, frameLen)) {
    return Status::ERROR_CRC;
  }

  result.unitAddress = frame[0];
  result.functionCode = frame[1];
  result.isException = isExceptionResponse(result.functionCode);

  if (result.isException) {
    // Exception response: [Unit] [FC|0x80] [ExceptionCode] [CRC]
    if (frameLen < 5) {
      return Status::ERROR_FRAME;
    }
    result.exceptionCode = static_cast<ExceptionCode>(frame[2]);
    result.data = nullptr;
    result.dataLength = 0;
    return Status::ERROR_EXCEPTION;
  }

  // Normal response: [Unit] [FC] [Data...] [CRC]
  result.exceptionCode = ExceptionCode::NONE;
  result.data = frame + 2;
  result.dataLength = frameLen - Constants::RTU_MIN_FRAME_SIZE; // Subtract unit + fc + crc

  return Status::SUCCESS;
}

/**
 * @brief Extract register values from a read registers response.
 * @param response Parsed response from parseRtuResponse().
 * @param values Output array for register values.
 * @param maxCount Maximum number of values to extract.
 * @param extractedCount Output: actual number of values extracted.
 * @return Status code.
 * @note RT-safe: O(n) in register count, no allocation.
 *
 * Expects response.data to point to: [ByteCount] [Reg0 Hi] [Reg0 Lo] ...
 */
inline Status extractRegisters(const ParsedResponse& response, std::uint16_t* values,
                               std::size_t maxCount, std::size_t& extractedCount) noexcept {
  extractedCount = 0;

  if (values == nullptr || response.data == nullptr || response.dataLength < 1) {
    return Status::ERROR_INVALID_ARG;
  }

  const std::uint8_t BYTE_COUNT = response.data[0];
  if (BYTE_COUNT % 2 != 0 || response.dataLength < static_cast<std::size_t>(BYTE_COUNT + 1)) {
    return Status::ERROR_FRAME;
  }

  const std::size_t REG_COUNT = BYTE_COUNT / 2;
  const std::size_t TO_EXTRACT = (REG_COUNT < maxCount) ? REG_COUNT : maxCount;

  for (std::size_t i = 0; i < TO_EXTRACT; ++i) {
    const std::size_t OFFSET = 1 + (i * 2);
    values[i] = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(response.data[OFFSET]) << 8) | response.data[OFFSET + 1]);
  }

  extractedCount = TO_EXTRACT;
  return Status::SUCCESS;
}

/**
 * @brief Extract coil/discrete input values from a read coils response.
 * @param response Parsed response from parseRtuResponse().
 * @param values Output array for coil values (one bool per coil).
 * @param maxCount Maximum number of values to extract.
 * @param extractedCount Output: actual number of values extracted.
 * @return Status code.
 * @note RT-safe: O(n) in coil count, no allocation.
 *
 * Expects response.data to point to: [ByteCount] [CoilByte0] [CoilByte1] ...
 * Coils are packed LSB-first in each byte.
 */
inline Status extractCoils(const ParsedResponse& response, bool* values, std::size_t maxCount,
                           std::size_t& extractedCount) noexcept {
  extractedCount = 0;

  if (values == nullptr || response.data == nullptr || response.dataLength < 1) {
    return Status::ERROR_INVALID_ARG;
  }

  const std::uint8_t BYTE_COUNT = response.data[0];
  if (response.dataLength < static_cast<std::size_t>(BYTE_COUNT + 1)) {
    return Status::ERROR_FRAME;
  }

  // Maximum possible coils based on byte count
  const std::size_t MAX_COILS = static_cast<std::size_t>(BYTE_COUNT) * 8;
  const std::size_t TO_EXTRACT = (MAX_COILS < maxCount) ? MAX_COILS : maxCount;

  for (std::size_t i = 0; i < TO_EXTRACT; ++i) {
    const std::size_t BYTE_IDX = 1 + (i / 8);
    const std::size_t BIT_IDX = i % 8;
    values[i] = (response.data[BYTE_IDX] & (1 << BIT_IDX)) != 0;
  }

  extractedCount = TO_EXTRACT;
  return Status::SUCCESS;
}

/* ----------------------------- Convenience Extraction ----------------------------- */

/**
 * @brief Extract register values directly from response frame.
 * @param frame Response frame data (including unit address and function code).
 * @param frameLen Length of frame (without CRC for TCP, with CRC for RTU).
 * @param values Output array for register values.
 * @param maxCount Maximum number of values to extract.
 * @return Number of registers extracted, or 0 on error.
 * @note RT-safe: O(n) in register count, no allocation.
 *
 * Expects frame format: [UnitAddr] [FC] [ByteCount] [Data...]
 * For RTU frames, frameLen should exclude the CRC bytes.
 */
inline std::size_t extractRegistersFromResponse(const std::uint8_t* frame, std::size_t frameLen,
                                                std::uint16_t* values,
                                                std::size_t maxCount) noexcept {
  if (frame == nullptr || values == nullptr || frameLen < 3) {
    return 0;
  }

  const std::uint8_t BYTE_COUNT = frame[2];
  if (BYTE_COUNT % 2 != 0 || frameLen < static_cast<std::size_t>(3 + BYTE_COUNT)) {
    return 0;
  }

  const std::size_t REG_COUNT = BYTE_COUNT / 2;
  const std::size_t TO_EXTRACT = (REG_COUNT < maxCount) ? REG_COUNT : maxCount;

  for (std::size_t i = 0; i < TO_EXTRACT; ++i) {
    const std::size_t OFFSET = 3 + (i * 2);
    values[i] = static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[OFFSET]) << 8) |
                                           frame[OFFSET + 1]);
  }

  return TO_EXTRACT;
}

/**
 * @brief Extract coil values directly from response frame as packed bytes.
 * @param frame Response frame data (including unit address and function code).
 * @param frameLen Length of frame.
 * @param values Output array for packed coil bytes.
 * @param maxBytes Maximum number of bytes to extract.
 * @return Number of bytes extracted, or 0 on error.
 * @note RT-safe: O(n) in byte count, no allocation.
 *
 * Expects frame format: [UnitAddr] [FC] [ByteCount] [CoilBytes...]
 */
inline std::size_t extractCoilsFromResponse(const std::uint8_t* frame, std::size_t frameLen,
                                            std::uint8_t* values, std::size_t maxBytes) noexcept {
  if (frame == nullptr || values == nullptr || frameLen < 3) {
    return 0;
  }

  const std::uint8_t BYTE_COUNT = frame[2];
  if (frameLen < static_cast<std::size_t>(3 + BYTE_COUNT)) {
    return 0;
  }

  const std::size_t TO_EXTRACT = (BYTE_COUNT < maxBytes) ? BYTE_COUNT : maxBytes;

  for (std::size_t i = 0; i < TO_EXTRACT; ++i) {
    values[i] = frame[3 + i];
  }

  return TO_EXTRACT;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_FRAME_HPP
