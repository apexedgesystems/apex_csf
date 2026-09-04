/**
 * @file main.cpp
 * @brief Entry point for the ApexAircraftAtmoDemo application.
 *
 * Spawns the executive, registers the atmosphere-only Earth, the
 * 6DOF Aircraft, its six-loop AircraftController, and the
 * ShmRingBridge, then runs the 50 Hz loop: the aircraft flies closed
 * loop at cruise and the bridge carries the ACFT/1 link on
 * /horizon_aircraft — 256-byte frames out, APROTO commands in. Runs
 * headless just as happily — with no consumer attached the bridge
 * back-pressures, command ingress stays live, and the sim is
 * unaffected.
 *
 * Usage:
 *   ApexAircraftAtmoDemo [--fs-root .apex_fs]
 */

#include "demos/apex_horizon_demo/aircraft_atmo/exec/inc/AircraftAtmoExecutive.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

int main(int argc, char* argv[]) {
  std::filesystem::path exec(argv[0]);

  // Parse --fs-root (default: ".apex_fs")
  std::filesystem::path rootfs(".apex_fs");
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string_view(argv[i]) == "--fs-root") {
      rootfs = argv[i + 1];
      break;
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(rootfs, ec);
  if (ec) {
    std::cerr << "Error creating filesystem: " << ec.message() << std::endl;
    return EXIT_FAILURE;
  }

  // Collect args (excluding --fs-root, which is consumed here).
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--fs-root" && i + 1 < argc) {
      ++i;
      continue;
    }
    args.emplace_back(argv[i]);
  }

  appsim::aircraft_atmo::AircraftAtmoExecutive app(exec, args, rootfs);

  const int STATUS = app.init();
  if (STATUS != 0) {
    std::cerr << "Init failed with status: " << STATUS << std::endl;
    return STATUS;
  }

  // A refused boot (ingest policy, init failure) must be visible to
  // scripts and supervisors as a nonzero exit.
  const auto RESULT = app.run();
  return RESULT == executive::RunResult::SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
