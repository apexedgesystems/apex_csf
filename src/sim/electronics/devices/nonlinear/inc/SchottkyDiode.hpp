#ifndef APEX_SCHOTTKYDIODE_HPP
#define APEX_SCHOTTKYDIODE_HPP
/**
 * @file SchottkyDiode.hpp
 * @brief Schottky barrier diode model (metal-semiconductor junction).
 *
 * Schottky diodes have lower forward voltage drop (~0.3V vs ~0.7V for silicon)
 * and faster switching due to majority carrier conduction (no charge storage).
 *
 * Key differences from PN junction diodes:
 * - Lower Vf: Forward voltage ~0.15-0.45V (vs ~0.6-0.7V silicon PN)
 * - Faster switching: No minority carrier storage (no reverse recovery)
 * - Higher leakage: Larger reverse saturation current Is
 * - Temperature sensitive: Strong temperature dependence
 *
 * Applications:
 * - Fast rectifiers (switching power supplies, RF detectors)
 * - Low-voltage power supplies (minimize voltage drop)
 * - Clamping circuits (fast response)
 *
 * This is a PHYSICS MODEL (Layer 2) that uses Layer 1 Newton-Raphson solver
 * for nonlinear circuit simulation.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/Junction.hpp"

#include <cmath>

namespace sim::electronics::devices::nonlinear {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/**
 * @brief Schottky diode model parameters.
 *
 * Physical parameters defining Schottky barrier diode behavior.
 * Default values represent a typical silicon Schottky at room temperature.
 */
struct SchottkyDiodeParams {
  double Is = 1e-12; ///< Saturation current (A), higher than PN junction
  double n = 1.0;    ///< Ideality factor (unitless), typically 1.0-1.2
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K
  double Rs = 0.0;   ///< Series resistance (ohm), parasitic resistance
};

/**
 * @brief Schottky barrier diode physics model.
 *
 * Provides I-V characteristic and derivatives for use with Newton-Raphson
 * nonlinear solver. Uses Shockley equation with Schottky-specific parameters.
 *
 * Usage:
 * @code
 * SchottkyDiodeParams params{.Is = 1e-12, .n = 1.05, .Rs = 0.5};
 *
 * // Forward bias (lower Vf than silicon PN)
 * double vDiode = 0.3;
 * double iDiode = SchottkyDiode::current(vDiode, params);  // ~mA range
 *
 * // Stamp into MNA system for Newton-Raphson
 * SchottkyDiode::stamp(mna, anodeNet, cathodeNet, vDiode, params);
 * @endcode
 */
struct SchottkyDiode {
  /**
   * @brief Compute Schottky diode current.
   *
   * I = Is * (exp(V / (n*Vt)) - 1)
   *
   * With optional series resistance (solved iteratively if Rs > 0).
   *
   * @param vDiode Voltage across diode terminals (anode - cathode) in volts
   * @param params Schottky diode physical parameters
   * @return Current through diode in amperes (positive = forward bias)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vDiode, const SchottkyDiodeParams& params) noexcept {
    return Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs)
        .current;
  }

  /**
   * @brief Compute Schottky diode conductance (dI/dV) for Newton-Raphson.
   *
   * g = dI/dV = gj / (1 + gj*Rs)
   *
   * where gj = junction conductance without series R
   *
   * @param vDiode Voltage across diode terminals in volts
   * @param params Schottky diode physical parameters
   * @return Small-signal conductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double conductance(double vDiode,
                                          const SchottkyDiodeParams& params) noexcept {
    return Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs)
        .conductance;
  }

  /**
   * @brief Stamp Schottky diode into MNA system for Newton-Raphson iteration.
   *
   * Linearizes diode around current operating point:
   *   I_total = I(V) = g * V + Ieq
   *   where: g = dI/dV, Ieq = I - g*V
   *
   * @param mna MNA system to stamp into
   * @param anodeNet Anode net ID
   * @param cathodeNet Cathode net ID
   * @param vDiode Current diode voltage (from previous iteration)
   * @param params Schottky diode physical parameters
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID anodeNet, NetID cathodeNet, double vDiode,
                    const SchottkyDiodeParams& params) {
    const JunctionPoint POINT =
        Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs);
    const double G = POINT.conductance;
    const double IEQ = POINT.current - G * vDiode;

    // Stamp linearized conductance; the equivalent source leaves the anode net
    mna.addConductance(anodeNet, cathodeNet, G);
    mna.addCurrent(anodeNet, cathodeNet, -IEQ);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_SCHOTTKYDIODE_HPP
