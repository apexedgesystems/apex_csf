// Generated once by cdef_gen --stub from the SpecProtoMax spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_PMX_HPP
#define APEX_SPEC_STUB_SPEC_PMX_HPP
/**
 * @file SpecProtoMax.hpp
 * @brief Maximal-profile proto-authored model.
 *
 * The tunable struct comes from spec_proto_max.proto, which uses
 * every feature the apex profile accepts. The step XORs the active
 * tunable bytes into a checksum so INSPECT + Report prove every lane
 * loaded from the TPRM intact -- the proto-first mirror of
 * SpecMatrix.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include "SpecProtoMaxCmdBase_auto.hpp"
#include "SpecProtoMaxState_auto.hpp"
#include "SpecProtoMaxTunableParams_auto.hpp"

#include <cstdint>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecProtoMax final : public SpecProtoMaxCmdBase<system_core::system_component::SwModelBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 217;
  static constexpr const char* COMPONENT_NAME = "SpecProtoMax";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_PMX"; }

  SpecProtoMax() noexcept = default;
  ~SpecProtoMax() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 10 Hz checksum refresh.
  };

  [[nodiscard]] const SpecProtoMaxState& state() const noexcept { return state_.get(); }

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
                   PATH, fullUid(), [](const SpecProtoMaxTunableParams&) noexcept { return true; },
                   &SPEC_PROTO_MAX_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecProtoMaxTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    setConfigured(true);
    return loaded;
  }

protected:
  /// Return the checksum/step snapshot.
  [[nodiscard]] std::uint8_t onReport(ReportResponse& response) noexcept override {
    const auto& s = state_.get();
    response.checksum = s.checksum;
    response.stepCount = s.stepCount;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecProtoMax, &SpecProtoMax::step>(static_cast<std::uint8_t>(TaskUid::STEP), this,
                                                    "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecProtoMaxTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecProtoMaxState));
    return 0;
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

  system_core::system_component::ParamBank<SpecProtoMaxTunableParams> paramBank_{};
  SpecProtoMaxTunableParams inspectParams_{};
  system_core::data::State<SpecProtoMaxState> state_{};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_PMX_HPP
