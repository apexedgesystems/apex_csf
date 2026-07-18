/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
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
