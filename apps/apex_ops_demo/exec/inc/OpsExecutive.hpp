#ifndef APEX_OPS_DEMO_EXECUTIVE_HPP
#define APEX_OPS_DEMO_EXECUTIVE_HPP
/**
 * @file OpsExecutive.hpp
 * @brief Ops demonstration executive with configurable waveform generators.
 *
 * OpsExecutive extends ApexExecutive with a minimal component set
 * designed to exercise every APROTO C2 capability:
 *   - WaveGenerator x2 (SW_MODEL): Configurable waveform telemetry sources
 *   - TelemetryManager (SUPPORT): Push telemetry for ground systems
 *   - SystemMonitor (SUPPORT): CPU/memory/FD health
 *   - TestPlugin (SW_MODEL): Hot-swap test target
 *
 * No hardware dependencies. Runs as pure SIL on any POSIX host.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"
#include "src/system/core/support/telemetry_manager/inc/TelemetryManager.hpp"

#include "apps/apex_ops_demo/wave/inc/WaveGenerator.hpp"
// Static build uses version 1; hot-swap replaces with version 2 .so.
#ifndef APEX_TEST_PLUGIN_VERSION
#define APEX_TEST_PLUGIN_VERSION 1
#endif
#include "apps/apex_ops_demo/test/plugin/TestPlugin.hpp"

namespace appsim {
namespace exec {

/* ----------------------------- OpsExecutive ----------------------------- */

/**
 * @class OpsExecutive
 * @brief Application executive for Zenith C2 system testing.
 *
 * Registers all components during startup. Action engine configuration
 * and scheduling are TPRM-driven. All components are member variables
 * to ensure lifetime matches executive lifetime.
 */
class OpsExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~OpsExecutive() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "OPS_DEMO_EXECUTIVE"; }

protected:
  /**
   * @brief Register all Ops demo application components.
   *
   * Registers:
   *   - 2x WaveGenerator (SW_MODEL, fullUid=0xD000, 0xD001)
   *   - 1x TelemetryManager (SUPPORT, fullUid=0xC900)
   *   - 1x SystemMonitor (SUPPORT, fullUid=0xC800)
   *   - 1x TestPlugin (SW_MODEL, fullUid=0xFA00) -- hot-swap test target
   *
   * @return true on success, false on registration failure.
   */
  [[nodiscard]] bool registerComponents() noexcept override;

private:
  /* ----------------------------- Components ----------------------------- */

  wave::WaveGenerator waveGen0_;
  wave::WaveGenerator waveGen1_;
  system_core::support::TelemetryManager telemetryMgr_;
  system_core::support::SystemMonitor sysMonitor_;
  test::TestPlugin testPlugin_;
};

} // namespace exec
} // namespace appsim

#endif // APEX_OPS_DEMO_EXECUTIVE_HPP
