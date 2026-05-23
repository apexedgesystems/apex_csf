#ifndef APEX_JFETSHICHMAN_HPP
#define APEX_JFETSHICHMAN_HPP
/**
 * @file JfetShichman.hpp
 * @brief Junction Field-Effect Transistor (JFET) Shichman-Hodges model.
 *
 * Implements 3-region JFET model for precision analog circuits and analog switches.
 * Used in high-input-impedance op-amps (TL071, LF356, OPA134) and analog switches.
 *
 * Three operating regions:
 * 1. Cutoff (Vgs > Vp): Id = 0 (gate reverse-biased beyond pinch-off)
 * 2. Linear (Vgs < Vp, Vds < Vgst): Ohmic region, controlled resistance
 * 3. Saturation (Vgs < Vp, Vds >= Vgst): Constant current source
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
 * @brief JFET Shichman-Hodges model parameters.
 *
 * Physical parameters defining JFET behavior. Default values represent
 * a typical N-channel JFET at room temperature.
 */
struct JfetShichmanParams {
  double Beta = 1e-3;   ///< Transconductance parameter (A/V^2)
  double Vp = -2.0;     ///< Pinch-off voltage (V), negative for N-channel
  double lambda = 0.01; ///< Channel-length modulation (1/V)
};

/**
 * @brief JFET Shichman-Hodges physics model.
 *
 * Provides I-V characteristic and derivatives for use with Newton-Raphson
 * nonlinear solver. Implements classic 3-region JFET model.
 *
 * Usage:
 * @code
 * JfetShichmanParams params{.Beta = 1e-3, .Vp = -2.0};
 *
 * // Saturation region (typical operating point)
 * double vgs = -0.5, vds = 5.0;
 * double id = JfetShichman::current(vgs, vds, params);  // Constant current
 *
 * // Linear region (analog switch)
 * double id_lin = JfetShichman::current(-0.5, 0.1, params);  // Ohmic
 *
 * // Stamp into MNA system for Newton-Raphson
 * JfetShichman::stamp(mna, drainNet, gateNet, sourceNet, vgs, vds, params);
 * @endcode
 */
struct JfetShichman {
  /**
   * @brief Compute JFET drain current.
   *
   * Cutoff: Id = 0 (Vgs > Vp)
   * Linear: Id = 2*Beta*[(Vgs - Vp)*Vds - 0.5*Vds^2] * (1 + lambda*Vds)
   * Saturation: Id = Beta*(Vgs - Vp)^2 * (1 + lambda*Vds)
   *
   * @param vgs Gate-source voltage (V)
   * @param vds Drain-source voltage (V)
   * @param params JFET physical parameters
   * @return Drain current (A), positive from drain to source
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vgs, double vds,
                                      const JfetShichmanParams& params) noexcept {
    // Cutoff region: gate at or beyond pinch-off (Vgs <= Vp for N-channel)
    if (vgs <= params.Vp) {
      return 0.0;
    }

    const double VGST = vgs - params.Vp; // Gate overdrive (positive for conduction)
    const double CHANNEL_MOD = 1.0 + params.lambda * vds;

    // Linear region: ohmic behavior
    if (vds < VGST) {
      return 2.0 * params.Beta * (VGST * vds - 0.5 * vds * vds) * CHANNEL_MOD;
    }

    // Saturation region: constant current source
    return params.Beta * VGST * VGST * CHANNEL_MOD;
  }

  /**
   * @brief Compute transconductance (dId/dVgs) for Newton-Raphson.
   *
   * @param vgs Gate-source voltage (V)
   * @param vds Drain-source voltage (V)
   * @param params JFET physical parameters
   * @return Transconductance (S)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double transconductance(double vgs, double vds,
                                               const JfetShichmanParams& params) noexcept {
    // Cutoff region
    if (vgs <= params.Vp) {
      return 0.0;
    }

    const double VGST = vgs - params.Vp;
    const double CHANNEL_MOD = 1.0 + params.lambda * vds;

    // Linear region
    if (vds < VGST) {
      return 2.0 * params.Beta * vds * CHANNEL_MOD;
    }

    // Saturation region
    return 2.0 * params.Beta * VGST * CHANNEL_MOD;
  }

  /**
   * @brief Compute output conductance (dId/dVds) for Newton-Raphson.
   *
   * @param vgs Gate-source voltage (V)
   * @param vds Drain-source voltage (V)
   * @param params JFET physical parameters
   * @return Output conductance (S)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double outputConductance(double vgs, double vds,
                                                const JfetShichmanParams& params) noexcept {
    // Cutoff region
    if (vgs <= params.Vp) {
      return 0.0;
    }

    const double VGST = vgs - params.Vp;
    const double CHANNEL_MOD = 1.0 + params.lambda * vds;
    const double ID0 = current(vgs, vds, params); // Current without lambda

    // Linear region
    if (vds < VGST) {
      const double DID_DVDS_MAIN = 2.0 * params.Beta * (VGST - vds) * CHANNEL_MOD;
      const double DID_DVDS_LAMBDA = (ID0 / CHANNEL_MOD) * params.lambda;
      return DID_DVDS_MAIN + DID_DVDS_LAMBDA;
    }

    // Saturation region (mainly channel-length modulation)
    return params.lambda * params.Beta * VGST * VGST;
  }

  /**
   * @brief Stamp JFET into MNA system for Newton-Raphson iteration.
   *
   * Linearizes JFET around current operating point using companion model:
   *   Id(Vgs, Vds) ~= gm*Vgs + gds*Vds + Ieq
   *   where: gm = dId/dVgs, gds = dId/dVds
   *
   * @param mna MNA system to stamp into
   * @param drainNet Drain net ID
   * @param gateNet Gate net ID
   * @param sourceNet Source net ID
   * @param vgs Current gate-source voltage (from previous iteration)
   * @param vds Current drain-source voltage (from previous iteration)
   * @param params JFET physical parameters
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet, double vgs,
                    double vds, const JfetShichmanParams& params) {
    const double ID = current(vgs, vds, params);
    const double GM = transconductance(vgs, vds, params);
    const double GDS = outputConductance(vgs, vds, params);

    // Equivalent current source: Ieq = Id - gm*Vgs - gds*Vds
    const double IEQ = ID - GM * vgs - GDS * vds;

    // Stamp transconductance (gate-controlled current source)
    // Creates VCCS: Id controlled by Vgs
    mna.addConductance(drainNet, gateNet, GM);
    mna.addConductance(drainNet, sourceNet, -GM);

    // Stamp output conductance (drain-source resistance)
    mna.addConductance(drainNet, sourceNet, GDS);

    // Stamp equivalent current source
    mna.addCurrent(drainNet, sourceNet, IEQ);
  }

  /**
   * @brief Determine JFET operating region.
   *
   * @param vgs Gate-source voltage (V)
   * @param vds Drain-source voltage (V)
   * @param params JFET physical parameters
   * @return Region code: 0=cutoff, 1=linear, 2=saturation
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static int region(double vgs, double vds,
                                  const JfetShichmanParams& params) noexcept {
    if (vgs <= params.Vp) {
      return 0; // Cutoff
    }
    const double VGST = vgs - params.Vp;
    if (vds < VGST) {
      return 1; // Linear
    }
    return 2; // Saturation
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_JFETSHICHMAN_HPP
