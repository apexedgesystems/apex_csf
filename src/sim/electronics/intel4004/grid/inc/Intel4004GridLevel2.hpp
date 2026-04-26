#ifndef APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL2_HPP
#define APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL2_HPP
/**
 * @file Intel4004GridLevel2.hpp
 * @brief Intel 4004 circuit with full BSIM3 physics in the latch feedback core.
 *
 * Plug-and-play extension of Intel4004GridLevel1: only the latch feedback
 * core stamp changes. Inherits everything else (NOR gates, pass gates,
 * loads, NR scaffolding, simulation loop, behavioral overlay, traceExecuteByte).
 *
 * Why this class exists:
 *   The 4004 has ~338 cross-coupled latch transistors classified as
 *   DYNAMIC_STORAGE with non-NOR-output gates. Shichman-Hodges (Level 1)
 *   cannot resolve their operating point because the depletion-load PMOS
 *   NOR's `VOL = 1.20V > VTH_enh = 1.17V` -- gate overdrive is -30 mV,
 *   which puts every latch transistor in cutoff in L1 physics. L1 papers
 *   over this with a binary-switch + behavioral-latch overlay (the 15%
 *   behavioral fraction). L2 replaces the binary switch with BSIM3's
 *   smooth `Vgst_eff = n*Vt * ln(1 + exp((Vgs - Vth) / (n*Vt)))`, which
 *   captures moderate-inversion conduction and gives a positive overdrive
 *   at the calibrated 4004 operating point (verified in
 *   `MosfetBsim3Probe.NFactorSweep` -- n_factor = 1.8 -> +2.5 mV).
 *
 * Architecture:
 *   - L1 owns the binary-switch path via `stampLatchFeedbackTransistor`
 *     (virtual hook).
 *   - L2 overrides ONLY that hook with the BSIM3 SPICE stamp pattern.
 *   - All other stamps (NOR gates, pass gates, loads, parasitic caps,
 *     GMIN) come from L1 unchanged.
 *
 * @note NOT RT-safe: initialization allocates. Stamp functions are RT-safe.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetBsim3.hpp"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel1.hpp"

#include <algorithm>

namespace sim::electronics::intel4004 {

using devices::nonlinear::MosfetBsim3;
using devices::nonlinear::MosfetBsim3Params;

/* ----------------------------- Intel4004GridLevel2 ----------------------------- */

/**
 * @brief Intel 4004 circuit with BSIM3 physics in the latch feedback core.
 *
 * Usage:
 * @code
 * Intel4004GridLevel2 grid;
 * auto circuit = grid.buildCircuit(netlist);
 * grid.enableSparseModeLevel1(circuit);  // <-- inherited; latch hook is overridden
 * auto state = grid.simulate(circuit, rom, romSize, numBytes);
 * @endcode
 */
struct Intel4004GridLevel2 : Intel4004GridLevel1 {

  /// L2 = 100% physics for the steady-state hold.
  ///
  /// Calibration story (commit history captures the full investigation):
  ///   1. BSIM3 stamps the ~338 latch feedback transistors -> accurate
  ///      moderate-inversion physics around the cross-coupled topology.
  ///   2. Behavioral overlay disabled. The NR pathology this exposed
  ///      (~73 nets drifting to ±100s of volts) was diagnosed as
  ///      *Jacobian rank deficiency*: with no algebraic constraint on
  ///      floating storage nets, the linear solve produces unbounded
  ///      step values.
  ///   3. Cure: differentiated GMIN.
  ///        gminTransient_ = 5e-3 (1.5x typical transistor gm at op-point)
  ///        gminDriven_    = 1e-12 (NOR outputs / clocks unaffected)
  ///      Empirically converges with 0 OOB nets, max|v| = 5.0V exactly.
  ///      See `DISABLED_DifferentiatedGminSweep` for the calibration grid.
  ///
  /// What L2 still does NOT do (separate milestone): pure-physics
  /// instruction execution. Even L1 currently relies on behavioral X3
  /// execution via `traceExecuteByte`'s C++ switch for the data-bus ->
  /// OPA -> ACC transfer. simulateLevel1 only runs the analog circuit;
  /// instruction state propagation requires the trace path.
  /// L2 = PURE PHYSICS. No behavioral stubs.
  ///
  /// L1 owns all behavioral overrides (latch overlay, X3 instruction
  /// switch, OPR/OPA/SC/timing-net forcing in forceBehavioral). L2's
  /// contract is: force only real external pins (CLK1, CLK2, D0..D3
  /// during ROM input phases); let physics compute everything else.
  ///
  /// What L2 owns architecturally:
  ///   - BSIM3 latch-feedback stamp (smooth Vgst_eff captures the
  ///     moderate-inversion conduction Shichman-Hodges misses)
  ///   - Behavioral overlay OFF (storage nets driven by physics)
  ///   - NR clamp [-1V, +6V] post-iteration -- legitimate SPICE-style
  ///     numerical aid (no current draw, just bounds NR step pathology)
  ///   - Weak GMIN (1e-9) -- no longer the anti-pathology aid; clamp
  ///     does that job, freeing weak GMIN to not fight pass-transistor
  ///     drive on dynamic-logic decode-chain nets like ~OPR.x
  ///   - Behavioral X3 instruction switch DISABLED
  ///
  /// Honest limit: pure-physics multi-instruction is currently blocked
  /// by structural issues in the decode chain (single-input depletion-
  /// load PMOS NORs can't pull below Vth = 1.17V, so dynamic logic
  /// stages stick at intermediate voltages). Steady-state hold works.
  /// Multi-instruction does not work end-to-end without the L1
  /// behavioral stubs that L2 explicitly does NOT inherit.
  Intel4004GridLevel2() {
    applyBehavioralLatchOverlay_ = false; // L2 contract: pure physics, no overlay
    applyBehavioralX3_ = false;           // L2 contract: pure physics, no X3 switch
    clampNrIterates_ = true;              // legitimate numerical aid (no current draw)
    gminTransient_ = 1e-9;
    gminDriven_ = 1e-12;                  // tiny GMIN on NOR-output / clock nets
  }

