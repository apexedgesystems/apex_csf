#ifndef APEX_SPEC_DEMO_EXECUTIVE_HPP
#define APEX_SPEC_DEMO_EXECUTIVE_HPP
/**
 * @file SpecExecutive.hpp
 * @brief Spec-driven demo executive.
 *
 * Every app component is spec-born, and together they cover the
 * component taxonomy and the spec vocabulary:
 *
 *   SpecSensor    0xD400  SW_MODEL  TOML     drift physics + mode machine
 *   SpecActuator  0xD500  SW_MODEL  proto    slew + bounded string
 *   SpecBusDriver 0xD600  DRIVER    TOML     loopback bus round-trips
 *   SpecMatrix    0xD700  SUPPORT   TOML     full type-vocabulary checksum
 *   SpecLimits    0xD800  SW_MODEL  TOML     every constraint kind
 *   SpecProtoMax  0xD900  SW_MODEL  proto    maximal-profile reference
 *   SpecChannel   0xDA00/01 SW_MODEL TOML    one spec, two instances
 *
 * Plus SystemMonitor (SUPPORT, 0xC800) for health telemetry. Pure SIL
 * on any POSIX host; the demo is the living compatibility suite for
 * the spec-driven path -- every command the checkout drives
 * dispatches through generated code.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include "demos/apex_spec_demo/actuator/inc/SpecActuator.hpp"
#include "demos/apex_spec_demo/bus_driver/inc/SpecBusDriver.hpp"
#include "demos/apex_spec_demo/channel/inc/SpecChannel.hpp"
#include "demos/apex_spec_demo/limits/inc/SpecLimits.hpp"
#include "demos/apex_spec_demo/matrix/inc/SpecMatrix.hpp"
#include "demos/apex_spec_demo/proto_max/inc/SpecProtoMax.hpp"
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
  spec::SpecBusDriver busDriver_;
  spec::SpecMatrix matrix_;
  spec::SpecLimits limits_;
  spec::SpecProtoMax protoMax_;
  spec::SpecChannel channelA_;
  spec::SpecChannel channelB_;
  system_core::support::SystemMonitor sysMonitor_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_SPEC_DEMO_EXECUTIVE_HPP
