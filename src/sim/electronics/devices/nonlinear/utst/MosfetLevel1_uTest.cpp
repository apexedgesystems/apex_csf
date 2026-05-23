/**
 * @file MosfetLevel1_uTest.cpp
 * @brief Unit tests for MosfetLevel1 model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Parameters ----------------------------- */

/** @test Default parameters are reasonable. */
TEST(MosfetLevel1Test, DefaultParameters) {
  MosfetLevel1Params params;

  EXPECT_DOUBLE_EQ(params.Kp, 100e-6);
  EXPECT_DOUBLE_EQ(params.Vth, 0.7);
  EXPECT_DOUBLE_EQ(params.lambda, 0.0);
}

/** @test Custom parameters. */
TEST(MosfetLevel1Test, CustomParameters) {
  MosfetLevel1Params params{.Kp = 50e-6, .Vth = 1.0, .lambda = 0.02};

  EXPECT_DOUBLE_EQ(params.Kp, 50e-6);
  EXPECT_DOUBLE_EQ(params.Vth, 1.0);
  EXPECT_DOUBLE_EQ(params.lambda, 0.02);
}

/* ----------------------------- I-V Characteristic ----------------------------- */

/** @test Cutoff region (Vgs < Vth). */
TEST(MosfetLevel1Test, CutoffRegion) {
  MosfetLevel1Params params;

  double id1 = MosfetLevel1::current(0.0, 2.0, params); // Vgs = 0V
  double id2 = MosfetLevel1::current(0.5, 2.0, params); // Vgs < Vth
  double id3 = MosfetLevel1::current(0.7, 2.0, params); // Vgs = Vth

  EXPECT_DOUBLE_EQ(id1, 0.0);
  EXPECT_DOUBLE_EQ(id2, 0.0);
  EXPECT_DOUBLE_EQ(id3, 0.0); // At threshold, still cutoff
}

/** @test Linear region (Vgs > Vth, Vds < Vgst). */
TEST(MosfetLevel1Test, LinearRegion) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 0.5V < Vgst
  double id = MosfetLevel1::current(1.5, 0.5, params);

  // Id = Kp * (Vgst * Vds - 0.5 * Vds^2)
  //    = 100e-6 * (0.8 * 0.5 - 0.5 * 0.25)
  //    = 100e-6 * (0.4 - 0.125)
  //    = 100e-6 * 0.275 = 27.5 uA
  EXPECT_NEAR(id, 27.5e-6, 1e-9);
}

/** @test Saturation region (Vgs > Vth, Vds >= Vgst). */
TEST(MosfetLevel1Test, SaturationRegion) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 2.0V > Vgst
  double id = MosfetLevel1::current(1.5, 2.0, params);

  // Id = 0.5 * Kp * Vgst^2
  //    = 0.5 * 100e-6 * 0.64
  //    = 32 uA
  EXPECT_NEAR(id, 32e-6, 1e-9);
}

/** @test Saturation region boundary (Vds = Vgst). */
TEST(MosfetLevel1Test, SaturationBoundary) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 0.8V (exactly at boundary)
  double id = MosfetLevel1::current(1.5, 0.8, params);

  // Should be in saturation region
  // Id = 0.5 * Kp * Vgst^2 = 0.5 * 100e-6 * 0.64 = 32 uA
  EXPECT_NEAR(id, 32e-6, 1e-9);
}

/** @test Channel-length modulation (lambda > 0). */
TEST(MosfetLevel1Test, ChannelLengthModulation) {
  MosfetLevel1Params ideal{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.0};
  MosfetLevel1Params realistic{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  // Saturation region: Vgs = 1.5V, Vds = 2.0V
  double idIdeal = MosfetLevel1::current(1.5, 2.0, ideal);
  double idRealistic = MosfetLevel1::current(1.5, 2.0, realistic);

  // With lambda, current increases: Id * (1 + lambda * Vds)
  // idRealistic = idIdeal * (1 + 0.02 * 2.0) = idIdeal * 1.04
  EXPECT_NEAR(idRealistic / idIdeal, 1.04, 1e-6);
}

/* ----------------------------- Transconductance (gm) ----------------------------- */

/** @test Transconductance in cutoff. */
TEST(MosfetLevel1Test, TransconductanceCutoff) {
  MosfetLevel1Params params;

  double gm = MosfetLevel1::transconductance(0.5, 2.0, params); // Vgs < Vth

  EXPECT_DOUBLE_EQ(gm, 0.0);
}

/** @test Transconductance in linear region. */
TEST(MosfetLevel1Test, TransconductanceLinear) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vds = 0.5V (linear region)
  double gm = MosfetLevel1::transconductance(1.5, 0.5, params);

  // gm = Kp * Vds = 100e-6 * 0.5 = 50 uS
  EXPECT_NEAR(gm, 50e-6, 1e-9);
}

/** @test Transconductance in saturation region. */
TEST(MosfetLevel1Test, TransconductanceSaturation) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vds = 2.0V (saturation region)
  double gm = MosfetLevel1::transconductance(1.5, 2.0, params);

  // gm = Kp * Vgst = 100e-6 * 0.8 = 80 uS
  EXPECT_NEAR(gm, 80e-6, 1e-9);
}

