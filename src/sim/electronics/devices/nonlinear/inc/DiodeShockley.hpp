#ifndef APEX_DIODESHOCKLEY_HPP
#define APEX_DIODESHOCKLEY_HPP
/**
 * @file DiodeShockley.hpp
 * @brief Shockley diode model (exponential I-V characteristic).
 *
 * Implements the classic Shockley diode equation:
 *   I = Is * (exp(V / (n * Vt)) - 1)
 *
 * Where:
 *   Is = saturation current (typically 1e-12 to 1e-15 A)
 *   n  = ideality factor (typically 1.0 to 2.0)
 *   Vt = thermal voltage (kT/q ~= 26mV at 300K)
 *
 * This is a PHYSICS MODEL (Layer 2) that uses Layer 1 Newton-Raphson solver
 * for nonlinear circuit simulation.
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
 * @brief Shockley diode model parameters.
 *
 * Physical parameters defining diode behavior. Default values represent
 * a typical silicon diode at room temperature.
 */
struct DiodeShockleyParams {
  double Is = 1e-14; ///< Saturation current (A), typical 1e-12 to 1e-15
  double n = 1.0;    ///< Ideality factor (dimensionless), typical 1.0 to 2.0
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K
};

/**
 * @brief Shockley diode physics model.
 *
 * Provides I-V characteristic and derivatives for use with Newton-Raphson
 * nonlinear solver.
 *
 * Usage:
 * @code
 * DiodeShockleyParams params{.Is = 1e-14, .n = 1.0, .Vt = 0.026};
 *
 * // Compute current at 0.7V forward bias
 * double current = DiodeShockley::current(0.7, params);
 *
 * // Compute conductance (dI/dV) for Newton-Raphson
 * double g = DiodeShockley::conductance(0.7, params);
 *
 * // Stamp into MNA system
 * DiodeShockley::stamp(mna, anodeNet, cathodeNet, vDiode, params);
 * @endcode
 */
struct DiodeShockley {
  /**
   * @brief Compute diode current using Shockley equation.
   *
   * I = Is * (exp(V / (n * Vt)) - 1)
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Diode physical parameters.
   * @return Current through diode in amperes (positive = forward bias).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vDiode, const DiodeShockleyParams& params) noexcept {
    const double EXPONENT = vDiode / (params.n * params.Vt);
    return params.Is * (std::exp(EXPONENT) - 1.0);
  }

  /**
   * @brief Compute diode conductance (dI/dV) for Newton-Raphson.
   *
   * g = dI/dV = (Is / (n * Vt)) * exp(V / (n * Vt))
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Diode physical parameters.
   * @return Small-signal conductance in siemens.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double conductance(double vDiode,
                                          const DiodeShockleyParams& params) noexcept {
    const double EXPONENT = vDiode / (params.n * params.Vt);
    return (params.Is / (params.n * params.Vt)) * std::exp(EXPONENT);
  }

  /**
   * @brief Stamp diode into MNA system for Newton-Raphson iteration.
   *
   * Linearizes diode around current operating point:
   *   I_total = I(V) = g * V + Ieq
   *   where: g = dI/dV, Ieq = I - g*V
   *
   * Stamps:
   *   G[anode][anode] += g
   *   G[anode][cathode] -= g
   *   G[cathode][anode] -= g
   *   G[cathode][cathode] += g
   *   I[anode] += Ieq
   *   I[cathode] -= Ieq
   *
   * @param mna MNA system to stamp into.
   * @param anodeNet Anode net ID.
   * @param cathodeNet Cathode net ID.
   * @param vDiode Current diode voltage (from previous iteration).
   * @param params Diode physical parameters.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID anodeNet, NetID cathodeNet, double vDiode,
                    const DiodeShockleyParams& params) {
    const double I = current(vDiode, params);
    const double G = conductance(vDiode, params);
    const double IEQ = I - G * vDiode;

    // Stamp linearized conductance
    mna.addConductance(anodeNet, cathodeNet, G);

    // Stamp equivalent current source
    mna.addCurrent(anodeNet, cathodeNet, IEQ);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_DIODESHOCKLEY_HPP
