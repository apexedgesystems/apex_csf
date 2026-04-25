#ifndef APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBSIM3_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBSIM3_HPP
/**
 * @file MosfetBsim3.hpp
 * @brief BSIM3v3 MOSFET model (Berkeley Short-channel IGFET Model v3).
 *
 * BSIM3 is the industry-standard MOSFET model for sub-micron processes
 * since 1996. Unlike Shichman-Hodges (Level 1) or SPICE Level 2 -- both
 * of which have piecewise regions glued together with discontinuous
 * derivatives at the boundaries -- BSIM3 uses a single smooth equation
 * spanning weak / moderate / strong inversion. That continuity is what
 * makes the cross-coupled latch feedback core actually resolve from
 * mid-rail (the fundamental Shichman-Hodges limitation that blocks
 * Intel 4004 L2 100% physics).
 *
 * Core innovations vs Level 1/2:
 *
 *  1. Smooth `Vgst_eff` blending function:
 *
 *       Vgst_eff = n*Vt * ln(1 + exp((Vgs - Vth) / (n*Vt)))
 *
 *     Reduces to Vgs - Vth far above threshold (strong inversion) and
 *     to Vt * exp((Vgs - Vth)/(n*Vt)) far below (weak inversion).
 *     Continuous and analytically differentiable everywhere.
 *
 *  2. DIBL (Drain Induced Barrier Lowering): Vth lowers with Vds,
 *     captured via the eta0 parameter:
 *
 *       Vth(Vds) = Vth0 - eta0 * Vds   (linear approximation; full
 *                                         BSIM uses a more elaborate
 *                                         expression with VtFactor)
 *
 *  3. Body effect with K1 / K2 (more accurate than Level 2's gamma):
 *
 *       Vth(Vbs) = Vth0 + K1*sqrt(phi-Vbs) - K1*sqrt(phi) - K2*Vbs
 *
 *  4. Smooth Vds saturation (single equation, no triode/saturation
 *     branching):
 *
 *       Vdseff = Vdsat - 0.5*(Vdsat - Vds - delta + sqrt((Vdsat-Vds-delta)^2 + 4*delta*Vdsat))
 *
 *  5. Mobility with vertical field degradation (ua, ub).
 *
 * This implementation is a *minimal* BSIM3 -- enough effects to fix
 * the latch feedback issue without the 100+ parameters of full BSIM3v3.
 * Reference: ngspice `src/spicelib/devices/bsim3/` (the canonical
 * implementation), and BSIM3v3.3 manual (UC Berkeley, 2005).
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <algorithm>
#include <cmath>

namespace sim::electronics::devices::nonlinear {

using mna::MnaSystem;
using mna::NetID;

/**
 * @brief Minimal BSIM3 parameters.
 *
 * Captures the essential effects: smooth weak/strong inversion
 * transition, DIBL, body effect, mobility degradation, channel-
 * length modulation. ~15 parameters vs full BSIM3's 100+.
 *
 * Defaults are calibrated for the Intel 4004's PMOS process (10 µm,
 * 1971). For other processes, override per-device.
 */
struct MosfetBsim3Params {
  // Basic transconductance / threshold
  double Kp = 5e-3;     ///< Process transconductance (A/V^2), mu*Cox
  double Vth0 = 1.17;   ///< Zero-bias threshold voltage (V)
  double lambda = 0.03; ///< Channel-length modulation (1/V)

  // Geometry. Set W = WL_ratio, L = 1.0 to fold W/L into Kp.
  double W = 1.0; ///< Channel width
  double L = 1.0; ///< Channel length

  // Smooth-inversion blending (the key BSIM3 feature)
  double n_factor = 1.5; ///< Subthreshold slope factor (1.0..2.5 typ.)
  double Vt = 0.026;     ///< Thermal voltage (V), kT/q at 300K

  // DIBL (Drain Induced Barrier Lowering)
  double eta0 = 0.08; ///< Vth shift per Vds (V/V), 0..0.2 typical

  // Body effect (BSIM3 K1/K2 formulation, more accurate than Level 2 gamma)
  double K1 = 0.5;  ///< First-order body effect coefficient (sqrt(V))
  double K2 = 0.0;  ///< Second-order body-effect coefficient (1/V)
  double phi = 0.7; ///< Surface potential at strong inversion (V)

  // Mobility degradation by vertical field
  double ua = 1e-9; ///< Linear mobility-degradation coefficient (m/V)
  double ub = 0.0;  ///< Quadratic mobility-degradation coefficient (m/V)^2
  double tox = 50e-9; ///< Oxide thickness (m); 50 nm for 10 µm process

  // Smooth Vds saturation transition width
  double delta = 0.01; ///< Vds smoothing parameter (V)
};

