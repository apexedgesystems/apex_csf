// Generated once by cdef_gen --stub from the SpecBusDriver spec.
// USER-OWNED after generation: fill in the component logic; the
// .auto headers (structs, dispatch) keep regenerating separately.
#ifndef APEX_SPEC_STUB_SPEC_BUS_HPP
#define APEX_SPEC_STUB_SPEC_BUS_HPP
/**
 * @file SpecBusDriver.hpp
 * @brief Spec-born DRIVER-tier loopback bus.
 *
 * Endpoint-blind: the "bus" is a single-frame software loopback.
 * SendFrame stages a frame; after a tunable number of steps it is
 * "received" back and the counters advance. The checkout proves
 * tx == rx round-trips through generated dispatch over DriverBase.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DriverBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"

#include "SpecBusDriverCmdBase_auto.hpp"
#include "SpecBusDriverOutput_auto.hpp"
#include "SpecBusDriverState_auto.hpp"
#include "SpecBusDriverTunableParams_auto.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>

namespace appsim {
namespace spec {

class SpecBusDriver final : public SpecBusDriverCmdBase<system_core::system_component::DriverBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 214;
  static constexpr const char* COMPONENT_NAME = "SpecBusDriver";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_BUS"; }

  SpecBusDriver() noexcept = default;
  ~SpecBusDriver() override = default;

  enum class TaskUid : std::uint8_t {
    STEP = 1, ///< 50 Hz loopback drain.
  };

  [[nodiscard]] const SpecBusDriverState& state() const noexcept { return state_.get(); }

  /** @brief Drain the loopback: a staged frame lands after its delay. @note RT-safe. */
  std::uint8_t step() noexcept {
    auto& s = state_.get();
    if (s.pending == 1U && countdown_ > 0U) {
      countdown_ -= 1U;
      if (countdown_ == 0U) {
        s.rxFrames += 1U;
        s.rxBytes += stagedLen_;
        s.pending = 0U;
        auto& out = output_.get();
        out.rxFrames = s.rxFrames;
        out.lastRxLen = stagedLen_;
      }
    }
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
                   PATH, fullUid(), [](const SpecBusDriverTunableParams&) noexcept { return true; },
                   &SPEC_BUS_DRIVER_TUNABLE_PARAMS_LAYOUT_HASH) ==
               system_core::system_component::Status::SUCCESS;
    }
    if (!loaded) {
      (void)paramBank_.load(SpecBusDriverTunableParams{});
    }
    (void)paramBank_.publishInitial();
    inspectParams_ = paramBank_.active();
    setConfigured(true);
    return loaded;
  }

protected:
  /// Stage a frame on the loopback; oversize or busy rejects.
  [[nodiscard]] std::uint8_t onSendFrame(const FrameRequest& request) noexcept override {
    auto& s = state_.get();
    const auto& p = paramBank_.active();
    if (request.length == 0U || request.length > p.maxFrameBytes ||
        request.length > sizeof(request.data) || s.pending == 1U) {
      s.rejects += 1U;
      return static_cast<std::uint8_t>(system_core::system_component::Status::ERROR_PARAM);
    }
    stagedLen_ = request.length;
    countdown_ = p.loopDelayTicks;
    s.pending = 1U;
    s.txFrames += 1U;
    s.txBytes += request.length;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Drop any staged frame.
  [[nodiscard]] std::uint8_t onFlush() noexcept override {
    state_.get().pending = 0U;
    countdown_ = 0U;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  /// Return the traffic counters.
  [[nodiscard]] std::uint8_t onGetStats(BusStats& response) noexcept override {
    const auto& s = state_.get();
    response.txFrames = s.txFrames;
    response.rxFrames = s.rxFrames;
    response.txBytes = s.txBytes;
    response.rxBytes = s.rxBytes;
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    registerTask<SpecBusDriver, &SpecBusDriver::step>(static_cast<std::uint8_t>(TaskUid::STEP),
                                                      this, "step");

    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(SpecBusDriverTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SpecBusDriverState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(SpecBusDriverOutput));
    return 0;
  }

private:
  system_core::system_component::ParamBank<SpecBusDriverTunableParams> paramBank_{};
  SpecBusDriverTunableParams inspectParams_{};
  system_core::data::State<SpecBusDriverState> state_{};
  system_core::data::Output<SpecBusDriverOutput> output_{};
  std::uint16_t countdown_{0};
  std::uint8_t stagedLen_{0};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_BUS_HPP
