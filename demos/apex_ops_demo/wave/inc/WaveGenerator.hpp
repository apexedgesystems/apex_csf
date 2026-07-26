#ifndef APEX_OPS_DEMO_WAVE_GENERATOR_HPP
#define APEX_OPS_DEMO_WAVE_GENERATOR_HPP
/**
 * @file WaveGenerator.hpp
 * @brief Configurable waveform generator for Ops demo telemetry.
 *
 * Generates one of five waveform types (sine, square, triangle, sawtooth,
 * composite) at a configurable frequency and amplitude. Two instances run
 * simultaneously to exercise multi-instance C2 addressing and independent
 * TPRM configuration.
 *
 * Tasks:
 *   - waveStep  (100 Hz): Phase advance, waveform computation, statistics
 *   - telemetry (1 Hz):   Status logging via componentLog()
 *
 * All waveform parameters are TPRM-configurable at runtime via RELOAD_TPRM,
 * and hot-swappable per instance through SET_PARAMS (0x0101) with rollback
 * via ROLLBACK_PARAMS (0x0102). Parameters live in a ParamBank: every
 * update stages into the inactive bank, validates, and publishes with an
 * atomic pointer swap, so the 100 Hz waveStep never observes a torn
 * parameter set and never stops ticking during a reload. A rejected set
 * is never published. Changing waveType or frequency produces immediate
 * visual feedback in C2 strip charts.
 *
 * @note waveStep is RT-safe (no allocation, bounded float math + LCG noise).
 * @note telemetry is NOT RT-safe (fmt::format I/O).
 */

