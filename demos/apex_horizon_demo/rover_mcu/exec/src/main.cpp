/**
 * @file main.cpp
 * @brief Entry point for the ApexRoverMcuDemo application.
 *
 * Spawns the executive, registers the Earth world, the GroundVehicle,
 * its RoverController, and the ShmRingBridge, then runs the 100 Hz
 * loop: the controller steps at 10 Hz, the rover at 100 Hz over Earth's
 * terrain, and the bridge streams the 256-byte ROVR/2 frame to the
 * /horizon_rover shared-memory ring every tick while draining the
 * reverse ring into the command bus. Sequences (the demo's A->B tours,
 * lamp actions, halts) live in the TPRM catalog and start by id. Runs
 * headless just as happily.
 *
 * Usage:
 *   ApexRoverMcuDemo [--fs-root .apex_fs]
 */

#include "demos/apex_horizon_demo/rover_mcu/exec/inc/RoverMcuExecutive.hpp"

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

  appsim::rover_mcu::RoverMcuExecutive app(exec, args, rootfs);

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
