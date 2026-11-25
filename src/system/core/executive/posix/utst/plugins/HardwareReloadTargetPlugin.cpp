/**
 * @file HardwareReloadTargetPlugin.cpp
 * @brief A plugin library whose component is a hardware model under the
 *        linked ReloadTarget's component id.
 */

#include "src/system/core/executive/posix/utst/plugins/ReloadTarget.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/HwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

namespace executive {
namespace test {

/// A hardware model that asks for a transport link, as a replacement for ReloadTarget.
class HardwareReloadTarget final : public system_core::system_component::HwModelBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override {
    return ReloadTarget::COMPONENT_ID;
  }
  [[nodiscard]] const char* componentName() const noexcept override {
    return ReloadTarget::COMPONENT_NAME;
  }
  [[nodiscard]] const char* label() const noexcept override { return "RELOAD_TARGET_HW"; }
  [[nodiscard]] system_core::system_component::TransportKind
  transportKind() const noexcept override {
    return system_core::system_component::TransportKind::UNIX_STREAM;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }
};

} // namespace test
} // namespace executive

APEX_PLUGIN_FACTORY(executive::test::HardwareReloadTarget)
