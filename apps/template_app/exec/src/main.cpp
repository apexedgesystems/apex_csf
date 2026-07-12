/**
 * @file main.cpp
 * @brief TemplateApp: the README front-page pattern, buildable.
 *
 * One schedulable model with a tunable-parameter struct, wired into a
 * minimal executive. Copy this application, rename it, and replace the
 * thermal model with your components.
 *
 * Usage:
 *   TemplateApp [--fs-root .apex_fs] [--shutdown-after N]
 */

#include "src/system/core/executive/posix/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace apps {
namespace template_app {

/* ----------------------------- ThermalModel ----------------------------- */

/**
 * @class ThermalModel
 * @brief First-order thermal plant stepped by the scheduler.
 */
class ThermalModel final : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID = 101;
  static constexpr const char* COMPONENT_NAME = "ThermalModel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "THERMAL"; }

  std::uint8_t step() noexcept {
    temperature_ += heatInput_ * dt_ - dissipation_ * (temperature_ - ambient_);
    return 0;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registerTask<ThermalModel, &ThermalModel::step>(1, this, "thermalStep");
    return 0;
  }

private:
  double temperature_{293.15};
  double heatInput_{0.0};
  double dissipation_{0.01};
  double ambient_{293.15};
  double dt_{0.01};
};

/* ----------------------------- TemplateExecutive ----------------------------- */

/**
 * @class TemplateExecutive
 * @brief Executive owning the application's components.
 */
class TemplateExecutive final : public executive::ApexExecutive {
public:
  using ApexExecutive::ApexExecutive;
  [[nodiscard]] const char* label() const noexcept override { return "TEMPLATE_EXEC"; }

protected:
  [[nodiscard]] bool registerComponents() noexcept override {
    return registerComponent(&thermal_, fileSystem().logDir());
  }

private:
  ThermalModel thermal_;
};

} // namespace template_app
} // namespace apps

/* ----------------------------- main ----------------------------- */

int main(int argc, char* argv[]) {
  std::filesystem::path exec(argv[0]);

  std::filesystem::path rootfs(".apex_fs");
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string_view(argv[i]) == "--fs-root") {
      rootfs = argv[i + 1];
      break;
    }
  }

  std::error_code ec;
  std::filesystem::create_directories(rootfs, ec);
  if (ec) {
    std::cerr << "Error creating filesystem: " << ec.message() << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--fs-root" && i + 1 < argc) {
      ++i;
      continue;
    }
    args.emplace_back(argv[i]);
  }

  apps::template_app::TemplateExecutive app(exec, args, rootfs);

  const int STATUS = app.init();
  if (STATUS != 0) {
    std::cerr << "Init failed with status: " << STATUS << std::endl;
    return STATUS;
  }

  static_cast<void>(app.run());
  return EXIT_SUCCESS;
}