  /// BSIM3 parameter template for the latch feedback core. Per-transistor
  /// `Kp` is overridden from `transistorKp_` (W/L-binned calibrated values).
  /// `n_factor = 2.5` lands in the >100 mV overdrive regime per the
  /// post-fix `MosfetBsim3Probe.NFactorSweep`:
  ///   n=1.5 -> +29 mV (insufficient, like the old broken impl)
  ///   n=2.0 -> +63 mV (positive but marginal)
  ///   n=2.5 -> +101 mV ("well below VTH" per docs)
  ///   n=3.0 -> +141 mV (deeper but n is on the high side of typical)
  /// 10-micron PMOS with thick (50 nm) gate oxide can support n around
  /// 2.0-3.0 due to depletion-region capacitance / interface states.
  MosfetBsim3Params bsim3LatchParams_{
      .Kp = KP_PROCESS, .Vth0 = VTH_ENH, .lambda = LAMBDA, .W = 1.0, .L = 1.0,
      .n_factor = 2.5, .Vt = 0.026, .eta0 = 0.0, .K1 = 0.0, .K2 = 0.0, .phi = 0.7,
      .ua = 0.0, .ub = 0.0, .tox = 50e-9, .delta = 0.01};

  /**
   * @brief Stamp a latch-feedback transistor with the full BSIM3 SPICE pattern.
   *
   * Replaces L1's binary-switch fallback. Uses the same xnrm/xrev SPICE
   * Jacobian + RHS pattern as MosfetLevel1; only the (id, gm, gds)
   * computation switches to BSIM3's smooth Vgst_eff.
   *
   * @note RT-safe.
   */
  void stampLatchFeedbackTransistor(mna::MnaSystemSparse& mna, std::size_t idx,
                                    const std::vector<double>& prevV) const override {
    const auto& t = transistors_[idx];

    const double VS = prevV[t.source];
    const double VD = prevV[t.drain];
    const double VG = prevV[t.gate];

    const double kp = transistorKp_[idx];
    const double vth = sameVtoMode_ ? VTH_ENH : (t.isDiodeLoad ? VTH_DEP : VTH_ENH);

    double VSG = VS - VG;
    double VSD = VS - VD;

    if (idx < prevVsg_.size()) {
      VSG = MosfetLevel1::fetlim(VSG, prevVsg_[idx], vth);
      VSD = MosfetLevel1::limvds(VSD, prevVsd_[idx]);
    }
    prevVsg_[idx] = VSG;
    prevVsd_[idx] = VSD;

    int xnrm, xrev;
    double evalVgs, evalVds;
    if (VSD >= 0.0) {
      xnrm = 1; xrev = 0;
      evalVgs = VSG;
      evalVds = VSD;
    } else {
      xnrm = 0; xrev = 1;
      evalVgs = VD - VG;
      evalVds = VD - VS;
      if (idx < prevVsg_.size()) {
        evalVgs = MosfetLevel1::fetlim(evalVgs, prevVsg_[idx], vth);
        evalVds = MosfetLevel1::limvds(evalVds, prevVsd_[idx]);
      }
    }

    const double vgsM = std::max(evalVgs, 0.0);
    const double vdsM = std::max(evalVds, 0.0);

    MosfetBsim3Params bp = bsim3LatchParams_;
    bp.Kp = kp;
    bp.Vth0 = vth;
    bp.lambda = LAMBDA;
    const auto SV = MosfetBsim3::stampValues(vgsM, vdsM, /*vbs=*/0.0, bp);
    const double id = SV.id;
    const double gm = SV.gm;
    const double gdsDevice = SV.gds;
    const double gdsStamp = std::max(gdsDevice, G_MIN);

    double cdreq;
    if (xnrm == 1) {
      cdreq = -(id - gdsDevice * VSD - gm * VSG);
    } else {
      cdreq = (id - gdsDevice * (-VSD) - gm * (VD - VG));
    }

    mna.addConductance(t.drain, t.source, gdsStamp);
    mna.addMatrixEntry(t.drain, t.drain, xrev * gm);
    mna.addMatrixEntry(t.source, t.source, xnrm * gm);
    mna.addMatrixEntry(t.drain, t.gate, (xnrm - xrev) * gm);
    mna.addMatrixEntry(t.drain, t.source, -static_cast<double>(xnrm) * gm);
    mna.addMatrixEntry(t.source, t.gate, -(xnrm - xrev) * gm);
    mna.addMatrixEntry(t.source, t.drain, -static_cast<double>(xrev) * gm);
    mna.addCurrent(t.drain, t.source, -cdreq);
  }

