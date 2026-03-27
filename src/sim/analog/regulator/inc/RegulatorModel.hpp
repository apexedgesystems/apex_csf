#ifndef APEX_SIM_ANALOG_REGULATOR_MODEL_HPP
#define APEX_SIM_ANALOG_REGULATOR_MODEL_HPP
/**
 * @file RegulatorModel.hpp
 * @brief LDO voltage regulator model for Monte Carlo tolerance analysis.
 *
 * Models a linear voltage regulator output stage with resistive feedback
 * divider, output capacitor with ESR, and voltage reference.
 *
 * Circuit:
 *            V_in
 *             |
 *          [LDO pass element]
 *             |
 *             +------ V_out ------+
 *             |                   |
 *            [R1]              [C_out + ESR]
 *             |                   |
 *             +--- V_fb ---+     GND
 *             |            |
 *            [R2]       [V_ref comparator]
 *             |
 *            GND
 *
 * The regulator drives V_out such that V_fb = V_ref:
 *   V_out = V_ref * (1 + R1/R2)
 *
 * Transient response modeled as second-order system:
 *   - Load step response with output cap + ESR
 *   - Settling time to within 1% of final value
 *   - Ripple from ESR * load current step
 *   - Phase margin proxy from damping ratio
 *
 * Parameters:
 *   - r1:       Feedback upper resistor (ohms)
 *   - r2:       Feedback lower resistor (ohms)
 *   - cOut:     Output capacitance (farads)
 *   - esr:      Output cap ESR (ohms)
 *   - vRef:     Internal voltage reference (volts)
 *   - vIn:      Input voltage (volts)
 *   - iLoad:    Load current step (amps)
 *   - bandwidth: Regulator loop bandwidth (Hz)
 *
 * Outputs:
 *   - vOut:         Regulated output voltage (V)
 *   - ripple:       Peak ripple from load step (V)
 *   - settlingTime: Time to settle within 1% (seconds)
 *   - phaseMargin:  Phase margin estimate (degrees)
 *   - inSpec:       True if vOut within +/-3% of nominal
 *
 * @note NOT RT-safe: Uses floating-point math, not intended for real-time.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sim {
namespace analog {

/* ----------------------------- Constants ----------------------------- */

/// Nominal output voltage target (V).
constexpr double NOMINAL_VOUT = 3.3;

/// Output voltage tolerance for in-spec determination (+/-%).
constexpr double SPEC_TOLERANCE_PCT = 3.0;

/// Settling criterion (fraction of final value).
constexpr double SETTLING_CRITERION = 0.01;

/* ----------------------------- RegulatorParams ----------------------------- */

/**
 * @struct RegulatorParams
 * @brief Input parameters for the voltage regulator model.
 *
 * All values have physical units. Nominal values represent a typical
 * 3.3V LDO design. Tolerances are applied by the MC sweep generator.
 */
struct RegulatorParams {
  double r1{100000.0};        ///< Feedback upper resistor (ohms). Nominal: 100k.
  double r2{60606.0};         ///< Feedback lower resistor (ohms). Nominal: 60.606k for 3.3V.
  double cOut{10.0e-6};       ///< Output capacitance (farads). Nominal: 10uF.
  double esr{0.010};          ///< Output cap ESR (ohms). Nominal: 10m.
  double vRef{1.25};          ///< Voltage reference (volts). Nominal: 1.25V.
  double vIn{5.0};            ///< Input voltage (volts). Nominal: 5.0V.
  double iLoad{0.100};        ///< Load current step (amps). Nominal: 100mA.
  double bandwidth{100000.0}; ///< Loop bandwidth (Hz). Nominal: 100kHz.
};

/* ----------------------------- RegulatorResult ----------------------------- */

/**
 * @struct RegulatorResult
 * @brief Output results from the voltage regulator model.
 */
