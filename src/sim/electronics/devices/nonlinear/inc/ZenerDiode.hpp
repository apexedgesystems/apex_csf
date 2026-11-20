#ifndef APEX_ZENERDIODE_HPP
#define APEX_ZENERDIODE_HPP
/**
 * @file ZenerDiode.hpp
 * @brief Zener diode model with breakdown characteristics.
 *
 * Exponential forward conduction plus an exponential breakdown current in
 * reverse bias. Used for voltage regulation, voltage references and clamps.
 *
 *   I(V) = Is * (exp(V / (n*Vt)) - 1) - Ibv * exp(-(V + Vz) / Vbv)
 *
 * The breakdown term is part of the current at every bias. It equals Ibv at
 * V = -Vz, grows by a factor e for every Vbv further into breakdown, and
 * falls by the same factor on the way back toward zero bias (7e-26 A at
 * V = 0 with the defaults). The curve and its slope are therefore continuous
 * through the knee, which a Newton-Raphson solve needs there.
 *
 * Regions, for diagnostics only (the equation above holds in all three):
 * 1. Forward (V >= 0): junction conduction
 * 2. Reverse (-Vz <= V < 0): leakage, rising toward the knee
 * 3. Breakdown (V < -Vz): regulation region
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
  double Ibv = 1e-3; ///< Breakdown current at V = -Vz (A)
  double Vbv = 0.1;  ///< Voltage per factor e of breakdown current (V); smaller is sharper
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
   * I = Is * (exp(V / (n*Vt)) - 1) - Ibv * exp(-(V + Vz) / Vbv)
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Zener diode physical parameters.
   * @return Current through diode in amperes (positive = forward bias).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vDiode, const ZenerDiodeParams& params) noexcept {
    const double I_JUNCTION = Junction::current(vDiode, params.Is, params.n * params.Vt);
    const double I_BREAKDOWN =
        params.Ibv * Junction::limitedExp(-(vDiode + params.Vz) / params.Vbv);
    return I_JUNCTION - I_BREAKDOWN;
  }

  /**
   * @brief Compute Zener diode conductance (dI/dV) for Newton-Raphson.
   *
   * g = (Is / (n*Vt)) * exp(V / (n*Vt)) + (Ibv / Vbv) * exp(-(V + Vz) / Vbv)
   *
   * @param vDiode Voltage across diode (anode - cathode) in volts.
   * @param params Zener diode physical parameters.
   * @return Small-signal conductance in siemens (always positive).
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double conductance(double vDiode, const ZenerDiodeParams& params) noexcept {
    const double G_JUNCTION = Junction::conductance(vDiode, params.Is, params.n * params.Vt);
    const double G_BREAKDOWN =
        (params.Ibv / params.Vbv) * Junction::limitedExpSlope(-(vDiode + params.Vz) / params.Vbv);
    return G_JUNCTION + G_BREAKDOWN;
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

    // Stamp linearized conductance; the equivalent source leaves the anode net
    mna.addConductance(anodeNet, cathodeNet, G);
    mna.addCurrent(anodeNet, cathodeNet, -IEQ);
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