/**
 * @brief BSIM3v3 MOSFET model (minimal).
 *
 * Provides id / gm / gds / gmb for the Newton-Raphson stamp. Mirrors
 * the API of MosfetLevel1 / MosfetLevel2 so callers can swap models
 * per-transistor without touching the MNA framework.
 *
 * Usage:
 * @code
 * MosfetBsim3Params params{ .Kp = 5e-3, .Vth0 = 1.17, .W = 3.23, .L = 1.0 };
 * double vgs = 1.5, vds = 2.0, vbs = 0.0;
 * double id  = MosfetBsim3::current(vgs, vds, vbs, params);
 * double gm  = MosfetBsim3::transconductance(vgs, vds, vbs, params);
 * double gds = MosfetBsim3::outputConductance(vgs, vds, vbs, params);
 * @endcode
 */
struct MosfetBsim3 {
  /* ----------------------------- Threshold voltage ----------------------------- */

  /**
   * @brief Threshold voltage with body effect and DIBL.
   *
   * Vth = Vth0 + K1*(sqrt(phi-Vbs) - sqrt(phi)) - K2*Vbs - eta0*Vds
   *
   * @param vds  Drain-source voltage (V), used for DIBL.
   * @param vbs  Bulk-source voltage (V), typically 0 or negative.
   * @param p    Parameters.
   * @return Effective threshold voltage (V).
   */
  [[nodiscard]] static double thresholdVoltage(double vds, double vbs,
                                               const MosfetBsim3Params& p) noexcept {
    double vth = p.Vth0;
    // Body effect.
    const double phiMinusVbs = std::max(p.phi - vbs, 1e-12);
    vth += p.K1 * (std::sqrt(phiMinusVbs) - std::sqrt(p.phi));
    vth -= p.K2 * vbs;
    // DIBL: Vth decreases with positive Vds.
    vth -= p.eta0 * std::max(vds, 0.0);
    return vth;
  }

  /* ----------------------------- Smooth Vgst_eff ----------------------------- */

  /**
   * @brief Smooth effective gate-source overdrive.
   *
   *   Vgst_eff = n*Vt * ln(1 + exp((Vgs - Vth) / (n*Vt)))
   *
   * Approaches `Vgs - Vth` deep in strong inversion; approaches
   * `Vt * exp((Vgs - Vth) / (n*Vt))` deep in weak inversion. Smooth
   * and analytically differentiable across the moderate-inversion
   * region. This is the **key BSIM3 feature** that gives the
   * latch feedback core a continuous derivative across the operating
   * point (Shichman-Hodges has a derivative discontinuity at Vth).
   */
  [[nodiscard]] static double vgstEff(double vgs, double vth,
                                      const MosfetBsim3Params& p) noexcept {
    const double nVt = p.n_factor * p.Vt;
    const double x = (vgs - vth) / nVt;
    // log1p / expm1 + clamp to avoid overflow at large positive x.
    if (x > 50.0) {
      // Strong inversion: Vgst_eff ≈ Vgs - Vth.
      return vgs - vth;
    }
    if (x < -50.0) {
      // Deep weak inversion: Vgst_eff ≈ nVt * exp(x) ≈ 0.
      return nVt * std::exp(x);
    }
    return nVt * std::log1p(std::exp(x));
  }

  /**
   * @brief dVgst_eff / dVgs analytically.
   *
   * Differentiating the smooth blend:
   *   d(Vgst_eff)/d(Vgs) = sigmoid(x) where x = (Vgs - Vth) / (n*Vt).
   * Approaches 1 in strong inversion, 0 in weak.
   */
  [[nodiscard]] static double dVgstEff_dVgs(double vgs, double vth,
                                            const MosfetBsim3Params& p) noexcept {
    const double nVt = p.n_factor * p.Vt;
    const double x = (vgs - vth) / nVt;
    if (x > 50.0) return 1.0;
    if (x < -50.0) return 0.0;
    // sigmoid(x) = 1 / (1 + exp(-x))
    return 1.0 / (1.0 + std::exp(-x));
  }

  /* ----------------------------- Saturation voltage ----------------------------- */

  /**
   * @brief Saturation voltage with smooth transition.
   *
   * Vdsat = Vgst_eff (simplified, ignoring velocity saturation for
   * the 10 µm process where velocity saturation is negligible).
   * Full BSIM3 includes Esat*L correction; for our process L is large
   * enough that Esat*L >> Vgst, so Vdsat ≈ Vgst_eff.
   */
  [[nodiscard]] static double saturationVoltage(double vgstEffVal,
                                                const MosfetBsim3Params& /*p*/) noexcept {
    return std::max(vgstEffVal, 0.0);
  }

  /**
   * @brief Smooth effective Vds clamp at Vdsat.
   *
   * Vdseff = Vdsat - 0.5 * (Vdsat - Vds - delta
   *                          + sqrt((Vdsat - Vds - delta)^2 + 4*delta*Vdsat))
   *
   * Approaches Vds in linear region, Vdsat in saturation, with smooth
   * (and analytically differentiable) transition.
   */
  [[nodiscard]] static double vdsEff(double vds, double vdsat,
                                     const MosfetBsim3Params& p) noexcept {
    const double dlt = std::max(p.delta, 1e-9);
    const double a = vdsat - vds - dlt;
    const double r = std::sqrt(a * a + 4.0 * dlt * std::max(vdsat, 1e-12));
    return vdsat - 0.5 * (a + r);
  }

