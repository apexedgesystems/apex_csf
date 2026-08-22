// Generated once by cdef_gen --stub from the SpecProtoMax spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_PMX_HPP
#define APEX_SPEC_STUB_SPEC_PMX_HPP
/**
 * @file SpecProtoMax.hpp
 * @brief Maximal-profile proto-authored model.
 *
 * The tunable struct comes from spec_proto_max.proto, which uses
 * every feature the apex profile accepts. The step XORs the active
 * tunable bytes into a checksum -- the proto-first mirror of
 * SpecMatrix.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecProtoMaxSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecProtoMax final
    : public SpecProtoMaxSpecBase<SpecProtoMax, system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 217;
  static constexpr const char* COMPONENT_NAME = "SpecProtoMax";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_PMX"; }

  SpecProtoMax() noexcept = default;
  ~SpecProtoMax() override = default;

  /** @brief Recompute the tunable checksum. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    s.stepCount += 1U;
    s.checksum = checksumOf(paramBank_.active());
    return 0;
  }

protected:
  /// Return the checksum/step snapshot.
  [[nodiscard]] std::uint8_t onReport(ReportResponse& response) noexcept override {
    const auto& s = state_.get();
    response.checksum = s.checksum;
    response.stepCount = s.stepCount;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  /// XOR of every tunable byte, folded into 32 bits by position.
  [[nodiscard]] static std::uint32_t checksumOf(const SpecProtoMaxTunableParams& p) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&p);
    std::uint32_t sum = 0U;
    for (std::size_t i = 0; i < sizeof(p); ++i) {
      sum ^= static_cast<std::uint32_t>(bytes[i]) << ((i % 4U) * 8U);
    }
    return sum;
  }
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_PMX_HPP