#include "demos/apex_ops_demo/wave/inc/WaveGenData.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace appsim {
namespace wave {

using system_core::data::Output;
using system_core::data::State;
using system_core::system_component::CommandResult;
using system_core::system_component::ParamBank;
using Status = system_core::system_component::Status;

/* ----------------------------- Constants ----------------------------- */

static constexpr double PI = 3.14159265358979323846;
static constexpr double TWO_PI = 2.0 * PI;
static constexpr double DT = 0.01; ///< 100 Hz step period [seconds].

/* ----------------------------- WaveGenerator ----------------------------- */

class WaveGenerator final : public system_core::system_component::SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 208;
  static constexpr const char* COMPONENT_NAME = "WaveGenerator";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "WAVE_GEN"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    WAVE_STEP = 1, ///< 100 Hz waveform generation.
    TELEMETRY = 2  ///< 1 Hz status logging.
  };

  /* ----------------------------- Construction ----------------------------- */

  WaveGenerator() noexcept = default;
  ~WaveGenerator() override = default;

  /* ----------------------------- Public Accessors ----------------------------- */

  [[nodiscard]] const WaveGenOutput& waveOutput() const noexcept { return waveOutput_.get(); }
  [[nodiscard]] const WaveGenState& state() const noexcept { return state_.get(); }

  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Generate one waveform sample (100 Hz).
   *
   * Advances phase accumulator, computes output based on waveType,
   * applies noise injection, and updates peak/RMS statistics.
   *
   * @return 0 on success.
   * @note RT-safe: bounded float math, LCG noise, no allocation.
   */
  std::uint8_t waveStep() noexcept {
    auto& s = state_.get();
    const auto& p = paramBank_.active();

    // Advance phase
    s.phase += TWO_PI * static_cast<double>(p.frequency) * DT;
    if (s.phase >= TWO_PI) {
      s.phase -= TWO_PI;
    }

    // Compute base waveform
    const double PHASE = s.phase + static_cast<double>(p.phaseOffset);
    float value = 0.0F;

    switch (static_cast<WaveType>(p.waveType)) {
    case WaveType::SINE:
      value = p.amplitude * static_cast<float>(std::sin(PHASE));
      break;

    case WaveType::SQUARE: {
      const double NORM = std::fmod(PHASE, TWO_PI);
      const double THRESHOLD = PI * static_cast<double>(p.dutyCycle) * 2.0;
      value = p.amplitude * ((NORM < THRESHOLD) ? 1.0F : -1.0F);
      break;
    }

    case WaveType::TRIANGLE: {
      const double NORM = std::fmod(PHASE, TWO_PI) / TWO_PI;
      value = p.amplitude * static_cast<float>(4.0 * std::abs(NORM - 0.5) - 1.0);
      break;
    }

    case WaveType::SAWTOOTH: {
      const double NORM = std::fmod(PHASE, TWO_PI) / TWO_PI;
      value = p.amplitude * static_cast<float>(2.0 * NORM - 1.0);
      break;
    }

    case WaveType::COMPOSITE:
      value = p.amplitude * static_cast<float>(std::sin(PHASE) + 0.333 * std::sin(3.0 * PHASE));
      break;

    default:
      value = 0.0F;
      break;
    }

    // Add DC offset
    value += p.dcOffset;

    // Add noise (LCG: RT-safe, deterministic per seed)
    if (p.noiseAmplitude > 0.0F) {
      s.noiseSeed = s.noiseSeed * 1103515245U + 12345U;
      const float NOISE_NORM =
          static_cast<float>(static_cast<std::int32_t>(s.noiseSeed >> 16) & 0x7FFF) / 16383.5F -
          1.0F;
      value += p.noiseAmplitude * NOISE_NORM;
    }

    // Update state
    s.output = value;
    if (value > s.peakPos) {
      s.peakPos = value;
    }
    if (value < s.peakNeg) {
      s.peakNeg = value;
    }
    s.rmsAccum += static_cast<double>(value) * static_cast<double>(value);
    ++s.sampleCount;
    ++s.stepCount;

    // Update published output
    auto& out = waveOutput_.get();
    out.output = value;
    out.phase = static_cast<float>(std::fmod(s.phase, TWO_PI) / TWO_PI);

    return 0;
  }

  /**
   * @brief Log telemetry status (1 Hz).
   *
   * Reports current waveform type, frequency, output value, peak-to-peak,
   * and RMS to the component log.
   *
   * @return 0 on success.
   * @note NOT RT-safe: uses fmt::format for logging.
   */
  std::uint8_t telemetry() noexcept {
    auto& s = state_.get();
    const auto& p = paramBank_.active();

    const double RMS =
        (s.sampleCount > 0) ? std::sqrt(s.rmsAccum / static_cast<double>(s.sampleCount)) : 0.0;

    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(),
                fmt::format("type={} f={:.1f}Hz out={:.3f} pk=[{:.3f},{:.3f}] rms={:.3f} n={}",
                            p.waveType, p.frequency, s.output, s.peakNeg, s.peakPos, RMS,
                            s.sampleCount));
    }
    ++s.tlmCount;
    return 0;
  }

  /* ----------------------------- Command Handling ----------------------------- */

  /**
   * @brief Handle commands dispatched to this WaveGenerator instance.
   *
   * Component-specific opcodes:
   *   - 0x0100: GET_STATS       - Returns WaveGenHealthTlm (32 bytes).
   *   - 0x0101: SET_PARAMS      - Stage + validate + atomically publish a
   *                               full WaveGenTunableParams (32 bytes). A
   *                               set that fails validation is never
   *                               published and NAKs ERROR_LOAD_INVALID.
   *   - 0x0102: ROLLBACK_PARAMS - Restore the previously active set (one
   *                               level; staging a new set forfeits it).
   *
   * Delegates to base class for common opcodes (0x0080-0x0082).
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    constexpr std::uint16_t GET_STATS = 0x0100;
    constexpr std::uint16_t SET_PARAMS = 0x0101;
    constexpr std::uint16_t ROLLBACK_PARAMS = 0x0102;

    switch (opcode) {
    case GET_STATS: {
      const auto& s = state_.get();
      const auto& p = paramBank_.active();

      WaveGenHealthTlm tlm{};
      tlm.waveType = p.waveType;
      tlm.frequency = p.frequency;
      tlm.amplitude = p.amplitude;
      tlm.output = s.output;
      tlm.peakPos = s.peakPos;
      tlm.peakNeg = s.peakNeg;
      tlm.rms = (s.sampleCount > 0)
                    ? static_cast<float>(std::sqrt(s.rmsAccum / static_cast<double>(s.sampleCount)))
                    : 0.0F;
      tlm.sampleCount = static_cast<std::uint32_t>(s.sampleCount & 0xFFFFFFFF);
      response.resize(sizeof(tlm));
      std::memcpy(response.data(), &tlm, sizeof(tlm));
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case SET_PARAMS: {
      if (payload.size() != sizeof(WaveGenTunableParams)) {
        return static_cast<std::uint8_t>(Status::ERROR_PARAM);
      }
      WaveGenTunableParams incoming{};
      std::memcpy(&incoming, payload.data(), sizeof(incoming));

      if (paramBank_.load(incoming, validateParams) != Status::SUCCESS) {
        return static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID);
      }
      publishStaged();
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case ROLLBACK_PARAMS: {
      if (paramBank_.rollback() != Status::SUCCESS) {
        return static_cast<std::uint8_t>(Status::WARN_NOOP);
      }
      inspectParams_ = paramBank_.active();
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    default:
      return SwModelBase::handleCommand(opcode, payload, response);
    }
  }

  /* ----------------------------- TPRM ----------------------------- */

  /**
   * @brief Load tunable parameters from TPRM binary file.
   *
   * File path: {tprmDir}/{fullUid:06x}.tprm
   *
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false if file not found or parse error.
   * @note NOT RT-safe: file I/O.
   */
  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
    const std::filesystem::path TPRM_PATH = tprmDir / filename;

    bool loaded = false;
    if (std::filesystem::exists(TPRM_PATH)) {
      loaded = paramBank_.load(TPRM_PATH, validateParams) == Status::SUCCESS;
    }
    if (!loaded) {
      // Missing, unreadable, or rejected file: stage defaults so the bank
      // always publishes a validated set.
      (void)paramBank_.load(WaveGenTunableParams{}, validateParams);
    }
    publishStaged();
    setConfigured(true);

    // Seed noise generator from instance index for deterministic per-instance noise
    state_.get().noiseSeed = static_cast<std::uint32_t>(instanceIndex()) * 2654435761U;

    if (loaded) {
      const auto& p = paramBank_.active();
      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), fmt::format("TPRM loaded: type={} f={:.1f}Hz amp={:.2f}", p.waveType,
                                       p.frequency, p.amplitude));
      }
    }
    return loaded;
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Components normally receive loadTprm() at registration; publish
    // defaults here if no set was ever staged.
    if (paramBank_.activeGeneration() == 0) {
      (void)paramBank_.load(WaveGenTunableParams{}, validateParams);
      publishStaged();
    }

    // Seed noise generator from instance index
    state_.get().noiseSeed = static_cast<std::uint32_t>(instanceIndex()) * 2654435761U;

    // Register tasks
    registerTask<WaveGenerator, &WaveGenerator::waveStep>(
        static_cast<std::uint8_t>(TaskUid::WAVE_STEP), this, "waveStep");
    registerTask<WaveGenerator, &WaveGenerator::telemetry>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    // Register data for registry (enables INSPECT from C2). Tunables are
    // registered through a stable mirror refreshed on every publish -- the
    // bank's active pointer moves between banks on each swap, and the
    // registry needs a fixed address.
    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &inspectParams_,
                 sizeof(WaveGenTunableParams));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(WaveGenState));
    registerData(DataCategory::OUTPUT, "output", &waveOutput_.get(), sizeof(WaveGenOutput));

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