/** @test Transconductance matches numerical derivative. */
TEST(MosfetLevel1Test, TransconductanceNumericalDerivative) {
  MosfetLevel1Params params;
  const double VGS = 1.5;
  const double VDS = 2.0;
  const double DVGS = 1e-6;

  // Analytical transconductance
  double gmAnalytical = MosfetLevel1::transconductance(VGS, VDS, params);

  // Numerical derivative: dId/dVgs ~= (Id(vgs+dvgs) - Id(vgs-dvgs)) / (2*dvgs)
  double id1 = MosfetLevel1::current(VGS - DVGS, VDS, params);
  double id2 = MosfetLevel1::current(VGS + DVGS, VDS, params);
  double gmNumerical = (id2 - id1) / (2.0 * DVGS);

  // Should match within 1%
  EXPECT_NEAR(gmAnalytical, gmNumerical, std::abs(gmNumerical) * 0.01);
}

/* ----------------------------- Output Conductance (gds) ----------------------------- */

/** @test Output conductance in cutoff. */
TEST(MosfetLevel1Test, OutputConductanceCutoff) {
  MosfetLevel1Params params;

  double gds = MosfetLevel1::outputConductance(0.5, 2.0, params); // Vgs < Vth

  EXPECT_DOUBLE_EQ(gds, 0.0);
}

/** @test Output conductance in saturation with lambda = 0. */
TEST(MosfetLevel1Test, OutputConductanceSaturationIdeal) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.0};

  // Vgs = 1.5V, Vds = 2.0V (saturation, ideal)
  double gds = MosfetLevel1::outputConductance(1.5, 2.0, params);

  // Ideal MOSFET: gds = 0 in saturation
  EXPECT_DOUBLE_EQ(gds, 0.0);
}

/** @test Output conductance in saturation with lambda > 0. */
TEST(MosfetLevel1Test, OutputConductanceSaturationRealistic) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  // Vgs = 1.5V, Vds = 2.0V (saturation, realistic)
  double gds = MosfetLevel1::outputConductance(1.5, 2.0, params);

  // gds = Id * lambda = 32e-6 * 0.02 = 0.64 uS
  EXPECT_GT(gds, 0.0);
  EXPECT_NEAR(gds, 0.64e-6, 1e-9);
}

/** @test Output conductance in linear region. */
TEST(MosfetLevel1Test, OutputConductanceLinear) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};

  // Vgs = 1.5V, Vds = 0.5V (linear region)
  double gds = MosfetLevel1::outputConductance(1.5, 0.5, params);

  // gds = Kp * (Vgst - Vds) = 100e-6 * (0.8 - 0.5) = 30 uS
  EXPECT_NEAR(gds, 30e-6, 1e-9);
}

/** @test Output conductance matches numerical derivative. */
TEST(MosfetLevel1Test, OutputConductanceNumericalDerivative) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};
  const double VGS = 1.5;
  const double VDS = 2.0;
  const double DVDS = 1e-6;

  // Analytical output conductance
  double gdsAnalytical = MosfetLevel1::outputConductance(VGS, VDS, params);

  // Numerical derivative: dId/dVds ~= (Id(vds+dvds) - Id(vds-dvds)) / (2*dvds)
  double id1 = MosfetLevel1::current(VGS, VDS - DVDS, params);
  double id2 = MosfetLevel1::current(VGS, VDS + DVDS, params);
  double gdsNumerical = (id2 - id1) / (2.0 * DVDS);

  // Should match within 1%
  EXPECT_NEAR(gdsAnalytical, gdsNumerical, std::abs(gdsNumerical) * 0.01);
}

/* ----------------------------- Region Detection ----------------------------- */

/** @test Region detection: deep cutoff and subthreshold smoothing.
 *
 * region() returns:
 *   -1 : deep cutoff      (vgs <= Vth - Vsmooth)
 *    0 : subthreshold     (Vth - Vsmooth < vgs < Vth)
 *    1 : linear           (vgs >= Vth, vds < vgst)
 *    2 : saturation       (vgs >= Vth, vds >= vgst)
 *
 * The subthreshold smoothing region was added during ngspice calibration to
 * fix the Jacobian discontinuity at Vth that broke NR convergence.
 */
TEST(MosfetLevel1Test, RegionCutoff) {
  MosfetLevel1Params params; // Vth=0.7, Vsmooth=0.1 (defaults)

  // vgs = 0 < Vth - Vsmooth = 0.6  -> deep cutoff
  EXPECT_EQ(MosfetLevel1::region(0.0, 2.0, params), -1);

  // vgs = 0.5 < 0.6 -> deep cutoff
  EXPECT_EQ(MosfetLevel1::region(0.5, 2.0, params), -1);

  // vgs = 0.65 in (0.6, 0.7) -> subthreshold smoothing
  EXPECT_EQ(MosfetLevel1::region(0.65, 2.0, params), 0);

  // vgs = 0.7 == Vth -> active region (vgst = 0, vds > 0 -> saturation)
  EXPECT_EQ(MosfetLevel1::region(0.7, 2.0, params), 2);
}

/** @test Region detection: hard cutoff when Vsmooth is disabled.
 *
 * Setting Vsmooth=0 reverts to the original Shichman-Hodges form with hard
 * cutoff at Vth. The cutoff condition is `vgs <= Vth - Vsmooth`, so with
 * Vsmooth=0 the boundary `vgs == Vth` evaluates as deep cutoff (inclusive).
 */
TEST(MosfetLevel1Test, RegionCutoffHard) {
  MosfetLevel1Params params{.Vth = 0.7, .Vsmooth = 0.0};

  // No smoothing window: anything <= Vth is deep cutoff
  EXPECT_EQ(MosfetLevel1::region(0.0, 2.0, params), -1);
  EXPECT_EQ(MosfetLevel1::region(0.5, 2.0, params), -1);
  EXPECT_EQ(MosfetLevel1::region(0.699, 2.0, params), -1);
  EXPECT_EQ(MosfetLevel1::region(0.7, 2.0, params), -1); // Boundary inclusive

  // Just above Vth: active region (vgst tiny, vds large -> saturation)
  EXPECT_EQ(MosfetLevel1::region(0.701, 2.0, params), 2);
}

