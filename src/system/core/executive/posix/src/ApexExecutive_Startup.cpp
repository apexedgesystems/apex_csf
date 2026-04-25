/**
 * @file ApexExecutive_Startup.cpp
 * @brief Implementation of ApexExecutive startup thread and configuration.
 */

#include "src/system/core/executive/posix/inc/ApexExecutive_Startup.hpp"
#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <fmt/core.h>

using enum executive::Status;

namespace executive {

void ApexExecutive::startup(std::promise<std::uint8_t>&& p) noexcept {
  const auto START_TIME = std::chrono::steady_clock::now();

  sysLog_->info(label(), "Starting up...");

  switch (startupConfig_.mode) {
  case StartupConfig::AUTO: {
    if (startupConfig_.delaySeconds > 0) {
      sysLog_->info(label(),
                    fmt::format("AUTO mode: delaying {} seconds", startupConfig_.delaySeconds));
      std::this_thread::sleep_for(std::chrono::seconds(startupConfig_.delaySeconds));
    } else {
      sysLog_->info(label(), "AUTO mode: starting immediately");
    }
    break;
  }

  case StartupConfig::INTERACTIVE: {
    sysLog_->info(label(), "INTERACTIVE mode: waiting for user input...");
    std::cout << "\n=== Press ENTER to start execution ===\n" << std::endl;

    std::string input;
    std::getline(std::cin, input);

    sysLog_->info(label(), "User input received, starting execution");
    break;
  }

  case StartupConfig::SCHEDULED: {
    auto now = std::chrono::high_resolution_clock::now();
    std::int64_t nowNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

    if (startupConfig_.startAtEpochNs > nowNs) {
      std::int64_t waitNs = startupConfig_.startAtEpochNs - nowNs;
      std::int64_t waitMs = waitNs / 1'000'000;
      sysLog_->info(label(), fmt::format("SCHEDULED mode: waiting {} ms until start time", waitMs));
      std::this_thread::sleep_for(std::chrono::nanoseconds(waitNs));
    } else {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_STARTUP_TIME_PASSED),
                       "SCHEDULED start time already passed, starting immediately");
    }
    break;
  }
  }

  // Record completion timestamp for relative shutdown calculations
  auto now = std::chrono::high_resolution_clock::now();
  std::int64_t nowNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  startupCompletedNs_.store(nowNs, std::memory_order_release);

  // Signal other threads that startup is complete
  controlState_.startupRequested.store(true, std::memory_order_release);
  cvStartup_.notify_all();

  // Calculate and report startup duration
  const auto ELAPSED = std::chrono::steady_clock::now() - START_TIME;
  const auto ELAPSED_MS = std::chrono::duration_cast<std::chrono::milliseconds>(ELAPSED).count();

  sysLog_->info(label(), fmt::format("Startup completed in {} ms", ELAPSED_MS));
  p.set_value(static_cast<uint8_t>(Status::SUCCESS));
}

} // namespace executive
