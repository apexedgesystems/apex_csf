/**
 * @file main.cpp
 * @brief Entry point for the ApexRoverTerrainDemo application.
 *
 * Spawns the executive, registers the Earth/Moon world (bodies +
 * probes), the GroundVehicle, and the ShmRingBridge, then runs the
 * 50 Hz loop: the rover steps at 10 Hz over Earth's terrain and the
 * bridge streams its 256-byte ROVR/1 frame to the /horizon_rover
 * shared-memory ring for an out-of-process visualizer. Runs headless
 * just as happily — with no consumer attached the bridge idles and the
 * sim is unaffected.
 *
 * Usage:
 *   ApexRoverTerrainDemo [--fs-root .apex_fs]
 */

#include "demos/apex_horizon_demo/rover_terrain/exec/inc/RoverTerrainExecutive.hpp"

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

  appsim::rover_terrain::RoverTerrainExecutive app(exec, args, rootfs);

  const int STATUS = app.init();
  if (STATUS != 0) {
    std::cerr << "Init failed with status: " << STATUS << std::endl;
    return STATUS;
  }

  static_cast<void>(app.run());
  return EXIT_SUCCESS;
}
