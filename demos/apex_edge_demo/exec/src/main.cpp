/**
 * @file main.cpp
 * @brief POSIX entry point for the edge compute GPU demonstration.
 *
 * Creates and runs an ApexExecutive with GPU workload models.
 * All configuration (frequencies, thread affinity, GPU task params)
 * is driven by TPRM files.
 *
 * Usage:
 *   ApexEdgeDemo --config <master.tprm packed by the build> --shutdown-after 10
 */

#include "demos/apex_edge_demo/exec/inc/EdgeExecutive.hpp"

#include <iostream>
#include <system_error>

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {

  std::filesystem::path exec(argv[0]);

  // Filesystem root: --fs-root <path> or default ".apex_fs"
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
    return -1;
  }

  // Full argument list, --fs-root included: the executive ignores
  // unknown flags and replays these args on a RELOAD_EXECUTIVE
  // restart, which must come back up on the same filesystem root.
  std::vector<std::string> args(argv + 1, argv + argc);

  appsim::exec::EdgeExecutive edge(exec, args, rootfs);

  int status = edge.init();
  if (status != 0) {
    std::cerr << "Init failed with status: " << status << std::endl;
    return status;
  }

  static_cast<void>(edge.run());

  return 0;
}
