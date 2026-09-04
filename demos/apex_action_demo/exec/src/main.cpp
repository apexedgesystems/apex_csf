/**
 * @file main.cpp
 * @brief Entry point for the ApexActionDemo application.
 *
 * Creates the action demo executive, initializes all components, and runs
 * the main execution loop. Demonstrates the action engine's observe-and-react
 * pipeline with DataTransform fault injection.
 *
 * Usage:
 *   ApexActionDemo [--fs-root .apex_fs]
 */

#include "demos/apex_action_demo/exec/inc/ActionDemoExecutive.hpp"

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

  // Full argument list, --fs-root included: the executive ignores
  // unknown flags and replays these args on a RELOAD_EXECUTIVE
  // restart, which must come back up on the same filesystem root.
  std::vector<std::string> args(argv + 1, argv + argc);

  appsim::exec::ActionDemoExecutive app(exec, args, rootfs);

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
