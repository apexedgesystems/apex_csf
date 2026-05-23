#ifndef APEX_ZENERDIODE_HPP
#define APEX_ZENERDIODE_HPP
/**
 * @file ZenerDiode.hpp
 * @brief Zener diode model with breakdown characteristics.
 *
 * Implements SPICE-compatible Zener diode with exponential forward conduction
 * and Zener/avalanche breakdown in reverse bias. Used for voltage regulation,
 * voltage references, and ESD protection.
 *
 * Three operating regions:
 * 1. Forward bias (V > 0): Exponential turn-on like regular diode
 * 2. Reverse bias (V < 0, |V| < Vz): Small leakage current -Is
 * 3. Breakdown (V < -Vz): Exponential current increase (regulation region)
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
 * @brief Zener diode model parameters.
 *
 * Physical parameters defining Zener diode behavior. Default values represent
 * a typical 5.1V silicon Zener at room temperature.
 */
struct ZenerDiodeParams {
  double Is = 1e-14; ///< Saturation current (A)
  double n = 1.0;    ///< Ideality factor (unitless), typical 1.0-2.0
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K
  double Vz = 5.1;   ///< Zener breakdown voltage (V), positive value
  double Ibv = 1e-3; ///< Breakdown knee current (A)
  double Vbv = 0.1;  ///< Breakdown voltage parameter (V), controls sharpness
};

/**
 * @brief Zener diode physics model.
 *
 * Provides I-V characteristic and derivatives for use with Newton-Raphson
 * nonlinear solver. Adds breakdown region to standard Shockley diode.
 *
 * Usage:
 * @code
 * ZenerDiodeParams params{.Vz = 5.1, .Ibv = 1e-3};
 *
 * // Forward bias (like regular diode)
 * double iFwd = ZenerDiode::current(0.7, params);  // ~mA range
 *
 * // Reverse breakdown (regulation region)
 * double iReg = ZenerDiode::current(-5.5, params);  // Regulated current
 *
 * // Stamp into MNA system for Newton-Raphson
 * ZenerDiode::stamp(mna, anodeNet, cathodeNet, vZener, params);
 * @endcode
 */
struct ZenerDiode {
  /**
   * @brief Compute Zener diode current.
   *
   * Forward: I = Is * (exp(V / (n * Vt)) - 1)
   * Reverse: I ~= -Is (small leakage)
   * Breakdown: I = -Is - Ibv * exp(-(V + Vz) / Vbv)
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Zener diode physical parameters.
   * @return Current through diode in amperes (positive = forward bias).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vDiode, const ZenerDiodeParams& params) noexcept {
    const double N_VT = params.n * params.Vt;

    // Forward bias: exponential turn-on
    if (vDiode > 0.0) {
      return params.Is * (std::exp(vDiode / N_VT) - 1.0);
    }

    // Reverse bias: check for breakdown
    const double V_BREAKDOWN = -params.Vz;
    if (vDiode < V_BREAKDOWN) {
      // Breakdown region: exponential current increase
      const double V_EXCESS = vDiode - V_BREAKDOWN; // How far past breakdown (negative)
      const double I_BREAKDOWN = -params.Ibv * std::exp(-V_EXCESS / params.Vbv);
      const double I_REVERSE = -params.Is;
      return I_REVERSE + I_BREAKDOWN;
    }

    // Reverse bias before breakdown: small leakage
    return params.Is * (std::exp(vDiode / N_VT) - 1.0);
  }

  /**
   * @brief Compute Zener diode conductance (dI/dV) for Newton-Raphson.
   *
   * Forward: g = (Is / (n * Vt)) * exp(V / (n * Vt))
   * Reverse: g = (Is / (n * Vt)) * exp(V / (n * Vt))  [very small]
   * Breakdown: g = (Ibv / Vbv) * exp(-(V + Vz) / Vbv)
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Zener diode physical parameters.
   * @return Small-signal conductance in siemens (always positive).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double conductance(double vDiode, const ZenerDiodeParams& params) noexcept {
    const double N_VT = params.n * params.Vt;

    // Forward bias: exponential conductance
    if (vDiode > 0.0) {
      return (params.Is / N_VT) * std::exp(vDiode / N_VT);
    }

    // Reverse bias: check for breakdown
    const double V_BREAKDOWN = -params.Vz;
    if (vDiode < V_BREAKDOWN) {
      // Breakdown region: exponential conductance
      const double V_EXCESS = vDiode - V_BREAKDOWN;
      return (params.Ibv / params.Vbv) * std::exp(-V_EXCESS / params.Vbv);
    }

    // Reverse bias before breakdown: small conductance
    return (params.Is / N_VT) * std::exp(vDiode / N_VT);
  }

  /**
   * @brief Stamp Zener diode into MNA system for Newton-Raphson iteration.
   *
   * Linearizes Zener diode around current operating point:
   *   I_total = I(V) = g * V + Ieq
   *   where: g = dI/dV, Ieq = I - g*V
   *
   * @param mna MNA system to stamp into.
   * @param anodeNet Anode net ID.
   * @param cathodeNet Cathode net ID.
   * @param vDiode Current diode voltage (from previous iteration).
   * @param params Zener diode physical parameters.
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID anodeNet, NetID cathodeNet, double vDiode,
                    const ZenerDiodeParams& params) {
    const double I = current(vDiode, params);
    const double G = conductance(vDiode, params);
    const double IEQ = I - G * vDiode;

    // Stamp linearized conductance and equivalent current source
    mna.addConductance(anodeNet, cathodeNet, G);
    mna.addCurrent(anodeNet, cathodeNet, IEQ);
  }

  /**
   * @brief Determine Zener diode operating region.
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Zener diode physical parameters.
   * @return Region code: 0=breakdown, 1=reverse, 2=forward
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static int region(double vDiode, const ZenerDiodeParams& params) noexcept {
    if (vDiode >= 0.0) {
      return 2; // Forward
    }
    if (vDiode < -params.Vz) {
      return 0; // Breakdown
    }
    return 1; // Reverse
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_ZENERDIODE_HPP
