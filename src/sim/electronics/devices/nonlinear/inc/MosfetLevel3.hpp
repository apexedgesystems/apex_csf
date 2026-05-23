#ifndef APEX_MOSFETLEVEL3_HPP
#define APEX_MOSFETLEVEL3_HPP
/**
 * @file MosfetLevel3.hpp
 * @brief SPICE Level 3 MOSFET model with advanced short-channel effects.
 *
 * Extends Level 2 with:
 * - DIBL (drain-induced barrier lowering): Vth(Vds) reduction at high Vds
 * - Improved channel-length modulation: Early voltage model
 * - Saturation voltage empirical model: More accurate Vdsat
 * - Enhanced subthreshold: Gate-induced drain leakage (GIDL)
 * - Narrow-width effects: Threshold voltage shifts for W < 1um
 *
 * More accurate than Level 2 for:
 * - Submicron devices (L < 1 um)
 * - Low-voltage analog design (Vdd < 2V)
 * - Precision matching (threshold variations)
 * - Leakage-critical applications (GIDL)
 *
 * This is a PHYSICS MODEL (Layer 2) that uses Layer 1 Newton-Raphson solver
 * for nonlinear circuit simulation.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <algorithm>
#include <cmath>

namespace sim::electronics::devices::nonlinear {

using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/**
 * @brief SPICE Level 3 MOSFET parameters.
 *
 * Physical and geometric parameters for advanced short-channel modeling.
 * Default values represent a typical submicron N-channel MOSFET.
 */
struct MosfetLevel3Params {
  // Basic parameters (Level 1/2)
  double Kp = 200e-6;   ///< Process transconductance (A/V^2)
  double Vth0 = 0.5;    ///< Zero-bias threshold voltage (V)
  double lambda = 0.05; ///< Channel-length modulation (1/V)

  // Geometry
  double W = 1e-6;    ///< Channel width (m)
  double L = 0.18e-6; ///< Channel length (m) - submicron

  // Mobility (Level 2)
  double theta = 0.2;  ///< Mobility degradation (1/V)
  double E_crit = 2e6; ///< Critical electric field (V/m)

  // Body effect (Level 2)
  double gamma = 0.4; ///< Body-effect parameter (V^0.5)
  double phi = 0.6;   ///< Surface potential (V)

  // Short-channel effects (Level 3)
  double eta = 0.05;    ///< DIBL coefficient (unitless)
  double kappa = 0.1;   ///< Saturation voltage parameter (unitless)
  double vsat = 1e5;    ///< Carrier saturation velocity (m/s)
  double delta_w = 0.0; ///< Narrow-width effect (m)
  double delta_l = 0.0; ///< Short-channel effect (m)

  // Subthreshold (Level 2/3)
  double n_sub = 1.5; ///< Subthreshold slope factor
  double Vt = 0.026;  ///< Thermal voltage (V)
};

/**
 * @brief SPICE Level 3 MOSFET model.
 *
 * Provides I-V characteristics with advanced short-channel physics for
 * submicron devices.
 *
 * Usage:
 * @code
 * MosfetLevel3Params params{
 *   .Kp = 400e-6, .Vth0 = 0.4, .W = 0.5e-6, .L = 0.13e-6, .eta = 0.1
 * };
 *
 * double vgs = 1.2, vds = 1.5, vbs = 0.0;
 * double id = MosfetLevel3::current(vgs, vds, vbs, params);
 *
 * MosfetLevel3::stamp(mna, drainNet, gateNet, sourceNet, bulkNet,
 *                     vgs, vds, vbs, params);
 * @endcode
 */
