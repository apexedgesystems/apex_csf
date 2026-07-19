/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/system/core/infrastructure/system_component/base/inc/CommandResult.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/ComponentType.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/IComponent.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/TransportKind.hpp"

using namespace system_core::system_component;

namespace {

struct ProbeComponent final : IComponent {
  [[nodiscard]] uint16_t componentId() const noexcept override { return 7; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Probe"; }
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::CORE;
  }
  [[nodiscard]] const char* label() const noexcept override { return "PROBE"; }
  [[nodiscard]] uint8_t init() noexcept override {
    initialized_ = true;
    return 0;
  }
  void reset() noexcept override { initialized_ = false; }
  [[nodiscard]] uint8_t status() const noexcept override { return 0; }
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }
  [[nodiscard]] uint32_t fullUid() const noexcept override { return 7u; }
  [[nodiscard]] uint8_t instanceIndex() const noexcept override { return 0; }
  [[nodiscard]] bool isRegistered() const noexcept override { return false; }

  bool initialized_{false};
};

} // namespace

uint32_t probe() {
  ProbeComponent comp;
  IComponent* iface = &comp;
  uint8_t rc = iface->init();

  return iface->fullUid() + rc + static_cast<uint32_t>(iface->isInitialized()) +
         static_cast<uint32_t>(CommandResult::SUCCESS) + static_cast<uint32_t>(Status::SUCCESS) +
         static_cast<uint32_t>(TransportKind::NONE);
}
