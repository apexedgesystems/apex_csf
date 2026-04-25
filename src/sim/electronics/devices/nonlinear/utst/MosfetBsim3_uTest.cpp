/**
 * @file MosfetBsim3_uTest.cpp
 * @brief Unit tests for the minimal BSIM3v3 MOSFET model.
 *
 * Coverage: each operating region (deep weak inversion, moderate
 * inversion, strong inversion linear, strong inversion saturation),
 * smooth transitions across boundaries, derivatives consistent with
 * numerical differentiation of `current()`.
 *
 * Then: the L2 unblocker scenario -- does BSIM3 with calibrated 4004
 * params drive the depletion-load PMOS NOR's VOL below VTH_enh?
 * (Subthreshold alone via MosfetLevel2 was previously shown not to
 * fix this; the smooth Vgst_eff blending in BSIM3 should.)
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
using sim::electronics::mna::MnaSystemSparse;
using sim::electronics::mna::NetID;

/* ========================== Region behavior ========================== */

/** @test In deep weak inversion (Vgs << Vth), current is exponentially small. */
TEST(MosfetBsim3, DeepWeakInversion_ExponentiallySmall) {
  MosfetBsim3Params p; // defaults: Vth0=1.17
  // Vgs = 0V, well below Vth = 1.17V. Vgs - Vth = -1.17V.
  // n*Vt = 1.5 * 0.026 = 0.039V -> exp(-1.17/0.039) = exp(-30) ~ 1e-13.
  const double id = MosfetBsim3::current(0.0, 1.0, 0.0, p);
  EXPECT_GE(id, 0.0);
  EXPECT_LT(id, 1e-9) << "Deep weak inversion current should be < 1 nA";
}

/** @test In moderate inversion (Vgs near Vth), current is small but non-zero. */
TEST(MosfetBsim3, ModerateInversion_NonZeroAndSmooth) {
  MosfetBsim3Params p;
  // Vgs = Vth exactly: should give a small non-zero current.
  const double idAtVth = MosfetBsim3::current(p.Vth0, 1.0, 0.0, p);
  EXPECT_GT(idAtVth, 0.0);
  EXPECT_LT(idAtVth, 1e-3); // Not yet in strong inversion

  // Slightly above Vth: current should grow.
  const double idAbove = MosfetBsim3::current(p.Vth0 + 0.05, 1.0, 0.0, p);
  EXPECT_GT(idAbove, idAtVth);
}

/** @test In strong inversion saturation, current matches the quadratic Level-1 form. */
TEST(MosfetBsim3, StrongInversionSaturation_MatchesLevel1Form) {
  MosfetBsim3Params p;
  p.eta0 = 0.0;     // Disable DIBL for direct comparison
  p.lambda = 0.0;   // Disable channel-length modulation
  p.ua = 0.0;       // Disable mobility degradation
  p.ub = 0.0;
  p.K1 = 0.0;       // Disable body effect
  p.delta = 1e-9;   // Sharp transition
  p.W = 1.0; p.L = 1.0;

  const double Vov = 1.0; // Strong overdrive: Vgst = 1V >> nVt
  const double vgs = p.Vth0 + Vov;
  const double vds = 5.0; // Deep saturation (Vds >> Vdsat = Vov)
  const double idBsim = MosfetBsim3::current(vgs, vds, 0.0, p);
  const double idL1Reference = 0.5 * p.Kp * Vov * Vov;
  EXPECT_NEAR(idBsim, idL1Reference, 0.05 * idL1Reference)
      << "BSIM3 strong-saturation should match L1 within 5%; bsim=" << idBsim
      << " L1ref=" << idL1Reference;
}

/** @test Linear region: Id increases with Vds, then plateaus near Vdsat. */
TEST(MosfetBsim3, LinearToSaturationTransition_Smooth) {
  MosfetBsim3Params p;
  p.eta0 = 0.0; p.lambda = 0.0; p.ua = 0.0; p.ub = 0.0; p.K1 = 0.0;
  const double vgs = p.Vth0 + 1.0;
  const double idLinear = MosfetBsim3::current(vgs, 0.1, 0.0, p);
  const double idSat = MosfetBsim3::current(vgs, 5.0, 0.0, p);
  EXPECT_LT(idLinear, idSat);
  EXPECT_GT(idLinear, 0.0);
}

/** @test Current and its derivatives are smooth across the threshold. */
TEST(MosfetBsim3, SmoothAcrossThreshold) {
  MosfetBsim3Params p;
  // Sample id at many Vgs around Vth.
  const double vds = 1.0;
  std::vector<double> ids;
  for (double vgs = p.Vth0 - 0.2; vgs <= p.Vth0 + 0.2; vgs += 0.01) {
    const double id = MosfetBsim3::current(vgs, vds, 0.0, p);
    ids.push_back(id);
  }
  // Verify monotonic increase (smoothness).
  for (std::size_t i = 1; i < ids.size(); ++i) {
    EXPECT_GE(ids[i], ids[i - 1]) << "id should increase monotonically with Vgs near Vth";
  }
  // Verify no large jumps (no kinks).
  for (std::size_t i = 1; i < ids.size(); ++i) {
    const double rel = std::fabs(ids[i] - ids[i - 1]) / std::max(ids[i - 1], 1e-12);
    EXPECT_LT(rel, 10.0)
        << "id jump at Vgs=" << (p.Vth0 - 0.2 + 0.01 * i) << " is too large (kink)";
  }
}

