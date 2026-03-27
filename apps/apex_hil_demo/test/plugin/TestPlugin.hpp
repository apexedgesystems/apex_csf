#ifndef APEX_TEST_PLUGIN_HPP
#define APEX_TEST_PLUGIN_HPP
/**
 * @file TestPlugin.hpp
 * @brief Minimal component for hot-swap testing.
 *
 * This component does nothing useful. It exists purely to verify
 * the RELOAD_LIBRARY (dlopen/dlclose) pipeline end-to-end:
 *   1. Load .so via dlopen
 *   2. Resolve factory symbols
 *   3. Create component instance
 *   4. Verify componentId matches old component
 *   5. Transfer identity (instanceIndex)
 *   6. loadTprm + init
 *   7. Re-wire scheduler tasks
 *   8. Update registry
 *
 * Two versions exist (VERSION 1 and VERSION 2) to verify that
 * after hot-swap, the new version is running.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/PluginInterface.hpp"

#include <cstdint>

namespace test {

/* ----------------------------- TestPlugin ----------------------------- */

class TestPlugin final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Use componentId 250 (0xFA) -- well outside system and demo ranges.
  static constexpr std::uint16_t COMPONENT_ID = 250;
  static constexpr const char* COMPONENT_NAME = "TestPlugin";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Version ----------------------------- */

  /// Version number -- changes between v1 and v2 builds.
  static constexpr std::uint8_t VERSION = APEX_TEST_PLUGIN_VERSION;

  [[nodiscard]] std::uint8_t version() const noexcept { return VERSION; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    TICK = 1 ///< Simple counter task.
  };

  /* ----------------------------- Construction ----------------------------- */

  TestPlugin() noexcept = default;
  ~TestPlugin() override = default;

  [[nodiscard]] const char* label() const noexcept override { return "TEST_PLUGIN"; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    registerTask<TestPlugin, &TestPlugin::tick>(static_cast<std::uint8_t>(TaskUid::TICK), this,
                                                "tick");
    return 0;
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  std::uint8_t tick() noexcept {
    ++tickCount_;
    return 0;
  }

  [[nodiscard]] std::uint32_t tickCount() const noexcept { return tickCount_; }

  /* ----------------------------- TPRM ----------------------------- */

  bool loadTprm(const std::filesystem::path& /*tprmDir*/) noexcept override {
    // No TPRM needed -- accept defaults.
    setConfigured(true);
    return true;
  }

private:
  std::uint32_t tickCount_{0};
};

} // namespace test

#endif // APEX_TEST_PLUGIN_HPP
