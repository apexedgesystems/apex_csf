/**
 * @file VCanInterface.cpp
 * @brief Linux virtual CAN interface setup/teardown implementation.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/VCanInterface.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <unistd.h>
#else
#error "VCanInterface is only supported on Linux systems."
#endif

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {
namespace util {

/* -------------------------- VCanInterface Methods ------------------------- */

VCanInterface::VCanInterface(const std::string& interfaceName, bool autoTeardown, bool useSudo)
    : ifName_(interfaceName), autoTeardown_(autoTeardown), useSudo_(useSudo) {}

VCanInterface::~VCanInterface() {
  if (autoTeardown_) {
    if (!teardown()) {
      std::cerr << "vcan: warning: failed to tear down " << ifName_ << '\n';
    }
  }
}

std::string VCanInterface::cmdPrefix() const {
  return useSudo_ ? std::string("sudo ") : std::string();
}

bool VCanInterface::run(const std::string& cmd) const {
  // Redirect stdout/stderr to keep tests quiet; callers print only on hard failure.
  std::string quiet = cmd + " > /dev/null 2>&1";
  int rc = std::system(quiet.c_str());
  return (rc == 0);
}

bool VCanInterface::isPresent() const { return run(cmdPrefix() + "ip link show " + ifName_); }

[[nodiscard]] bool VCanInterface::setup() {
#ifdef __linux__
  // Best-effort: load vcan module
  (void)run(cmdPrefix() + "modprobe vcan");

  if (isPresent()) {
    // Ensure it's up
    return run(cmdPrefix() + "ip link set " + ifName_ + " up");
  }

  // Create, then bring up
  if (!run(cmdPrefix() + "ip link add dev " + ifName_ + " type vcan")) {
    std::cerr << "vcan: add failed for " << ifName_ << '\n';
    return false;
  }
  if (!run(cmdPrefix() + "ip link set " + ifName_ + " up")) {
    std::cerr << "vcan: set up failed for " << ifName_ << '\n';
    return false;
  }
  return true;
#else
  return false;
#endif
}

[[nodiscard]] bool VCanInterface::teardown() {
#ifdef __linux__
  // If not present, consider it success (idempotent)
  if (!isPresent())
    return true;

  if (!run(cmdPrefix() + "ip link delete " + ifName_)) {
    std::cerr << "vcan: delete failed for " << ifName_ << '\n';
    return false;
  }
  return true;
#else
  return false;
#endif
}

} // namespace util
} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex
