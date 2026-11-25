#ifndef APEX_OPS_DEMO_TEST_PLUGIN_HPP
#define APEX_OPS_DEMO_TEST_PLUGIN_HPP
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
 * Two versions exist (VERSION 1 and VERSION 2). The registered output
 * block holds the version and the number of times the task has run
 * since the instance was initialized, so a client reading it over
 * INSPECT sees which build its data comes from and that the build's
 * task is the one being scheduled.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

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

  /* ----------------------------- Output ----------------------------- */

  /// The registered output block.
  struct Output {
    std::uint32_t version{VERSION}; ///< The build this instance came from.
    std::uint32_t tickCount{0};     ///< Task runs since this instance was initialized.
  };

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
    const bool OK = registerData(system_core::data::DataCategory::OUTPUT, "output", &output_,
                                 sizeof(output_)) &&
                    registerTask<TestPlugin, &TestPlugin::tick>(
                        static_cast<std::uint8_t>(TaskUid::TICK), this, "tick");
    return OK ? 0 : 1;
  }

public:
  /* ----------------------------- Task Methods ----------------------------- */

  std::uint8_t tick() noexcept {
    ++output_.tickCount;
    return 0;
  }

  [[nodiscard]] std::uint32_t tickCount() const noexcept { return output_.tickCount; }

  /* ----------------------------- TPRM ----------------------------- */

  system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& /*tprmDir*/) noexcept override {
    // No TPRM surface -- nothing to ingest.
    setConfigured(true);
    return system_core::system_component::TprmIngest::NONE;
  }

private:
  Output output_{};
};

} // namespace test

#endif // APEX_OPS_DEMO_TEST_PLUGIN_HPP
