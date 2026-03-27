#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_TCP_TRANSPORT_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_TCP_TRANSPORT_HPP
/**
 * @file ModbusTcpTransport.hpp
 * @brief Modbus TCP transport over TCP/IP.
 *
 * Implements the ModbusTransport interface for Modbus TCP/IP protocol.
 * Uses MBAP (Modbus Application Protocol) header framing instead of CRC.
 *
 * Features:
 *  - Uses TcpSocketClient for network communication
 *  - Automatic transaction ID management
 *  - MBAP header construction/validation
 *  - Statistics tracking
 *
 * RT-Safety:
 *  - I/O operations are RT-safe (bounded syscalls via TcpSocketClient)
 *  - No heap allocation on I/O path
 */

#include "src/system/core/infrastructure/protocols/common/inc/ByteTrace.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusConfig.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTransport.hpp"
#include "src/system/core/infrastructure/protocols/network/tcp/inc/TcpSocketClient.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

// Namespace alias for TCP types
namespace tcp = apex::protocols::tcp;

/* ----------------------------- ModbusTcpTransport ----------------------------- */

/**
 * @class ModbusTcpTransport
 * @brief Modbus TCP transport implementation over TCP/IP.
 *
 * Lifecycle:
 *  1. Construct with host, port, and TCP config
 *  2. Call open() to establish connection
 *  3. Use sendRequest()/receiveResponse() for transactions
 *  4. Call close() to disconnect
 *
 * Ownership:
 *  - Owns the TcpSocketClient instance.
 *  - Manages connection lifecycle.
 *
 * Thread Safety:
 *  - NOT thread-safe. External synchronization required.
 */
class ModbusTcpTransport final : public ModbusTransport, public apex::protocols::ByteTrace {
public:
  /**
   * @brief Construct TCP transport.
   * @param host Server hostname or IP address.
   * @param port Server port (usually 502).
   * @param config TCP configuration.
   * @note NOT RT-safe: allocates internal buffers and socket.
   */
  ModbusTcpTransport(const std::string& host, std::uint16_t port, const ModbusTcpConfig& config);

  ~ModbusTcpTransport() override;

  ModbusTcpTransport(const ModbusTcpTransport&) = delete;
  ModbusTcpTransport& operator=(const ModbusTcpTransport&) = delete;
  ModbusTcpTransport(ModbusTcpTransport&&) = delete;
  ModbusTcpTransport& operator=(ModbusTcpTransport&&) = delete;

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

  /* ----------------------------- TCP-Specific ----------------------------- */

  /**
   * @brief Get the current transaction ID.
   * @return Current transaction ID value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint16_t transactionId() const noexcept { return transactionId_; }

  /**
   * @brief Get the host address.
   * @return Host string.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const std::string& host() const noexcept { return host_; }

  /**
   * @brief Get the port number.
   * @return Port number.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

private:
  std::string host_;
  std::uint16_t port_;
  ModbusTcpConfig config_;
  std::unique_ptr<tcp::TcpSocketClient> client_;
  std::uint16_t transactionId_;
  bool isOpen_;
  ModbusStats stats_;
  std::string description_;

  /**
   * @brief Build MBAP header and send frame.
   * @param frame RTU frame (without CRC - unit ID + PDU).
   * @param timeoutMs Timeout in milliseconds.
   * @return Status code.
   */
  Status sendMbapFrame(const FrameBuffer& frame, int timeoutMs) noexcept;

  /**
   * @brief Receive and validate MBAP frame.
   * @param frame Output buffer for PDU.
   * @param timeoutMs Timeout in milliseconds.
   * @return Status code.
   */
  Status receiveMbapFrame(FrameBuffer& frame, int timeoutMs) noexcept;
};

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_TCP_TRANSPORT_HPP
