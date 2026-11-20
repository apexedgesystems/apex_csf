#ifndef APEX_MOSFETLEVEL2_HPP
#define APEX_MOSFETLEVEL2_HPP
/**
 * @file MosfetLevel2.hpp
 * @brief SPICE Level 2 MOSFET model with geometry-dependent effects.
 *
 * Extends Level 1 (Shichman-Hodges) with:
 * - Geometry effects: Width (W), Length (L), channel width modulation
 * - Mobility degradation: mu(Vgs) = mu0 / (1 + theta*(Vgs - Vth))
 * - Velocity saturation: Vdsat = Vgst / (1 + Vgst / (E_crit * L))
 * - Threshold voltage modulation: Vth(Vsb) = Vth0 + gamma*(sqrt(phi + Vsb) - sqrt(phi))
 * - Subthreshold conduction: weak-inversion current below the turn-on
 *   voltage Von = Vth + n_sub*Vt, exponential in Vgs
 *
 * More accurate than Level 1 for:
 * - Short-channel devices (L < 2 um)
 * - High-field effects (velocity saturation)
 * - Body-effect circuits (source-bulk bias)
 * - Precision analog design (threshold shifts, mobility)
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
 * @brief SPICE Level 2 MOSFET parameters.
 *
 * Physical and geometric parameters for N-channel or P-channel MOSFET.
 * Default values represent a typical N-channel MOSFET at room temperature.
 */
struct MosfetLevel2Params {
  // Basic parameters (Level 1)
  double Kp = 100e-6;   ///< Process transconductance (A/V^2), mu * Cox
  double Vth0 = 0.7;    ///< Zero-bias threshold voltage (V)
  double lambda = 0.02; ///< Channel-length modulation (1/V)

  // Geometry
  double W = 10e-6; ///< Channel width (m)
  double L = 1e-6;  ///< Channel length (m)

  // Mobility degradation
  double theta = 0.1; ///< Mobility degradation coefficient (1/V)

  // Velocity saturation
  double E_crit = 1e6; ///< Critical electric field (V/m)

  // Body effect (threshold modulation)
  double gamma = 0.5; ///< Body-effect parameter (V^0.5)
  double phi = 0.6;   ///< Surface potential (V), 2*phi_f

  // Subthreshold
  double n_sub = 1.5; ///< Subthreshold slope factor: the slope voltage is n_sub*Vt
  double Vt = 0.026;  ///< Thermal voltage (V), kT/q ~= 26mV at 300K
};

/**
 * @brief SPICE Level 2 MOSFET model.
 *
 * Provides I-V characteristics and derivatives for use with Newton-Raphson
 * nonlinear solver. Includes geometry, mobility, velocity saturation, and
 * body-effect modeling.
 *
 * Usage:
 * @code
 * MosfetLevel2Params params{
 *   .Kp = 200e-6, .Vth0 = 0.5, .W = 20e-6, .L = 0.5e-6
 * };
 *
 * // Operating point
 * double vgs = 2.0, vds = 3.0, vbs = 0.0;
 * double id = MosfetLevel2::current(vgs, vds, vbs, params);
 *
 * // Stamp into MNA system
 * MosfetLevel2::stamp(mna, drainNet, gateNet, sourceNet, bulkNet,
 *                     vgs, vds, vbs, params);
 * @endcode
 */
