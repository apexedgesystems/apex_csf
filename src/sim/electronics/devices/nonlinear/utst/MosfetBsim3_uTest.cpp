/**
 * @file MosfetBsim3_uTest.cpp
 * @brief Unit tests for the minimal BSIM3v3 MOSFET model.
 *
 * Coverage: each operating region (deep weak inversion, moderate
 * inversion, strong inversion linear, strong inversion saturation),
 * smooth transitions across boundaries, derivatives consistent with
 * numerical differentiation of `current()`, and the Meyer
 * intrinsic-capacitance model across cutoff / linear / saturation.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetBsim3.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using sim::electronics::devices::nonlinear::MosfetBsim3;
using sim::electronics::devices::nonlinear::MosfetBsim3Params;
using sim::electronics::algorithms::mna::MnaSystemSparse;
using sim::electronics::algorithms::mna::NetID;

/* ========================== Region behavior ========================== */

/** @test In deep weak inversion (Vgs << Vth), current is exponentially small. */
TEST(MosfetBsim3Test, DeepWeakInversion_ExponentiallySmall) {
  MosfetBsim3Params p; // defaults: Vth0=1.17
  // Vgs = 0V, well below Vth = 1.17V. Vgs - Vth = -1.17V.
  // n*Vt = 1.5 * 0.026 = 0.039V -> exp(-1.17/0.039) = exp(-30) ~ 1e-13.
  const double ID = MosfetBsim3::current(0.0, 1.0, 0.0, p);
  EXPECT_GE(ID, 0.0);
  EXPECT_LT(ID, 1e-9) << "Deep weak inversion current should be < 1 nA";
}

/** @test In moderate inversion (Vgs near Vth), current is small but non-zero. */
TEST(MosfetBsim3Test, ModerateInversion_NonZeroAndSmooth) {
  MosfetBsim3Params p;
  // Vgs = Vth exactly: should give a small non-zero current.
  const double ID_AT_VTH = MosfetBsim3::current(p.Vth0, 1.0, 0.0, p);
  EXPECT_GT(ID_AT_VTH, 0.0);
  EXPECT_LT(ID_AT_VTH, 1e-3); // Not yet in strong inversion

  // Slightly above Vth: current should grow.
  const double ID_ABOVE = MosfetBsim3::current(p.Vth0 + 0.05, 1.0, 0.0, p);
  EXPECT_GT(ID_ABOVE, ID_AT_VTH);
}

/** @test In strong inversion saturation, current matches the quadratic Level-1 form. */
TEST(MosfetBsim3Test, StrongInversionSaturation_MatchesLevel1Form) {
  MosfetBsim3Params p;
  p.eta0 = 0.0;     // Disable DIBL for direct comparison
  p.lambda = 0.0;   // Disable channel-length modulation
  p.ua = 0.0;       // Disable mobility degradation
  p.ub = 0.0;
  p.K1 = 0.0;       // Disable body effect
  p.delta = 1e-9;   // Sharp transition
  p.W = 1.0; p.L = 1.0;

  const double Vov = 1.0; // Strong overdrive: Vgst = 1V >> nVt
  const double VGS = p.Vth0 + Vov;
  const double VDS = 5.0; // Deep saturation (Vds >> Vdsat = Vov)
  const double ID_BSIM = MosfetBsim3::current(VGS, VDS, 0.0, p);
  const double ID_L1_REFERENCE = 0.5 * p.Kp * Vov * Vov;
  // Tolerance 10% (relaxed from 5%). The BSIM3 weak-inversion correction
  // (the +2*n*Vt term in the multiplicative kernel) adds ~beta*n*Vt*Vgst
  // overhead in strong inversion -- about 8% at Vgst=1V, n=1.5. This is
  // the documented trade-off for getting exp((Vgs-Vth)/(n*Vt))
  // subthreshold scaling instead of exp(2*(Vgs-Vth)/(n*Vt)). Without
  // this term, weak-inv current is ~10x too small and the documented
  // L2 latch-feedback overdrive is unreachable.
  EXPECT_NEAR(ID_BSIM, ID_L1_REFERENCE, 0.10 * ID_L1_REFERENCE)
      << "BSIM3 strong-saturation should match L1 within 10%; bsim=" << ID_BSIM
      << " L1ref=" << ID_L1_REFERENCE;
}

