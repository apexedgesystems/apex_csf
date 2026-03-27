#ifndef APEX_PROTOCOLS_FIELDBUS_LIN_FRAME_HPP
#define APEX_PROTOCOLS_FIELDBUS_LIN_FRAME_HPP
/**
 * @file LinFrame.hpp
 * @brief Zero-allocation LIN frame building and parsing.
 *
 * Provides utilities for constructing and parsing LIN (Local Interconnect
 * Network) frames without dynamic memory allocation.
 *
 * LIN Frame Structure:
 *  - Break field: dominant for >= 13 bit times (generated via UART break)
 *  - Sync field: 0x55 (used for baud rate synchronization)
 *  - Protected Identifier (PID): 6-bit ID + 2 parity bits
 *  - Data: 0-8 bytes
 *  - Checksum: Classic (data only) or Enhanced (PID + data)
 *
 * RT-Safety:
 *  - All frame operations use caller-provided buffers.
 *  - No dynamic allocation on any path.
 *  - Checksum calculation is O(n) in data length.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

/* ----------------------------- Constants ----------------------------- */

/**
 * @struct Constants
 * @brief LIN protocol constants.
 */
struct Constants {
  static constexpr std::uint8_t SYNC_BYTE = 0x55;         ///< Sync field value.
  static constexpr std::size_t MAX_DATA_LENGTH = 8;       ///< Maximum data bytes per frame.
  static constexpr std::size_t MIN_FRAME_SIZE = 2;        ///< Sync + PID minimum.
  static constexpr std::size_t MAX_FRAME_SIZE = 11;       ///< Sync + PID + 8 data + checksum.
  static constexpr std::uint8_t ID_MASK = 0x3F;           ///< 6-bit ID mask.
  static constexpr std::uint8_t PARITY_MASK = 0xC0;       ///< 2-bit parity mask.
  static constexpr std::uint8_t MASTER_REQUEST_ID = 0x3C; ///< Master request frame ID.
  static constexpr std::uint8_t SLAVE_RESPONSE_ID = 0x3D; ///< Slave response frame ID.
};

/* ----------------------------- Checksum Type ----------------------------- */

/**
 * @enum ChecksumType
 * @brief LIN checksum calculation method.
 */
enum class ChecksumType : std::uint8_t {
  CLASSIC, ///< Checksum over data bytes only (LIN 1.x).
  ENHANCED ///< Checksum over PID + data bytes (LIN 2.x).
};

/* ----------------------------- Frame ID Helpers ----------------------------- */

/**
 * @brief Check if frame ID is valid (0-63).
 * @param id Frame ID (6-bit).
 * @return true if valid.
 * @note RT-safe: O(1).
 */
inline constexpr bool isValidFrameId(std::uint8_t id) noexcept { return id <= Constants::ID_MASK; }

/**
 * @brief Calculate P0 parity bit (ID0 XOR ID1 XOR ID2 XOR ID4).
 * @param id 6-bit frame ID.
 * @return P0 bit value (0 or 1).
 * @note RT-safe: O(1).
 */
inline constexpr std::uint8_t calculateP0(std::uint8_t id) noexcept {
  const std::uint8_t ID = id & Constants::ID_MASK;
  return ((ID >> 0) ^ (ID >> 1) ^ (ID >> 2) ^ (ID >> 4)) & 0x01;
}

/**
 * @brief Calculate P1 parity bit (NOT(ID1 XOR ID3 XOR ID4 XOR ID5)).
 * @param id 6-bit frame ID.
 * @return P1 bit value (0 or 1).
 * @note RT-safe: O(1).
 */
inline constexpr std::uint8_t calculateP1(std::uint8_t id) noexcept {
  const std::uint8_t ID = id & Constants::ID_MASK;
  return (~((ID >> 1) ^ (ID >> 3) ^ (ID >> 4) ^ (ID >> 5))) & 0x01;
}

/**
 * @brief Calculate Protected Identifier from 6-bit frame ID.
 * @param id 6-bit frame ID (0-63).
 * @return Protected ID with parity bits (8-bit).
 * @note RT-safe: O(1).
 */
inline constexpr std::uint8_t calculatePid(std::uint8_t id) noexcept {
  const std::uint8_t ID = id & Constants::ID_MASK;
  const std::uint8_t P0 = calculateP0(ID);
  const std::uint8_t P1 = calculateP1(ID);
  return ID | (P0 << 6) | (P1 << 7);
}

/**
 * @brief Extract 6-bit frame ID from Protected Identifier.
 * @param pid Protected ID (8-bit).
 * @return 6-bit frame ID.
 * @note RT-safe: O(1).
 */
inline constexpr std::uint8_t extractFrameId(std::uint8_t pid) noexcept {
  return pid & Constants::ID_MASK;
}

/**
 * @brief Verify parity bits in Protected Identifier.
 * @param pid Protected ID (8-bit).
 * @return true if parity is correct.
 * @note RT-safe: O(1).
 */
inline constexpr bool verifyPidParity(std::uint8_t pid) noexcept {
  const std::uint8_t ID = extractFrameId(pid);
  const std::uint8_t EXPECTED_PID = calculatePid(ID);
  return pid == EXPECTED_PID;
}

