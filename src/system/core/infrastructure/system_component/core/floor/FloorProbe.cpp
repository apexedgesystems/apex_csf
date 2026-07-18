/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
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
