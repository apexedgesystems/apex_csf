/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/infrastructure/system_component/core/inc/ComponentCore.hpp"

using namespace system_core::system_component;

namespace {

struct ProbeComponent final : ComponentCore {
  [[nodiscard]] uint16_t componentId() const noexcept override { return 7; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Probe"; }
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::CORE;
  }
  [[nodiscard]] const char* label() const noexcept override { return "PROBE"; }

protected:
  [[nodiscard]] uint8_t doInit() noexcept override { return 0; }
};

} // namespace

uint32_t probe() {
  ProbeComponent comp;
  uint8_t rc = comp.init();
  comp.reset();

  return rc + comp.status() + static_cast<uint32_t>(comp.isInitialized()) + comp.fullUid();
}