/**
 * @brief Determine data length from frame ID.
 * @param id 6-bit frame ID.
 * @return Expected data length (2, 4, or 8 bytes based on LIN 2.x spec).
 * @note RT-safe: O(1).
 *
 * LIN 2.x data length encoding:
 *  - ID 0-31: 2 bytes (diagnostic or signal frames)
 *  - ID 32-47: 4 bytes
 *  - ID 48-63: 8 bytes
 *
 * Note: Diagnostic frames (0x3C, 0x3D) always use 8 bytes regardless of this.
 */
inline constexpr std::size_t dataLengthFromId(std::uint8_t id) noexcept {
  const std::uint8_t ID = id & Constants::ID_MASK;
  if (ID >= 48) {
    return 8;
  }
  if (ID >= 32) {
    return 4;
  }
  return 2;
}

/**
 * @brief Check if frame ID is a diagnostic frame.
 * @param id 6-bit frame ID.
 * @return true if diagnostic frame (0x3C or 0x3D).
 * @note RT-safe: O(1).
 */
inline constexpr bool isDiagnosticFrame(std::uint8_t id) noexcept {
  const std::uint8_t ID = id & Constants::ID_MASK;
  return ID == Constants::MASTER_REQUEST_ID || ID == Constants::SLAVE_RESPONSE_ID;
}

/* ----------------------------- Checksum Calculation ----------------------------- */

/**
 * @brief Calculate LIN checksum.
 * @param data Data bytes.
 * @param len Number of data bytes.
 * @param pid Protected ID (only used for ENHANCED checksum).
 * @param type Checksum type (CLASSIC or ENHANCED).
 * @return Checksum byte (inverted sum with carry).
 * @note RT-safe: O(n) in data length, no allocation.
 *
 * Checksum algorithm:
 *  1. Sum all bytes (including PID for enhanced)
 *  2. Add carry bits back into sum
 *  3. Invert the result
 */
inline std::uint8_t calculateChecksum(const std::uint8_t* data, std::size_t len, std::uint8_t pid,
                                      ChecksumType type) noexcept {
  std::uint16_t sum = 0;

  // Include PID in enhanced checksum (but not for diagnostic frames in LIN 2.x)
  if (type == ChecksumType::ENHANCED) {
    sum = pid;
  }

  // Sum all data bytes with carry propagation
  for (std::size_t i = 0; i < len; ++i) {
    sum += data[i];
    if (sum > 0xFF) {
      sum = (sum & 0xFF) + 1; // Add carry
    }
  }

  // Final carry check and invert
  if (sum > 0xFF) {
    sum = (sum & 0xFF) + 1;
  }
  return static_cast<std::uint8_t>(~sum);
}

/**
 * @brief Verify LIN checksum.
 * @param data Data bytes.
 * @param len Number of data bytes (excluding checksum).
 * @param checksum Received checksum byte.
 * @param pid Protected ID.
 * @param type Checksum type.
 * @return true if checksum is valid.
 * @note RT-safe: O(n) in data length, no allocation.
 */
inline bool verifyChecksum(const std::uint8_t* data, std::size_t len, std::uint8_t checksum,
                           std::uint8_t pid, ChecksumType type) noexcept {
  return calculateChecksum(data, len, pid, type) == checksum;
}

/* ----------------------------- Frame Buffer ----------------------------- */

/**
 * @struct FrameBuffer
 * @brief Fixed-size buffer for LIN frame construction.
 *
 * Provides a stack-allocated buffer suitable for any LIN frame.
 * Note: Break field is transmitted separately via UART break mechanism.
 *
 * RT-Safety: No allocation, fixed size.
 */