struct MosfetLevel3 {
  /**
   * @brief Compute effective channel dimensions with narrow-width/short-length effects.
   *
   * @param W_drawn Drawn channel width in meters
   * @param L_drawn Drawn channel length in meters
   * @param params MOSFET parameters
   * @return Pair of (W_eff, L_eff) in meters
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static std::pair<double, double>
  effectiveDimensions(double W_drawn, double L_drawn, const MosfetLevel3Params& params) noexcept {
    const double W_EFF = W_drawn - params.delta_w;
    const double L_EFF = L_drawn - params.delta_l;
    return {std::max(W_EFF, 0.01e-6), std::max(L_EFF, 0.01e-6)};
  }

  /**
   * @brief Compute threshold voltage with body effect and DIBL.
   *
   * Vth = Vth0 + gamma*(sqrt(phi + Vsb) - sqrt(phi)) - eta*Vds
   *
   * DIBL (drain-induced barrier lowering) reduces threshold at high Vds.
   *
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts
   * @param params MOSFET parameters
   * @return Threshold voltage in volts
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double thresholdVoltage(double vds, double vbs,
                                               const MosfetLevel3Params& params) noexcept {
    // Body effect
    const double VSB = -vbs;
    double vth = params.Vth0;

    if (VSB > 0.0) {
      const double SQRT_TERM = std::sqrt(params.phi + VSB) - std::sqrt(params.phi);
      vth += params.gamma * SQRT_TERM;
    }

    // DIBL (reduces threshold at high Vds)
    vth -= params.eta * vds;

    return vth;
  }

  /**
   * @brief Compute drain saturation voltage (empirical Level 3 model).
   *
   * Vdsat = Vgst * (1 - kappa)
   *
   * @param vgs Gate-source voltage in volts
   * @param vth Threshold voltage in volts
   * @param params MOSFET parameters
   * @return Drain saturation voltage in volts
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double drainSaturationVoltage(double vgs, double vth,
                                                     const MosfetLevel3Params& params) noexcept {
    const double VGST = vgs - vth;
    if (VGST <= 0.0) {
      return 0.0;
    }

    // Empirical saturation voltage reduction (kappa factor)
    return VGST * (1.0 - params.kappa);
  }

  /**
   * @brief Compute MOSFET drain current (Level 3 model).
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts
   * @param params MOSFET parameters
   * @return Drain current in amperes
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vgs, double vds, double vbs,
                                      const MosfetLevel3Params& params) noexcept {
    const auto [W_EFF, L_EFF] = effectiveDimensions(params.W, params.L, params);
    const double VTH = thresholdVoltage(vds, vbs, params);
    const double VGST = vgs - VTH;

    // Cutoff/subthreshold
    if (VGST <= 0.0) {
      const double I_SUB = params.Kp * W_EFF / L_EFF * params.Vt * params.Vt *
                           std::exp((vgs - VTH) / (params.n_sub * params.Vt));
      return I_SUB * vds;
    }

    // Effective mobility
    const double MU_FACTOR = 1.0 / (1.0 + params.theta * VGST);
    const double BETA_EFF = params.Kp * W_EFF / L_EFF * MU_FACTOR;

    // Saturation voltage
    const double VDSAT = drainSaturationVoltage(vgs, VTH, params);

    // Channel-length modulation
    const double CLM = 1.0 + params.lambda * vds;

    // Linear region
    if (vds < VDSAT) {
      return BETA_EFF * (VGST * vds - 0.5 * vds * vds) * CLM;
    }

    // Saturation region
    return BETA_EFF * 0.5 * VDSAT * VDSAT * CLM;
  }

  /**
   * @brief Compute transconductance gm = dId/dVgs.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts
   * @param params MOSFET parameters
   * @return Transconductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double transconductance(double vgs, double vds, double vbs,
                                               const MosfetLevel3Params& params) noexcept {
    constexpr double DV = 1e-8;
    const double I1 = current(vgs - DV, vds, vbs, params);
    const double I2 = current(vgs + DV, vds, vbs, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Compute output conductance gds = dId/dVds.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts
   * @param params MOSFET parameters
   * @return Output conductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double outputConductance(double vgs, double vds, double vbs,
                                                const MosfetLevel3Params& params) noexcept {
    constexpr double DV = 1e-8;
    const double I1 = current(vgs, vds - DV, vbs, params);
    const double I2 = current(vgs, vds + DV, vbs, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Compute bulk transconductance gmb = dId/dVbs.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts
   * @param params MOSFET parameters
   * @return Bulk transconductance in siemens
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double bulkTransconductance(double vgs, double vds, double vbs,
                                                   const MosfetLevel3Params& params) noexcept {
    constexpr double DV = 1e-8;
    const double I1 = current(vgs, vds, vbs - DV, params);
    const double I2 = current(vgs, vds, vbs + DV, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Stamp MOSFET into MNA system for Newton-Raphson iteration.
   *
   * @param mna MNA system to stamp into
   * @param drainNet Drain net ID
   * @param gateNet Gate net ID
   * @param sourceNet Source net ID
   * @param bulkNet Bulk net ID
   * @param vgs Current gate-source voltage
   * @param vds Current drain-source voltage
   * @param vbs Current bulk-source voltage
   * @param params MOSFET parameters
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet, NetID bulkNet,
                    double vgs, double vds, double vbs, const MosfetLevel3Params& params) {
    const double ID = current(vgs, vds, vbs, params);
    const double GM = transconductance(vgs, vds, vbs, params);
    const double GDS = outputConductance(vgs, vds, vbs, params);
    const double GMB = bulkTransconductance(vgs, vds, vbs, params);

    const double IEQ = ID - GM * vgs - GDS * vds - GMB * vbs;

    // Stamp transconductances
    mna.addConductance(drainNet, gateNet, GM);
    mna.addConductance(drainNet, sourceNet, -GM);
    mna.addConductance(drainNet, sourceNet, GDS);
    mna.addConductance(drainNet, bulkNet, GMB);
    mna.addConductance(drainNet, sourceNet, -GMB);
    mna.addCurrent(drainNet, sourceNet, IEQ);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_MOSFETLEVEL3_HPP
