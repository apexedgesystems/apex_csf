// Generated once by cdef_gen --stub from the SpecSensor spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_SNS_HPP
#define APEX_SPEC_STUB_SPEC_SNS_HPP
/**
 * @file SpecSensor.hpp
 * @brief Spec-driven environment sensor model.
 *
 * Model: a measurement drifting away from a calibrated reference at a
 * tunable rate with tunable noise. MEASURE samples; FAULT_INJECT
 * (reachable only from MEASURE) adds a bias spike so downstream
 * monitoring has something to catch; IDLE holds the last value.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecSensorSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecSensor final
    : public SpecSensorSpecBase<SpecSensor, system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 212;
  static constexpr const char* COMPONENT_NAME = "SpecSensor";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_SNS"; }

  SpecSensor() noexcept = default;
  ~SpecSensor() override = default;

  enum class Mode : std::uint8_t { IDLE = 0, MEASURE = 1, FAULT_INJECT = 2 };

  /** @brief Advance the model one 50 Hz tick. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    const auto& p = paramBank_.active();

    s.elapsed += DT;
    if (s.mode == static_cast<std::uint8_t>(Mode::IDLE)) {
      return 0;
    }

    s.drift += p.driftRate * DT;

    // Deterministic LCG noise in [-noiseAmplitude, +noiseAmplitude].
    noiseSeed_ = noiseSeed_ * 1664525U + 1013904223U;
    const float UNIT =
        (static_cast<float>(noiseSeed_ >> 8) / static_cast<float>(0x00FFFFFF)) * 2.0F - 1.0F;

    float value = p.referenceValue + s.drift + p.noiseAmplitude * UNIT;
    if (s.mode == static_cast<std::uint8_t>(Mode::FAULT_INJECT)) {
      value += FAULT_BIAS;
    }

    auto& out = output_.get();
    out.value = value;
    out.sequence += 1U;
    s.samples += 1U;
    return 0;
  }

protected:
  /// Mode boots from the published parameter set.
  void onParamsLoaded() noexcept override { state_.get().mode = paramBank_.active().mode; }

  /// Select the operating mode; FAULT_INJECT only from MEASURE.
  [[nodiscard]] std::uint8_t onSetMode(const SetModeRequest& request) noexcept override {
    auto& s = state_.get();
    if (request.mode > static_cast<std::uint8_t>(Mode::FAULT_INJECT) ||
        (request.mode == static_cast<std::uint8_t>(Mode::FAULT_INJECT) &&
         s.mode != static_cast<std::uint8_t>(Mode::MEASURE))) {
      s.rejects += 1U;
      return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);
    }
    s.mode = request.mode;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Set a new reference, zero the drift, return the stats snapshot.
  [[nodiscard]] std::uint8_t onRecalibrate(const RecalibrateRequest& request,
                                           StatsResponse& response) noexcept override {
    auto& s = state_.get();
    auto staged = paramBank_.active();
    staged.referenceValue = request.referenceValue;
    (void)paramBank_.load(staged);
    (void)paramBank_.apply();
    inspectParams_ = paramBank_.active();
    s.drift = 0.0F;
    s.elapsed = 0.0F;
    fillStats(response);
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Return the stats snapshot.
  [[nodiscard]] std::uint8_t onGetStats(StatsResponse& response) noexcept override {
    fillStats(response);
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Zero counters and drift; mode returns to the tunable default.
  [[nodiscard]] std::uint8_t onReset() noexcept override {
    auto& s = state_.get();
    s.samples = 0U;
    s.rejects = 0U;
    s.drift = 0.0F;
    s.elapsed = 0.0F;
    s.mode = paramBank_.active().mode;
    output_.get().sequence = 0U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  static constexpr float DT = 0.02F;         ///< 50 Hz step period [s].
  static constexpr float FAULT_BIAS = 50.0F; ///< Injected fault offset.

  void fillStats(StatsResponse& response) noexcept {
    const auto& s = state_.get();
    response.samples = s.samples;
    response.rejects = s.rejects;
    response.lastValue = output_.get().value;
    response.mode = s.mode;
  }

  std::uint32_t noiseSeed_{0x5EED5EEDU};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_SNS_HPP
