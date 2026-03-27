#ifndef APEX_PROTOCOLS_SERIAL_UART_CONFIG_HPP
#define APEX_PROTOCOLS_SERIAL_UART_CONFIG_HPP
/**
 * @file UartConfig.hpp
 * @brief UART port configuration types for baud rate, parity, stop bits, etc.
 *
 * Provides strongly-typed enumerations for all standard UART configuration
 * parameters. These are used by UartAdapter::configure() to set up the
 * serial port via termios.
 */

#include <cstdint>

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ----------------------------- Enumerations ----------------------------- */

/**
 * @enum BaudRate
 * @brief Standard UART baud rates.
 *
 * Values match their numeric baud rates for clarity. Additional rates
 * can be added as needed for specific hardware.
 */
enum class BaudRate : std::uint32_t {
  B_1200 = 1200,
  B_2400 = 2400,
  B_4800 = 4800,
  B_9600 = 9600,
  B_19200 = 19200,
  B_38400 = 38400,
  B_57600 = 57600,
  B_115200 = 115200,
  B_230400 = 230400,
  B_460800 = 460800,
  B_500000 = 500000,
  B_576000 = 576000,
  B_921600 = 921600,
  B_1000000 = 1000000,
  B_1152000 = 1152000,
  B_1500000 = 1500000,
  B_2000000 = 2000000,
  B_2500000 = 2500000,
  B_3000000 = 3000000,
  B_3500000 = 3500000,
  B_4000000 = 4000000
};

/**
 * @enum DataBits
 * @brief Number of data bits per character.
 */
enum class DataBits : std::uint8_t { FIVE = 5, SIX = 6, SEVEN = 7, EIGHT = 8 };

/**
 * @enum Parity
 * @brief Parity checking mode.
 */
enum class Parity : std::uint8_t {
  NONE = 0, ///< No parity bit.
  ODD = 1,  ///< Odd parity (parity bit makes total 1s odd).
  EVEN = 2  ///< Even parity (parity bit makes total 1s even).
};

/**
 * @enum StopBits
 * @brief Number of stop bits.
 */
enum class StopBits : std::uint8_t {
  ONE = 1, ///< One stop bit (standard).
  TWO = 2  ///< Two stop bits (higher stability, lower throughput).
};

/**
 * @enum FlowControl
 * @brief Hardware and software flow control modes.
 */
enum class FlowControl : std::uint8_t {
  NONE = 0,     ///< No flow control.
  HARDWARE = 1, ///< RTS/CTS hardware flow control.
  SOFTWARE = 2  ///< XON/XOFF software flow control.
};

/* ----------------------------- UartConfig ----------------------------- */

/**
 * @struct UartConfig
 * @brief Complete UART port configuration.
 *
 * Aggregates all parameters needed to configure a UART port. Default
 * values represent a common 8N1 configuration at 115200 baud.
 *
 * @note RT-safe: Simple POD struct, no allocation.
 */
struct UartConfig {
  BaudRate baudRate = BaudRate::B_115200;      ///< Transmission speed.
  DataBits dataBits = DataBits::EIGHT;         ///< Bits per character.
  Parity parity = Parity::NONE;                ///< Parity checking mode.
  StopBits stopBits = StopBits::ONE;           ///< Stop bit count.
  FlowControl flowControl = FlowControl::NONE; ///< Flow control mode.

  /**
   * @brief RS-485 mode configuration.
   *
   * When enabled, the adapter will use TIOCSRS485 ioctl to configure
   * the kernel driver for RS-485 half-duplex operation with automatic
   * RTS direction control.
   */
  struct Rs485Config {
    bool enabled = false;                   ///< Enable RS-485 mode.
    bool rtsOnSend = true;                  ///< Assert RTS during transmission.
    bool rtsAfterSend = false;              ///< Keep RTS asserted after transmission.
    std::uint32_t delayRtsBeforeSendUs = 0; ///< Delay before asserting RTS (microseconds).
    std::uint32_t delayRtsAfterSendUs = 0;  ///< Delay after deasserting RTS (microseconds).
  } rs485;

  /**
   * @brief Low-latency mode configuration.
   *
   * When enabled, attempts to minimize latency by setting ASYNC_LOW_LATENCY
   * flag via TIOCGSERIAL/TIOCSSERIAL ioctls. Not all drivers support this.
   */
  bool lowLatency = false;

  /**
   * @brief Exclusive access mode.
   *
   * When enabled, uses flock() with LOCK_EX to prevent other processes
   * from accessing the device simultaneously.
   */
  bool exclusiveAccess = true;
};

/* ----------------------------- toString Functions ----------------------------- */

/**
 * @brief Convert BaudRate to human-readable string.
 * @param rate Baud rate to convert.
 * @return String literal (e.g., "115200").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(BaudRate rate) noexcept;

/**
 * @brief Convert DataBits to human-readable string.
 * @param bits Data bits to convert.
 * @return String literal (e.g., "8").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(DataBits bits) noexcept;

/**
 * @brief Convert Parity to human-readable string.
 * @param parity Parity to convert.
 * @return String literal (e.g., "NONE", "ODD", "EVEN").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(Parity parity) noexcept;

/**
 * @brief Convert StopBits to human-readable string.
 * @param bits Stop bits to convert.
 * @return String literal (e.g., "1", "2").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(StopBits bits) noexcept;

/**
 * @brief Convert FlowControl to human-readable string.
 * @param fc Flow control to convert.
 * @return String literal (e.g., "NONE", "HARDWARE", "SOFTWARE").
 * @note RT-safe: Returns static string literals.
 */
const char* toString(FlowControl fc) noexcept;

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_SERIAL_UART_CONFIG_HPP
