#ifndef APEX_JFETLEVEL2_HPP
#define APEX_JFETLEVEL2_HPP
/**
 * @file JfetLevel2.hpp
 * @brief JFET model with gate leakage and subthreshold conduction.
 *
 * Above pinch-off the drain current is the JfetShichman square law. This
 * model adds to it:
 * - Gate leakage current: the gate-source junction, reverse biased in
 *   normal operation
 * - Subthreshold conduction: a small drain current below pinch-off
 *
 * Use it where the gate current matters (high-impedance inputs, precision
 * analog) or where the device is biased near pinch-off.
 *
 * This is a PHYSICS MODEL (Layer 2) that uses Layer 1 Newton-Raphson solver
 * for nonlinear circuit simulation.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/Junction.hpp"

#include <algorithm>
#include <cmath>

namespace sim::electronics::devices::nonlinear {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/**
 * @brief JFET Level 2 model parameters.
 *
 * Physical parameters of an N-channel JFET. Default values represent a
 * typical small-signal device.
 */
struct JfetLevel2Params {
  // Basic parameters (Shichman)
  double Beta = 1e-3;   ///< Transconductance parameter (A/V^2)
  double Vp = -2.0;     ///< Pinch-off voltage (V), negative for N-channel
  double lambda = 0.01; ///< Channel-length modulation (1/V)

  // Gate junction
  double Is = 1e-14; ///< Gate junction saturation current (A)
  double n = 1.0;    ///< Gate junction ideality factor
  double Vt = 0.026; ///< Thermal voltage (V), kT/q ~= 26mV at 300K

  // Capacitance: carried for a caller's own companion models; the drain
  // and gate currents here do not read them
  double Cgd0 = 1e-12; ///< Zero-bias gate-drain capacitance (F)
  double Cgs0 = 1e-12; ///< Zero-bias gate-source capacitance (F)

  // Subthreshold
  double n_sub = 1.5; ///< Subthreshold slope factor: the slope voltage is n_sub*Vt
};

/**
 * @brief JFET Level 2 model.
 *
 * Provides I-V characteristics with gate leakage and subthreshold
 * conduction.
 *
 * Usage:
 * @code
 * JfetLevel2Params params{
 *   .Beta = 2e-3, .Vp = -2.5, .lambda = 0.02, .Is = 1e-13
 * };
 *
 * double vgs = -0.5, vds = 5.0;
 * double id = JfetLevel2::drainCurrent(vgs, vds, params);
 * double ig = JfetLevel2::gateCurrent(vgs, params);
 *
 * JfetLevel2::stamp(mna, drainNet, gateNet, sourceNet, vgs, vds, params);
 * @endcode
 */
struct JfetLevel2 {
  /**
   * @brief Compute drain current.
   *
   * With Vgst = Vgs - Vp, at and above the turn-on voltage
   * Von = Vp + n_sub*Vt the current is the square law:
   *   Linear (Vds < Vgst):      Id = 2*Beta*(Vgst*Vds - Vds^2/2) * (1 + lambda*Vds)
   *   Saturation (Vds >= Vgst): Id = Beta*Vgst^2 * (1 + lambda*Vds)
   * The two meet at Vds = Vgst with the same value and slope.
   *
   * Below Von the current is the square-law current at Von, scaled by
   * exp((Vgs - Von) / (n_sub*Vt)). It meets the square law at Von, and it
   * saturates in Vds as the square law does there. The slope in Vgs steps
   * at Von.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param params JFET parameters
   * @return Drain current in amperes
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double drainCurrent(double vgs, double vds,
                                           const JfetLevel2Params& params) noexcept {
    const double SLOPE_V = params.n_sub * params.Vt;
    const double VON = params.Vp + SLOPE_V;

    // The square law of the basic model, held at turn-on for a gate below it
    const double ID_SQUARE_LAW = JfetShichman::current(
        std::max(vgs, VON), vds,
        JfetShichmanParams{.Beta = params.Beta, .Vp = params.Vp, .lambda = params.lambda});
    if (vgs >= VON) {
      return ID_SQUARE_LAW;
    }

    // Subthreshold: exponential in the gate voltage below turn-on
    return ID_SQUARE_LAW * std::exp((vgs - VON) / SLOPE_V);
  }

  /**
   * @brief Compute gate leakage current (reverse-biased junction).
   *
   * Ig = Is * (exp(Vgs / (n*Vt)) - 1)
   *
   * For N-channel JFET with Vgs < 0, this is reverse leakage.
   *
   * @param vgs Gate-source voltage in volts
   * @param params JFET parameters
   * @return Gate current in amperes (positive = into gate)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double gateCurrent(double vgs, const JfetLevel2Params& params) noexcept {
    return Junction::current(vgs, params.Is, params.n * params.Vt);
  }

  /**
   * @brief Compute transconductance gm = dId/dVgs.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param params JFET parameters
   * @return Transconductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double transconductance(double vgs, double vds,
                                               const JfetLevel2Params& params) noexcept {
    constexpr double DV = 1e-8;
    const double I1 = drainCurrent(vgs - DV, vds, params);
    const double I2 = drainCurrent(vgs + DV, vds, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Compute output conductance gds = dId/dVds.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param params JFET parameters
   * @return Output conductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double outputConductance(double vgs, double vds,
                                                const JfetLevel2Params& params) noexcept {
    constexpr double DV = 1e-8;
    const double I1 = drainCurrent(vgs, vds - DV, params);
    const double I2 = drainCurrent(vgs, vds + DV, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Compute gate conductance gg = dIg/dVgs.
   *
   * @param vgs Gate-source voltage in volts
   * @param params JFET parameters
   * @return Gate conductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double gateConductance(double vgs, const JfetLevel2Params& params) noexcept {
    return Junction::conductance(vgs, params.Is, params.n * params.Vt);
  }

  /**
   * @brief Stamp JFET into MNA system for Newton-Raphson iteration.
   *
   * Includes both drain current (VCCS) and gate leakage current.
   *
   * @param mna MNA system to stamp into
   * @param drainNet Drain net ID
   * @param gateNet Gate net ID
   * @param sourceNet Source net ID
   * @param vgs Current gate-source voltage
   * @param vds Current drain-source voltage
   * @param params JFET parameters
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet, double vgs,
                    double vds, const JfetLevel2Params& params) {
    // Drain current (VCCS controlled by Vgs)
    const double ID = drainCurrent(vgs, vds, params);
    const double GM = transconductance(vgs, vds, params);
    const double GDS = outputConductance(vgs, vds, params);
    const double IEQ_D = ID - GM * vgs - GDS * vds;

    // Gate leakage current
    const double IG = gateCurrent(vgs, params);
    const double GG = gateConductance(vgs, params);
    const double IEQ_G = IG - GG * vgs;

    // Drain current: gm controlled by Vgs, gds across drain-source; the
    // equivalent source leaves the drain net
    mna.addTransconductance(drainNet, sourceNet, gateNet, sourceNet, GM);
    mna.addConductance(drainNet, sourceNet, GDS);
    mna.addCurrent(drainNet, sourceNet, -IEQ_D);

    // Gate leakage: a junction from gate to source
    mna.addConductance(gateNet, sourceNet, GG);
    mna.addCurrent(gateNet, sourceNet, -IEQ_G);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_JFETLEVEL2_HPP
