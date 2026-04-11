/**
 * @file main.cpp
 * @brief Entry point for the ApexOpsDemo application.
 *
 * Creates the Ops demo executive, initializes all components, and runs
 * the main execution loop. The executive handles signal-driven shutdown,
 * clock management, and task scheduling.
 *
 * Usage:
 *   ApexOpsDemo --config tprm/master.tprm [--fs-root .apex_fs] [--shutdown-after N]
 */

#include "apps/apex_ops_demo/exec/inc/OpsExecutive.hpp"

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

  // Collect args (excluding --fs-root which is consumed here)
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--fs-root" && i + 1 < argc) {
      ++i; // skip value
      continue;
    }
    args.emplace_back(argv[i]);
  }

  appsim::exec::OpsExecutive app(exec, args, rootfs);

  const int STATUS = app.init();
  if (STATUS != 0) {
    std::cerr << "Init failed with status: " << STATUS << std::endl;
    return STATUS;
  }

  static_cast<void>(app.run());
  return EXIT_SUCCESS;
}