/** @test gm matches numerical derivative of current (sanity). */
TEST(MosfetBsim3, GmConsistentWithNumericalDerivative) {
  MosfetBsim3Params p;
  for (double vgs : {0.5, 1.0, 1.17, 1.5, 2.0, 3.0}) {
    const double dv = 1e-5;
    const double idMinus = MosfetBsim3::current(vgs - dv, 1.0, 0.0, p);
    const double idPlus = MosfetBsim3::current(vgs + dv, 1.0, 0.0, p);
    const double gmRef = (idPlus - idMinus) / (2.0 * dv);
    const double gmModel = MosfetBsim3::transconductance(vgs, 1.0, 0.0, p);
    EXPECT_NEAR(gmModel, gmRef, std::max(1e-9, 1e-4 * std::fabs(gmRef)))
        << "vgs=" << vgs;
  }
}

/* ========================== L2 Unblocker Probe ========================== */

namespace {

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
  // 4004 process is 10 µm -- second-order effects are mild.
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
  // Note: BSIM3 in this implementation does NOT clamp Vgs/Vds to [0, ∞)
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

} // namespace

/**
 * @test BSIM3 L2 unblocker probe: same depletion-load PMOS inverter
 *       topology as the prior Level1 / Level2 tests, asking whether
 *       BSIM3's smooth Vgst_eff drives VOL below VTH_enh = 1.170V.
 *
 * If yes -> BSIM3 is the correct fix for the L2 latch feedback issue.
 * If no  -> the analysis was wrong; look at the latch topology
 *           directly (cross-coupled pair, not a single inverter).
 */
TEST(MosfetBsim3Probe, InverterVolL2Unblocker) {
  std::vector<double> V = {0, VDD, 2.0, 0};

  double vout = solveDc(4, V, [](MnaSystemSparse& mna, const std::vector<double>& v) {
    mna.addVoltageSource(1, 0, VDD);
    mna.addVoltageSource(3, 0, 0.0);
    stampPmosBsim3(mna, 2, 1, 1, v, KP * WL_DEP, VTH_DEP);
    stampPmosBsim3(mna, 0, 3, 2, v, KP * WL_ENH, VTH_ENH);
  });

  const double overdrive = VTH_ENH - vout;
  std::printf("\n  ==== BSIM3 L2 Unblocker Probe ====\n");
  std::printf("    VOL_BSIM3:   %.4fV\n", vout);
  std::printf("    VTH_enh:     %.4fV\n", VTH_ENH);
  std::printf("    Overdrive:   %.4fV  (positive = latch can resolve)\n", overdrive);
  std::printf("    L1 reference: VOL=1.2010V, overdrive=-30 mV\n");
  if (overdrive > 0.0) {
    std::printf("  ==> BSIM3 unblocks L2.\n");
  } else {
    std::printf("  ==> BSIM3 alone does NOT unblock with this topology.\n");
    std::printf("      Means the analysis is in the latch (cross-coupled), not the inverter.\n");
  }

  EXPECT_GT(vout, 0.0);
  EXPECT_LT(vout, VDD);
}

/**
 * @test BSIM3 L2 unblocker probe with `n_factor` swept across the
 *       physical range [1.0 .. 2.5] to see how much weak-inversion
 *       broadening drops VOL.
 */
TEST(MosfetBsim3Probe, NFactorSweep) {
  std::printf("\n  ==== BSIM3 n_factor sweep ====\n");
  std::printf("    n_factor    VOL    Overdrive\n");
  for (double n : {1.0, 1.2, 1.5, 1.8, 2.0, 2.2, 2.5, 3.0}) {
    std::vector<double> V = {0, VDD, 2.0, 0};
    auto pmosWithN = [n](MnaSystemSparse& mna, NetID drain, NetID gate, NetID source,
                         const std::vector<double>& v, double kpScaled, double vth) {
      auto p = make4004Params(kpScaled, vth);
      p.n_factor = n;
      const double VS = v[source], VD = v[drain], VG = v[gate];
      const double VSG = VS - VG, VSD = VS - VD;
      NetID sD = drain, sS = source;
      double eVSG = VSG, eVSD = VSD;
      if (VSD < 0.0) {
        std::swap(sD, sS);
        eVSG = VD - VG;
        eVSD = VD - VS;
      }
      const double vgsM = std::max(eVSG, 0.0);
      const double vdsM = std::max(eVSD, 0.0);
      const double id = MosfetBsim3::current(vgsM, vdsM, 0.0, p);
      const double gm = MosfetBsim3::transconductance(vgsM, vdsM, 0.0, p);
      const double gds = std::max(MosfetBsim3::outputConductance(vgsM, vdsM, 0.0, p), 1e-12);
      const double ieq = id - gm * eVSG - gds * eVSD;
      mna.addConductance(sD, sS, gds);
      mna.addMatrixEntry(sD, gate, gm);
      mna.addMatrixEntry(sD, sS, -gm);
      mna.addMatrixEntry(sS, gate, -gm);
      mna.addMatrixEntry(sS, sS, gm);
      mna.addCurrent(sD, sS, ieq);
    };
    double vout = solveDc(4, V, [&](MnaSystemSparse& mna, const std::vector<double>& v) {
      mna.addVoltageSource(1, 0, VDD);
      mna.addVoltageSource(3, 0, 0.0);
      pmosWithN(mna, 2, 1, 1, v, KP * WL_DEP, VTH_DEP);
      pmosWithN(mna, 0, 3, 2, v, KP * WL_ENH, VTH_ENH);
    });
    std::printf("    %.2f      %.4fV   %+.4fV %s\n", n, vout, VTH_ENH - vout,
                (VTH_ENH - vout) > 0.0 ? "<-- POSITIVE" : "");
  }
}
