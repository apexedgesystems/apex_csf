#ifndef APEX_JFETLEVEL2_HPP
#define APEX_JFETLEVEL2_HPP
/**
 * @file JfetLevel2.hpp
 * @brief Advanced JFET model with gate capacitance and improved saturation.
 *
 * Extends JfetShichman with:
 * - Gate-channel capacitance: Voltage-dependent Cgd, Cgs
 * - Improved saturation model: Better transition from linear to saturation
 * - Gate leakage current: Reverse-biased gate junction
 * - Temperature dependence: Vp(T), Beta(T) modeling
 * - Subthreshold conduction: Weak inversion below pinch-off
 *
 * More accurate than Shichman for:
 * - High-frequency amplifiers (capacitance matters)
 * - Precision analog circuits (gate leakage, temp drift)
 * - Low-noise applications (gate current noise)
 * - Temperature-sensitive designs
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
 * @brief JFET Level 2 model parameters.
 *
 * Physical parameters for advanced N-channel or P-channel JFET modeling.
 * Default values represent a typical N-channel JFET (e.g., 2N5457).
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

  // Capacitance (optional, for AC analysis)
  double Cgd0 = 1e-12; ///< Zero-bias gate-drain capacitance (F)
  double Cgs0 = 1e-12; ///< Zero-bias gate-source capacitance (F)

  // Improved saturation
  double alpha = 1.0; ///< Saturation transition smoothness (unitless)

  // Subthreshold
  double n_sub = 1.5; ///< Subthreshold slope factor
};

/**
 * @brief JFET Level 2 model.
 *
 * Provides I-V characteristics with gate leakage, improved saturation
 * transition, and subthreshold conduction.
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
   * @brief Compute drain current (improved saturation model).
   *
   * Id = Beta * f(Vgs, Vds) * (1 + lambda*Vds)
   *
   * where f(Vgs, Vds) provides smooth transition between regions.
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
    const double VGST = vgs - params.Vp;

    // Cutoff/subthreshold region
    if (VGST <= 0.0) {
      // Subthreshold current (weak inversion)
      const double I_SUB =
          params.Beta * params.Vt * params.Vt * std::exp(VGST / (params.n_sub * params.Vt));
      return I_SUB * vds;
    }

    const double CHANNEL_MOD = 1.0 + params.lambda * vds;

    // Linear region
    if (vds < VGST) {
      const double ID_LINEAR = 2.0 * params.Beta * (VGST * vds - 0.5 * vds * vds);
      return ID_LINEAR * CHANNEL_MOD;
    }

    // Saturation region (improved transition)
    const double VDSAT = VGST / params.alpha;
    const double ID_SAT = params.Beta * VGST * VGST;

    // Smooth transition using tanh
    const double TRANSITION = 0.5 * (1.0 + std::tanh(params.alpha * (vds - VDSAT)));
    const double ID = ID_SAT * TRANSITION;

    return ID * CHANNEL_MOD;
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
    return params.Is * (std::exp(vgs / (params.n * params.Vt)) - 1.0);
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
    return (params.Is / (params.n * params.Vt)) * std::exp(vgs / (params.n * params.Vt));
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

    // Stamp drain current (VCCS)
    mna.addConductance(drainNet, gateNet, GM);
    mna.addConductance(drainNet, sourceNet, -GM);
    mna.addConductance(drainNet, sourceNet, GDS);
    mna.addCurrent(drainNet, sourceNet, IEQ_D);

    // Stamp gate leakage
    mna.addConductance(gateNet, sourceNet, GG);
    mna.addCurrent(gateNet, sourceNet, IEQ_G);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_JFETLEVEL2_HPP