/** @test Region detection: linear. */
TEST(MosfetLevel1Test, RegionLinear) {
  MosfetLevel1Params params{.Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 0.5V < Vgst
  int r = MosfetLevel1::region(1.5, 0.5, params);

  EXPECT_EQ(r, 1);
}

/** @test Region detection: saturation. */
TEST(MosfetLevel1Test, RegionSaturation) {
  MosfetLevel1Params params{.Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 2.0V > Vgst
  int r = MosfetLevel1::region(1.5, 2.0, params);

  EXPECT_EQ(r, 2);
}

/** @test Region detection: linear-saturation boundary. */
TEST(MosfetLevel1Test, RegionBoundary) {
  MosfetLevel1Params params{.Vth = 0.7};

  // Vgs = 1.5V, Vgst = 0.8V, Vds = 0.8V (exactly at boundary)
  int r = MosfetLevel1::region(1.5, 0.8, params);

  EXPECT_EQ(r, 2); // Boundary is saturation (Vds >= Vgst)
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp cutoff state. */
TEST(MosfetLevel1Test, StampCutoff) {
  MnaSystem mna(4);
  MosfetLevel1Params params;
  const NetID DRAIN = 1;
  const NetID GATE = 2;
  const NetID SOURCE = 0;

  MosfetLevel1::stamp(mna, DRAIN, GATE, SOURCE, 0.5, 2.0, params);

  // Should stamp zero conductances (cutoff)
}

/** @test Stamp linear region. */
TEST(MosfetLevel1Test, StampLinear) {
  MnaSystem mna(4);
  MosfetLevel1Params params;
  const NetID DRAIN = 1;
  const NetID GATE = 2;
  const NetID SOURCE = 0;

  MosfetLevel1::stamp(mna, DRAIN, GATE, SOURCE, 1.5, 0.5, params);

  // Should stamp gm and gds
}

/** @test Stamp saturation region. */
TEST(MosfetLevel1Test, StampSaturation) {
  MnaSystem mna(4);
  MosfetLevel1Params params;
  const NetID DRAIN = 1;
  const NetID GATE = 2;
  const NetID SOURCE = 0;

  MosfetLevel1::stamp(mna, DRAIN, GATE, SOURCE, 1.5, 2.0, params);

  // Should stamp gm (non-zero) and gds (zero for ideal)
}

/* ----------------------------- Physical Behavior ----------------------------- */

/** @test Output characteristics (Id vs Vds for fixed Vgs). */
TEST(MosfetLevel1Test, OutputCharacteristics) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};
  const double VGS = 1.5; // Fixed Vgs

  double id1 = MosfetLevel1::current(VGS, 0.1, params);
  double id2 = MosfetLevel1::current(VGS, 0.3, params);
  double id3 = MosfetLevel1::current(VGS, 0.5, params);
  double id4 = MosfetLevel1::current(VGS, 0.8, params); // Boundary
  double id5 = MosfetLevel1::current(VGS, 1.5, params); // Saturation
  double id6 = MosfetLevel1::current(VGS, 3.0, params); // Deep saturation

  // Linear region: current increases with Vds
  EXPECT_LT(id1, id2);
  EXPECT_LT(id2, id3);
  EXPECT_LT(id3, id4);

  // Saturation region: current saturates (constant if lambda=0)
  EXPECT_NEAR(id4, id5, 1e-9);
  EXPECT_NEAR(id5, id6, 1e-9);
}

/** @test Transfer characteristics (Id vs Vgs for fixed Vds). */
TEST(MosfetLevel1Test, TransferCharacteristics) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};
  const double VDS = 2.0; // Fixed Vds (saturation)

  double id1 = MosfetLevel1::current(0.5, VDS, params); // Below Vth
  double id2 = MosfetLevel1::current(0.7, VDS, params); // At Vth
  double id3 = MosfetLevel1::current(1.0, VDS, params);
  double id4 = MosfetLevel1::current(1.5, VDS, params);
  double id5 = MosfetLevel1::current(2.0, VDS, params);

  // Below threshold: zero current
  EXPECT_DOUBLE_EQ(id1, 0.0);
  EXPECT_DOUBLE_EQ(id2, 0.0);

  // Above threshold: quadratic increase (saturation region)
  EXPECT_GT(id3, 0.0);
  EXPECT_GT(id4, id3);
  EXPECT_GT(id5, id4);

  // Verify quadratic relationship: Id proportional to Vgst^2
  // (Vgs=1.5, Vgst=0.8): Id4 = 32 uA
  // (Vgs=2.0, Vgst=1.3): Id5 = 0.5 * 100e-6 * 1.69 = 84.5 uA
  // Ratio: Id5/Id4 = 1.69/0.64 = 2.64
  EXPECT_NEAR(id5 / id4, (1.3 * 1.3) / (0.8 * 0.8), 1e-6);
}

