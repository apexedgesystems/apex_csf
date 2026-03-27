/**
 * @file main.cpp
 * @brief POSIX entry point for the HIL flight demonstration.
 *
 * Creates and runs an ApexExecutive with the HilPlantModel.
 * All configuration (frequencies, thread affinity, plant parameters)
 * is driven by TPRM files.
 *
 * Usage:
 *   ApexHilDemo --config tprm/master.tprm --shutdown-after 10
 */

#include "apps/apex_hil_demo/exec/inc/HilExecutive.hpp"

#include <iostream>
#include <system_error>

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {

  // Program path
  std::filesystem::path exec(argv[0]);

  // Determine filesystem root:
  //   --fs-root <path>   Use specified path (relative to CWD or absolute).
  //   (default)          CWD-relative ".apex_fs".
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

  // Program arguments (excluding --fs-root which is consumed here)
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--fs-root" && i + 1 < argc) {
      ++i; // skip value
      continue;
    }
    args.emplace_back(argv[i]);
  }

  // Create HIL executive (with plant model)
  appsim::exec::HilExecutive hil(exec, args, rootfs);

  // Initialize (loads TPRM, registers components, configures scheduler)
  int status = hil.init();
  if (status != 0) {
    std::cerr << "Init failed with status: " << status << std::endl;
    return status;
  }

  // Run (clock + task execution + watchdog)
  static_cast<void>(hil.run());

  return 0;
}
