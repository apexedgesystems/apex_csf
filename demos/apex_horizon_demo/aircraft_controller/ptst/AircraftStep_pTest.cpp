/**
 * @file AircraftStep_pTest.cpp
 * @brief Per-tick cost of the aircraft plant and the closed-loop tick.
 *
 * The 100 Hz aircraftStep is the demo's RT path: 6DOF integration,
 * telemetry publish, and -- while a wire-armed excitation's window is
 * open -- a 20 Hz mode-trace capture into a bounded buffer that the
 * 1 Hz telemetry task drains. The loops here isolate that capture's
 * cost from the plant's: the same step with the trace idle and with a
 * window armed (drained at the demo's cadence so the measured path is
 * the append, not the overflow branch), plus the 25 Hz controller tick
 * with its four plant sub-steps as the scheduler runs them.
 */

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#include "demos/apex_horizon_demo/aircraft/inc/Aircraft.hpp"
#include "demos/apex_horizon_demo/aircraft/inc/AircraftCommand.hpp"
#include "demos/apex_horizon_demo/aircraft_controller/inc/AircraftController.hpp"
#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBodyData.hpp"

namespace {

using appsim::aircraft::Aircraft;
using appsim::aircraft::AircraftOpcode;
using appsim::aircraft_controller::AircraftController;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

/// Analytic Earth: no data files, the closed-loop suite's fixture.
CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

/// Trim-cruise plant + controller wired as the demo scheduler sees them.
struct Rig {
  CelestialBody earth;
  Aircraft aircraft;
  AircraftController controller;

  bool init() {
    earth.tunables().set(analyticEarth());
    if (earth.init() != 0u) {
      return false;
    }
    aircraft.setBody(&earth);
    controller.setAircraft(&aircraft);
    auto& p = controller.tunables().get();
    const auto& at = aircraft.tunables().get();
    p.target_altitude_m = at.init_alt_m;
    p.target_airspeed_m_s = at.init_speed_m_s;
    p.target_heading_deg = at.init_heading_deg;
    p.enable_mode = 1;
    aircraft.setControllerOutput(&controller.controllerOutput());
    return true;
  }

  /// One 25 Hz controller tick + four 100 Hz plant sub-steps.
  void closedLoopTick() {
    controller.controllerStep();
    aircraft.aircraftStep();
    aircraft.aircraftStep();
    aircraft.aircraftStep();
    aircraft.aircraftStep();
  }
};

/// Arm the Dutch-roll set-piece excitation over the wire (opens a 60 s
/// trace window); re-armed whenever the window closes so the loop
/// never measures an idle trace.
void armTrace(Aircraft& a) {
  std::uint8_t mode = 1u;
  apex::compat::rospan<std::uint8_t> payload(&mode, 1);
  std::vector<std::uint8_t> resp;
  (void)a.handleCommand(static_cast<std::uint16_t>(AircraftOpcode::EXCITE_MODE), payload, resp);
}

} // namespace

PERF_TEST(AircraftStep, PlantIdle) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[AircraftStep] rig init failed\n");
    return;
  }
  perf.warmup([&] { rig.aircraft.aircraftStep(); });
  auto result = perf.throughputLoop([&] { rig.aircraft.aircraftStep(); }, "plant_idle");
  std::printf("\n[AircraftStep] aircraftStep, trace idle: %.0f steps/s (%.3f us/step)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_TEST(AircraftStep, PlantTraceArmed) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[AircraftStep] rig init failed\n");
    return;
  }
  // A real component log (as the executive attaches one) so
  // telemetryTick drains the buffer at the demo's 1 Hz cadence; the
  // scratch log directory is removed afterwards.
  const std::filesystem::path LOG_DIR = std::filesystem::temp_directory_path() /
                                        ("aircraft_step_ptest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(LOG_DIR);
  rig.aircraft.initComponentLog(LOG_DIR);

  armTrace(rig.aircraft);
  std::uint32_t tick = 0;
  perf.warmup([&] { rig.aircraft.aircraftStep(); });
  auto result = perf.throughputLoop(
      [&] {
        rig.aircraft.aircraftStep();
        if (++tick % 100u == 0u) {
          (void)rig.aircraft.telemetryTick();
          if (rig.aircraft.activeExcitation() == Aircraft::ExciteMode::NONE) {
            armTrace(rig.aircraft);
          }
        }
      },
      "plant_trace_armed");
  std::printf("\n[AircraftStep] aircraftStep, trace armed (drained 1 Hz): %.0f steps/s "
              "(%.3f us/step), pending=%zu\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond,
              rig.aircraft.modeTracePending());
  std::error_code ec;
  std::filesystem::remove_all(LOG_DIR, ec);
}

PERF_TEST(AircraftStep, ClosedLoopTick) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[AircraftStep] rig init failed\n");
    return;
  }
  perf.warmup([&] { rig.closedLoopTick(); });
  auto result = perf.throughputLoop([&] { rig.closedLoopTick(); }, "closed_loop_tick");
  std::printf("\n[AircraftStep] controllerStep + 4 aircraftStep: %.0f ticks/s (%.3f us/tick)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