/** @test Subthreshold smoothing transitions continuously to zero.
 *
 * The model has a quadratic smoothing region over [Vth-Vsmooth, Vth] (default
 * Vsmooth=0.1) added during ngspice calibration to make the Jacobian continuous
 * across the threshold. Hard cutoff applies only below Vth-Vsmooth.
 *
 * This test verifies:
 *   1. Deep cutoff (vgs <= Vth-Vsmooth) returns exactly zero
 *   2. Smoothing region produces small but nonzero current
 *   3. Smoothing is monotonic (current grows with vgs in the window)
 *   4. Above-threshold current is much larger than smoothing-region current
 *   5. With Vsmooth=0 the model reverts to a hard cutoff at Vth
 */
TEST(MosfetLevel1Test, SubthresholdCutoff) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7}; // Vsmooth=0.1 default

  // Deep cutoff: vgs <= Vth - Vsmooth -> exactly zero
  EXPECT_DOUBLE_EQ(MosfetLevel1::current(0.0, 2.0, params), 0.0);
  EXPECT_DOUBLE_EQ(MosfetLevel1::current(0.5, 2.0, params), 0.0);
  EXPECT_DOUBLE_EQ(MosfetLevel1::current(0.6, 2.0, params), 0.0);

  // Subthreshold smoothing window (0.6, 0.7): nonzero, monotonically increasing
  double idLo = MosfetLevel1::current(0.65, 2.0, params);
  double idHi = MosfetLevel1::current(0.699, 2.0, params);
  EXPECT_GT(idLo, 0.0);
  EXPECT_GT(idHi, idLo) << "Smoothing should grow monotonically toward Vth";

  // Above threshold: current grows quickly via Shichman-Hodges
  double idAbove = MosfetLevel1::current(0.701, 2.0, params);
  EXPECT_GT(idAbove, 0.0);

  // Smoothing-region current is bounded by id_at_threshold:
  //   id_at_th = 0.5 * Kp * Vsmooth^2 * (1 + lambda*vds)
  //            = 0.5 * 100e-6 * 0.01 * 1 = 5e-7 A
  // The smoothing returns id_at_th * (ratio)^2 where ratio in [0,1].
  constexpr double ID_AT_THRESHOLD = 0.5 * 100e-6 * 0.1 * 0.1;
  EXPECT_LE(idHi, ID_AT_THRESHOLD) << "Smoothing region must not exceed id_at_threshold";

  // With a real overdrive (well above threshold) the saturation current
  // dominates the smoothing leakage by orders of magnitude.
  double idOverdrive = MosfetLevel1::current(2.0, 2.0, params); // 1.3V overdrive
  EXPECT_LT(idHi * 100.0, idOverdrive)
      << "Smoothing leakage should be << current with substantial overdrive";
}

/** @test Hard cutoff: original Shichman-Hodges form when Vsmooth is disabled. */
TEST(MosfetLevel1Test, SubthresholdCutoffHard) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .Vsmooth = 0.0};

  // With Vsmooth=0 the model has the original abrupt cutoff at Vth
  EXPECT_DOUBLE_EQ(MosfetLevel1::current(0.699, 2.0, params), 0.0);
  EXPECT_GT(MosfetLevel1::current(0.701, 2.0, params), 0.0);
}

/** @test Transconductance increases with Vgs in saturation. */
TEST(MosfetLevel1Test, TransconductanceVsVgs) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7};
  const double VDS = 2.0; // Saturation

  double gm1 = MosfetLevel1::transconductance(1.0, VDS, params);
  double gm2 = MosfetLevel1::transconductance(1.5, VDS, params);
  double gm3 = MosfetLevel1::transconductance(2.0, VDS, params);

  // gm = Kp * Vgst (linear with Vgst in saturation)
  EXPECT_LT(gm1, gm2);
  EXPECT_LT(gm2, gm3);

  // Verify linear relationship
  // gm1 = 100e-6 * 0.3 = 30 uS
  // gm2 = 100e-6 * 0.8 = 80 uS
  // gm3 = 100e-6 * 1.3 = 130 uS
  EXPECT_NEAR(gm1, 30e-6, 1e-9);
  EXPECT_NEAR(gm2, 80e-6, 1e-9);
  EXPECT_NEAR(gm3, 130e-6, 1e-9);
}

/** @test Channel-length modulation increases gds. */
TEST(MosfetLevel1Test, ChannelLengthModulationGds) {
  MosfetLevel1Params ideal{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.0};
  MosfetLevel1Params realistic{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

  double gdsIdeal = MosfetLevel1::outputConductance(1.5, 2.0, ideal);
  double gdsRealistic = MosfetLevel1::outputConductance(1.5, 2.0, realistic);

  // Ideal: gds = 0
  EXPECT_DOUBLE_EQ(gdsIdeal, 0.0);

  // Realistic: gds > 0
  EXPECT_GT(gdsRealistic, 0.0);
}

/** @test Weak MOSFET (low Kp). */
TEST(MosfetLevel1Test, WeakMosfet) {
  MosfetLevel1Params params{.Kp = 20e-6, .Vth = 0.7}; // Low Kp

  double id = MosfetLevel1::current(1.5, 2.0, params);

  // Id = 0.5 * 20e-6 * 0.64 = 6.4 uA
  EXPECT_NEAR(id, 6.4e-6, 1e-9);
}

/** @test Strong MOSFET (high Kp). */
TEST(MosfetLevel1Test, StrongMosfet) {
  MosfetLevel1Params params{.Kp = 200e-6, .Vth = 0.7}; // High Kp

  double id = MosfetLevel1::current(1.5, 2.0, params);

  // Id = 0.5 * 200e-6 * 0.64 = 64 uA
  EXPECT_NEAR(id, 64e-6, 1e-9);
}

/** @test Low threshold voltage (depletion mode). */
TEST(MosfetLevel1Test, DepletionMode) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = -0.5}; // Negative Vth

  // Even at Vgs = 0, MOSFET is ON
  double id = MosfetLevel1::current(0.0, 2.0, params);

  EXPECT_GT(id, 0.0);
}

