// Generated once by cdef_gen --stub from the SpecChannel spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_CHN_HPP
#define APEX_SPEC_STUB_SPEC_CHN_HPP
/**
 * @file SpecChannel.hpp
 * @brief Multi-instance ramp channel.
 *
 * One spec, several registered instances: each ramps
 * value = gain * elapsed + offset from its own per-instance TPRM
 * (same layout hash, different authored values).
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecChannelSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecChannel final
    : public SpecChannelSpecBase<SpecChannel, system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 218;
  static constexpr const char* COMPONENT_NAME = "SpecChannel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_CHN"; }

  SpecChannel() noexcept = default;
  ~SpecChannel() override = default;

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

protected:
  /// Restart the ramp.
  [[nodiscard]] std::uint8_t onZero() noexcept override {
    auto& s = state_.get();
    s.elapsed = 0.0F;
    s.zeros += 1U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  static constexpr float DT = 0.02F; ///< 50 Hz step period [s].
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_CHN_HPP
