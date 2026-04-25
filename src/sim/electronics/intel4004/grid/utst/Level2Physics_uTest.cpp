/**
 * @file Level2Physics_uTest.cpp
 * @brief L2 unblocker probe: does MosfetLevel2 subthreshold cure the
 *        VOL > VTH problem on Intel 4004 calibrated parameters?
 *
 * Per `src/sim/electronics/intel4004/SESSION_CONTINUITY.md`:
 *
 *   Depletion-load PMOS inverter VOL formula (Shichman-Hodges Level 1):
 *     VOL = VTH_enh + |VTH_dep| * sqrt(WL_dep / WL_enh)
 *         = 1.17 + 0.17 * sqrt(0.10 / 3.23) = 1.200V
 *   With VTH_enh = 1.170V:
 *     Overdrive = VTH_enh - VOL = -30 mV  (negative)
 *   Cross-coupled latch feedback core needs positive overdrive to
 *   resolve from mid-rail. The hypothesis is that BSIM-style
 *   subthreshold (weak inversion) leakage extends the on-state
 *   conduction below VTH, lowering VOL and giving the latch a
 *   positive overdrive margin.
 *
 * This test sets up the same PMOS depletion-load inverter the
 * `Level1Physics.InverterInputLow` test uses, but stamps the
 * enhancement transistor through `MosfetLevel2` (which includes
 * the exponential subthreshold tail). Reports VOL and the
 * resulting overdrive.
 *
 * Pass criterion (informational): VOL_L2 < VOL_L1 by at least
 * a few mV. The exact margin depends on the n_sub parameter; a
 * positive overdrive (VOL_L2 < VTH_enh = 1.170V) means
 * subthreshold alone is sufficient to unblock L2. A negative
 * overdrive means we need full BSIM3 (gate effect, body effect,
 * mobility, etc.) and the scope grows accordingly.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel2.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using sim::electronics::devices::nonlinear::MosfetLevel2;
using sim::electronics::devices::nonlinear::MosfetLevel2Params;
using sim::electronics::mna::MnaSystemSparse;
using sim::electronics::mna::NetID;

namespace {

// 4004 calibrated parameters, matching `Level1Physics_uTest.cpp`.
constexpr double KP = 5e-3;
constexpr double VTH_ENH = 1.17;
constexpr double VTH_DEP = -0.17;
constexpr double LAMBDA = 0.03;
constexpr double VDD = 5.0;
constexpr double WL_DEP = 0.10;
constexpr double WL_ENH = 3.23;

// Build MosfetLevel2 params from the 4004 calibration. The Level 2
// model expects geometry (W, L) separately from Kp; collapse W/L
// into Kp directly by setting W=WL, L=1 so Kp_eff = Kp * WL.
// This matches Level 1's `Kp = KP_PROCESS * (W/L)` semantics.
MosfetLevel2Params makeL2Params(double kpScaled, double vth) {
  MosfetLevel2Params p;
  p.Kp = kpScaled;       // Already includes the W/L factor
  p.W = 1.0;             // Geometry collapsed into Kp
  p.L = 1.0;
  p.Vth0 = vth;
  p.lambda = LAMBDA;
  p.theta = 0.0;         // Disable mobility degradation (not in L1 calibration)
  p.E_crit = 1e30;       // Disable velocity saturation (not in L1 calibration)
  p.gamma = 0.0;         // Disable body effect for VBS=0 case
  p.phi = 0.7;           // Default surface potential
  p.n_sub = 1.5;         // Default subthreshold slope factor
  p.Vt = 0.026;          // Thermal voltage at 300K
  return p;
}

// Stamp a PMOS using `MosfetLevel2`. Mirrors the `stampPmos` from
// Level1Physics_uTest.cpp with two changes: (1) uses MosfetLevel2
// API which includes subthreshold, (2) keeps VBS=0 for now.
void stampPmosL2(MnaSystemSparse& mna, NetID drain, NetID gate, NetID source,
                 const std::vector<double>& V, double kpScaled, double vth) {
  const auto params = makeL2Params(kpScaled, vth);
  const double VS = V[source], VD = V[drain], VG = V[gate];
  const double VSG = VS - VG, VSD = VS - VD;

  // SPICE-style drain/source swap for reverse mode (VSD < 0).
  NetID sD = drain, sS = source;
  double eVSG = VSG, eVSD = VSD;
  if (VSD < 0.0) {
    std::swap(sD, sS);
    eVSG = VD - VG;
    eVSD = VD - VS;
  }

  // MosfetLevel2 uses NMOS sign convention; for PMOS in NMOS-mirror
  // we pass VSG (positive when ON) and VSD (positive in normal mode).
  const double vgsM = std::max(eVSG, 0.0);
  const double vdsM = std::max(eVSD, 0.0);
  constexpr double VBS = 0.0;

  const double id = MosfetLevel2::current(vgsM, vdsM, VBS, params);
  const double gm = MosfetLevel2::transconductance(vgsM, vdsM, VBS, params);
  const double gdsRaw = MosfetLevel2::outputConductance(vgsM, vdsM, VBS, params);
  const double gds = std::max(gdsRaw, 1e-12);
  const double ieq = id - gm * eVSG - gds * eVSD;

  // Same stamp pattern as the Level 1 test (ngspice mos1load.c).
  mna.addConductance(sD, sS, gds);
  mna.addMatrixEntry(sD, gate, gm);
  mna.addMatrixEntry(sD, sS, -gm);
  mna.addMatrixEntry(sS, gate, -gm);
  mna.addMatrixEntry(sS, sS, gm);
  mna.addCurrent(sD, sS, ieq);
}

// Damped NR with GMIN stepping; same recipe as Level1Physics_uTest.
double solveDc(std::size_t n, std::vector<double>& V,
               std::function<void(MnaSystemSparse&, const std::vector<double>&)> fn) {
  for (double gmin : {1e-6, 1e-8, 1e-10, 1e-12}) {
    for (int iter = 0; iter < 500; ++iter) {
      MnaSystemSparse mna(n);
      fn(mna, V);
      mna.addConductance(2, 0, gmin);
      if (!mna.factorize()) return -1;
      auto r = mna.solve();
      if (!r.success) return -1;
      double maxD = 0;
      for (std::size_t i = 0; i < n; ++i) {
        double nv = V[i] + 0.5 * (r.nodeVoltages[i] - V[i]);
        double d = std::fabs(nv - V[i]);
        if (d > maxD) maxD = d;
        V[i] = nv;
      }
      if (maxD < 1e-6) break;
    }
  }
  return V[2];
}

} // namespace

/**
 * @test Level 2 inverter VOL probe: does subthreshold lower VOL
 *       below VTH_enh = 1.170V on the calibrated 4004 params?
 *
 * Same circuit as `Level1Physics.InverterInputLow`:
 *   Net 0 = GND, 1 = VDD, 2 = OUT, 3 = IN (input LOW)
 *   Depletion load (always-on): drain=OUT, gate=VDD, source=VDD
 *   Enhancement pull-down:      drain=GND, gate=IN(=0V), source=OUT
 *
 * With Level 1 (no subthreshold) this test reports VOL = 1.201V.
 * The 30 mV negative overdrive is the documented L2 blocker.
 */
