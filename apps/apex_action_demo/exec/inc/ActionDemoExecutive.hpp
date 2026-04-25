#ifndef APEX_ACTION_DEMO_EXECUTIVE_HPP
#define APEX_ACTION_DEMO_EXECUTIVE_HPP
/**
 * @file ActionDemoExecutive.hpp
 * @brief Action engine and data transform demonstration executive.
 *
 * ActionDemoExecutive extends ApexExecutive with a component set designed
 * to exercise the action engine's observe-and-react pipeline alongside the
 * DataTransform support component for runtime data mutation:
 *
 *   - SensorModel (SW_MODEL): Temperature ramp with overtemp detection.
 *     Provides a predictable output for watchpoint threshold monitoring.
 *
 *   - DataTransform (SUPPORT): Byte-level data mutation via ByteMaskProxy.
 *     Demonstrates fault injection and value overrides triggered by the
 *     action engine via COMMAND routing.
 *
 *   - SystemMonitor (SUPPORT): CPU/memory/FD health monitoring.
 *
 * Demonstration scenarios (configured via TPRM/ground commands):
 *   1. Watchpoint fires when sensor temperature crosses threshold.
 *   2. Event notification logs the threshold crossing.
 *   3. Sequence starts on event, sends COMMANDs to DataTransform.
 *   4. DataTransform applies masks to corrupt/override sensor output.
 *   5. ARM_CONTROL dynamically enables/disables watchpoints and sequences.
 *
 * No hardware dependencies. Runs as pure SIL on any POSIX host.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/data_transform/inc/DataTransform.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include "apps/apex_action_demo/sensor/inc/SensorModel.hpp"

namespace appsim {
namespace exec {

/* ----------------------------- ActionDemoExecutive ----------------------------- */

class ActionDemoExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~ActionDemoExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "ACTION_DEMO_EXECUTIVE"; }

protected:
  /**
   * @brief Register all action demo components.
   *
   * Registers:
   *   - 1x SensorModel (SW_MODEL, componentId=210)
   *   - 1x DataTransform (SUPPORT, componentId=202)
   *   - 1x SystemMonitor (SUPPORT, componentId=200)
   *
   * @return true on success, false on registration failure.
   */
  [[nodiscard]] bool registerComponents() noexcept override;

  /**
   * @brief Wire monotonic time provider for ATS absolute time sequences.
   *
   * Called after all components are registered and the internal bus is wired.
   * Enables ATS sequences to use real time (microseconds) instead of cycle counts.
   */
  void configureComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  sensor::SensorModel sensor_;
  system_core::support::DataTransform dataTransform_;
  system_core::support::SystemMonitor sysMonitor_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_ACTION_DEMO_EXECUTIVE_HPP
