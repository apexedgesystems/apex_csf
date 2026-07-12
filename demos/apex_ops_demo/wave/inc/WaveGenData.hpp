#ifndef APEX_OPS_DEMO_WAVE_GEN_DATA_HPP
#define APEX_OPS_DEMO_WAVE_GEN_DATA_HPP
/**
 * @file WaveGenData.hpp
 * @brief Data structures for WaveGenerator component.
 *
 * Contains:
 *  - WaveGenTunableParams: TPRM-configurable waveform parameters (32 bytes)
 *  - WaveGenState: Runtime state and statistics (48 bytes)
 *  - WaveGenOutput: Published output for INSPECT access (8 bytes)
 *  - WaveGenHealthTlm: Health telemetry returned by GET_STATS (32 bytes)
 *
 * All structures are POD, trivially copyable, with static_assert on size
 * for binary compatibility with TPRM and C2 wire formats.
 *
 * @note RT-safe: Pure data structures, no allocation or I/O.
 */

#include <cstdint>

namespace appsim {
namespace wave {

/* ----------------------------- Constants ----------------------------- */

/// Waveform type selection values.
enum class WaveType : std::uint8_t {
  SINE = 0,     ///< Standard sine wave.
  SQUARE = 1,   ///< Square wave with configurable duty cycle.
  TRIANGLE = 2, ///< Triangle wave.
  SAWTOOTH = 3, ///< Sawtooth (ramp) wave.
  COMPOSITE = 4 ///< Fourier composite (fundamental + 3rd harmonic).
};

/* ----------------------------- WaveGenTunableParams ----------------------------- */

/**
 * @struct WaveGenTunableParams
 * @brief TPRM-configurable waveform parameters.
 *
 * Loaded from binary .tprm file via hex2cpp. Field order matches
 * TOML template generation order -- DO NOT REORDER.
 *
 * Size: 32 bytes.
 */
struct WaveGenTunableParams {
  float frequency{1.0F};      ///< Primary frequency [Hz], range 0..50.
  float amplitude{1.0F};      ///< Peak amplitude, >= 0.
  float dcOffset{0.0F};       ///< DC bias added to output.
  float phaseOffset{0.0F};    ///< Phase offset [radians].
  float noiseAmplitude{0.0F}; ///< Gaussian-like noise peak amplitude.
  float dutyCycle{0.5F};      ///< Duty cycle for SQUARE wave, range 0..1.
  std::uint8_t waveType{0};   ///< WaveType enum value (0=SINE..4=COMPOSITE).
  std::uint8_t reserved[3]{}; ///< Alignment padding.
  float reserved2{0.0F};      ///< Reserved for future use.
};

static_assert(sizeof(WaveGenTunableParams) == 32,
              "WaveGenTunableParams size changed - update TOML template and regenerate binaries");
static_assert(__is_trivially_copyable(WaveGenTunableParams),
              "WaveGenTunableParams must be trivially copyable for binary serialization");

/* ----------------------------- WaveGenState ----------------------------- */

/**
 * @struct WaveGenState
 * @brief Runtime state and accumulated statistics.
 *
 * Updated by the waveStep task (100 Hz) and telemetry task (1 Hz).
 *
 * Size: 48 bytes.
 */
struct WaveGenState {
  double phase{0.0};            ///< Current phase accumulator [radians].
  double rmsAccum{0.0};         ///< Running sum of output^2 (for RMS computation).
  float output{0.0F};           ///< Last computed waveform output value.
  float peakPos{0.0F};          ///< Positive peak since last reset.
  float peakNeg{0.0F};          ///< Negative peak since last reset.
  std::uint32_t noiseSeed{0};   ///< LCG state for deterministic noise.
  std::uint64_t sampleCount{0}; ///< Total samples generated since init.
  std::uint32_t stepCount{0};   ///< waveStep task invocations.
  std::uint32_t tlmCount{0};    ///< telemetry task invocations.
};

static_assert(sizeof(WaveGenState) == 48, "WaveGenState size changed - update struct dictionary");

/* ----------------------------- WaveGenOutput ----------------------------- */

/**
 * @struct WaveGenOutput
 * @brief Published output accessible via INSPECT (DataCategory::OUTPUT).
 *
 * Updated every waveStep tick. Provides the current waveform value
 * and normalized phase for real-time plotting.
 *
 * Size: 8 bytes.
 */
struct WaveGenOutput {
  float output{0.0F}; ///< Current waveform output value.
  float phase{0.0F};  ///< Current phase normalized to [0, 1].
};

static_assert(sizeof(WaveGenOutput) == 8, "WaveGenOutput size changed - update struct dictionary");

/* ----------------------------- WaveGenHealthTlm ----------------------------- */

/**
 * @struct WaveGenHealthTlm
 * @brief Health telemetry returned by GET_STATS (opcode 0x0100).
 *
 * Packed wire format for C2 consumption. Contains current configuration
 * snapshot and computed statistics.
 *
 * Size: 32 bytes.
 */
struct __attribute__((packed)) WaveGenHealthTlm {
  std::uint8_t waveType{0};     ///< Active waveform type.
  std::uint8_t reserved[3]{};   ///< Alignment padding.
  float frequency{0.0F};        ///< Active frequency [Hz].
  float amplitude{0.0F};        ///< Active amplitude.
  float output{0.0F};           ///< Current output value.
  float peakPos{0.0F};          ///< Positive peak.
  float peakNeg{0.0F};          ///< Negative peak.
  float rms{0.0F};              ///< Root mean square (computed from accumulator).
  std::uint32_t sampleCount{0}; ///< Total samples (lower 32 bits).
};

static_assert(sizeof(WaveGenHealthTlm) == 32,
              "WaveGenHealthTlm size changed - update protocol documentation");

} // namespace wave
} // namespace appsim

#endif // APEX_OPS_DEMO_WAVE_GEN_DATA_HPP
