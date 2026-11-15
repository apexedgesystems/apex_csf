#ifndef APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
/**
 * @file ReloadTarget.hpp
 * @brief A model that writes one line to its component log when it
 *        initializes, for the RELOAD_LIBRARY tests: the test links one
 *        instance and a plugin library provides its replacement.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <cstdint>
#include <string>

namespace executive {
namespace test {

class ReloadTarget final : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID = 251;
  static constexpr const char* COMPONENT_NAME = "ReloadTarget";

  /// ORIGIN says where this instance came from; it is part of the start-up line.
  explicit ReloadTarget(const char* origin = "loaded from a library") noexcept : origin_(origin) {}

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "RELOAD_TARGET"; }

  /// The line an instance of ORIGIN writes when it initializes.
  [[nodiscard]] static std::string startupLine(const char* origin) {
    return std::string("ReloadTarget ") + origin + ": started";
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    if (auto* log = componentLog()) {
      (void)log->info(label(), startupLine(origin_));
    }
    return 0;
  }

private:
  const char* origin_;
};

} // namespace test
} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
