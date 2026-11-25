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
 * Two versions exist (VERSION 1 and VERSION 2). A client tells them
 * apart, and tells that the build's task is the one being scheduled,
 * through two observations:
 *   - The registered output block holds the version. It is written
 *     before the instance is registered and never after, so INSPECT,
 *     which copies registered bytes on the command thread, reads it
 *     without synchronization.
 *   - GET_STATS returns the version and the number of times the task
 *     has run since the instance was initialized. The count is an
 *     atomic the task increments and the command loads, so it is never
 *     reached through a copy of raw bytes.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/PluginInterface.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

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

  /// The registered output block. Never written once the instance exists.
  struct Output {
    std::uint32_t version{VERSION}; ///< The build this instance came from.
  };

  /* ----------------------------- Commands ----------------------------- */

  /// Returns a Stats block.
  static constexpr std::uint16_t GET_STATS = 0x0100;

  /// The GET_STATS reply.
  struct Stats {
    std::uint32_t version{VERSION}; ///< The build that answered.
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
    tickCount_.fetch_add(1U, std::memory_order_relaxed);
    return 0;
  }

  [[nodiscard]] std::uint32_t tickCount() const noexcept {
    return tickCount_.load(std::memory_order_relaxed);
  }

  /* ----------------------------- Command Handling ----------------------------- */

  /**
   * @brief Answer GET_STATS; every other opcode goes to the base class.
   * @note The run count is a counter on its own: no other state is read
   *       with it, so relaxed ordering is enough on both sides.
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    if (opcode != GET_STATS) {
      return SwModelBase::handleCommand(opcode, payload, response);
    }
    Stats stats{};
    stats.tickCount = tickCount();
    response.resize(sizeof(stats));
    std::memcpy(response.data(), &stats, sizeof(stats));
    return static_cast<std::uint8_t>(system_core::system_component::CommandResult::SUCCESS);
  }

  /* ----------------------------- TPRM ----------------------------- */

  system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& /*tprmDir*/) noexcept override {
    // No TPRM surface -- nothing to ingest.
    setConfigured(true);
    return system_core::system_component::TprmIngest::NONE;
  }

private:
  const Output output_{};
  std::atomic<std::uint32_t> tickCount_{0};
};

} // namespace test

#endif // APEX_OPS_DEMO_TEST_PLUGIN_HPP
