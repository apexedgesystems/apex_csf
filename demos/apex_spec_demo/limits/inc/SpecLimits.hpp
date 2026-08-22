// Generated once by cdef_gen --stub from the SpecLimits spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_LIM_HPP
#define APEX_SPEC_STUB_SPEC_LIM_HPP
/**
 * @file SpecLimits.hpp
 * @brief Constraint-torture model.
 *
 * Every constraint kind the rails support appears on its tunables.
 * The model accumulates Nudge deltas into a value clamped to
 * [0, banded], counting rejects -- both the TPRM-load rails and a
 * user-code guard are observable from the checkout.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecLimitsSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecLimits final
    : public SpecLimitsSpecBase<SpecLimits, system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 216;
  static constexpr const char* COMPONENT_NAME = "SpecLimits";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_LIM"; }

  SpecLimits() noexcept = default;
  ~SpecLimits() override = default;

  /** @brief Periodic tick; the model is command-driven. @note RT-safe. */
  std::uint8_t step() noexcept { return 0; }

protected:
  /// The accumulator restarts with each published parameter set.
  void onParamsLoaded() noexcept override { state_.get().value = 0.0F; }

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
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_LIM_HPP
