#ifndef APEX_DIODESPICE_HPP
#define APEX_DIODESPICE_HPP
/**
 * @file DiodeSpice.hpp
 * @brief SPICE-level diode model with series resistance and junction capacitance.
 *
 * Extends DiodeShockley with:
 * - Series resistance (Rs): voltage drop across parasitic resistance
 * - Junction capacitance (Cj0): charge storage for accurate transient analysis
 * - Grading coefficient (M): junction profile (0.33 = abrupt, 0.5 = linear)
 *
 * More accurate than DiodeShockley for:
 * - High-frequency rectifiers (junction capacitance affects switching)
 * - Power diodes (series resistance causes voltage drop at high current)
 * - Precision circuits (models parasitic effects)
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
 * @brief SPICE-level diode model parameters.
 *
 * Physical parameters defining diode behavior including parasitic effects.
 * Default values represent a typical silicon diode at room temperature.
 */
struct DiodeSpiceParams {
  double Is = 1e-14; ///< Saturation current (A)
  double n = 1.0;    ///< Ideality factor (unitless), typical 1.0-2.0
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K
  double Rs = 0.0;   ///< Series resistance (ohm), parasitic resistance
  double Cj0 = 0.0;  ///< Zero-bias junction capacitance (F)
  double Vj = 0.7;   ///< Built-in junction potential (V)
  double M = 0.5;    ///< Grading coefficient (unitless), 0.33-0.5
};

/**
 * @brief SPICE-level diode physics model.
 *
 * Provides I-V characteristic and derivatives for use with Newton-Raphson
 * nonlinear solver. Adds series resistance to DiodeShockley model.
 *
 * The series resistance creates a voltage divider:
 *   V_total = V_junction + I * Rs
 *
 * Usage:
 * @code
 * DiodeSpiceParams params{.Is = 1e-14, .Rs = 1.0};  // 1 ohm series R
 *
 * // Forward bias
 * double vDiode = 0.7;
 * double iDiode = DiodeSpice::current(vDiode, params);  // Lower than ideal
 *
 * // Stamp into MNA system for Newton-Raphson
 * DiodeSpice::stamp(mna, anodeNet, cathodeNet, vDiode, params);
 * @endcode
 */
struct DiodeSpice {
  /**
   * @brief Compute diode current with series resistance.
   *
   * I = Is * (exp((V - I*Rs) / (n*Vt)) - 1)
   *
   * Solved iteratively for I given V (implicit equation).
   *
   * @param vDiode Voltage across diode terminals (anode - cathode) in volts
   * @param params Diode physical parameters
   * @return Current through diode in amperes (positive = forward bias); NaN
   *         if the series-resistance solve does not converge. Over the
   *         values diodes have it converges; Junction::seriesJunctionVoltage()
   *         says what it refuses and where, far outside them, it may not
   *
   * @note RT-safe: no allocation; with a series resistance, at most
   *       Junction::SERIES_MAX_ITER iterations.
   */
  [[nodiscard]] static double current(double vDiode, const DiodeSpiceParams& params) noexcept {
    return Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs)
        .current;
  }

  /**
   * @brief Compute diode conductance (dI/dV) for Newton-Raphson.
   *
   * g = dI/dV = gj / (1 + gj*Rs)
   *
   * where gj = junction conductance without series R
   *
   * @param vDiode Voltage across diode terminals in volts
   * @param params Diode physical parameters
   * @return Small-signal conductance in siemens; NaN as for current()
   *
   * @note RT-safe: no allocation; with a series resistance, at most
   *       Junction::SERIES_MAX_ITER iterations.
   */
  [[nodiscard]] static double conductance(double vDiode, const DiodeSpiceParams& params) noexcept {
    return Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs)
        .conductance;
  }

  /**
   * @brief Stamp diode into MNA system for Newton-Raphson iteration.
   *
   * Linearizes diode around current operating point:
   *   I_total = I(V) = g * V + Ieq
   *   where: g = dI/dV, Ieq = I - g*V
   *
   * @param mna MNA system to stamp into
   * @param anodeNet Anode net ID
   * @param cathodeNet Cathode net ID
   * @param vDiode Current diode voltage (from previous iteration)
   * @param params Diode physical parameters
   *
   * @note RT-safe: stamps into a pre-allocated matrix and vector; with a
   *       series resistance, at most Junction::SERIES_MAX_ITER iterations.
   * @note With parameters the series-resistance solve refuses, the values
   *       stamped are NaN. The system's solve does not report that: check
   *       its result for voltages that are not finite.
   */
  static void stamp(MnaSystem& mna, NetID anodeNet, NetID cathodeNet, double vDiode,
                    const DiodeSpiceParams& params) {
    const JunctionPoint POINT =
        Junction::withSeriesResistance(vDiode, params.Is, params.n * params.Vt, params.Rs);
    const double G = POINT.conductance;
    const double IEQ = POINT.current - G * vDiode;

    // Stamp linearized conductance; the equivalent source leaves the anode net
    mna.addConductance(anodeNet, cathodeNet, G);
    mna.addCurrent(anodeNet, cathodeNet, -IEQ);
  }

  /**
   * @brief Compute junction capacitance at given voltage.
   *
   * C(V) = Cj0 / (1 - V/Vj)^M
   *
   * @param vDiode Voltage across diode in volts
   * @param params Diode physical parameters
   * @return Junction capacitance in farads
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double junctionCapacitance(double vDiode,
                                                  const DiodeSpiceParams& params) noexcept {
    if (params.Cj0 <= 0.0) {
      return 0.0;
    }

    // Voltage-dependent capacitance
    const double FACTOR = 1.0 - vDiode / params.Vj;
    if (FACTOR <= 0.0) {
      return params.Cj0; // Clamp to C j0 at forward bias
    }

    return params.Cj0 / std::pow(FACTOR, params.M);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_DIODESPICE_HPP