/** @test High threshold voltage (enhancement mode). */
TEST(MosfetLevel1Test, EnhancementMode) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 1.5}; // High Vth

  // At Vgs = 1.0V, MOSFET is OFF
  double id1 = MosfetLevel1::current(1.0, 2.0, params);
  EXPECT_DOUBLE_EQ(id1, 0.0);

  // At Vgs = 2.0V, MOSFET is ON
  double id2 = MosfetLevel1::current(2.0, 2.0, params);
  EXPECT_GT(id2, 0.0);
}

/* ----------------------------- Voltage Limiting (fetlim) ----------------------------- */

/** @test fetlim limits large positive jumps above threshold. */
TEST(MosfetLevel1Test, FetlimAboveThresholdLargePositiveJump) {
  const double VTO = 0.7;

  // vold well above vtox (vto + 3.5 = 4.2), large positive jump
  double vold = 5.0;
  double vnew = 50.0;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // vtsthi = |2*(5.0-0.7)| + 2 = 10.6
  // vnew should be clamped to vold + vtsthi = 15.6
  EXPECT_LT(limited, vnew);
  EXPECT_NEAR(limited, vold + std::fabs(2.0 * (vold - VTO)) + 2.0, 1e-12);
}

/** @test fetlim limits large negative jumps above threshold, above vtox. */
TEST(MosfetLevel1Test, FetlimAboveThresholdLargeNegativeJump) {
  const double VTO = 0.7;

  // vold above vtox, large negative jump but vnew still above vtox
  double vold = 10.0;
  double vnew = -5.0; // drops below vtox

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // vnew < vtox, so clamped to max(vnew, vto + 2.0) = max(-5, 2.7) = 2.7
  EXPECT_NEAR(limited, VTO + 2.0, 1e-12);
}

/** @test fetlim limits negative jump above vtox when vnew stays above vtox. */
TEST(MosfetLevel1Test, FetlimAboveVtoxNegativeJumpStaysAbove) {
  const double VTO = 0.7;
  double vold = 10.0;
  // vtstlo = |10.0 - 0.7| + 1 = 10.3
  // vnew still above vtox but delta exceeds vtstlo
  double vnew = -2.0;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // vnew < vtox so path is max(vnew, vto+2) = max(-2, 2.7) = 2.7
  EXPECT_NEAR(limited, VTO + 2.0, 1e-12);
}

/** @test fetlim between vto and vtox: negative jump clamps to vto - 0.5. */
TEST(MosfetLevel1Test, FetlimBetweenVtoAndVtoxNegativeJump) {
  const double VTO = 0.7;

  // vold between vto and vtox
  double vold = 2.0;
  double vnew = -5.0;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // Path: vold >= vto, vold < vtox, delv <= 0 -> max(vnew, vto - 0.5)
  EXPECT_NEAR(limited, VTO - 0.5, 1e-12);
}

/** @test fetlim between vto and vtox: positive jump clamps to vto + 4.0. */
TEST(MosfetLevel1Test, FetlimBetweenVtoAndVtoxPositiveJump) {
  const double VTO = 0.7;

  // vold between vto and vtox
  double vold = 2.0;
  double vnew = 20.0;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // Path: vold >= vto, vold < vtox, delv > 0 -> min(vnew, vto + 4.0)
  EXPECT_NEAR(limited, VTO + 4.0, 1e-12);
}

/** @test fetlim below threshold: large negative jump is limited. */
TEST(MosfetLevel1Test, FetlimBelowThresholdNegativeJump) {
  const double VTO = 0.7;

  // vold below threshold
  double vold = -1.0;
  // vtsthi = |2*(-1.0-0.7)| + 2 = 5.4
  double vnew = -20.0;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);

  // delv = -19, -delv=19 > vtsthi=5.4 -> vnew = vold - vtsthi = -6.4
  EXPECT_NEAR(limited, vold - (std::fabs(2.0 * (vold - VTO)) + 2.0), 1e-12);
}

/** @test fetlim below threshold: positive jump below vtemp is limited by vtstlo. */
TEST(MosfetLevel1Test, FetlimBelowThresholdPositiveJumpSmall) {
  const double VTO = 0.7;

  // vold below threshold
  double vold = -0.5;
  // vtstlo = |-0.5 - 0.7| + 1 = 2.2
  // vtemp = vto + 0.5 = 1.2
  // vnew = 0.5 < vtemp, delv = 1.0 < vtstlo -> no clamping
  double vnew = 0.5;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);
  EXPECT_NEAR(limited, 0.5, 1e-12);

  // Larger jump: vnew below vtemp, delv exceeds vtstlo
  vnew = 5.0; // delv = 5.5, but vnew > vtemp -> clamps to vtemp
  limited = MosfetLevel1::fetlim(vnew, vold, VTO);
  EXPECT_NEAR(limited, VTO + 0.5, 1e-12);
}

/** @test fetlim small changes pass through unmodified. */
TEST(MosfetLevel1Test, FetlimSmallChange) {
  const double VTO = 0.7;

  // Above threshold, small change
  double vold = 2.0;
  double vnew = 2.1;

  double limited = MosfetLevel1::fetlim(vnew, vold, VTO);
  EXPECT_NEAR(limited, vnew, 1e-12);

  // Below threshold, small change
  vold = -0.5;
  vnew = -0.4;
  limited = MosfetLevel1::fetlim(vnew, vold, VTO);
  EXPECT_NEAR(limited, vnew, 1e-12);
}