struct FrameBuffer {
  static constexpr std::size_t CAPACITY = Constants::MAX_FRAME_SIZE;

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

/* ----------------------------- Frame Building ----------------------------- */

/**
 * @brief Build a LIN header (sync + protected ID).
 * @param buf Output buffer.
 * @param frameId 6-bit frame ID (0-63).
 * @return Status code.
 * @note RT-safe: O(1), no allocation.
 *
 * The break field must be transmitted separately before this header.
 */
inline Status buildHeader(FrameBuffer& buf, std::uint8_t frameId) noexcept {
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.append(Constants::SYNC_BYTE);
  buf.append(calculatePid(frameId));

  return Status::SUCCESS;
}

/**
 * @brief Build a complete LIN response (data + checksum).
 * @param buf Output buffer.
 * @param pid Protected ID (for enhanced checksum calculation).
 * @param frameData Data bytes.
 * @param dataLen Number of data bytes (1-8).
 * @param checksumType Checksum calculation method.
 * @return Status code.
 * @note RT-safe: O(n) in data length, no allocation.
 */
inline Status buildResponse(FrameBuffer& buf, std::uint8_t pid, const std::uint8_t* frameData,
                            std::size_t dataLen, ChecksumType checksumType) noexcept {
  if (frameData == nullptr || dataLen == 0 || dataLen > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  buf.appendBytes(frameData, dataLen);
  buf.append(calculateChecksum(frameData, dataLen, pid, checksumType));

  return Status::SUCCESS;
}

/**
 * @brief Build a complete LIN frame (sync + PID + data + checksum).
 * @param buf Output buffer.
 * @param frameId 6-bit frame ID (0-63).
 * @param frameData Data bytes.
 * @param dataLen Number of data bytes (1-8).
 * @param checksumType Checksum calculation method.
 * @return Status code.
 * @note RT-safe: O(n) in data length, no allocation.
 *
 * The break field must be transmitted separately before this frame.
 */
inline Status buildFrame(FrameBuffer& buf, std::uint8_t frameId, const std::uint8_t* frameData,
                         std::size_t dataLen, ChecksumType checksumType) noexcept {
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (frameData == nullptr || dataLen == 0 || dataLen > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_INVALID_ARG;
  }

  buf.reset();
  const std::uint8_t PID = calculatePid(frameId);
  buf.append(Constants::SYNC_BYTE);
  buf.append(PID);
  buf.appendBytes(frameData, dataLen);
  buf.append(calculateChecksum(frameData, dataLen, PID, checksumType));

  return Status::SUCCESS;
}

/* ----------------------------- Response Parsing ----------------------------- */

/**
 * @struct ParsedFrame
 * @brief Result of parsing a LIN frame.
 *
 * Contains parsed header fields and a pointer to the data payload.
 * The data pointer references the original frame buffer (no copy).
 */
struct ParsedFrame {
  std::uint8_t pid{0};               ///< Protected Identifier.
  std::uint8_t frameId{0};           ///< Extracted 6-bit frame ID.
  const std::uint8_t* data{nullptr}; ///< Pointer to data portion.
  std::size_t dataLength{0};         ///< Length of data portion.
  std::uint8_t checksum{0};          ///< Received checksum.
  bool parityValid{false};           ///< PID parity check result.
  bool checksumValid{false};         ///< Checksum verification result.
};

/**
 * @brief Parse a LIN response (data + checksum) after header.
 * @param responseData Response data (data bytes + checksum).
 * @param responseLen Length of response.
 * @param pid Protected ID from header (for enhanced checksum).
 * @param expectedDataLen Expected data length (from frame ID or configuration).
 * @param checksumType Checksum type to verify.
 * @param result Output: parsed response fields.
 * @return Status code.
 * @note RT-safe: O(n) in data length, no allocation.
 */
inline Status parseResponse(const std::uint8_t* responseData, std::size_t responseLen,
                            std::uint8_t pid, std::size_t expectedDataLen,
                            ChecksumType checksumType, ParsedFrame& result) noexcept {
  if (responseData == nullptr) {
    return Status::ERROR_INVALID_ARG;
  }

  // Response must have at least data + checksum
  if (responseLen < expectedDataLen + 1) {
    return Status::ERROR_FRAME;
  }

  result.pid = pid;
  result.frameId = extractFrameId(pid);
  result.parityValid = verifyPidParity(pid);
  result.data = responseData;
  result.dataLength = expectedDataLen;
  result.checksum = responseData[expectedDataLen];
  result.checksumValid =
      verifyChecksum(responseData, expectedDataLen, result.checksum, pid, checksumType);

  if (!result.parityValid) {
    return Status::ERROR_PARITY;
  }
  if (!result.checksumValid) {
    return Status::ERROR_CHECKSUM;
  }

  return Status::SUCCESS;
}

/**
 * @brief Parse a complete LIN frame (sync + PID + data + checksum).
 * @param frame Complete frame data.
 * @param frameLen Length of frame.
 * @param checksumType Checksum type to verify.
 * @param result Output: parsed frame fields.
 * @return Status code.
 * @note RT-safe: O(n) in data length, no allocation.
 *
 * Validates sync byte, PID parity, and checksum.
 */
inline Status parseFrame(const std::uint8_t* frame, std::size_t frameLen, ChecksumType checksumType,
                         ParsedFrame& result) noexcept {
  if (frame == nullptr) {
    return Status::ERROR_INVALID_ARG;
  }
  if (frameLen < Constants::MIN_FRAME_SIZE + 1) { // At least sync + PID + 1 data + checksum
    return Status::ERROR_FRAME;
  }

  // Verify sync byte
  if (frame[0] != Constants::SYNC_BYTE) {
    return Status::ERROR_SYNC;
  }

  const std::uint8_t PID = frame[1];
  result.pid = PID;
  result.frameId = extractFrameId(PID);
  result.parityValid = verifyPidParity(PID);

  if (!result.parityValid) {
    return Status::ERROR_PARITY;
  }

  // Data length is frameLen - sync - PID - checksum
  const std::size_t DATA_LEN = frameLen - 3;
  if (DATA_LEN > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_FRAME;
  }

  result.data = frame + 2;
  result.dataLength = DATA_LEN;
  result.checksum = frame[frameLen - 1];
  result.checksumValid = verifyChecksum(result.data, DATA_LEN, result.checksum, PID, checksumType);

  if (!result.checksumValid) {
    return Status::ERROR_CHECKSUM;
  }

  return Status::SUCCESS;
}

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_LIN_FRAME_HPP
