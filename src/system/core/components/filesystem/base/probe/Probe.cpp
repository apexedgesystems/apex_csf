/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/components/filesystem/base/inc/IFileSystem.hpp"

namespace {

struct ProbeFileSystem final : system_core::filesystem::IFileSystem {};

} // namespace

bool probe() {
  ProbeFileSystem fs;
  system_core::filesystem::IFileSystem* iface = &fs;

  return iface != nullptr;
}
