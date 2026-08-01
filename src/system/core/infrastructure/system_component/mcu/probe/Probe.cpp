/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/infrastructure/system_component/mcu/inc/McuComponentBase.hpp"

using namespace system_core::system_component;

namespace {

struct ProbeMcuComponent final : mcu::McuComponentBase {
  [[nodiscard]] uint16_t componentId() const noexcept override { return 7; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Probe"; }
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::DRIVER;
  }
  [[nodiscard]] const char* label() const noexcept override { return "PROBE"; }

protected:
  [[nodiscard]] uint8_t doInit() noexcept override { return 0; }
};

} // namespace

uint32_t probe() {
  ProbeMcuComponent comp;
  comp.setInstanceIndex(1);
  const uint8_t RC = comp.init();

  return comp.fullUid() + RC + static_cast<uint32_t>(comp.isInitialized()) +
         static_cast<uint32_t>(comp.isRegistered()) + comp.status();
}
