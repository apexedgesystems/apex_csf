#ifndef APEX_HIL_DEMO_EXECUTIVE_HPP
#define APEX_HIL_DEMO_EXECUTIVE_HPP
/**
 * @file HilExecutive.hpp
 * @brief HIL demonstration executive with plant, drivers, and comparator.
 *
 * HilExecutive extends ApexExecutive with the full HIL component set:
 *   - HilPlantModel (SW_MODEL): 3DOF physics simulation
 *   - VirtualFlightCtrl (HW_MODEL): Emulated STM32 behind PTY
 *   - HilDriver x2 (DRIVER): One for real STM32, one for emulated
 *   - HilComparator (SUPPORT): Diffs ControlCmd from both drivers
 *
 * Transport provisioning is handled by the framework: HW_MODELs self-
 * provision via TransportLink during registerComponent(). The executive
 * only wires the peer endpoint to the matching driver.
 *
 * Action engine configuration is loaded from TPRM, not hardcoded.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include "apps/apex_hil_demo/driver/inc/HilDriver.hpp"
#include "apps/apex_hil_demo/support/inc/HilComparator.hpp"
#include "apps/apex_hil_demo/model/inc/HilPlantModel.hpp"
#include "apps/apex_hil_demo/model/inc/VirtualFlightCtrl.hpp"
// Static build uses version 1; hot-swap replaces with version 2 .so.
#ifndef APEX_TEST_PLUGIN_VERSION
#define APEX_TEST_PLUGIN_VERSION 1
#endif
#include "apps/apex_hil_demo/test/plugin/TestPlugin.hpp"

namespace appsim {
namespace exec {

/* ----------------------------- HilExecutive ----------------------------- */

/**
 * @class HilExecutive
 * @brief Application executive with full HIL component set.
 *
 * Registers all components during startup. Transport provisioning and
 * action engine configuration are handled by the framework (TPRM-driven).
 * Models, drivers, and support components are member variables to ensure
 * lifetime matches executive lifetime.
 */
class HilExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~HilExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "HIL_EXECUTIVE"; }

protected:
  /**
   * @brief Register all HIL application components.
   *
   * Registers:
   *   - 1x HilPlantModel (SW_MODEL, fullUid=0x7800)
   *   - 1x VirtualFlightCtrl (HW_MODEL, fullUid=0x7900)
   *   - 2x HilDriver (DRIVER, fullUid=0x7A00, 0x7A01)
   *   - 1x HilComparator (SUPPORT, fullUid=0x7B00)
   *   - 1x SystemMonitor (SUPPORT, fullUid=0xC800)
   *   - 1x TestPlugin (SW_MODEL, fullUid=0xFA00) -- hot-swap test target
   *
   * Transport is auto-provisioned by the framework for HW_MODEL components.
   * The executive wires the peer endpoint to the matching driver.
   *
   * @return true on success, false on registration failure.
   */
  [[nodiscard]] bool registerComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  plant::HilPlantModel plantModel_;
  model::VirtualFlightCtrl virtualCtrl_;
  driver::HilDriver driverReal_;
  driver::HilDriver driverEmulated_;
  support::HilComparator comparator_;
  system_core::support::SystemMonitor sysMonitor_;
  test::TestPlugin testPlugin_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_HIL_DEMO_EXECUTIVE_HPP