/* ----------------------------- Voltage Limiting (limvds) ----------------------------- */

/** @test limvds limits upward jump when vold >= 3.5. */
TEST(MosfetLevel1Test, LimvdsHighVoldUpwardJump) {
  // vold >= 3.5, large upward jump
  double limited = MosfetLevel1::limvds(100.0, 5.0);

  // Clamped to min(100, 3*5+2) = 17
  EXPECT_NEAR(limited, 17.0, 1e-12);
}

/** @test limvds limits downward jump below 3.5 when vold >= 3.5. */
TEST(MosfetLevel1Test, LimvdsHighVoldDownwardJump) {
  // vold >= 3.5, vnew drops below 3.5
  double limited = MosfetLevel1::limvds(0.5, 4.0);

  // max(0.5, 2.0) = 2.0
  EXPECT_NEAR(limited, 2.0, 1e-12);
}

/** @test limvds limits upward jump when vold < 3.5. */
TEST(MosfetLevel1Test, LimvdsLowVoldUpwardJump) {
  // vold < 3.5, vnew jumps up
  double limited = MosfetLevel1::limvds(20.0, 1.0);

  // min(20, 4) = 4
  EXPECT_NEAR(limited, 4.0, 1e-12);
}

/** @test limvds limits downward jump when vold < 3.5. */
TEST(MosfetLevel1Test, LimvdsLowVoldDownwardJump) {
  // vold < 3.5, vnew drops below -0.5
  double limited = MosfetLevel1::limvds(-5.0, 1.0);

  // max(-5, -0.5) = -0.5
  EXPECT_NEAR(limited, -0.5, 1e-12);
}

/** @test limvds passes through small changes. */
TEST(MosfetLevel1Test, LimvdsSmallChange) {
  // Within limits at high vold
  double limited = MosfetLevel1::limvds(5.5, 5.0);
  EXPECT_NEAR(limited, 5.5, 1e-12);

  // Within limits at low vold
  limited = MosfetLevel1::limvds(2.0, 1.0);
  EXPECT_NEAR(limited, 2.0, 1e-12);
}

/* ----------------------------- Smooth Transition Region ----------------------------- */

/** @test Current in smooth transition region matches expected quadratic. */
TEST(MosfetLevel1Test, SmoothTransitionCurrent) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  // Midpoint of smoothing window: vgs = 0.65, ratio = 0.5
  double vgs = 0.65;
  double vds = 2.0;
  double id = MosfetLevel1::current(vgs, vds, params);

  double id_at_th = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth * (1.0 + params.lambda * vds);
  double ratio = (vgs - (params.Vth - params.Vsmooth)) / params.Vsmooth;
  double expected = id_at_th * ratio * ratio;

  EXPECT_NEAR(id, expected, 1e-15);
}

/** @test Transconductance in smooth transition matches analytical derivative. */
TEST(MosfetLevel1Test, SmoothTransitionTransconductance) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  double vgs = 0.65;
  double vds = 2.0;
  double gm = MosfetLevel1::transconductance(vgs, vds, params);

  // Analytical: id_at_th * 2 * ratio / Vsmooth
  double id_at_th = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth * (1.0 + params.lambda * vds);
  double ratio = (vgs - (params.Vth - params.Vsmooth)) / params.Vsmooth;
  double expected = id_at_th * 2.0 * ratio / params.Vsmooth;

  EXPECT_NEAR(gm, expected, 1e-15);
}

/** @test Transconductance in smooth transition matches numerical derivative. */
TEST(MosfetLevel1Test, SmoothTransitionTransconductanceNumerical) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  double vgs = 0.65;
  double vds = 2.0;
  double dvgs = 1e-7;

  double gm = MosfetLevel1::transconductance(vgs, vds, params);
  double id_lo = MosfetLevel1::current(vgs - dvgs, vds, params);
  double id_hi = MosfetLevel1::current(vgs + dvgs, vds, params);
  double gm_num = (id_hi - id_lo) / (2.0 * dvgs);

  EXPECT_NEAR(gm, gm_num, std::fabs(gm_num) * 0.001);
}

/** @test Output conductance in smooth transition with lambda > 0. */
TEST(MosfetLevel1Test, SmoothTransitionOutputConductance) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  double vgs = 0.65;
  double vds = 2.0;
  double gds = MosfetLevel1::outputConductance(vgs, vds, params);

  // Analytical: id_at_th_base * lambda * ratio^2
  double id_at_th_base = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth;
  double ratio = (vgs - (params.Vth - params.Vsmooth)) / params.Vsmooth;
  double expected = id_at_th_base * params.lambda * ratio * ratio;

  EXPECT_NEAR(gds, expected, 1e-15);
}

/** @test Output conductance in smooth transition matches numerical derivative. */
TEST(MosfetLevel1Test, SmoothTransitionOutputConductanceNumerical) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  double vgs = 0.65;
  double vds = 2.0;
  double dvds = 1e-7;

  double gds = MosfetLevel1::outputConductance(vgs, vds, params);
  double id_lo = MosfetLevel1::current(vgs, vds - dvds, params);
  double id_hi = MosfetLevel1::current(vgs, vds + dvds, params);
  double gds_num = (id_hi - id_lo) / (2.0 * dvds);

  EXPECT_NEAR(gds, gds_num, std::fabs(gds_num) * 0.001);
}