struct MosfetLevel2 {
  /**
   * @brief Compute threshold voltage with body effect.
   *
   * Vth = Vth0 + gamma * (sqrt(phi + Vsb) - sqrt(phi))
   *
   * @param vbs Bulk-source voltage in volts (usually 0 or negative)
   * @param params MOSFET parameters
   * @return Threshold voltage in volts
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double thresholdVoltage(double vbs,
                                               const MosfetLevel2Params& params) noexcept {
    // Body effect: Vth increases with reverse bulk-source bias
    const double VSB = -vbs; // Source-bulk voltage (typically positive)
    if (VSB <= 0.0) {
      return params.Vth0;
    }

    const double SQRT_TERM = std::sqrt(params.phi + VSB) - std::sqrt(params.phi);
    return params.Vth0 + params.gamma * SQRT_TERM;
  }

  /**
   * @brief Compute effective mobility with field-dependent degradation.
   *
   * mu_eff = mu0 / (1 + theta * (Vgs - Vth))
   *
   * @param vgs Gate-source voltage in volts
   * @param vth Threshold voltage in volts
   * @param params MOSFET parameters
   * @return Mobility degradation factor (unitless)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double mobilityFactor(double vgs, double vth,
                                             const MosfetLevel2Params& params) noexcept {
    const double VGST = vgs - vth;
    if (VGST <= 0.0) {
      return 1.0;
    }
    return 1.0 / (1.0 + params.theta * VGST);
  }

  /**
   * @brief Compute drain saturation voltage with velocity saturation.
   *
   * Vdsat = Vgst / (1 + Vgst / (E_crit * L))
   *
   * @param vgs Gate-source voltage in volts
   * @param vth Threshold voltage in volts
   * @param params MOSFET parameters
   * @return Drain saturation voltage in volts
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double drainSaturationVoltage(double vgs, double vth,
                                                     const MosfetLevel2Params& params) noexcept {
    const double VGST = vgs - vth;
    if (VGST <= 0.0) {
      return 0.0;
    }

    const double VDSAT_IDEAL = VGST;
    const double VDSAT_SAT = params.E_crit * params.L;

    // Velocity saturation reduces effective saturation voltage
    return VDSAT_IDEAL / (1.0 + VDSAT_IDEAL / VDSAT_SAT);
  }

  /**
   * @brief Compute MOSFET drain current (Level 2 model).
   *
   * Includes geometry, mobility degradation, velocity saturation, body effect,
   * and subthreshold conduction.
   *
   * With Vgst = Vgs - Vth, at and above the turn-on voltage
   * Von = Vth + n_sub*Vt the device is in strong inversion:
   *   Id = beta * (Vgst*V - V^2/2) * (1 + lambda*Vds),  V = min(Vds, Vdsat)
   * so the saturation current is the linear-region current at Vdsat and the
   * two regions meet there.
   *
   * Below Von the current is the strong-inversion current at Von, scaled by
   * exp((Vgs - Von) / (n_sub*Vt)). It meets the strong-inversion current at
   * Von, and it saturates in Vds as that current does there. The slope in
   * Vgs steps at Von.
   *
   * @param vgs Gate-source voltage in volts
   * @param vds Drain-source voltage in volts
   * @param vbs Bulk-source voltage in volts (usually 0 or negative)
   * @param params MOSFET parameters
   * @return Drain current in amperes (positive = drain to source)
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double current(double vgs, double vds, double vbs,
                                      const MosfetLevel2Params& params) noexcept {
    const double VTH = thresholdVoltage(vbs, params);
    const double SLOPE_V = params.n_sub * params.Vt;
    const double VON = VTH + SLOPE_V;

    // Strong inversion, held at turn-on for a gate below it
    const double VGS_STRONG = std::max(vgs, VON);
    const double VGST = VGS_STRONG - VTH;
    const double MU_FACTOR = mobilityFactor(VGS_STRONG, VTH, params);
    const double BETA_EFF = params.Kp * params.W / params.L * MU_FACTOR;
    const double VDSAT = drainSaturationVoltage(VGS_STRONG, VTH, params);

    // Channel-length modulation
    const double CLM = 1.0 + params.lambda * vds;

    // The channel voltage stops at Vdsat, so saturation carries the
    // linear-region current at Vdsat whatever lowers Vdsat below the overdrive
    const double V_CHANNEL = std::min(vds, VDSAT);
    const double ID_STRONG = BETA_EFF * (VGST * V_CHANNEL - 0.5 * V_CHANNEL * V_CHANNEL) * CLM;
    if (vgs >= VON) {
      return ID_STRONG;
    }

    // Weak inversion: exponential in the gate voltage below turn-on
    return ID_STRONG * std::exp((vgs - VON) / SLOPE_V);
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
                                               const MosfetLevel2Params& params) noexcept {
    // Numerical derivative for complex Level 2 model
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
                                                const MosfetLevel2Params& params) noexcept {
    // Numerical derivative
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
                                                   const MosfetLevel2Params& params) noexcept {
    // Numerical derivative
    constexpr double DV = 1e-8;
    const double I1 = current(vgs, vds, vbs - DV, params);
    const double I2 = current(vgs, vds, vbs + DV, params);
    return (I2 - I1) / (2.0 * DV);
  }

  /**
   * @brief Stamp MOSFET into MNA system for Newton-Raphson iteration.
   *
   * Linearizes MOSFET around current operating point using three-terminal
   * (gate, drain, source, bulk) transconductances.
   *
   * @param mna MNA system to stamp into
   * @param drainNet Drain net ID
   * @param gateNet Gate net ID
   * @param sourceNet Source net ID
   * @param bulkNet Bulk (substrate) net ID
   * @param vgs Current gate-source voltage
   * @param vds Current drain-source voltage
   * @param vbs Current bulk-source voltage
   * @param params MOSFET parameters
   *
   * @note RT-safe (stamps into pre-allocated matrix/vector).
   */
  static void stamp(MnaSystem& mna, NetID drainNet, NetID gateNet, NetID sourceNet, NetID bulkNet,
                    double vgs, double vds, double vbs, const MosfetLevel2Params& params) {
    const double ID = current(vgs, vds, vbs, params);
    const double GM = transconductance(vgs, vds, vbs, params);
    const double GDS = outputConductance(vgs, vds, vbs, params);
    const double GMB = bulkTransconductance(vgs, vds, vbs, params);

    // Equivalent current: Ieq = Id - gm*Vgs - gds*Vds - gmb*Vbs
    const double IEQ = ID - GM * vgs - GDS * vds - GMB * vbs;

    // Transconductance gm: drain-to-source current controlled by Vgs
    mna.addTransconductance(drainNet, sourceNet, gateNet, sourceNet, GM);

    // Output conductance gds (drain-source)
    mna.addConductance(drainNet, sourceNet, GDS);

    // Bulk transconductance gmb: drain-to-source current controlled by Vbs
    mna.addTransconductance(drainNet, sourceNet, bulkNet, sourceNet, GMB);

    // Equivalent current source (leaves the drain net)
    mna.addCurrent(drainNet, sourceNet, -IEQ);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_MOSFETLEVEL2_HPP
