#ifndef APEX_BJTEBERSMOLL_HPP
#define APEX_BJTEBERSMOLL_HPP
/**
 * @file BjtEbersMoll.hpp
 * @brief BJT Ebers-Moll model (bipolar junction transistor, 4-region operation).
 *
 * Implements the classic Ebers-Moll equations for NPN bipolar junction transistor:
 *
 * Operating regions:
 * 1. Cutoff: Both junctions reverse-biased (Vbe < 0, Vbc < 0)
 * 2. Forward Active: Base-emitter forward, base-collector reverse (Vbe > 0, Vbc < 0)
 * 3. Reverse Active: Base-collector forward, base-emitter reverse (Vbe < 0, Vbc > 0)
 * 4. Saturation: Both junctions forward-biased (Vbe > 0, Vbc > 0)
 *
 * Collector current (simplified):
 *   Ic = Is * (exp(Vbe/Vt) - 1) - Is/Br * (exp(Vbc/Vt) - 1)
 *
 * Where:
 *   Is = saturation current (A), typically 1e-14 to 1e-16
 *   Bf = forward current gain (beta), typically 50-300
 *   Br = reverse current gain, typically 1-10
 *   Vt = thermal voltage (kT/q ~= 26mV at 300K)
 *
 * This is a PHYSICS MODEL (Layer 2) for analog circuit simulation with
 * smooth I-V curves and derivatives for Newton-Raphson convergence.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <cmath>

namespace sim::electronics::devices::nonlinear {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/**
 * @brief BJT Ebers-Moll model parameters.
 *
 * Physical parameters defining NPN BJT behavior. Default values represent
 * a typical small-signal NPN transistor (e.g., 2N2222, 2N3904).
 */
struct BjtEbersMollParams {
  double Is = 1e-14; ///< Saturation current (A), typical 1e-14 to 1e-16
  double Bf = 100.0; ///< Forward current gain (beta), typical 50-300
  double Br = 1.0;   ///< Reverse current gain, typical 1-10
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K
};

/**
 * @brief BJT Ebers-Moll physics model.
 *
 * Provides collector/base/emitter currents and transconductances for use
 * with Newton-Raphson nonlinear solver.
 *
 * Usage:
 * @code
 * BjtEbersMollParams params{.Is = 1e-14, .Bf = 100.0, .Br = 1.0, .Vt = 0.026};
 *
 * // Compute currents at operating point
 * double vbe = 0.7, vbc = -5.0;  // Forward active region
 * double ic = BjtEbersMoll::collectorCurrent(vbe, vbc, params);
 * double ib = BjtEbersMoll::baseCurrent(vbe, vbc, params);
 *
 * // Compute transconductances for Newton-Raphson
 * double gm = BjtEbersMoll::transconductance(vbe, vbc, params);
 *
 * // Stamp into MNA system
 * BjtEbersMoll::stamp(mna, collectorNet, baseNet, emitterNet, vbe, vbc, params);
 * @endcode
 */