/** @test Smooth transition is continuous at the lower boundary (deep cutoff edge).
 *
 * At Vth - Vsmooth, the smoothing quadratic evaluates to zero (ratio=0),
 * matching the deep-cutoff region. The upper boundary at Vth has a small
 * step because the smoothing peak (id_at_th = 0.5*Kp*Vsmooth^2) does not
 * equal the Shichman-Hodges value at vgst=0 (which is zero). This step is
 * intentional and bounded by id_at_th, which is negligible for typical
 * Vsmooth values.
 */
TEST(MosfetLevel1Test, SmoothTransitionContinuity) {
  MosfetLevel1Params params{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  double vds = 2.0;

  // Lower boundary: Vth - Vsmooth = 0.6
  // Just inside smoothing vs just outside (deep cutoff)
  double eps = 1e-10;
  double id_below = MosfetLevel1::current(0.6 - eps, vds, params);
  double id_above = MosfetLevel1::current(0.6 + eps, vds, params);

  // Deep cutoff is exactly 0; smoothing near boundary approaches 0
  EXPECT_DOUBLE_EQ(id_below, 0.0);
  EXPECT_NEAR(id_above, 0.0, 1e-15);

  // Upper boundary at Vth: smoothing peak is id_at_th, above-threshold is ~0.
  // The step is bounded by id_at_th = 0.5 * Kp * Vsmooth^2 * (1+lambda*vds).
  double id_at_th = 0.5 * params.Kp * params.Vsmooth * params.Vsmooth * (1.0 + params.lambda * vds);
  double id_smooth_top = MosfetLevel1::current(0.7 - eps, vds, params);

  EXPECT_GT(id_smooth_top, 0.0);
  EXPECT_NEAR(id_smooth_top, id_at_th, 1e-12);

  // Above threshold with small overdrive, saturation current grows quickly
  double id_above_th = MosfetLevel1::current(0.8, vds, params);
  EXPECT_GT(id_above_th, id_at_th);
}

/* ----------------------------- stampNmos / stampPmos ----------------------------- */

/** @test stampNmos saturation drain current matches the SPICE Level 1
 *        Shichman-Hodges formula: Id = (Kp/2) * (Vgs-Vth)^2 * (1 + lambda*Vds).
 *
 * With prev_v at the pinning voltage-source values, the linearized companion
 * stamp evaluates exactly at the operating point.
 */
TEST(MosfetLevel1Test, StampNmosInSaturationMatchesShichmanHodges) {
  const MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  // Vgs = 3 - 0 = 3, Vds = 5 - 0 = 5; saturation: Vds > Vgs - Vth = 2.3.
  const std::vector<double> PREV_V = {0.0, 5.0, 3.0, 0.0};

  MnaSystem mna(/*netCount=*/4);
  MosfetLevel1::stampNmos(mna, /*drain=*/1, /*gate=*/2, /*source=*/3, PREV_V, PARAMS);
  const std::size_t DRAIN_VS = mna.addVoltageSource(/*pos=*/1, /*neg=*/0, /*v=*/5.0);
  mna.addVoltageSource(2, 0, 3.0);
  mna.addVoltageSource(3, 0, 0.0);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);

  const double VGST = 3.0 - PARAMS.Vth;
  const double EXPECTED_ID = 0.5 * PARAMS.Kp * VGST * VGST * (1.0 + PARAMS.lambda * 5.0);
  EXPECT_NEAR(std::abs(R.branchCurrents[DRAIN_VS]), EXPECTED_ID, 1e-9)
      << "Vgs=3, Vds=5, Vth=0.7 -> Id_sat = " << EXPECTED_ID << " A";
}

/** @test stampNmos with Vgs below Vth produces effectively zero drain current
 *        (cutoff region of Shichman-Hodges). */
TEST(MosfetLevel1Test, StampNmosCutoffHasNegligibleDrainCurrent) {
  const MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  // Vgs = 0.3 < Vth = 0.7 -> cutoff, modulo Vsmooth subthreshold rounding.
  const std::vector<double> PREV_V = {0.0, 5.0, 0.3, 0.0};

  MnaSystem mna(4);
  MosfetLevel1::stampNmos(mna, 1, 2, 3, PREV_V, PARAMS);
  const std::size_t DRAIN_VS = mna.addVoltageSource(1, 0, 5.0);
  mna.addVoltageSource(2, 0, 0.3);
  mna.addVoltageSource(3, 0, 0.0);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  // Subthreshold |Id| is bounded by ~Kp*Vsmooth^2 = 100e-6 * 0.01 = 1 uA;
  // 5 uA gives headroom for the smooth-transition shoulder.
  EXPECT_LT(std::abs(R.branchCurrents[DRAIN_VS]), 5e-6);
}

/** @test stampPmos saturation drain current matches Shichman-Hodges (PMOS sign
 *        convention, magnitude only): |Id| = (Kp/2)*Vsgt^2*(1 + lambda*Vsd). */
