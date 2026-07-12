#ifndef APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_DATA_HPP
#define APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_DATA_HPP
/**
 * @file VirtualFlightCtrlData.hpp
 * @brief Data structures for VirtualFlightCtrl.
 *
 * Runtime state for the emulated flight controller. Registered with the
 * executive registry for C2 inspection.
 *
 * @note RT-safe: Pure data structure, no allocation or I/O.
 */

#include <cstdint>

namespace appsim {
namespace model {

/* ----------------------------- State ----------------------------- */

/**
 * @struct VfcState
 * @brief Runtime state for VirtualFlightCtrl.
 *
 * Size: 16 bytes.
 */
struct VfcState {
  std::uint32_t rxCount{0};   ///< Valid VehicleState frames received.
  std::uint32_t txCount{0};   ///< ControlCmd frames sent.
  std::uint32_t crcErrors{0}; ///< CRC validation failures.
  std::uint32_t pollCount{0}; ///< Transport poll iterations.
};

static_assert(sizeof(VfcState) == 16, "VfcState size mismatch");

} // namespace model
} // namespace appsim

#endif // APEX_HIL_DEMO_VIRTUAL_FLIGHT_CTRL_DATA_HPP