struct BjtEbersMoll {
  /**
   * @brief Compute collector current using Ebers-Moll equations.
   *
   * Ic = Is * (exp(Vbe/Vt) - 1) - Is/Br * (exp(Vbc/Vt) - 1)
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return Collector current in amperes (positive = conventional direction).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double collectorCurrent(double vbe, double vbc,
                                               const BjtEbersMollParams& params) noexcept {
    const double ICF = params.Is * (std::exp(vbe / params.Vt) - 1.0); // Forward current
    const double ICR =
        (params.Is / params.Br) * (std::exp(vbc / params.Vt) - 1.0); // Reverse current
    return ICF - ICR;
  }

  /**
   * @brief Compute base current using Ebers-Moll equations.
   *
   * Ib = (Is/Bf) * (exp(Vbe/Vt) - 1) + (Is/Br) * (exp(Vbc/Vt) - 1)
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return Base current in amperes (positive = into base).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double baseCurrent(double vbe, double vbc,
                                          const BjtEbersMollParams& params) noexcept {
    const double IBF = (params.Is / params.Bf) * (std::exp(vbe / params.Vt) - 1.0);
    const double IBR = (params.Is / params.Br) * (std::exp(vbc / params.Vt) - 1.0);
    return IBF + IBR;
  }

  /**
   * @brief Compute emitter current (Kirchhoff: Ie = -(Ic + Ib)).
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return Emitter current in amperes (positive = conventional direction).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double emitterCurrent(double vbe, double vbc,
                                             const BjtEbersMollParams& params) noexcept {
    const double IC = collectorCurrent(vbe, vbc, params);
    const double IB = baseCurrent(vbe, vbc, params);
    return -(IC + IB);
  }

  /**
   * @brief Compute transconductance gm = dIc/dVbe for Newton-Raphson.
   *
   * gm = (Is/Vt) * exp(Vbe/Vt)
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts (unused for gm).
   * @param params BJT physical parameters.
   * @return Transconductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double transconductance(double vbe, double vbc,
                                               const BjtEbersMollParams& params) noexcept {
    (void)vbc; // Unused, but kept for API consistency
    return (params.Is / params.Vt) * std::exp(vbe / params.Vt);
  }

  /**
   * @brief Compute output conductance go = dIc/dVbc for Newton-Raphson.
   *
   * go = -(Is/(Br*Vt)) * exp(Vbc/Vt)
   *
   * @param vbe Base-emitter voltage in volts (unused for go).
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return Output conductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double outputConductance(double vbe, double vbc,
                                                const BjtEbersMollParams& params) noexcept {
    (void)vbe; // Unused, but kept for API consistency
    return -(params.Is / (params.Br * params.Vt)) * std::exp(vbc / params.Vt);
  }

  /**
   * @brief Compute base-emitter conductance gbe = dIb/dVbe for Newton-Raphson.
   *
   * gbe = (Is/(Bf*Vt)) * exp(Vbe/Vt)
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts (unused for gbe).
   * @param params BJT physical parameters.
   * @return Base-emitter conductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double baseConductanceBE(double vbe, double vbc,
                                                const BjtEbersMollParams& params) noexcept {
    (void)vbc; // Unused
    return (params.Is / (params.Bf * params.Vt)) * std::exp(vbe / params.Vt);
  }

  /**
   * @brief Compute base-collector conductance gbc = dIb/dVbc for Newton-Raphson.
   *
   * gbc = (Is/(Br*Vt)) * exp(Vbc/Vt)
   *
   * @param vbe Base-emitter voltage in volts (unused for gbc).
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return Base-collector conductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double baseConductanceBC(double vbe, double vbc,
                                                const BjtEbersMollParams& params) noexcept {
    (void)vbe; // Unused
    return (params.Is / (params.Br * params.Vt)) * std::exp(vbc / params.Vt);
  }

  /**
   * @brief Stamp BJT into MNA system for Newton-Raphson iteration.
   *
   * Linearizes BJT around current operating point using companion model:
   *   Ic = gm * Vbe + go * Vbc + Ic_eq
   *   Ib = gbe * Vbe + gbc * Vbc + Ib_eq
   *   Ie = -(Ic + Ib)
   *
   * Where:
   *   gm = dIc/dVbe, go = dIc/dVbc
   *   gbe = dIb/dVbe, gbc = dIb/dVbc
   *   Ic_eq = Ic - gm*Vbe - go*Vbc
   *   Ib_eq = Ib - gbe*Vbe - gbc*Vbc
   *
   * @param mna MNA system to stamp into.
   * @param collectorNet Collector net ID.
   * @param baseNet Base net ID.
   * @param emitterNet Emitter net ID.
   * @param vbe Current base-emitter voltage (from previous iteration).
   * @param vbc Current base-collector voltage (from previous iteration).
   * @param params BJT physical parameters.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID collectorNet, NetID baseNet, NetID emitterNet, double vbe,
                    double vbc, const BjtEbersMollParams& params) {
    // Compute currents
    const double IC = collectorCurrent(vbe, vbc, params);
    const double IB = baseCurrent(vbe, vbc, params);

    // Compute conductances (Jacobian elements)
    const double GM = transconductance(vbe, vbc, params);   // dIc/dVbe
    const double GO = outputConductance(vbe, vbc, params);  // dIc/dVbc
    const double GBE = baseConductanceBE(vbe, vbc, params); // dIb/dVbe
    const double GBC = baseConductanceBC(vbe, vbc, params); // dIb/dVbc

    // Compute equivalent current sources
    const double IC_EQ = IC - GM * vbe - GO * vbc;
    const double IB_EQ = IB - GBE * vbe - GBC * vbc;

    // Stamp collector current: Ic controlled by Vbe and Vbc
    // NOTE: addConductance stamps symmetrically. For production use, the BJT
    // stamp should use asymmetric addGEntry for the VCCS terms. The symmetric
    // stamp works for per-device testing but produces incorrect MNA solutions
    // for circuit-level DC operating point. See MISSING_FEATURES.md.
    mna.addConductance(collectorNet, baseNet, GM);       // dIc/dVbe term
    mna.addConductance(collectorNet, emitterNet, -GM);   // -dIc/dVbe term
    mna.addConductance(collectorNet, baseNet, GO);       // dIc/dVbc term
    mna.addConductance(collectorNet, collectorNet, -GO); // -dIc/dVbc term
    mna.addCurrent(collectorNet, emitterNet, IC_EQ);

    // Stamp base current: Ib controlled by Vbe and Vbc
    mna.addConductance(baseNet, baseNet, GBE + GBC); // dIb/dVbe + dIb/dVbc
    mna.addConductance(baseNet, emitterNet, -GBE);   // -dIb/dVbe term
    mna.addConductance(baseNet, collectorNet, -GBC); // -dIb/dVbc term
    mna.addCurrent(baseNet, emitterNet, IB_EQ);
  }

  /**
   * @brief Determine operating region for diagnostic purposes.
   *
   * @param vbe Base-emitter voltage in volts.
   * @param vbc Base-collector voltage in volts.
   * @param params BJT physical parameters.
   * @return 0 = cutoff, 1 = forward active, 2 = reverse active, 3 = saturation.
   */
  [[nodiscard]] static int region(double vbe, double vbc,
                                  const BjtEbersMollParams& params) noexcept {
    (void)params; // Region detection doesn't need params (just voltage comparisons)

    const bool BE_FORWARD = vbe > 0.5; // BE junction forward (typical Si turn-on ~0.6-0.7V)
    const bool BC_FORWARD = vbc > 0.5; // BC junction forward

    if (!BE_FORWARD && !BC_FORWARD) {
      return 0; // Cutoff
    } else if (BE_FORWARD && !BC_FORWARD) {
      return 1; // Forward active
    } else if (!BE_FORWARD && BC_FORWARD) {
      return 2; // Reverse active
    } else {
      return 3; // Saturation
    }
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_BJTEBERSMOLL_HPP