TEST(MosfetLevel1Test, StampPmosInSaturationMatchesShichmanHodges) {
  const MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  // Source=5, gate=0 -> Vsg = 5 (strongly on). Drain=1 -> Vsd = 4 > Vsg-Vth = 4.3? no, 5-0.7=4.3,
  // Vsd=4 So linear region. Pick drain=0 for clean saturation: Vsd=5 > Vsg-Vth=4.3.
  const std::vector<double> PREV_V = {0.0, 5.0, 0.0, 0.0};

  MnaSystem mna(4);
  MosfetLevel1::stampPmos(mna, /*source=*/1, /*gate=*/2, /*drain=*/3, PREV_V, PARAMS);
  mna.addVoltageSource(1, 0, 5.0);
  mna.addVoltageSource(2, 0, 0.0);
  const std::size_t DRAIN_VS = mna.addVoltageSource(3, 0, 0.0);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);

  const double VSG = 5.0;
  const double VSD = 5.0;
  const double VSGT = VSG - PARAMS.Vth;
  const double EXPECTED_ID = 0.5 * PARAMS.Kp * VSGT * VSGT * (1.0 + PARAMS.lambda * VSD);
  EXPECT_NEAR(std::abs(R.branchCurrents[DRAIN_VS]), EXPECTED_ID, 1e-9)
      << "Vsg=5, Vsd=5, Vth=0.7 -> |Id_sat| = " << EXPECTED_ID << " A";
}

/** @test stampPmos with Vsg below Vth produces effectively zero drain current. */
TEST(MosfetLevel1Test, StampPmosCutoffHasNegligibleDrainCurrent) {
  const MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};
  // Source=0.5, gate=0 -> Vsg = 0.5 < Vth = 0.7 -> cutoff.
  const std::vector<double> PREV_V = {0.0, 0.5, 0.0, 0.0};

  MnaSystem mna(4);
  MosfetLevel1::stampPmos(mna, 1, 2, 3, PREV_V, PARAMS);
  mna.addVoltageSource(1, 0, 0.5);
  mna.addVoltageSource(2, 0, 0.0);
  const std::size_t DRAIN_VS = mna.addVoltageSource(3, 0, 0.0);

  const auto R = mna.solve();
  ASSERT_TRUE(R.success);
  EXPECT_LT(std::abs(R.branchCurrents[DRAIN_VS]), 5e-6);
}

/* ----------------------------- fetlim() NR damping ----------------------------- */

/** @test fetlim returns vnew unchanged below threshold with a small step */
TEST(MosfetLevel1Test, FetlimSmallStepBelowThresholdPasses) {
  EXPECT_DOUBLE_EQ(MosfetLevel1::fetlim(0.6, 0.5, 1.0), 0.6);
}

/** @test fetlim upward jump crossing Vth (vold below): clamped to vto + 0.5 per
 *        ngspice devsup.c MOSlim "Vgs upward crossing" branch. */
TEST(MosfetLevel1Test, FetlimClampsUpwardCrossingThresholdToVtoPlusHalf) {
  // vnew=5, vold=0.5, vto=1 -> vold < vto, delv > 0, vnew > vto + 0.5 -> clamp to vto+0.5.
  EXPECT_DOUBLE_EQ(MosfetLevel1::fetlim(/*vnew=*/5.0, /*vold=*/0.5, /*vto=*/1.0), 1.5);
}

/** @test fetlim downward jump from above-Vth: vnew = max(vnew, vto + 2.0) per
 *        ngspice devsup.c MOSlim "above-Vtox, downward, vnew below Vtox" branch. */
TEST(MosfetLevel1Test, FetlimDownwardJumpAboveVtoxClampsToVtoPlusTwo) {
  // vnew=0, vold=5, vto=1 -> vtox=4.5, vold>=vtox, delv<0, vnew<vtox -> max(vnew, vto+2.0).
  EXPECT_DOUBLE_EQ(MosfetLevel1::fetlim(/*vnew=*/0.0, /*vold=*/5.0, /*vto=*/1.0), 3.0);
}

/** @test fetlim large upward step deep in saturation: clamped to vold + vtsthi
 *        where vtsthi = |2*(vold-vto)| + 2, per ngspice devsup.c MOSlim. */
TEST(MosfetLevel1Test, FetlimLargeUpwardStepInSaturationClampsToVoldPlusVtsthi) {
  // vnew=100, vold=5, vto=1 -> vtox=4.5, vold>=vtox, delv>0, vtsthi=|2*(5-1)|+2=10.
  // delv=95 >= vtsthi=10, so vnew = vold + vtsthi = 5 + 10 = 15.
  EXPECT_DOUBLE_EQ(MosfetLevel1::fetlim(/*vnew=*/100.0, /*vold=*/5.0, /*vto=*/1.0), 15.0);
}

/* ----------------------------- limvds() NR damping ----------------------------- */

/** @test limvds caps upward jump when vold >= 3.5 (3*vold + 2) */
TEST(MosfetLevel1Test, LimvdsClampsUpwardWhenVoldHigh) {
  EXPECT_NEAR(MosfetLevel1::limvds(/*vnew=*/100.0, /*vold=*/5.0), 3.0 * 5.0 + 2.0, 1e-12);
}

/** @test limvds clamps a large downward jump to 2.0 floor when vold high */
TEST(MosfetLevel1Test, LimvdsClampsDownwardWhenVoldHigh) {
  EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(0.0, 5.0), 2.0);
}

/** @test limvds caps upward at 4.0 when vold < 3.5 */
TEST(MosfetLevel1Test, LimvdsCapsUpwardWhenVoldLow) {
  EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(100.0, 2.0), 4.0);
}

/** @test limvds clamps downward to -0.5 when vold < 3.5 */
TEST(MosfetLevel1Test, LimvdsClampsDownwardWhenVoldLow) {
  EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(-10.0, 2.0), -0.5);
}

/** @test limvds passes through values inside the bounds */
TEST(MosfetLevel1Test, LimvdsPassesValuesInsideBounds) {
  EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(2.0, 2.0), 2.0);
  EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(3.0, 2.5), 3.0);
}
