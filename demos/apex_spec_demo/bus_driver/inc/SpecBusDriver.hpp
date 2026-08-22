// Generated once by cdef_gen --stub from the SpecBusDriver spec.
// USER-OWNED after generation: fill in the component logic. The
// generated SpecBase owns members, loadTprm, doInit, and dispatch;
// this file owns identity, task methods, and hooks.
#ifndef APEX_SPEC_STUB_SPEC_BUS_HPP
#define APEX_SPEC_STUB_SPEC_BUS_HPP
/**
 * @file SpecBusDriver.hpp
 * @brief Spec-born DRIVER-tier loopback bus.
 *
 * Endpoint-blind: the "bus" is a single-frame software loopback.
 * SendFrame stages a frame; after a tunable number of steps it is
 * "received" back and the counters advance.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DriverBase.hpp"

#include "SpecBusDriverSpecBase_auto.hpp"

#include <cstdint>

namespace appsim {
namespace spec {

class SpecBusDriver final
    : public SpecBusDriverSpecBase<SpecBusDriver, system_core::system_component::DriverBase> {
public:
  static constexpr std::uint16_t COMPONENT_ID = 214;
  static constexpr const char* COMPONENT_NAME = "SpecBusDriver";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SPEC_BUS"; }

  SpecBusDriver() noexcept = default;
  ~SpecBusDriver() override = default;

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

private:
  std::uint16_t countdown_{0};
  std::uint8_t stagedLen_{0};
};

} // namespace spec
} // namespace appsim

#endif // APEX_SPEC_STUB_SPEC_BUS_HPP
