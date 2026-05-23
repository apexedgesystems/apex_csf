#ifndef APEX_MOSFETBSIM3_HPP
#define APEX_MOSFETBSIM3_HPP
/**
 * @file MosfetBsim3.hpp
 * @brief BSIM3v3 MOSFET model.
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
 * @brief Minimal BSIM3 parameters.
 *
 * Captures the essential effects: smooth weak/strong inversion
 * transition, DIBL, body effect, mobility degradation, channel-
 * length modulation. ~15 parameters vs full BSIM3's 100+.
 *
 * Defaults are calibrated for the Intel 4004's PMOS process (10 um,
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
  double ua = 1e-9;   ///< Linear mobility-degradation coefficient (m/V)
  double ub = 0.0;    ///< Quadratic mobility-degradation coefficient (m/V)^2
  double tox = 50e-9; ///< Oxide thickness (m); 50 nm for 10 um process

  // Smooth Vds saturation transition width
  double delta = 0.01; ///< Vds smoothing parameter (V)

  // ============================================================================
  // Meyer capacitance model (intrinsic + overlap). For dynamic-logic
  // simulation. See `meyerCapacitances()` for the full physics + limitations.
  // ============================================================================
  /// Lateral diffusion of source/drain under the gate (m). Sets the
  /// physical overlap capacitance Cgs_ov = Cgd_ov = Cox * W * Lov.
  /// 4004 10 micron process: Lov ~= 1 um typical (~10% of L).
  double Lov = 1e-6;

  /// Whether to include Meyer intrinsic + overlap capacitances in the
  /// stamp. Default false: pure DC I-V only (no transient charge
  /// dynamics). Set true for transient simulations of dynamic logic
  /// where intrinsic gate caps couple clock edges across stages.
  bool include_caps = false;
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
    const double PHI_MINUS_VBS = std::max(p.phi - vbs, 1e-12);
    vth += p.K1 * (std::sqrt(PHI_MINUS_VBS) - std::sqrt(p.phi));
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
  [[nodiscard]] static double vgstEff(double vgs, double vth, const MosfetBsim3Params& p) noexcept {
    const double N_VT = p.n_factor * p.Vt;
    const double x = (vgs - vth) / N_VT;
    // log1p / expm1 + clamp to avoid overflow at large positive x.
    if (x > 50.0) {
      // Strong inversion: Vgst_eff ~= Vgs - Vth.
      return vgs - vth;
    }
    if (x < -50.0) {
      // Deep weak inversion: Vgst_eff ~= N_VT * exp(x) ~= 0.
      return N_VT * std::exp(x);
    }
    return N_VT * std::log1p(std::exp(x));
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
    const double N_VT = p.n_factor * p.Vt;
    const double x = (vgs - vth) / N_VT;
    if (x > 50.0)
      return 1.0;
    if (x < -50.0)
      return 0.0;
    // sigmoid(x) = 1 / (1 + exp(-x))
    return 1.0 / (1.0 + std::exp(-x));
  }

  /* ----------------------------- Saturation voltage ----------------------------- */

  /**
   * @brief Saturation voltage with smooth transition.
   *
   * Long-channel BSIM3: Vdsat ~= Vgst_eff in strong inversion. In weak
   * inversion, real BSIM3 has Vdsat ~= ~2*Vt (a constant floor) so that
   * the channel saturates at a few thermal voltages, not at the
   * vanishing Vgst_eff. Without this floor the I-V degenerates to
   * Id  proportional to  Vgst_eff^2 (exp(2x) scaling); with the floor Id  proportional to  Vgst_eff
   * (exp(x) scaling) -- the canonical exponential subthreshold.
   *
   * Smooth blend: sqrt(Vgst^2 + (2*Vt)^2). Strong inv -> Vgst_eff;
   * weak inv -> 2*Vt.
   */
  [[nodiscard]] static double saturationVoltage(double vgstEffVal,
                                                const MosfetBsim3Params& p) noexcept {
    const double FLOOR2VT = 2.0 * p.Vt;
    return std::sqrt(vgstEffVal * vgstEffVal + FLOOR2VT * FLOOR2VT);
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
    const double DLT = std::max(p.delta, 1e-9);
    const double A = vdsat - vds - DLT;
    const double R = std::sqrt(A * A + 4.0 * DLT * std::max(vdsat, 1e-12));
    return vdsat - 0.5 * (A + R);
  }

  /* ----------------------------- Mobility degradation ----------------------------- */

  /**
   * @brief Effective mobility scaling with vertical field.
   *
   *   mu_eff = mu0 / (1 + ua * (Vgst + 2*Vt) + ub * (Vgst + 2*Vt)^2)
   *
   * Reduces the effective Kp at high gate overdrive due to surface
   * scattering. For 10 um processes, ua/ub are small; effect is
   * minor at 4004 operating voltages but included for completeness.
   */
  [[nodiscard]] static double mobilityFactor(double vgstEffVal,
                                             const MosfetBsim3Params& p) noexcept {
    const double E = (vgstEffVal + 2.0 * p.Vt) / p.tox;
    return 1.0 / (1.0 + p.ua * E + p.ub * E * E);
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
    const double VTH = thresholdVoltage(vds, vbs, p);
    const double VGST_EFF_VAL = vgstEff(vgs, VTH, p);
    if (VGST_EFF_VAL <= 0.0) {
      return 0.0;
    }
    const double MU = mobilityFactor(VGST_EFF_VAL, p);
    const double BETA = p.Kp * p.W / p.L * MU;
    const double VDSAT = saturationVoltage(VGST_EFF_VAL, p);
    const double VDSEFF_VAL = vdsEff(vds, VDSAT, p);

    // BSIM3-style saturation current. Multiplicative form so Id -> 0 as
    // Vgst_eff -> 0 (no current floor in deep weak inversion).
    //
    //   Id_sat = (BETA/2) * Vgst_eff * (Vgst_eff + 2*n*Vt)
    //
    //   Strong  (Vgst_eff >> n*Vt): Id_sat -> 0.5*BETA*Vgst^2. OK
    //   Weak    (Vgst_eff << n*Vt): Id_sat -> BETA * n*Vt * Vgst_eff.
    //     With Vgst_eff = n*Vt * exp((Vgs-Vth)/(n*Vt)) in weak inv,
    //     this gives Id  proportional to  exp((Vgs-Vth)/(n*Vt)) -- the canonical
    //     subthreshold exponential the documented L2 design relies on.
    //   Vgst_eff -> 0:           Id_sat -> 0. OK
    const double N_VT_2 = 2.0 * p.n_factor * p.Vt;
    const double ID_SAT = 0.5 * BETA * VGST_EFF_VAL * (VGST_EFF_VAL + N_VT_2);

    // Smooth linear-to-saturation transition: FACTOR = 1 - (1-R)^2 where
    // R = Vdseff / Vdsat in [0, 1]. Equivalent to the
    // (Vgst*Vds - 0.5*Vds^2) form when Vdseff < Vdsat, and reaches 1 at
    // Vds = Vdsat (saturation).
    const double R = VDSEFF_VAL / std::max(VDSAT, 1e-12);
    const double FACTOR = R * (2.0 - R);

    // Channel-length modulation in saturation only.
    const double CLM = 1.0 + p.lambda * std::max(vds - VDSEFF_VAL, 0.0);
    return ID_SAT * FACTOR * CLM;
  }

  /* ----------------------------- Derivatives ----------------------------- */

  /**
   * @brief gm = dId / dVgs via central-difference numerical differentiation.
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

  /* ----------------------------- Meyer capacitance model ----------------------------- */

  /**
   * @brief Intrinsic + overlap MOSFET gate capacitances per the Meyer model
   *        (the bias-dependent precursor to BSIM3's full charge model).
   *
   * Three regions depending on operating point:
   *
   *   Cutoff       (Vgst < 0):     Cgs=0,            Cgd=0,            Cgb=Cox*W*L
   *   Linear/triode (Vds < Vdsat): Cgs and Cgd via the standard Meyer
   *                                interpolation:
   *                                  Cgs = (2/3)*Cox*W*L * [1 - ((Vgst-Vds)/(2Vgst-Vds))^2]
   *                                  Cgd = (2/3)*Cox*W*L * [1 - (Vgst    /(2Vgst-Vds))^2]
   *                                Cgb = 0 (channel screens bulk in inversion)
   *   Saturation   (Vds >= Vdsat): Cgs = (2/3)*Cox*W*L, Cgd = 0,         Cgb=0
   *
   * Plus constant overlap caps (gate physically extends Lov over the
   * source/drain diffusion):
   *   Cgs_ov = Cgd_ov = Cox * W * Lov
   *
   * Cox = eps_ox / tox where eps_ox = 3.45e-11 F/m for thermal SiO2.
   *
   * Documented limitations:
   *   1. Meyer caps are NOT charge-conservative. Each cap is computed
   *      independently from bias; the underlying Q is not a single-valued
   *      function of Vgs/Vds/Vbs. For digital logic with rail-to-rail
   *      transitions, this introduces small errors that average out per
   *      cycle. For analog precision (e.g. switched-capacitor circuits)
   *      a charge-based model (BSIM3 full Qg/Qd/Qs) is required.
   *   2. No drain/source-substrate junction capacitance modeled here.
   *      For the 4004, junction caps are ~0.1-0.5 fF/um^2 and are absorbed
   *      into the existing parasitic-cap stamp (CPARA_L1).
   *   3. Sharp transition between cutoff (channel off) and inversion (channel on)
   *      at Vgst=0 -- not smoothed. For NR convergence with small step
   *      sizes this is generally OK; pathologically tight loops would need
   *      smoothing via Vgst_eff.
   *   4. No fringe / sidewall capacitance contributions; absorbed into
   *      the overlap cap parameter Lov.
   *
   * Calibration target: 4004 10 um process with tox=50 nm, Lov=1 um.
   *   Cox ~= 0.69 fF/um^2. W=10um, L=10um transistor: Cox*W*L ~= 69 fF.
   *   Cgs_ov = 0.69 * 10 * 1 = 6.9 fF.
   */
  struct MeyerCaps {
    double Cgs; ///< Intrinsic + overlap gate-source cap (F).
    double Cgd; ///< Intrinsic + overlap gate-drain cap (F).
    double Cgb; ///< Intrinsic gate-bulk cap (F).
  };

  /// Oxide capacitance density (F/m^2): Cox = eps_ox / tox.
  [[nodiscard]] static double oxideCapDensity(const MosfetBsim3Params& p) noexcept {
    constexpr double EPS_OX = 3.45e-11; // F/m, thermal SiO2
    return EPS_OX / std::max(p.tox, 1e-12);
  }

  /// Meyer intrinsic + overlap capacitances at the given bias.
  [[nodiscard]] static MeyerCaps meyerCapacitances(double vgs, double vds, double vbs,
                                                   const MosfetBsim3Params& p) noexcept {
    const double Cox = oxideCapDensity(p);
    const double CoxWL = Cox * p.W * p.L;
    const double Cov = Cox * p.W * p.Lov;

    const double VTH = thresholdVoltage(vds, vbs, p);
    const double Vgst = vgs - VTH;

    // Cutoff: gate cap to bulk only.
    if (Vgst <= 0.0) {
      return {Cov, Cov, CoxWL};
    }

    const double Vdsat = std::max(Vgst, 1e-12); // long-channel approx
    if (vds <= Vdsat) {
      // Linear/triode region.
      const double DENOM = std::max(2.0 * Vgst - vds, 1e-12);
      const double A = (Vgst - vds) / DENOM;
      const double B = Vgst / DENOM;
      const double Cgs_int = (2.0 / 3.0) * CoxWL * (1.0 - A * A);
      const double Cgd_int = (2.0 / 3.0) * CoxWL * (1.0 - B * B);
      return {std::max(Cgs_int, 0.0) + Cov, std::max(Cgd_int, 0.0) + Cov, 0.0};
    }

    // Saturation: only Cgs intrinsic (channel pinched off at drain end).
    return {(2.0 / 3.0) * CoxWL + Cov, Cov, 0.0};
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
    // Skip body transconductance (gmb) by default. Every current
    // production caller uses vbs=0 and reads only id, gm, gds, so gmb
    // is left at 0 (struct shape preserved). Eliminating the central-
    // difference computation for gmb saves 2 of 7 current() evaluations
    // per stamp. Callers that need gmb can use the explicit
    // `bodyTransconductance(vgs, vds, vbs, p)` entry, or call
    // `stampValuesWithBodyEffect` (below).
    return {current(vgs, vds, vbs, p), transconductance(vgs, vds, vbs, p),
            outputConductance(vgs, vds, vbs, p),
            /*gmb=*/0.0};
  }

  /// stampValues including body transconductance (gmb). Use this when a
  /// caller actually needs dId/dVbs (analog circuits with non-zero
  /// body bias). Adds 2 current() central-diff evaluations relative to
  /// `stampValues`.
  [[nodiscard]] static StampValues stampValuesWithBodyEffect(double vgs, double vds, double vbs,
                                                             const MosfetBsim3Params& p) noexcept {
    return {current(vgs, vds, vbs, p), transconductance(vgs, vds, vbs, p),
            outputConductance(vgs, vds, vbs, p), bodyTransconductance(vgs, vds, vbs, p)};
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_MOSFETBSIM3_HPP
