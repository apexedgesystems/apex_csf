#ifndef APEX_SYSTEM_CORE_INTERFACE_SOCKET_CONFIGURATION_HPP
#define APEX_SYSTEM_CORE_INTERFACE_SOCKET_CONFIGURATION_HPP
/**
 * @file InterfaceSocketConfiguration.hpp
 * @brief Socket transport and configuration types for InterfaceBase.
 *
 * SocketEndpoint/SocketIoConfig/SocketConfiguration contain heap-allocated strings and containers
 * and are NOT RT-safe. Use only during initialization, never in RT context.
 *
 * Notes:
 * - Validation is performed by InterfaceBase::configureSockets().
 * - Reconfiguration requires shutdown() before a new configureSockets() call.
 */

#include <cstddef> // std::size_t
#include <cstdint> // std::uint8_t, std::uint16_t
#include <string>  // std::string
#include <vector>  // std::vector

namespace system_core {
namespace interface {

/**
 * @brief Socket transport type.
 */
enum class SocketTransport : std::uint8_t { TCP = 0, UDP = 1 };

/**
 * @brief Endpoint definition for a socket server (bind address + port).
 */
struct SocketEndpoint {
  SocketTransport transport{SocketTransport::TCP};
  std::string bindAddress{}; ///< Interface or hostname (e.g., "0.0.0.0", "127.0.0.1").
  std::uint16_t port{0};     ///< Numeric port.
};

/**
 * @brief Per-interface I/O configuration (sizes/timeouts/queues).
 */
struct SocketIoConfig {
  std::size_t rxBufferBytes{1024};      ///< Per-server RX buffer size (bytes).
  std::size_t txBufferBytes{1024};      ///< Per-server TX buffer size (bytes).
  std::uint32_t rxTimeoutMs{0};         ///< RX operation timeout (ms).
  std::uint32_t txTimeoutMs{0};         ///< TX operation timeout (ms).
  std::size_t pipeCapacityMessages{50}; ///< Bounded pipe capacity (messages).
};

/**
 * @brief Complete configuration bundle consumed by InterfaceBase::configureSockets().
 */
struct SocketConfiguration {
  std::vector<SocketEndpoint> endpoints{}; ///< endpoints.size() <= MAX_SERVERS.
  SocketIoConfig ioConfig{};               ///< Shared defaults for RX/TX/pipe sizing.
  std::size_t maxIoBufferBytes{4096}; ///< <= GLOBAL_BUFFER_MAX (enforced by configureSockets()).
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_SOCKET_CONFIGURATION_HPP
