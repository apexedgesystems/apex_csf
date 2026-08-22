// Generated once by cdef_gen --stub from the SpecMatrix spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_MTX_HPP
#define APEX_SPEC_STUB_SPEC_MTX_HPP
/**
 * @file SpecMatrix.hpp
 * @brief SUPPORT-tier full-vocabulary checksum component.
 *
 * The tunable struct spans every spec type lane. The step XORs the
 * active tunable bytes into a checksum, so INSPECT + GetSnapshot
 * prove every lane loaded from the TPRM intact.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SupportComponentBase.hpp"

#include "SpecMatrixSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecMatrix final
    : public SpecMatrixSpecBase<SpecMatrix, system_core::system_component::SupportComponentBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 215;
  static constexpr const char* COMPONENT_NAME = "SpecMatrix";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_MTX"; }

  SpecMatrix() noexcept = default;
  ~SpecMatrix() override = default;

  /** @brief Recompute the tunable checksum. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    s.stepCount += 1U;
    s.checksum = checksumOf(paramBank_.active());
    return 0;
  }

protected:
  /// Return the current checksum/step snapshot.
  [[nodiscard]] std::uint8_t onGetSnapshot(MatrixSnapshot& response) noexcept override {
    const auto& s = state_.get();
    response.checksum = s.checksum;
    response.stepCount = s.stepCount;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

private:
  /// XOR of every tunable byte, folded into 32 bits by position.
  [[nodiscard]] static std::uint32_t checksumOf(const SpecMatrixTunableParams& p) noexcept {
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

#endif // APEX_SPEC_STUB_SPEC_MTX_HPP