  /* ----------------------------- Meyer cap dynamics ----------------------------- */

  /// Enable per-transistor Meyer intrinsic + overlap caps in the stamp.
  /// Default false -- L2 ships pure DC steady-state hold (already
  /// validated). Set true when running transient simulations of dynamic
  /// logic where charge dynamics matter (clock-edge coupling through
  /// Cgd, refresh through gate-source caps). The cap-companion stamps
  /// add ~3 caps per transistor x 2,242 transistors = ~6,700 cap stamps
  /// per NR iteration; expect simulation to slow noticeably.
  bool enableMeyerCaps_ = false;

  /// Meyer cap parameters template. Process-level constants
  /// (oxide thickness, overlap diffusion); per-transistor W/L is
  /// folded into Kp via the existing transistorKp_ binning.
  MosfetBsim3Params meyerCapParams_{
      .Kp = KP_PROCESS, .Vth0 = VTH_ENH, .lambda = LAMBDA, .W = 1.0, .L = 1.0,
      .n_factor = 2.5, .Vt = 0.026, .eta0 = 0.0, .K1 = 0.0, .K2 = 0.0, .phi = 0.7,
      .ua = 0.0, .ub = 0.0, .tox = 50e-9, .delta = 0.01,
      .Lov = 1e-6, .include_caps = true};

  /**
   * @brief Per-transistor Meyer cap-companion stamping for dynamic logic.
   *
   * For each transistor in `transistors_`, computes Meyer intrinsic +
   * overlap capacitances (Cgs, Cgd, Cgb) at the previous-timestep
   * operating point and stamps backward-Euler cap companions:
   *
   *   For a cap C between nodes a and b:
   *     Geq = C / dt
   *     Ieq = Geq * (V_a(t-dt) - V_b(t-dt))
   *   Stamp: addConductance(a, b, Geq); addCurrent(a, b, Ieq).
   *
   * Cgb stamps to ground (PMOS-only convention, body tied to GND).
   * Skips clock-gated transistors (their gate is externally forced;
   * cap dynamics on those nets are dominated by the external drive).
   *
   * @note RT-safe.
   */
  void stampDynamicCharge(mna::MnaSystemSparse& mna,
                          const std::vector<double>& prevTimestepV) const override {
    if (!enableMeyerCaps_) return;
    if (stepDt_ <= 0.0) return;

    if (transistorKp_.empty()) computeTransistorKp();

    for (std::size_t idx = 0; idx < transistors_.size(); ++idx) {
      const auto& t = transistors_[idx];
      // Skip clock-gated transistors; clocks are externally forced.
      if (t.gate == clk1Net_ || t.gate == clk2Net_) continue;

      const double VS = prevTimestepV[t.source];
      const double VD = prevTimestepV[t.drain];
      const double VG = prevTimestepV[t.gate];
      const double VSG = VS - VG;
      const double VSD = VS - VD;
      // PMOS in NMOS-mirror convention.
      const double vgs = std::max(VSG, 0.0);
      const double vds = std::max(VSD, 0.0);

      // Per-transistor params: use calibrated Kp + W/L bin.
      MosfetBsim3Params pp = meyerCapParams_;
      pp.Kp = transistorKp_[idx];
      pp.Vth0 = sameVtoMode_ ? VTH_ENH : (t.isDiodeLoad ? VTH_DEP : VTH_ENH);

      const auto caps = MosfetBsim3::meyerCapacitances(vgs, vds, /*vbs=*/0.0, pp);

      // Cap companion stamps: G = C/dt, I = G * (V_a_prev - V_b_prev).
      auto stampCap = [&](mna::NetID a, mna::NetID b, double C) {
        if (C <= 0.0) return;
        const double Geq = C / stepDt_;
        mna.addConductance(a, b, Geq);
        const double Va = (a < prevTimestepV.size()) ? prevTimestepV[a] : 0.0;
        const double Vb = (b < prevTimestepV.size()) ? prevTimestepV[b] : 0.0;
        mna.addCurrent(a, b, Geq * (Va - Vb));
      };

      // Cgs between gate and source (PMOS effective source is at higher V).
      stampCap(t.gate, t.source, caps.Cgs);
      // Cgd between gate and drain.
      stampCap(t.gate, t.drain, caps.Cgd);
      // Cgb between gate and bulk (= GND for PMOS-only single-well).
      stampCap(t.gate, circuit::Circuit::ground(), caps.Cgb);
    }
  }
};

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL2_HPP
