/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built at
 *        each declared posix_cpp dialect on hosted builds -- so a regression
 *        against the support contract fails the build that owns the claim.
 *
 * Probes carry no target dependencies; the surface exercised here is the
 * dependency-light public API (the SystemLog formatting internals live in
 * the compiled library, not these headers).
 */

#include "src/system/core/infrastructure/logs/inc/AsyncLogBackend.hpp"
#include "src/system/core/infrastructure/logs/inc/LogDrainService.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

using namespace logs;

uint32_t probe() {
  const LogEntry ENTRY(std::string_view{"probe"});

  return static_cast<uint32_t>(Status::OK) + static_cast<uint32_t>(Status::ERROR_WRITE) +
         static_cast<uint32_t>(SystemLog::Level::FATAL) +
         static_cast<uint32_t>(SystemLog::Mode::ASYNC) + ENTRY.length +
         static_cast<uint32_t>(LogEntry::MAX_MSG_LEN);
}
