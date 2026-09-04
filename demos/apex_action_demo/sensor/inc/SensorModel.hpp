#ifndef APEX_ACTION_DEMO_SENSOR_MODEL_HPP
#define APEX_ACTION_DEMO_SENSOR_MODEL_HPP
/**
 * @file SensorModel.hpp
 * @brief Simple temperature sensor simulation for action engine demos.
 *
 * Outputs a linearly ramping temperature that wraps around at maxTemp.
 * Provides a predictable, observable data source for:
 *   - Watchpoint threshold detection (temperature > limit)
 *   - DataTransform fault injection (corrupt the temperature output)
 *   - Sequence triggering (respond to overtemp events)
 *
 * Tasks:
 *   - sensorStep (10 Hz): Advance temperature, update output
 *   - telemetry  (1 Hz):  Status logging
 */

#include "demos/apex_action_demo/sensor/inc/SensorData.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace appsim {
namespace sensor {

using system_core::data::Output;
using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::CommandResult;
using Status = system_core::system_component::Status;

/* ----------------------------- SensorModel ----------------------------- */

class SensorModel final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 210;
  static constexpr const char* COMPONENT_NAME = "SensorModel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SENSOR"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    SENSOR_STEP = 1, ///< 10 Hz temperature update.
    TELEMETRY = 2    ///< 1 Hz status logging.
  };

  /* ----------------------------- Construction ----------------------------- */

  SensorModel() noexcept = default;
  ~SensorModel() override = default;

  /* ----------------------------- Public Accessors ----------------------------- */

  [[nodiscard]] const SensorOutput& sensorOutput() const noexcept { return output_.get(); }
  [[nodiscard]] const SensorState& state() const noexcept { return state_.get(); }

  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Advance temperature by ramp rate (10 Hz).
   * @return 0 on success.
   * @note RT-safe: bounded float math only.
   */
  std::uint8_t sensorStep() noexcept {
    auto& s = state_.get();
    auto& out = output_.get();
    const auto& p = tunableParams_.get();

    out.temperature += p.rampRate;
    out.temperatureRate = p.rampRate;

    // Wrap around at max
    if (out.temperature >= p.maxTemp) {
      out.temperature = p.initialTemp;
      ++s.wrapCount;
    }

    // Overtemp flag
    out.overtemp = (out.temperature > 100.0F) ? 1 : 0;

    // Copy native + byte-swap for cross-platform wire format demo
    outputPair_.native = out;
    endianProxy_.resolve();

    ++s.stepCount;
    return 0;
  }

  /**
   * @brief Log telemetry status (1 Hz).
   * @return 0 on success.
   * @note NOT RT-safe: uses fmt::format.
   */
  std::uint8_t telemetry() noexcept {
    const auto& out = output_.get();
    const auto& s = state_.get();

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("temp={:.1f} rate={:.2f} overtemp={} steps={} wraps={}",
                                     out.temperature, out.temperatureRate, out.overtemp,
                                     s.stepCount, s.wrapCount));
    }
    return 0;
  }

  /* ----------------------------- Command Handling ----------------------------- */

  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    constexpr std::uint16_t GET_STATS = 0x0100;

    switch (opcode) {
    case GET_STATS: {
      const auto& out = output_.get();
      const auto& s = state_.get();
      const auto& p = tunableParams_.get();

      SensorHealthTlm tlm{};
      tlm.temperature = out.temperature;
      tlm.rampRate = p.rampRate;
      tlm.stepCount = s.stepCount;
      tlm.wrapCount = s.wrapCount;
      tlm.overtemp = out.overtemp;
      response.resize(sizeof(tlm));
      std::memcpy(response.data(), &tlm, sizeof(tlm));
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    default:
      return SwModelBase::handleCommand(opcode, payload, response);
    }
  }

  /* ----------------------------- TPRM ----------------------------- */

  /**
   * @brief Ingest the packed sensor payload, enforcing the spec hash.
   */
  [[nodiscard]] system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    namespace sc = system_core::system_component;
    const std::filesystem::path PATH = tprmDir / sc::SystemComponentBase::tprmFilename(fullUid());
    std::error_code ec;
    if (!std::filesystem::exists(PATH, ec)) {
      return TprmIngest::DEFAULTS;
    }
    SensorTunableParams loaded{};
    const auto CHECK =
        sc::readTprmPayload(PATH, fullUid(), loaded, &SENSOR_TUNABLE_PARAMS_LAYOUT_HASH);
    if (CHECK != sc::TprmPayloadCheck::OK) {
      if (auto* log = componentLog()) {
        log->error(label(), sc::toFaultCode(CHECK),
                   fmt::format("TPRM rejected ({}): {}", sc::toString(CHECK), PATH.string()));
      }
      return TprmIngest::REJECTED;
    }
    tunableParams_.get() = loaded;
    return TprmIngest::LOADED;
  }

  /** @brief Staged-payload verification enforces the spec hash. */
  [[nodiscard]] const std::uint32_t* expectedLayoutHash() const noexcept override {
    return &SENSOR_TUNABLE_PARAMS_LAYOUT_HASH;
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Set initial temperature
    output_.get().temperature = tunableParams_.get().initialTemp;

    // Register tasks
    registerTask<SensorModel, &SensorModel::sensorStep>(
        static_cast<std::uint8_t>(TaskUid::SENSOR_STEP), this, "sensorStep");
    registerTask<SensorModel, &SensorModel::telemetry>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    // Register data for registry
    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(SensorTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(SensorState));
    registerData(DataCategory::OUTPUT, "output", &output_.get(), sizeof(SensorOutput));
    registerData(DataCategory::INPUT, "outputPair", &outputPair_, sizeof(SensorOutputWithSwap));

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

private:
  /* ----------------------------- Data Members ----------------------------- */

  TunableParam<SensorTunableParams> tunableParams_{};
  State<SensorState> state_{};
  Output<SensorOutput> output_{};
  SensorOutputWithSwap outputPair_{}; ///< Native + swapped pair for atomic INSPECT.
  system_core::data_proxy::EndiannessProxy<SensorOutput, true> endianProxy_{&outputPair_.native,
                                                                            &outputPair_.swapped};
};

} // namespace sensor
} // namespace appsim

#endif // APEX_ACTION_DEMO_SENSOR_MODEL_HPP