struct RegulatorResult {
  double vOut{0.0};         ///< Regulated output voltage (V).
  double ripple{0.0};       ///< Peak ripple from load transient (V).
  double settlingTime{0.0}; ///< Settling time to 1% of final value (s).
  double phaseMargin{0.0};  ///< Phase margin estimate (degrees).
  bool inSpec{false};       ///< True if vOut within +/-3% of 3.3V target.
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Run the voltage regulator model.
 * @param params Input parameters (component values with tolerances applied).
 * @return RegulatorResult with output voltage, ripple, settling, phase margin.
 *
 * Physics:
 *   1. V_out = V_ref * (1 + R1/R2) — resistor divider sets output
 *   2. Ripple = ESR * I_load — ESR dominates transient response
 *   3. Settling = f(bandwidth, output pole, damping) — second-order model
 *   4. Phase margin = f(output pole vs bandwidth) — stability indicator
 *
 * The output pole is set by the output capacitor and load:
 *   f_pole = 1 / (2 * pi * C_out * (R_load || R_feedback))
 *
 * The ESR zero provides phase boost:
 *   f_zero = 1 / (2 * pi * C_out * ESR)
 *
 * @note Deterministic: same params always produce same result.
 */
inline RegulatorResult simulate(const RegulatorParams& params) {
  RegulatorResult result;

  // Output voltage from resistor divider
  result.vOut = params.vRef * (1.0 + params.r1 / params.r2);

  // Effective load resistance (for pole calculation)
  const double R_LOAD = result.vOut / params.iLoad;

  // Feedback resistance (R1 + R2 in series, parallel with load for AC)
  const double R_FB = params.r1 + params.r2;
  const double R_EFF = (R_LOAD * R_FB) / (R_LOAD + R_FB);

  // Output pole frequency
  const double F_POLE = 1.0 / (2.0 * M_PI * params.cOut * R_EFF);

  // ESR zero frequency (provides phase boost for stability)
  const double F_ZERO = 1.0 / (2.0 * M_PI * params.cOut * params.esr);

  // Ripple from load transient through ESR
  // Initial voltage spike = ESR * delta_I, then RC decay
  result.ripple = params.esr * params.iLoad;

  // Add capacitive charge sharing component
  // delta_V = I * dt / C, where dt ~ 1/(2*bandwidth)
  const double DT = 1.0 / (2.0 * params.bandwidth);
  const double CAP_RIPPLE = params.iLoad * DT / params.cOut;
  result.ripple += CAP_RIPPLE;

  // Damping ratio from pole/zero/bandwidth relationship
  // Higher bandwidth relative to output pole = more damping
  const double DAMPING =
      std::sqrt(F_POLE / params.bandwidth) * std::sqrt(F_ZERO / params.bandwidth);

  // Clamp damping to physically meaningful range
  const double ZETA = std::clamp(DAMPING, 0.01, 2.0);

  // Settling time: envelope decay to SETTLING_CRITERION
  // For second-order system: t_settle ~ -ln(criterion) / (zeta * omega_n)
  const double OMEGA_N = 2.0 * M_PI * std::sqrt(F_POLE * params.bandwidth);
  if (ZETA * OMEGA_N > 0.0) {
    result.settlingTime = -std::log(SETTLING_CRITERION) / (ZETA * OMEGA_N);
  }

  // Phase margin estimate
  // At bandwidth frequency, output pole contributes -90 deg,
  // ESR zero adds back phase. Net phase margin:
  const double POLE_PHASE = std::atan2(params.bandwidth, F_POLE) * 180.0 / M_PI;
  const double ZERO_PHASE = std::atan2(params.bandwidth, F_ZERO) * 180.0 / M_PI;
  result.phaseMargin = 180.0 - POLE_PHASE + ZERO_PHASE;

  // Clamp to [0, 180] range
  result.phaseMargin = std::clamp(result.phaseMargin, 0.0, 180.0);

  // Spec check: within +/-3% of nominal
  const double LOWER = NOMINAL_VOUT * (1.0 - SPEC_TOLERANCE_PCT / 100.0);
  const double UPPER = NOMINAL_VOUT * (1.0 + SPEC_TOLERANCE_PCT / 100.0);
  result.inSpec = (result.vOut >= LOWER && result.vOut <= UPPER);

  return result;
}

} // namespace analog
} // namespace sim

#endif // APEX_SIM_ANALOG_REGULATOR_MODEL_HPP
