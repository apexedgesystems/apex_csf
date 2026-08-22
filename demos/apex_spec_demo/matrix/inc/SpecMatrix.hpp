// Generated once by cdef_gen --stub from the SpecMatrix spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_MTX_HPP
#define APEX_SPEC_STUB_SPEC_MTX_HPP
/**
 * @file SpecMatrix.hpp
 * @brief SUPPORT-tier full-vocabulary checksum component.
 *
 * The tunable struct spans every spec type lane (all integer widths,
 * both floats, bool, arrays, bounded string, string array, byte
 * buffer). The step XORs the active tunable bytes into a checksum, so
 * INSPECT + GetSnapshot prove every lane loaded from the TPRM intact.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SupportComponentBase.hpp"

#include "SpecMatrixCmdBase_auto.hpp"
#include "SpecMatrixState_auto.hpp"
#include "SpecMatrixTunableParams_auto.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecMatrix final
    : public SpecMatrixCmdBase<system_core::system_component::SupportComponentBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 215;
  static constexpr const char* COMPONENT_NAME = "SpecMatrix";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_MTX"; }

  SpecMatrix() noexcept = default;
  ~SpecMatrix() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 1 Hz checksum refresh.
  };

  [[nodiscard]] const SpecMatrixState& state() const noexcept { return state_.get(); }

  /** @brief Recompute the tunable checksum. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    s.stepCount += 1U;
    s.checksum = checksumOf(paramBank_.active());
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
                   PATH, fullUid(), [](const SpecMatrixTunableParams&) noexcept { return true; },
                   &SPEC_MATRIX_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecMatrixTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    setConfigured(true);
    return loaded;
  }

protected:
  /// Return the current checksum/step snapshot.
  [[nodiscard]] std::uint8_t onGetSnapshot(MatrixSnapshot& response) noexcept override {
    const auto& s = state_.get();
    response.checksum = s.checksum;
    response.stepCount = s.stepCount;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecMatrix, &SpecMatrix::step>(static_cast<std::uint8_t>(TaskUid::STEP), this,
                                                "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecMatrixTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecMatrixState));
    return 0;
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

  system_core::system_component::ParamBank<SpecMatrixTunableParams> paramBank_{};
  SpecMatrixTunableParams inspectParams_{};
  system_core::data::State<SpecMatrixState> state_{};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_MTX_HPP