private:
  /* ----------------------------- Parameter Staging ----------------------------- */

  /**
   * @brief Validate a candidate parameter set before it can be published.
   *
   * Ranges follow the field contracts in WaveGenData.hpp; non-finite
   * floats are rejected wholesale.
   */
  [[nodiscard]] static bool validateParams(const WaveGenTunableParams& p) noexcept {
    return std::isfinite(p.frequency) && p.frequency >= 0.0F && p.frequency <= 50.0F &&
           std::isfinite(p.amplitude) && p.amplitude >= 0.0F && std::isfinite(p.dcOffset) &&
           std::isfinite(p.phaseOffset) && std::isfinite(p.noiseAmplitude) &&
           p.noiseAmplitude >= 0.0F && std::isfinite(p.dutyCycle) && p.dutyCycle >= 0.0F &&
           p.dutyCycle <= 1.0F && p.waveType <= 4;
  }

  /** @brief Publish the staged set (initial or hot swap) and refresh the INSPECT mirror. */
  void publishStaged() noexcept {
    if (paramBank_.activeGeneration() == 0) {
      (void)paramBank_.publishInitial();
    } else {
      (void)paramBank_.apply();
    }
    inspectParams_ = paramBank_.active();
  }

  /* ----------------------------- Data Members ----------------------------- */

  ParamBank<WaveGenTunableParams> paramBank_{};
  WaveGenTunableParams inspectParams_{}; ///< Stable address for registry INSPECT.
  State<WaveGenState> state_{};
  Output<WaveGenOutput> waveOutput_{};
};

} // namespace wave
} // namespace appsim

#endif // APEX_OPS_DEMO_WAVE_GENERATOR_HPP