TEST(Level2PhysicsProbe, InverterVolWithSubthreshold) {
  std::vector<double> V = {0, VDD, 2.0, 0};

  double vout = solveDc(4, V, [](MnaSystemSparse& mna, const std::vector<double>& v) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addVoltageSource(3, 0, 0.0);
    stampPmosL2(mna, 2, 1, 1, v, KP * WL_DEP, VTH_DEP); // depletion load
    stampPmosL2(mna, 0, 3, 2, v, KP * WL_ENH, VTH_ENH); // enhancement
  });

  const double VTH = VTH_ENH;
  const double overdrive = VTH - vout;
  std::printf("\n  ==== Level 2 Subthreshold Probe ====\n");
  std::printf("    VOL_L2:    %.4fV\n", vout);
  std::printf("    VTH_enh:   %.4fV\n", VTH);
  std::printf("    Overdrive: %.4fV  (positive = latch can resolve)\n", overdrive);
  std::printf("    L1 reference VOL: 1.2010V (overdrive = -30 mV)\n");

  // Informational: report whether subthreshold alone unblocks L2.
  if (overdrive > 0.0) {
    std::printf("  ==> Subthreshold IS sufficient to unblock L2.\n");
  } else {
    std::printf("  ==> Subthreshold alone NOT sufficient. Full BSIM may be needed.\n");
    std::printf("      Try smaller n_sub, or include body effect / mobility / DIBL.\n");
  }

  // Pass condition: solver must converge to a valid voltage.
  EXPECT_GT(vout, 0.0);
  EXPECT_LT(vout, VDD);
}
