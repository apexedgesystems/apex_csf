#ifndef APEX_ACTION_DEMO_SENSOR_DATA_HPP
#define APEX_ACTION_DEMO_SENSOR_DATA_HPP
/**
 * @file SensorData.hpp
 * @brief Data structures for the SensorModel component.
 *
 * The base structs (tunables, state, output, health telemetry) are
 * spec-defined in sensor_data.toml and generated into .auto/ -- this
 * header carries only the hand-crafted pieces: the composite
 * native+swapped INSPECT block and the ADL endianSwap the
 * EndiannessProxy finds.
 */

#include "src/utilities/data_proxy/inc/EndiannessProxy.hpp"

#include "SensorHealthTlm_auto.hpp"
#include "SensorOutput_auto.hpp"
#include "SensorState_auto.hpp"
#include "SensorTunableParams_auto.hpp"

#include <cstdint>

namespace appsim {
namespace sensor {

/**
 * @struct SensorOutputWithSwap
 * @brief Native + byte-swapped output in one contiguous block for atomic INSPECT.
 */
struct SensorOutputWithSwap {
  SensorOutput native{};  ///< Native-endian output (first 12 bytes).
  SensorOutput swapped{}; ///< Byte-swapped output (next 12 bytes).
};

static_assert(sizeof(SensorOutputWithSwap) == 24, "SensorOutputWithSwap size mismatch");

/**
 * @brief Byte-swap SensorOutput fields for cross-platform wire format.
 *
 * Found via ADL by EndiannessProxy when SwapRequired=true.
 * Swaps multi-byte fields (float, uint16); single-byte fields unchanged.
 */
inline void endianSwap(const SensorOutput& in, SensorOutput& out) noexcept {
  // Use the proxy's swapBytes for IEEE 754 floats
  out.temperature = system_core::data_proxy::swapBytes(in.temperature);
  out.temperatureRate = system_core::data_proxy::swapBytes(in.temperatureRate);
  out.overtemp = in.overtemp; // Single byte — no swap needed.
  out.reserved[0] = in.reserved[0];
  out.reserved[1] = in.reserved[1];
  out.reserved[2] = in.reserved[2];
}

} // namespace sensor
} // namespace appsim

#endif // APEX_ACTION_DEMO_SENSOR_DATA_HPP