/** @test Linear region: Id increases with Vds, then plateaus near Vdsat. */
TEST(MosfetBsim3Test, LinearToSaturationTransition_Smooth) {
  MosfetBsim3Params p;
  p.eta0 = 0.0; p.lambda = 0.0; p.ua = 0.0; p.ub = 0.0; p.K1 = 0.0;
  const double VGS = p.Vth0 + 1.0;
  const double ID_LINEAR = MosfetBsim3::current(VGS, 0.1, 0.0, p);
  const double ID_SAT = MosfetBsim3::current(VGS, 5.0, 0.0, p);
  EXPECT_LT(ID_LINEAR, ID_SAT);
  EXPECT_GT(ID_LINEAR, 0.0);
}

/** @test Current and its derivatives are smooth across the threshold. */
TEST(MosfetBsim3Test, SmoothAcrossThreshold) {
  MosfetBsim3Params p;
  // Sample id at many Vgs around Vth.
  const double VDS = 1.0;
  std::vector<double> ids;
  for (double vgs = p.Vth0 - 0.2; vgs <= p.Vth0 + 0.2; vgs += 0.01) {
    const double ID = MosfetBsim3::current(vgs, VDS, 0.0, p);
    ids.push_back(ID);
  }
  // Verify monotonic increase (smoothness).
  for (std::size_t i = 1; i < ids.size(); ++i) {
    EXPECT_GE(ids[i], ids[i - 1]) << "id should increase monotonically with Vgs near Vth";
  }
  // Verify no large jumps (no kinks).
  for (std::size_t i = 1; i < ids.size(); ++i) {
    const double REL = std::fabs(ids[i] - ids[i - 1]) / std::max(ids[i - 1], 1e-12);
    EXPECT_LT(REL, 10.0)
        << "id jump at Vgs=" << (p.Vth0 - 0.2 + 0.01 * i) << " is too large (kink)";
  }
}

/** @test gm matches numerical derivative of current (sanity). */
TEST(MosfetBsim3Test, GmConsistentWithNumericalDerivative) {
  MosfetBsim3Params p;
  for (double vgs : {0.5, 1.0, 1.17, 1.5, 2.0, 3.0}) {
    const double DV = 1e-5;
    const double ID_MINUS = MosfetBsim3::current(vgs - DV, 1.0, 0.0, p);
    const double ID_PLUS = MosfetBsim3::current(vgs + DV, 1.0, 0.0, p);
    const double GM_REF = (ID_PLUS - ID_MINUS) / (2.0 * DV);
    const double GM_MODEL = MosfetBsim3::transconductance(vgs, 1.0, 0.0, p);
    EXPECT_NEAR(GM_MODEL, GM_REF, std::max(1e-9, 1e-4 * std::fabs(GM_REF)))
        << "vgs=" << vgs;
  }
}

/* ========================== Meyer cap model ========================== */

/** @test In cutoff, all gate cap concentrates on gate-bulk; Cgs/Cgd are
 *        only the constant overlap caps. Per Meyer model textbook. */
TEST(MosfetBsim3MeyerTest, CutoffRegion_GateBulkDominates) {
  MosfetBsim3Params p;
  p.K1 = 0.0; p.eta0 = 0.0; // disable body/DIBL for clean check
  // Vgs = 0 < Vth0 = 1.17, deep cutoff
  const auto C = MosfetBsim3::meyerCapacitances(0.0, 1.0, 0.0, p);
  const double Cox = MosfetBsim3::oxideCapDensity(p);
  const double CoxWL = Cox * p.W * p.L;
  const double Cov = Cox * p.W * p.Lov;
  EXPECT_NEAR(C.Cgs, Cov, 1e-18) << "cutoff Cgs = overlap only";
  EXPECT_NEAR(C.Cgd, Cov, 1e-18) << "cutoff Cgd = overlap only";
  EXPECT_NEAR(C.Cgb, CoxWL, 1e-18) << "cutoff Cgb = full Cox*W*L";
}

