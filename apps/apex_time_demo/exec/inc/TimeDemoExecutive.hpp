#ifndef APEX_TIME_DEMO_EXECUTIVE_HPP
#define APEX_TIME_DEMO_EXECUTIVE_HPP
/**
 * @file TimeDemoExecutive.hpp
 * @brief Apex executive for the PPS time-distribution demo.
 *
 * Wires a MockPps source into the executive's TimeServer and spawns a
 * background "GPS simulator" thread that drives the four canonical
 * scenarios from the ticket:
 *
 *   t=0..3 s    Cold-start dark period: no edges, no reference -> NONE.
 *   t=3 s       Reference time arrives + first PPS edge -> VALID/FINE.
 *   t=3..18 s   1 Hz edges; quality climbs to PRECISE.
 *   t=18..25 s  PPS dropout (no edges); STALE then FREERUN.
 *   t=25 s      resetCorrelation + fresh reference -> VALID/FINE.
 *   t=25..    Resumed 1 Hz edges; PRECISE returns.
 *
 * Components registered:
 *   - SystemMonitor (CPU/mem/FD)
 *
 * No real GPS receiver, no /dev/pps[N], no hardware needed. Connect
 * with the standard ops client during the run to inspect TNT,
 * TimeServer OUTPUT, and ATS state.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/hal/mock/inc/MockPps.hpp"
#include "src/system/core/support/system_monitor/inc/SystemMonitor.hpp"

#include <atomic>
#include <thread>

namespace appsim {
namespace exec {

/* ----------------------------- TimeDemoExecutive ----------------------------- */

class TimeDemoExecutive : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;

  ~TimeDemoExecutive() override;

  [[nodiscard]] const char* label() const noexcept override { return "TIME_DEMO_EXECUTIVE"; }

protected:
  /**
   * @brief Wire MockPps into TimeServer; register SystemMonitor.
   */
  [[nodiscard]] bool registerComponents() noexcept override;

  /**
   * @brief Spawn the GPS simulator thread.
   * @note Called after registerComponents and the bus wire-up.
   */
  void configureComponents() noexcept override;

private:
  /// GPS simulator. Runs on its own thread; periodically injects PPS
  /// edges and SET_REFERENCE_TIME commands to drive the demo scenarios.
  void runGpsSimulator() noexcept;

  /// Components.
  apex::hal::MockPps pps_;
  system_core::support::SystemMonitor sysMonitor_;

  /// Simulator thread state.
  std::thread simThread_;
  std::atomic<bool> simRunning_{false};
};

} // namespace exec
} // namespace appsim

#endif // APEX_TIME_DEMO_EXECUTIVE_HPP
