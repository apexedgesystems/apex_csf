// Generated once by cdef_gen --stub from the SpecChannel spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_CHN_HPP
#define APEX_SPEC_STUB_SPEC_CHN_HPP
/**
 * @file SpecChannel.hpp
 * @brief Multi-instance ramp channel.
 *
 * One spec, several registered instances: each ramps
 * value = gain * elapsed + offset from its own per-instance TPRM
 * (same layout hash, different authored values), carrying its channel
 * tag. INSPECT across instances proves per-instance configuration
 * through the spec path.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecChannelCmdBase_auto.hpp"
#include "SpecChannelOutput_auto.hpp"
#include "SpecChannelState_auto.hpp"
#include "SpecChannelTunableParams_auto.hpp"

#include <cstdint>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecChannel final : public SpecChannelCmdBase<system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 218;
  static constexpr const char* COMPONENT_NAME = "SpecChannel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_CHN"; }

  SpecChannel() noexcept = default;
  ~SpecChannel() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 50 Hz ramp step.
  };

  [[nodiscard]] const SpecChannelOutput& output() const noexcept { return output_.get(); }

  /** @brief Advance the ramp one 50 Hz tick. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    const auto& p = paramBank_.active();
    s.elapsed += DT;
    auto& out = output_.get();
    out.value = p.gain * s.elapsed + p.offset;
    out.sequence += 1U;
    return 0;
  }

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }
    const std::filesystem::path PATH = tprmDir / tprmFilename(fullUid());
    bool loaded = false;
    if (std::filesystem::exists(PATH)) {
      loaded = paramBank_.load(
                   PATH, fullUid(), [](const SpecChannelTunableParams&) noexcept { return true; },
                   &SPEC_CHANNEL_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecChannelTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    setConfigured(true);
    return loaded;
  }

protected:
  /// Restart the ramp.
  [[nodiscard]] std::uint8_t onZero() noexcept override {
    auto& s = state_.get();
    s.elapsed = 0.0F;
    s.zeros += 1U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecChannel, &SpecChannel::step>(static_cast<std::uint8_t>(TaskUid::STEP), this,
                                                  "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecChannelTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecChannelState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(SpecChannelOutput));
    return 0;
  }

private:
  static constexpr float DT = 0.02F; ///< 50 Hz step period [s].

  system_core::system_component::ParamBank<SpecChannelTunableParams> paramBank_{};
  SpecChannelTunableParams inspectParams_{};
  system_core::data::State<SpecChannelState> state_{};
  system_core::data::Output<SpecChannelOutput> output_{};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_CHN_HPP
