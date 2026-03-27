#ifndef APEX_SYSTEM_CORE_INTERFACE_TUNABLES_HPP
#define APEX_SYSTEM_CORE_INTERFACE_TUNABLES_HPP
/**
 * @file ApexInterfaceTunables.hpp
 * @brief Fixed-size tunables for ApexInterface: single TCP socket with configurable framing.
 *
 * All types in this header are RT-safe (POD structs, no dynamic allocations).
 *
 * Notes:
 * - No dynamic allocations in this packet.
 * - Host buffer is null-terminated and zero-padded.
 * - Default configuration: TCP server on port 9000 with SLIP framing.
 * - Use makeFixedChar() to build constexpr defaults from string_view/literals.
 */

#include "src/utilities/helpers/inc/Strings.hpp"

#include <cstddef>
#include <cstdint>

#include <array>
#include <string_view>

namespace system_core {
namespace interface {

/* ----------------------------- Constants ----------------------------- */

/// Max host buffer size (including null terminator).
inline constexpr std::size_t APEX_HOST_MAX = 64;

/// Default host literal.
inline constexpr std::string_view APEX_DEFAULT_HOST = "127.0.0.1";

/// Default TCP port.
inline constexpr std::uint16_t APEX_DEFAULT_PORT = 9000;

/// Precomputed default host buffer.
inline constexpr auto APEX_DEFAULT_HOST_ARR =
    apex::helpers::strings::makeFixedChar<APEX_HOST_MAX>(APEX_DEFAULT_HOST);

/* ----------------------------- FramingType ----------------------------- */

/**
 * @enum FramingType
 * @brief Framing protocol selection for interface I/O.
 */
enum class FramingType : std::uint8_t {
  SLIP = 0, ///< SLIP framing (default, RFC 1055).
  COBS = 1  ///< COBS framing (Consistent Overhead Byte Stuffing).
};

/* ----------------------------- ApexInterfaceTunables ----------------------------- */

/**
 * @struct ApexInterfaceTunables
 * @brief Single-socket TCP tunables with configurable framing.
 *
 * Default configuration:
 * - TCP server on 127.0.0.1:9000
 * - SLIP framing for cmd/tlm packets
 * - CRC disabled (optional, can be enabled via protocol flags)
 *
 * Fields are POD and sized for easy packing. Host is a null-terminated,
 * zero-padded buffer.
 */
struct ApexInterfaceTunables {
  std::array<char, APEX_HOST_MAX> host{APEX_DEFAULT_HOST_ARR}; ///< Host/interface C-string buffer.
  std::uint16_t port{APEX_DEFAULT_PORT};                       ///< TCP server port.
  FramingType framing{FramingType::SLIP};                      ///< Framing protocol.
  std::uint8_t crcEnabled{0};                                  ///< Enable CRC in APROTO packets.
  std::uint16_t cmdQueueCapacity{32};  ///< Command queue capacity per component.
  std::uint16_t tlmQueueCapacity{64};  ///< Telemetry queue capacity per component.
  std::uint16_t maxPayloadBytes{4096}; ///< Maximum APROTO payload size.
  std::uint32_t pollTimeoutMs{10};     ///< Socket poll timeout in ms.
  std::uint16_t droppedFrameReportThreshold{
      1}; ///< Report dropped frames after N drops (0=disable).
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_TUNABLES_HPP
