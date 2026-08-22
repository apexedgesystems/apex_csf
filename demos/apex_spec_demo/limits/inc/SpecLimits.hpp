// Generated once by cdef_gen --stub from the SpecLimits spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_LIM_HPP
#define APEX_SPEC_STUB_SPEC_LIM_HPP
/**
 * @file SpecLimits.hpp
 * @brief Constraint-torture model.
 *
 * Every constraint kind the rails support appears on its tunables
 * (min-only, max-only, band, step, allowed list, array inheritance).
 * The model itself accumulates Nudge deltas into a value clamped to
 * [0, banded], counting rejects -- so both the TPRM-load rails and a
 * user-code guard are observable from the checkout.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecLimitsCmdBase_auto.hpp"
#include "SpecLimitsState_auto.hpp"
#include "SpecLimitsTunableParams_auto.hpp"

#include <cstdint>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecLimits final : public SpecLimitsCmdBase<system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 216;
  static constexpr const char* COMPONENT_NAME = "SpecLimits";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_LIM"; }

  SpecLimits() noexcept = default;
  ~SpecLimits() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 10 Hz tick (keeps the component schedulable).
  };

  [[nodiscard]] const SpecLimitsState& state() const noexcept { return state_.get(); }

  /** @brief Periodic tick; the model is command-driven. @note RT-safe. */
  std::uint8_t step() noexcept { return 0; }

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }
    const std::filesystem::path PATH = tprmDir / tprmFilename(fullUid());
    bool loaded = false;
    if (std::filesystem::exists(PATH)) {
      loaded = paramBank_.load(
                   PATH, fullUid(), [](const SpecLimitsTunableParams&) noexcept { return true; },
                   &SPEC_LIMITS_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecLimitsTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    state_.get().value = 0.0F;
    setConfigured(true);
    return loaded;
  }

protected:
  /// Accumulate a delta; results outside [0, banded] reject and count.
  [[nodiscard]] std::uint8_t onNudge(const NudgeRequest& request) noexcept override {
    auto& s = state_.get();
    const float NEXT = s.value + request.delta;
    if (NEXT < 0.0F || NEXT > paramBank_.active().banded) {
      s.rejects += 1U;
      return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);
    }
    s.value = NEXT;
    s.nudges += 1U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecLimits, &SpecLimits::step>(static_cast<std::uint8_t>(TaskUid::STEP), this,
                                                "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecLimitsTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecLimitsState));
    return 0;
  }

private:
  system_core::system_component::ParamBank<SpecLimitsTunableParams> paramBank_{};
  SpecLimitsTunableParams inspectParams_{};
  system_core::data::State<SpecLimitsState> state_{};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_LIM_HPP
