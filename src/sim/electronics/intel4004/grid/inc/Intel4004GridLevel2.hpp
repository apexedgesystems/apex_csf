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

  /// BSIM3 parameter template for the latch feedback core. Per-transistor
  /// `Kp` is overridden from `transistorKp_` (W/L-binned calibrated values).
  /// `n_factor = 1.8` is calibrated for the 10 micron PMOS process and
  /// gives positive overdrive on the depletion-load NOR gate
  /// (see `MosfetBsim3Probe.NFactorSweep`).
  MosfetBsim3Params bsim3LatchParams_{
      .Kp = KP_PROCESS, .Vth0 = VTH_ENH, .lambda = LAMBDA, .W = 1.0, .L = 1.0,
      .n_factor = 1.8, .Vt = 0.026, .eta0 = 0.0, .K1 = 0.0, .K2 = 0.0, .phi = 0.7,
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
};

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_INTEL4004GRIDLEVEL2_HPP
