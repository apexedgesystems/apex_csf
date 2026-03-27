#ifndef APEX_HIL_DEMO_DRIVER_DATA_HPP
#define APEX_HIL_DEMO_DRIVER_DATA_HPP
/**
 * @file HilDriverData.hpp
 * @brief Data structures for HilDriver.
 *
 * Runtime state for the HIL serial driver. Registered with the executive
 * registry for C2 inspection.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace appsim {
namespace driver {

/* ----------------------------- State ----------------------------- */

/**
 * @struct DriverState
 * @brief Runtime state for HilDriver.
 *
 * Size: 36 bytes.
 */
struct DriverState {
  std::uint32_t txCount{0};       ///< VehicleState frames sent to device.
  std::uint32_t rxCount{0};       ///< ControlCmd frames received from device.
  std::uint32_t crcErrors{0};     ///< CRC validation failures.
  std::uint32_t txMisses{0};      ///< sendState calls skipped (no UART or no state source).
  std::uint32_t rxMisses{0};      ///< recvCommand calls with no data available (non-blocking).
  std::uint32_t commLostCount{0}; ///< Number of comm loss events.
  std::uint32_t seqGaps{0};       ///< Detected gaps in received ControlCmd sequence.
  std::uint16_t txSeq{0};         ///< Last sent VehicleState.seqNum.
  std::uint16_t rxSeq{0};         ///< Last received ControlCmd.seqNum.
  std::uint8_t hasCmd{0};         ///< 1 if a valid ControlCmd has been received.
  std::uint8_t uartOpen{0};       ///< 1 if UART is open and configured.
  std::uint8_t commLost{0};       ///< 1 if comm watchdog detected link loss.
  std::uint8_t reserved{};        ///< Padding for alignment.
};

static_assert(sizeof(DriverState) == 36, "DriverState size mismatch");

} // namespace driver
} // namespace appsim

#endif // APEX_HIL_DEMO_DRIVER_DATA_HPP
