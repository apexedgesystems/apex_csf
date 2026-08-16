// Generated once by cdef_gen --stub from the SpecActuator spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_ACT_HPP
#define APEX_SPEC_STUB_SPEC_ACT_HPP
/**
 * @file SpecActuator.hpp
 * @brief Proto-authored slewing actuator model.
 *
 * Born from actuator/spec_actuator.proto via the manifest's
 * proto_spec reference: the tunable, state, output, and command
 * payload structs plus the dispatch base live in .auto/ and
 * regenerate from the spec; this file was generated once as the
 * skeleton and is user-owned -- the slew physics and handler logic
 * below are the hand-written part of the component.
 *
 * Model: a position slews toward a commanded target at a tunable rate
 * limit and settles inside a hold band. Move sets the target (with a
 * range guard), Halt freezes the target at the current position.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecActuatorCmdBase_auto.hpp"
#include "SpecActuatorOutput_auto.hpp"
#include "SpecActuatorState_auto.hpp"
#include "SpecActuatorTunableParams_auto.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecActuator final : public SpecActuatorCmdBase<system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 213;
  static constexpr const char* COMPONENT_NAME = "SpecActuator";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_ACT"; }

  SpecActuator() noexcept = default;
  ~SpecActuator() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 50 Hz slew step.
  };

  [[nodiscard]] const SpecActuatorOutput& output() const noexcept { return output_.get(); }
  [[nodiscard]] const SpecActuatorState& state() const noexcept { return state_.get(); }

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

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }
    const std::filesystem::path PATH = tprmDir / tprmFilename(fullUid());
    bool loaded = false;
    if (std::filesystem::exists(PATH)) {
      loaded = paramBank_.load(
                   PATH, fullUid(), [](const SpecActuatorTunableParams&) noexcept { return true; },
                   &SPEC_ACTUATOR_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecActuatorTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    auto& s = state_.get();
    s.position = inspectParams_.startPosition;
    s.target = inspectParams_.startPosition;
    setConfigured(true);
    return loaded;
  }

protected:
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

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecActuator, &SpecActuator::step>(static_cast<std::uint8_t>(TaskUid::STEP), this,
                                                    "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecActuatorTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecActuatorState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(SpecActuatorOutput));
    return 0;
  }

private:
  static constexpr float DT = 0.02F;               ///< 50 Hz step period [s].
  static constexpr float POSITION_LIMIT = 1000.0F; ///< Legal |target| bound [units].

  system_core::system_component::ParamBank<SpecActuatorTunableParams> paramBank_{};
  SpecActuatorTunableParams inspectParams_{};
  system_core::data::State<SpecActuatorState> state_{};
  system_core::data::Output<SpecActuatorOutput> output_{};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_ACT_HPP
