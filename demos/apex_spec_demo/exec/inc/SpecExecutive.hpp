#ifndef APEX_SPEC_DEMO_EXECUTIVE_HPP
#define APEX_SPEC_DEMO_EXECUTIVE_HPP
/**
 * @file SpecExecutive.hpp
 * @brief Spec-driven demo executive.
 *
 * The minimal component set around the spec-born pair: SpecSensor
 * (SW_MODEL, fullUid=0xD400, TOML-authored spec) and SpecActuator
 * (SW_MODEL, fullUid=0xD500, proto-authored spec), plus
 * SystemMonitor (SUPPORT, fullUid=0xC800) for health telemetry.
 * Pure SIL on any POSIX host; the demo exists to prove the
 * spec-driven path in both authoring formats -- every command the
 * checkout drives dispatches through generated code.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include "demos/apex_spec_demo/actuator/inc/SpecActuator.hpp"
#include "demos/apex_spec_demo/sensor/inc/SpecSensor.hpp"

namespace appsim {
namespace exec {

class SpecExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~SpecExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "SPEC_DEMO_EXECUTIVE"; }

protected:
  /** @brief Register the spec demo components. */
  [[nodiscard]] bool registerComponents() noexcept override;

private:
  spec::SpecSensor sensor_;
  spec::SpecActuator actuator_;
  system_core::support::SystemMonitor sysMonitor_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_SPEC_DEMO_EXECUTIVE_HPP
