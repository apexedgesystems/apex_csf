#ifndef APEX_ACTION_DEMO_SENSOR_DATA_HPP
#define APEX_ACTION_DEMO_SENSOR_DATA_HPP
/**
 * @file SensorData.hpp
 * @brief Data structures for the SensorModel component.
 *
 * Simple sensor simulation with a temperature ramp and limit detection.
 * Provides a predictable output for action engine watchpoint testing.
 * Includes ADL endianSwap for EndiannessProxy integration.
 */

#include "src/utilities/data_proxy/inc/EndiannessProxy.hpp"

#include <cstdint>

namespace appsim {
namespace sensor {

/* ----------------------------- SensorTunableParams ----------------------------- */

/**
 * @struct SensorTunableParams
 * @brief TPRM-configurable sensor parameters.
 */
struct SensorTunableParams {
  float rampRate{0.5F};     ///< Temperature increase per step [deg/step].
  float initialTemp{20.0F}; ///< Starting temperature [deg].
  float maxTemp{120.0F};    ///< Wrap-around temperature (resets to initial).
};

static_assert(sizeof(SensorTunableParams) == 12, "SensorTunableParams size mismatch");

/* ----------------------------- SensorState ----------------------------- */

/**
 * @struct SensorState
 * @brief Runtime state for the sensor model.
 */
struct SensorState {
  std::uint32_t stepCount{0}; ///< Total step invocations.
  std::uint32_t wrapCount{0}; ///< Number of temperature wrap-arounds.
};

static_assert(sizeof(SensorState) == 8, "SensorState size mismatch");

/* ----------------------------- SensorOutput ----------------------------- */

/**
 * @struct SensorOutput
 * @brief Published output for registry access and watchpoint monitoring.
 */
struct SensorOutput {
  float temperature{0.0F};     ///< Current temperature reading [deg].
  float temperatureRate{0.0F}; ///< Rate of change [deg/step].
  std::uint8_t overtemp{0};    ///< 1 if temperature > 100.0 (threshold).
  std::uint8_t reserved[3]{};  ///< Alignment padding.
};

static_assert(sizeof(SensorOutput) == 12, "SensorOutput size mismatch");

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

/* ----------------------------- SensorHealthTlm ----------------------------- */

/**
 * @struct SensorHealthTlm
 * @brief Health telemetry returned by GET_STATS (opcode 0x0100).
 */
struct __attribute__((packed)) SensorHealthTlm {
  float temperature{0.0F};
  float rampRate{0.0F};
  std::uint32_t stepCount{0};
  std::uint32_t wrapCount{0};
  std::uint8_t overtemp{0};
  std::uint8_t reserved[3]{};
};

static_assert(sizeof(SensorHealthTlm) == 20, "SensorHealthTlm size mismatch");

} // namespace sensor
} // namespace appsim

#endif // APEX_ACTION_DEMO_SENSOR_DATA_HPP
