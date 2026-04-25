#ifndef APEX_SYSTEM_COMPONENT_DRIVER_BASE_HPP
#define APEX_SYSTEM_COMPONENT_DRIVER_BASE_HPP
/**
 * @file DriverBase.hpp
 * @brief Base class for real hardware interface components (drivers).
 *
 * Drivers are schedulable components that communicate with real hardware:
 * CAN bus adapters, serial ports, IMUs, GPS receivers, etc.
 *
 * Drivers:
 *   - CAN have scheduled tasks (inherits SchedulableComponentBase)
 *   - Return ComponentType::DRIVER
 *   - May have multiple instances (e.g., multiple CAN buses)
 *   - Are managed by the executive alongside models
 *
 * Hardware Abstraction Pattern:
 *   For hardware that can be simulated, define an interface (e.g., IImu)
 *   that both the driver (ImuDriver) and simulation model (ImuModel)
 *   implement. This enables swapping real hardware for simulation.
 *
 * Example:
 * @code
 *   // Interface
 *   class IImu {
 *   public:
 *     virtual ~IImu() = default;
 *     virtual ImuReading read() noexcept = 0;
 *   };
 *
 *   // Real hardware driver
 *   class ImuDriver final : public DriverBase, public IImu {
 *     ImuReading read() noexcept override { ... }
 *   };
 *
 *   // Simulation model
 *   class ImuModel final : public SimModelBase, public IImu {
 *     ImuReading read() noexcept override { ... }
 *   };
 * @endcode
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SchedulableComponentBase.hpp"

namespace system_core {
namespace system_component {

/* ----------------------------- DriverBase ----------------------------- */

/**
 * @class DriverBase
 * @brief Abstract base for real hardware interface components.
 *
 * Provides explicit typing for schedulable hardware drivers.
 * Example derived classes: CanBusDriver, SerialDriver, ImuDriver.
 *
 * Drivers participate in scheduled execution like models but interface
 * with real hardware rather than simulating behavior.
 */
class DriverBase : public SchedulableComponentBase {
public:
  /** @brief Default constructor. */
  DriverBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~DriverBase() override = default;

  // Non-copyable, non-movable (inherited from SchedulableComponentBase)
  DriverBase(const DriverBase&) = delete;
  DriverBase& operator=(const DriverBase&) = delete;
  DriverBase(DriverBase&&) = delete;
  DriverBase& operator=(DriverBase&&) = delete;

  /**
   * @brief Get component type classification.
   * @return ComponentType::DRIVER for all hardware drivers.
   */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::DRIVER;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_DRIVER_BASE_HPP
