#ifndef APEX_SYSTEM_COMPONENT_SUPPORT_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_SUPPORT_COMPONENT_BASE_HPP
/**
 * @file SupportComponentBase.hpp
 * @brief Base class for runtime support service components.
 *
 * Support components are schedulable infrastructure that provides runtime
 * services to models and drivers: health monitoring, fault injection,
 * diagnostics, telemetry aggregation, etc.
 *
 * Support components:
 *   - CAN have scheduled tasks (inherits SchedulableComponentBase)
 *   - Return ComponentType::SUPPORT
 *   - May have multiple instances
 *   - Are managed by the executive alongside models
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SchedulableComponentBase.hpp"

namespace system_core {
namespace system_component {

/* ----------------------------- SupportComponentBase ----------------------------- */

/**
 * @class SupportComponentBase
 * @brief Abstract base for runtime support service components.
 *
 * Provides explicit typing for schedulable support infrastructure.
 * Example derived classes: HealthMonitor, FaultInjector, TelemetryAggregator.
 *
 * Support components participate in scheduled execution like models but
 * serve infrastructure purposes rather than simulation functionality.
 */
class SupportComponentBase : public SchedulableComponentBase {
public:
  /** @brief Default constructor. */
  SupportComponentBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~SupportComponentBase() override = default;

  // Non-copyable, non-movable (inherited from SchedulableComponentBase)
  SupportComponentBase(const SupportComponentBase&) = delete;
  SupportComponentBase& operator=(const SupportComponentBase&) = delete;
  SupportComponentBase(SupportComponentBase&&) = delete;
  SupportComponentBase& operator=(SupportComponentBase&&) = delete;

  /**
   * @brief Get component type classification.
   * @return ComponentType::SUPPORT for all support components.
   */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::SUPPORT;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_SUPPORT_COMPONENT_BASE_HPP
