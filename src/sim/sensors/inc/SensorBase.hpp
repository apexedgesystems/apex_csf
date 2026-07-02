#ifndef APEX_SIM_SENSORS_SENSOR_BASE_HPP
#define APEX_SIM_SENSORS_SENSOR_BASE_HPP
/**
 * @file SensorBase.hpp
 * @brief Shared base for sensor measurement models.
 *
 * A sensor model maps ground truth to a measurement carrying physical error
 * (noise, bias). Sensor models are:
 *   - device-agnostic -- they report a physical quantity, not a wire
 *     representation; protocol, framing, cadence, and reported latency belong
 *     to a hardware-emulation layer, not here;
 *   - timing-agnostic -- they report "the measurement if sampled now"; when to
 *     sample is the scheduler's concern.
 *
 * Sensors measure different physical quantities, so each concrete sensor defines
 * its own measure() signature and its own flat measurement struct. This base
 * shares the common machinery: a deterministic noise source, seeding for
 * reproducible replay, and a kind/name identity for logging and routing.
 */

#include "src/sim/sensors/inc/GaussianSampler.hpp"

#include <cstdint>

namespace sim::sensors {

/* ----------------------------- SensorKind ----------------------------- */

/**
 * @brief Characterization of a sensor for logging and routing.
 *
 * A tag only -- behavior is defined by the concrete class, never switched on
 * this value. External is the catch-all for user-defined sensors.
 */
enum class SensorKind : std::uint8_t {
  Gnss,
  AirData,
  Altimeter,
  Imu,
  Magnetometer,
  Lidar,
  External, ///< user-defined / catch-all
};

/* ----------------------------- SensorBase ----------------------------- */

class SensorBase {
public:
  explicit SensorBase(SensorKind kind = SensorKind::External, const char* name = "",
                      std::uint32_t seed = 0u) noexcept
      : sampler_(seed), kind_(kind), name_(name) {}

  virtual ~SensorBase() = default;

  [[nodiscard]] SensorKind kind() const noexcept { return kind_; }
  [[nodiscard]] const char* name() const noexcept { return name_; }

  /** @brief Reseed the noise stream for deterministic replay / Monte Carlo. */
  void reseed(std::uint32_t seed) noexcept { sampler_.seed(seed); }

protected:
  GaussianSampler sampler_; ///< deterministic, portable noise source

private:
  SensorKind kind_;
  const char* name_;
};

} // namespace sim::sensors

#endif // APEX_SIM_SENSORS_SENSOR_BASE_HPP