/** @test Saturation: Cgs is (2/3)*Cox*W*L, Cgd is just overlap, Cgb is 0.
 *        Standard Meyer textbook value. */
TEST(MosfetBsim3MeyerTest, SaturationRegion_TwoThirdsCgs) {
  MosfetBsim3Params p;
  p.K1 = 0.0; p.eta0 = 0.0;
  const double VGS = p.Vth0 + 1.0; // strong overdrive
  const double VDS = 5.0;          // deep saturation
  const auto C = MosfetBsim3::meyerCapacitances(VGS, VDS, 0.0, p);
  const double Cox = MosfetBsim3::oxideCapDensity(p);
  const double CoxWL = Cox * p.W * p.L;
  const double Cov = Cox * p.W * p.Lov;
  EXPECT_NEAR(C.Cgs, (2.0 / 3.0) * CoxWL + Cov, 1e-18)
      << "saturation Cgs = (2/3)*Cox*W*L + overlap";
  EXPECT_NEAR(C.Cgd, Cov, 1e-18) << "saturation Cgd = overlap only";
  EXPECT_NEAR(C.Cgb, 0.0, 1e-18) << "saturation Cgb = 0 (channel screens bulk)";
}

/** @test Linear region with Vds=0: symmetric Cgs = Cgd = Cox*W*L/2.
 *        Standard Meyer textbook value at Vds=0. */
TEST(MosfetBsim3MeyerTest, LinearRegion_VdsZero_SymmetricCgsCgd) {
  MosfetBsim3Params p;
  p.K1 = 0.0; p.eta0 = 0.0;
  const double VGS = p.Vth0 + 1.0; // strong overdrive
  const double VDS = 0.0;
  const auto C = MosfetBsim3::meyerCapacitances(VGS, VDS, 0.0, p);
  const double Cox = MosfetBsim3::oxideCapDensity(p);
  const double CoxWL = Cox * p.W * p.L;
  const double Cov = Cox * p.W * p.Lov;
  EXPECT_NEAR(C.Cgs, 0.5 * CoxWL + Cov, 1e-18) << "Vds=0 linear: Cgs = Cox*W*L/2";
  EXPECT_NEAR(C.Cgd, 0.5 * CoxWL + Cov, 1e-18) << "Vds=0 linear: Cgd = Cox*W*L/2";
  EXPECT_NEAR(C.Cgb, 0.0, 1e-18);
}

/** @test Linear region near boundary to saturation (Vds = Vdsat - epsilon):
 *        approaches Cgs = (2/3)*Cox*W*L, Cgd = 0. Continuous with sat region. */
TEST(MosfetBsim3MeyerTest, LinearToSaturationContinuity) {
  MosfetBsim3Params p;
  p.K1 = 0.0; p.eta0 = 0.0;
  const double Vov = 1.0;
  const double VGS = p.Vth0 + Vov;
  const auto C_LIN_NEAR_SAT = MosfetBsim3::meyerCapacitances(VGS, Vov - 1e-9, 0.0, p);
  const auto C_SAT = MosfetBsim3::meyerCapacitances(VGS, Vov + 1e-9, 0.0, p);
  EXPECT_NEAR(C_LIN_NEAR_SAT.Cgs, C_SAT.Cgs, 1e-12)
      << "Cgs continuous across linear/sat boundary";
  EXPECT_NEAR(C_LIN_NEAR_SAT.Cgd, C_SAT.Cgd, 1e-12)
      << "Cgd continuous across linear/sat boundary";
}

/** @test Total gate charge conservation (approximate). In cutoff Cgg = Cgb.
 *        In strong inversion (sat), Cgg = Cgs + Cgd (overlap component) +
 *        intrinsic 2/3*CoxWL. Sanity check on total cap magnitudes. */
