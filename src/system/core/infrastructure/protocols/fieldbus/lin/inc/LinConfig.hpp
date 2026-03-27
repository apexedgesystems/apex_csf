#ifndef APEX_PROTOCOLS_FIELDBUS_LIN_CONFIG_HPP
#define APEX_PROTOCOLS_FIELDBUS_LIN_CONFIG_HPP
/**
 * @file LinConfig.hpp
 * @brief Configuration structures for LIN controller.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinFrame.hpp"
#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

/* ----------------------------- LinConfig ----------------------------- */

/**
 * @struct LinConfig
 * @brief Configuration for LIN controller.
 *
 * LIN uses fixed serial parameters (8N1) but allows configurable baud rates.
 * Standard LIN baud rates are 9600, 10417, and 19200.
 */
struct LinConfig {
  /**
   * @brief Baud rate in bits per second.
   * Standard LIN rates: 9600, 10417, 19200.
   */
  std::uint32_t baudRate{19200};

  /**
   * @brief Checksum type (CLASSIC for LIN 1.x, ENHANCED for LIN 2.x).
   */
  ChecksumType checksumType{ChecksumType::ENHANCED};

  /**
   * @brief Break detection threshold (number of bit times).
   * LIN spec requires >= 13 dominant bits. Typical detection is 11-13.
   */
  std::uint8_t breakThreshold{11};

  /**
   * @brief Inter-byte timeout in bit times.
   * Maximum time between bytes within a frame.
   * LIN spec: 1.4 * (10 bit times) typical.
   */
  std::uint8_t interByteTimeoutBits{14};

  /**
   * @brief Response timeout in milliseconds.
   * Maximum time to wait for slave response after header.
   */
  std::uint16_t responseTimeoutMs{50};

  /**
   * @brief Enable bus collision detection via readback.
   * When enabled, transmitted bytes are read back and compared.
   */
  bool enableCollisionDetection{true};

  /**
   * @brief Convert to underlying UART configuration.
   * @return UartConfig with LIN-appropriate settings.
   * @note RT-safe: O(1), no allocation.
   *
   * LIN always uses 8 data bits, no parity, 1 stop bit.
   * Hardware flow control is not used in LIN.
   * Maps the numeric baud rate to the closest standard UART BaudRate enum.
   */
  [[nodiscard]] apex::protocols::serial::uart::UartConfig toUartConfig() const noexcept {
    apex::protocols::serial::uart::UartConfig cfg;
    // Map LIN baud rate to closest standard UART baud rate
    if (baudRate <= 2400) {
      cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_2400;
    } else if (baudRate <= 4800) {
      cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_4800;
    } else if (baudRate <= 9600) {
      cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_9600;
    } else {
      cfg.baudRate = apex::protocols::serial::uart::BaudRate::B_19200;
    }
    cfg.dataBits = apex::protocols::serial::uart::DataBits::EIGHT;
    cfg.parity = apex::protocols::serial::uart::Parity::NONE;
    cfg.stopBits = apex::protocols::serial::uart::StopBits::ONE;
    cfg.flowControl = apex::protocols::serial::uart::FlowControl::NONE;
    return cfg;
  }

  /**
   * @brief Calculate inter-byte timeout in microseconds.
   * @return Timeout in microseconds.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint32_t interByteTimeoutUs() const noexcept {
    // Bit time = 1,000,000 / baudRate microseconds
    // Timeout = bitTime * interByteTimeoutBits
    return (1000000UL * interByteTimeoutBits) / baudRate;
  }

  /**
   * @brief Calculate frame slot time in microseconds.
   * @param dataLen Number of data bytes in frame.
   * @return Maximum slot time in microseconds.
   * @note RT-safe: O(1).
   *
   * Slot time = (header + response) * nominal + tolerance
   * Header: break (13) + sync (10) + PID (10) = 33 bits
   * Response: (dataLen + checksum) * 10 bits
   * Tolerance: 40% per LIN spec
   */
  [[nodiscard]] std::uint32_t frameSlotTimeUs(std::size_t dataLen) const noexcept {
    const std::uint32_t HEADER_BITS = 33;
    const std::uint32_t RESPONSE_BITS = (static_cast<std::uint32_t>(dataLen) + 1) * 10;
    const std::uint32_t TOTAL_BITS = HEADER_BITS + RESPONSE_BITS;
    // Nominal time with 40% tolerance
    const std::uint32_t NOMINAL_US = (1000000UL * TOTAL_BITS) / baudRate;
    return (NOMINAL_US * 140) / 100;
  }
};

/* ----------------------------- LinScheduleEntry ----------------------------- */

/**
 * @struct LinScheduleEntry
 * @brief Single entry in a LIN schedule table.
 *
 * LIN masters use schedule tables to sequence frame transmission.
 * Each entry specifies a frame ID and its timing parameters.
 */
struct LinScheduleEntry {
  std::uint8_t frameId{0};      ///< 6-bit frame ID.
  std::uint16_t slotTimeMs{10}; ///< Slot time in milliseconds.
  std::uint8_t dataLength{0};   ///< Expected data length (0 = auto from ID).

  /**
   * @brief Check if entry is valid.
   * @return true if frame ID is valid.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] bool isValid() const noexcept { return isValidFrameId(frameId); }

  /**
   * @brief Get effective data length.
   * @return Data length (from entry or derived from ID).
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::size_t effectiveDataLength() const noexcept {
    if (dataLength > 0 && dataLength <= Constants::MAX_DATA_LENGTH) {
      return dataLength;
    }
    // Diagnostic frames always use 8 bytes
    if (isDiagnosticFrame(frameId)) {
      return 8;
    }
    return dataLengthFromId(frameId);
  }
};

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_LIN_CONFIG_HPP