  /* ----------------------------- Mobility degradation ----------------------------- */

  /**
   * @brief Effective mobility scaling with vertical field.
   *
   *   mu_eff = mu0 / (1 + ua * (Vgst + 2*Vt) + ub * (Vgst + 2*Vt)^2)
   *
   * Reduces the effective Kp at high gate overdrive due to surface
   * scattering. For 10 µm processes, ua/ub are small; effect is
   * minor at 4004 operating voltages but included for completeness.
   */
  [[nodiscard]] static double mobilityFactor(double vgstEffVal,
                                             const MosfetBsim3Params& p) noexcept {
    const double e = (vgstEffVal + 2.0 * p.Vt) / p.tox;
    return 1.0 / (1.0 + p.ua * e + p.ub * e * e);
  }

  /* ----------------------------- Drain current ----------------------------- */

  /**
   * @brief BSIM3 drain current. Single smooth expression spanning
   *        weak / moderate / strong inversion and linear / saturation.
   *
   *   Id = (Kp * W/L) * mu_eff * Vgst_eff * Vdseff *
   *        (1 - 0.5 * Vdseff / Vgst_eff) *
   *        (1 + lambda * (Vds - Vdseff))
   *
   * The (1 - Vdseff/(2*Vgst_eff)) term encodes the linear/saturation
   * transition continuously. (1 + lambda*(Vds - Vdseff)) is channel-
   * length modulation in saturation. In weak inversion Vgst_eff is
   * small but non-zero, so the formula remains well-defined.
   */
  [[nodiscard]] static double current(double vgs, double vds, double vbs,
                                      const MosfetBsim3Params& p) noexcept {
    const double vth = thresholdVoltage(vds, vbs, p);
    const double vgstEffVal = vgstEff(vgs, vth, p);
    if (vgstEffVal <= 0.0) {
      // Numerical zero (vanishing tail far below threshold).
      return 0.0;
    }
    const double mu = mobilityFactor(vgstEffVal, p);
    const double beta = p.Kp * p.W / p.L * mu;
    const double vdsat = saturationVoltage(vgstEffVal, p);
    const double vdseffVal = vdsEff(vds, vdsat, p);

    // Quadratic-in-Vdseff core, smooth across linear/saturation:
    //   I = beta * Vgst_eff * Vdseff * (1 - 0.5 * Vdseff / Vgst_eff)
    //     = beta * (Vgst_eff * Vdseff - 0.5 * Vdseff^2)
    const double idCore = beta * (vgstEffVal * vdseffVal - 0.5 * vdseffVal * vdseffVal);

    // Channel-length modulation in saturation only (Vds beyond Vdsat).
    const double clm = 1.0 + p.lambda * std::max(vds - vdseffVal, 0.0);
    return idCore * clm;
  }

  /* ----------------------------- Derivatives ----------------------------- */

  /**
   * @brief gm = dId / dVgs via numerical differentiation.
   *
   * For the first cut, use central difference. Future pass can
   * replace with analytical derivative once the model is validated.
   */
  [[nodiscard]] static double transconductance(double vgs, double vds, double vbs,
                                               const MosfetBsim3Params& p) noexcept {
    constexpr double DV = 1e-6;
    return (current(vgs + DV, vds, vbs, p) - current(vgs - DV, vds, vbs, p)) / (2.0 * DV);
  }

  /**
   * @brief gds = dId / dVds via numerical differentiation.
   */
  [[nodiscard]] static double outputConductance(double vgs, double vds, double vbs,
                                                const MosfetBsim3Params& p) noexcept {
    constexpr double DV = 1e-6;
    return (current(vgs, vds + DV, vbs, p) - current(vgs, vds - DV, vbs, p)) / (2.0 * DV);
  }

  /**
   * @brief gmb = dId / dVbs via numerical differentiation (body transconductance).
   */
  [[nodiscard]] static double bodyTransconductance(double vgs, double vds, double vbs,
                                                   const MosfetBsim3Params& p) noexcept {
    constexpr double DV = 1e-6;
    return (current(vgs, vds, vbs + DV, p) - current(vgs, vds, vbs - DV, p)) / (2.0 * DV);
  }

  /* ----------------------------- Combined entry ----------------------------- */

  struct StampValues {
    double id;  ///< Drain current.
    double gm;  ///< dId/dVgs.
    double gds; ///< dId/dVds.
    double gmb; ///< dId/dVbs.
  };

  /**
   * @brief Compute (id, gm, gds, gmb) together via 4-point numerical
   *        differentiation. Mirrors MosfetLevel1::stampValues for the
   *        NR stamp inner loop.
   */
  [[nodiscard]] static StampValues stampValues(double vgs, double vds, double vbs,
                                               const MosfetBsim3Params& p) noexcept {
    return {current(vgs, vds, vbs, p), transconductance(vgs, vds, vbs, p),
            outputConductance(vgs, vds, vbs, p), bodyTransconductance(vgs, vds, vbs, p)};
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBSIM3_HPP