TEST(MosfetBsim3MeyerTest, TotalGateCapacitanceSanity) {
  MosfetBsim3Params p;
  p.K1 = 0.0; p.eta0 = 0.0;
  const double Cox = MosfetBsim3::oxideCapDensity(p);
  const double CoxWL = Cox * p.W * p.L;

  // Cutoff: total = Cgb ~= CoxWL (plus 2*Cov for overlaps to source/drain)
  const auto C_CUT = MosfetBsim3::meyerCapacitances(0.0, 1.0, 0.0, p);
  const double TOTAL_CUT = C_CUT.Cgs + C_CUT.Cgd + C_CUT.Cgb;
  EXPECT_GT(TOTAL_CUT, CoxWL); // Cgb plus 2*Cov
  EXPECT_LT(TOTAL_CUT, 1.5 * CoxWL); // not pathologically large

  // Saturation: total ~= (2/3)*CoxWL + 2*Cov (Cgs + Cgd_ov)
  const auto C_SAT = MosfetBsim3::meyerCapacitances(p.Vth0 + 1.0, 5.0, 0.0, p);
  const double TOTAL_SAT = C_SAT.Cgs + C_SAT.Cgd + C_SAT.Cgb;
  EXPECT_GT(TOTAL_SAT, 0.6 * CoxWL);
  EXPECT_LT(TOTAL_SAT, 0.8 * CoxWL);
}

/* ========================== L2 Unblocker Probe ========================== */


constexpr double KP = 5e-3;
constexpr double VTH_ENH = 1.17;
constexpr double VTH_DEP = -0.17;
constexpr double LAMBDA = 0.03;
constexpr double VDD = 5.0;
constexpr double WL_DEP = 0.10;
constexpr double WL_ENH = 3.23;

MosfetBsim3Params make4004Params(double kpScaled, double vth) {
  MosfetBsim3Params p;
  p.Kp = kpScaled;
  p.W = 1.0; p.L = 1.0;
  p.Vth0 = vth;
  p.lambda = LAMBDA;
  // 4004 process is 10 um -- second-order effects are mild.
  p.eta0 = 0.0;        // Negligible DIBL at long channel
  p.K1 = 0.0;          // Body effect disabled (vbs = 0)
  p.K2 = 0.0;
  p.ua = 0.0;          // Mobility degradation negligible
  p.ub = 0.0;
  p.n_factor = 1.5;    // Standard subthreshold slope factor
  p.Vt = 0.026;
  p.delta = 0.01;      // Smooth Vds saturation
  return p;
}

void stampPmosBsim3(MnaSystemSparse& mna, NetID drain, NetID gate, NetID source,
                    const std::vector<double>& V, double kpScaled, double vth) {
  const auto p = make4004Params(kpScaled, vth);
  const double VS = V[source], VD = V[drain], VG = V[gate];
  const double VSG = VS - VG, VSD = VS - VD;

  NetID sD = drain, sS = source;
  double eVSG = VSG, eVSD = VSD;
  if (VSD < 0.0) {
    std::swap(sD, sS);
    eVSG = VD - VG;
    eVSD = VD - VS;
  }
  // Note: BSIM3 in this implementation does NOT clamp Vgs/Vds to [0, inf)
  // because the smooth Vgst_eff handles vgs < Vth analytically. Pass
  // signed values directly.
  const double vgsM = std::max(eVSG, 0.0);
  const double vdsM = std::max(eVSD, 0.0);
  constexpr double VBS = 0.0;
  const double id = MosfetBsim3::current(vgsM, vdsM, VBS, p);
  const double gm = MosfetBsim3::transconductance(vgsM, vdsM, VBS, p);
  const double gdsRaw = MosfetBsim3::outputConductance(vgsM, vdsM, VBS, p);
  const double gds = std::max(gdsRaw, 1e-12);
  const double ieq = id - gm * eVSG - gds * eVSD;

  mna.addConductance(sD, sS, gds);
  mna.addMatrixEntry(sD, gate, gm);
  mna.addMatrixEntry(sD, sS, -gm);
  mna.addMatrixEntry(sS, gate, -gm);
  mna.addMatrixEntry(sS, sS, gm);
  mna.addCurrent(sD, sS, ieq);
}

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


