// Generated once by cdef_gen --stub from the SpecActuator spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_ACT_HPP
#define APEX_SPEC_STUB_SPEC_ACT_HPP
/**
 * @file SpecActuator.hpp
 * @brief Proto-authored slewing actuator model.
 *
 * Model: a position slews toward a commanded target at a tunable rate
 * limit and settles inside a hold band. Move sets the target (with a
 * range guard), Halt freezes the target at the current position.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecActuatorSpecBase_auto.hpp"

#include <cmath>
#include <cstdint>

namespace appsim {
namespace spec {

class SpecActuator final
    : public SpecActuatorSpecBase<SpecActuator, system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 213;
  static constexpr const char* COMPONENT_NAME = "SpecActuator";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_ACT"; }

  SpecActuator() noexcept = default;
  ~SpecActuator() override = default;

  /** @brief Advance the slew one 50 Hz tick. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    const auto& p = paramBank_.active();

    const float STEP_MAX = p.rateLimit * DT;
    const float ERROR = s.target - s.position;
    if (ERROR > STEP_MAX) {
      s.position += STEP_MAX;
    } else if (ERROR < -STEP_MAX) {
      s.position -= STEP_MAX;
    } else {
      s.position = s.target;
    }

    auto& out = output_.get();
    out.position = s.position;
    out.sequence += 1U;
    return 0;
  }

protected:
  /// Position and target boot from the published parameter set.
  void onParamsLoaded() noexcept override {
    auto& s = state_.get();
    s.position = paramBank_.active().startPosition;
    s.target = s.position;
  }

  /// Command a new target; out-of-range or non-finite targets reject.
  [[nodiscard]] std::uint8_t onMove(const MoveRequest& request) noexcept override {
    auto& s = state_.get();
    if (!std::isfinite(request.position) || request.position > POSITION_LIMIT ||
        request.position < -POSITION_LIMIT) {
      s.rejects += 1U;
      return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);
    }
    s.target = request.position;
    s.moves += 1U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Stop where we are: the target becomes the current position.
  [[nodiscard]] std::uint8_t onHalt() noexcept override {
    auto& s = state_.get();
    s.target = s.position;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Position + settling snapshot.
  [[nodiscard]] std::uint8_t onGetPosition(PositionResponse& response) noexcept override {
    const auto& s = state_.get();
    response.position = s.position;
    const float BAND = paramBank_.active().holdBand;
    response.moving = (std::fabs(s.target - s.position) > BAND) ? 1U : 0U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  static constexpr float DT = 0.02F;               ///< 50 Hz step period [s].
  static constexpr float POSITION_LIMIT = 1000.0F; ///< Legal |target| bound [units].
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_ACT_HPP
