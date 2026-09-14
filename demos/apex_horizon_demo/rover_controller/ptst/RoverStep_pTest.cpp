/**
 * @file RoverStep_pTest.cpp
 * @brief Per-tick cost of the rover plant and controller.
 *
 * The 100 Hz vehicleStep is the demo's RT path: kinematics, the
 * terrain clamp and slope samples, the (decimated) lidar sweep, the
 * lamp strobe, the frame stamping, and -- while a sequence runs --
 * the 20 Hz trace capture. The loops isolate what the branch added:
 * the plant idle at 100 Hz (lidar every 10th step as the demo runs),
 * the same step with both lamps strobing and a sequence tracing
 * (drained through a real component log at 1 Hz), and the 10 Hz
 * controller step in WAYPOINT mode.
 */

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicle.hpp"
#include "demos/apex_horizon_demo/ground_vehicle/inc/GroundVehicleCommand.hpp"
#include "demos/apex_horizon_demo/rover_controller/inc/RoverController.hpp"
#include "src/bench/inc/Perf.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBody.hpp"
#include "src/sim/environment/celestial_body/inc/CelestialBodyData.hpp"

namespace {

using appsim::ground_vehicle::GroundVehicle;
using appsim::ground_vehicle::RoverOpcode;
using appsim::rover_controller::DriveMode;
using appsim::rover_controller::RoverController;
using sim::environment::AtmosphereFidelity;
using sim::environment::Body;
using sim::environment::GravityFidelity;
using sim::environment::TerrainFidelity;
using sim::environment::celestial_body::CelestialBody;
using sim::environment::celestial_body::CelestialBodyTunables;

CelestialBodyTunables analyticEarth() {
  CelestialBodyTunables t{};
  t.body = Body::EARTH;
  t.gravity_fidelity = GravityFidelity::J2;
  t.terrain_fidelity = TerrainFidelity::ELLIPSOID;
  t.atmosphere_fidelity = AtmosphereFidelity::EXPONENTIAL;
  return t;
}

/// The demo's composition on the closed-loop suite's analytic Earth.
struct Rig {
  CelestialBody earth;
  GroundVehicle rover;
  RoverController ctl;

  bool init() {
    earth.tunables().set(analyticEarth());
    if (earth.init() != 0u) {
      return false;
    }
    auto& p = rover.tunables().get();
    p.step_hz = 100u;
    p.lidar_divisor = 10u;
    p.init_from_grid = 1u;
    rover.setBody(&earth);
    ctl.setVehicle(&rover);
    ctl.tunables().get().boot_mode = static_cast<std::uint8_t>(DriveMode::HOLD);
    rover.setDriveCommand(ctl.driveCommand());
    (void)ctl.controllerStep();
    (void)rover.vehicleStep();
    return true;
  }

  std::uint8_t send(RoverOpcode op, const std::vector<std::uint8_t>& bytes) {
    apex::compat::rospan<std::uint8_t> payload(bytes.data(), bytes.size());
    std::vector<std::uint8_t> resp;
    return rover.handleCommand(static_cast<std::uint16_t>(op), payload, resp);
  }
};

} // namespace

PERF_TEST(RoverStep, PlantIdle100Hz) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[RoverStep] rig init failed\n");
    return;
  }
  perf.warmup([&] { rig.rover.vehicleStep(); });
  auto result = perf.throughputLoop([&] { rig.rover.vehicleStep(); }, "plant_idle");
  std::printf("\n[RoverStep] vehicleStep idle (lidar every 10th): %.0f steps/s (%.3f us/step)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_TEST(RoverStep, PlantLampsAndTrace) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[RoverStep] rig init failed\n");
    return;
  }
  // A real component log so the 1 Hz drain runs as the executive's
  // does; the scratch directory is removed afterwards.
  const std::filesystem::path LOG_DIR =
      std::filesystem::temp_directory_path() / ("rover_step_ptest_" + std::to_string(::getpid()));
  std::filesystem::create_directories(LOG_DIR);
  rig.rover.initComponentLog(LOG_DIR);
  (void)rig.send(RoverOpcode::SET_LED, {1u, 2u, 5u});   // green 10 Hz
  (void)rig.send(RoverOpcode::SET_LED, {2u, 1u, 2u});   // red 1 Hz
  (void)rig.send(RoverOpcode::SET_SEQ_STATE, {3u, 2u}); // a sequence is running: trace on
  std::uint32_t tick = 0;
  perf.warmup([&] { rig.rover.vehicleStep(); });
  auto result = perf.throughputLoop(
      [&] {
        rig.rover.vehicleStep();
        if (++tick % 100u == 0u) {
          (void)rig.rover.telemetryTick();
        }
      },
      "plant_lamps_trace");
  std::printf("\n[RoverStep] vehicleStep with both lamps strobing + trace (drained 1 Hz): "
              "%.0f steps/s (%.3f us/step), pending=%zu\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond, rig.rover.seqTracePending());
  std::error_code ec;
  std::filesystem::remove_all(LOG_DIR, ec);
}

PERF_TEST(RoverStep, ControllerWaypoint10Hz) {
  UB_PERF_GUARD(perf);
  Rig rig;
  if (!rig.init()) {
    std::printf("[RoverStep] rig init failed\n");
    return;
  }
  rig.ctl.setMode(DriveMode::WAYPOINT);
  rig.ctl.setTargetAbs(500.0, 500.0); // far target: the law stays in cruise
  perf.warmup([&] { rig.ctl.controllerStep(); });
  auto result = perf.throughputLoop([&] { rig.ctl.controllerStep(); }, "controller_waypoint");
  std::printf("\n[RoverStep] controllerStep WAYPOINT: %.0f steps/s (%.3f us/step)\n",
              result.callsPerSecond, 1.0e6 / result.callsPerSecond);
}

PERF_MAIN()
