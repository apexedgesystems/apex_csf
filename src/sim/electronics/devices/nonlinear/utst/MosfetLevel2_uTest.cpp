/**
 * @file MosfetLevel2_uTest.cpp
 * @brief Unit tests for MosfetLevel2.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from the model equations, which each table restates.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel2.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel2;
using sim::electronics::devices::nonlinear::MosfetLevel2Params;
using sim::electronics::devices::nonlinear::NetID;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

namespace {

/// Relative tolerance on a value against its reference.
constexpr double VALUE_TOL = 1e-12;

/// Relative tolerance on a conductance: the model differentiates numerically.
constexpr double CONDUCTANCE_TOL = 1e-4;

/// Every parameter moved off its default.
constexpr MosfetLevel2Params OTHER{.Kp = 200e-6,
                                   .Vth0 = 0.5,
                                   .lambda = 0.01,
                                   .W = 20e-6,
                                   .L = 0.5e-6,
                                   .theta = 0.2,
                                   .E_crit = 2e6,
                                   .gamma = 0.3,
                                   .phi = 0.7,
                                   .n_sub = 1.2,
                                   .Vt = 0.025};

/// Turn-on voltage: one subthreshold slope voltage above threshold.
double turnOnVoltage(double vbs, const MosfetLevel2Params& params) {
  return MosfetLevel2::thresholdVoltage(vbs, params) + params.n_sub * params.Vt;
}

} // namespace

/* ----------------------------- Threshold, Mobility, Saturation Voltage
 * ----------------------------- */

/** @test Threshold rises with reverse body bias as gamma*(sqrt(phi + Vsb) - sqrt(phi)) */
TEST(MosfetLevel2Test, ThresholdVoltageFollowsBodyEffect) {
  const MosfetLevel2Params PARAMS;

  EXPECT_DOUBLE_EQ(MosfetLevel2::thresholdVoltage(0.0, PARAMS), 0.7);
  EXPECT_NEAR(MosfetLevel2::thresholdVoltage(-1.0, PARAMS), 0.9451571974129341, VALUE_TOL);

  // A forward-biased bulk leaves the threshold at its zero-bias value.
  EXPECT_DOUBLE_EQ(MosfetLevel2::thresholdVoltage(0.5, PARAMS), 0.7);
}

/** @test Mobility factor is 1/(1 + theta*Vgst) above threshold and 1 below */
TEST(MosfetLevel2Test, MobilityFactorMatchesReference) {
  const MosfetLevel2Params PARAMS;

  EXPECT_NEAR(MosfetLevel2::mobilityFactor(2.0, 0.7, PARAMS), 0.8849557522123894, VALUE_TOL);
  EXPECT_DOUBLE_EQ(MosfetLevel2::mobilityFactor(0.0, 0.7, PARAMS), 1.0);
}

/** @test Saturation voltage is Vgst/(1 + Vgst/(E_crit*L)) above threshold and 0 below */
TEST(MosfetLevel2Test, DrainSaturationVoltageMatchesReference) {
  const MosfetLevel2Params PARAMS;

  EXPECT_NEAR(MosfetLevel2::drainSaturationVoltage(2.0, 0.7, PARAMS), 0.5652173913043478,
              VALUE_TOL);
  EXPECT_DOUBLE_EQ(MosfetLevel2::drainSaturationVoltage(0.0, 0.7, PARAMS), 0.0);
}

/* ----------------------------- Drain Current ----------------------------- */

/** @test Drain current matches the reference in every region */
TEST(MosfetLevel2Test, CurrentMatchesReference) {
  struct Row {
    double vgs;
    double vds;
    double vbs;
    double id;
  };

  // With Vgst = Vgs - Vth, beta = Kp*(W/L)/(1 + theta*Vgst),
  // Vdsat = Vgst/(1 + Vgst/(E_crit*L)) and Von = Vth + n_sub*Vt:
  //   Vgs >= Von: beta*(Vgst*V - V^2/2)*(1 + lambda*Vds), V = min(Vds, Vdsat);
  //   Vgs <  Von: that current at Vgs = Von, times exp((Vgs - Von)/(n_sub*Vt)).
  const MosfetLevel2Params PARAMS;
  const std::array<Row, 11> ROWS = {{
      Row{2.0, 0.2, 0.0, 0.00021323893805309734},  // linear region
      Row{2.0, 3.0, 0.0, 0.0005394248624052729},   // saturation
      Row{2.0, 3.0, -1.0, 0.00039287831843409803}, // saturation, reverse body bias
      Row{2.0, 3.0, 0.5, 0.0005394248624052729},   // forward body bias changes nothing
      Row{0.75, 1.0, 0.0, 1.2657799438183234e-06}, // just above turn-on
      Row{0.7, 1.0, 0.0, 2.8385864135326064e-07},  // at threshold
      Row{0.5, 1.0, 0.0, 1.6824839520397813e-09},  // weak inversion
      Row{0.5, 5.0, 0.0, 1.8144434776899602e-09},  // same gate, higher drain: saturated
      Row{0.5, 0.01, 0.0, 7.386345903756864e-10},  // below the turn-on saturation voltage
      Row{0.0, 1.0, 0.0, 4.550645002827162e-15},   // deep weak inversion
      Row{0.9, 1.0, -1.0, 8.917489161195104e-08},  // weak inversion under body bias
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(MosfetLevel2::current(row.vgs, row.vds, row.vbs, PARAMS), row.id,
                VALUE_TOL * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
  }

  EXPECT_NEAR(MosfetLevel2::current(1.5, 2.0, -0.5, OTHER), 0.0022558467803422927,
              VALUE_TOL * 0.0022558467803422927);
  EXPECT_NEAR(MosfetLevel2::current(0.4, 2.0, -0.5, OTHER), 3.5983548885270198e-09,
              VALUE_TOL * 3.5983548885270198e-09);
}

/** @test gm, gds and gmb are the derivatives of the drain current */
TEST(MosfetLevel2Test, ConductancesMatchReference) {
  struct Row {
    double vgs;
    double vds;
    double vbs;
    double gm;
    double gds;
    double gmb;
  };

  // The bulk transconductance is compared under reverse body bias only: the
  // body effect switches off at Vbs = 0, so the slope there is one-sided.
  const MosfetLevel2Params PARAMS;
  const std::array<Row, 6> ROWS = {{
      Row{2.0, 0.2, 0.0, 0.0001588284125616728, 0.0009815929203539824, 0.0},
      Row{2.0, 3.0, 0.0, 0.0006127630715835526, 1.017782759255232e-05, 0.0},
      Row{2.0, 3.0, -1.0, 0.0005796528225288656, 7.412798461020718e-06, 0.00011456394820853612},
      Row{0.5, 1.0, 0.0, 4.314061415486618e-08, 3.298988141254473e-11, 0.0},
      Row{0.5, 0.01, 0.0, 1.8939348471171447e-08, 6.301595538755084e-08, 0.0},
      Row{0.9, 1.0, -1.0, 2.286535682357719e-06, 1.748527286508844e-09, 4.519162942186048e-07},
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(MosfetLevel2::transconductance(row.vgs, row.vds, row.vbs, PARAMS), row.gm,
                CONDUCTANCE_TOL * row.gm)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    EXPECT_NEAR(MosfetLevel2::outputConductance(row.vgs, row.vds, row.vbs, PARAMS), row.gds,
                CONDUCTANCE_TOL * row.gds)
        << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    if (row.vbs < 0.0) {
      EXPECT_NEAR(MosfetLevel2::bulkTransconductance(row.vgs, row.vds, row.vbs, PARAMS), row.gmb,
                  CONDUCTANCE_TOL * row.gmb)
          << "vgs = " << row.vgs << ", vds = " << row.vds << ", vbs = " << row.vbs;
    }
  }

  EXPECT_NEAR(MosfetLevel2::transconductance(1.5, 2.0, -0.5, OTHER), 0.0037495737764899266,
              CONDUCTANCE_TOL * 0.0037495737764899266);
  EXPECT_NEAR(MosfetLevel2::outputConductance(1.5, 2.0, -0.5, OTHER), 2.2116144905316597e-05,
              CONDUCTANCE_TOL * 2.2116144905316597e-05);
  EXPECT_NEAR(MosfetLevel2::bulkTransconductance(1.5, 2.0, -0.5, OTHER), 0.0005134315346033417,
              CONDUCTANCE_TOL * 0.0005134315346033417);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(MosfetLevel2Test, StampIsLinearizationAtOperatingPoint) {
  const MosfetLevel2Params PARAMS;

  struct Bias {
    double vgs;
    double vds;
    double vbs;
    const char* region;
  };
  struct Step {
    double gate;
    double drain;
    double source;
    double bulk;
  };

  // The source sits off ground so every stamped entry takes part.
  const double V_SOURCE = 0.8;
  const double DV = 0.05;

  for (const Bias& bias : {Bias{0.3, 1.0, 0.0, "weak inversion"}, Bias{2.0, 0.2, -0.5, "linear"},
                           Bias{2.0, 3.0, -0.5, "saturation"}}) {
    const double ID = MosfetLevel2::current(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GM = MosfetLevel2::transconductance(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GDS = MosfetLevel2::outputConductance(bias.vgs, bias.vds, bias.vbs, PARAMS);
    const double GMB = MosfetLevel2::bulkTransconductance(bias.vgs, bias.vds, bias.vbs, PARAMS);

    // Terminal order: drain, gate, source, bulk.
    const auto STAMP = [&](MnaSystem& mna) {
      MosfetLevel2::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), terminalNet(3),
                          bias.vgs, bias.vds, bias.vbs, PARAMS);
    };

    for (const Step& step :
         {Step{0.0, 0.0, 0.0, 0.0}, Step{DV, 0.0, 0.0, 0.0}, Step{0.0, DV, 0.0, 0.0},
          Step{0.0, 0.0, DV, 0.0}, Step{0.0, 0.0, 0.0, DV}}) {
      const auto PINNED =
          pinTerminals({V_SOURCE + bias.vds + step.drain, V_SOURCE + bias.vgs + step.gate,
                        V_SOURCE + step.source, V_SOURCE + bias.vbs + step.bulk},
                       STAMP);
      ASSERT_TRUE(PINNED.solved) << bias.region;

      const double D_VGS = step.gate - step.source;
      const double D_VDS = step.drain - step.source;
      const double D_VBS = step.bulk - step.source;
      const double I_DRAIN = ID + GM * D_VGS + GDS * D_VDS + GMB * D_VBS;
      expectCurrent(PINNED.into[0], I_DRAIN, "drain");
      expectCurrent(PINNED.into[1], 0.0, "gate");
      expectCurrent(PINNED.into[2], -I_DRAIN, "source");
      expectCurrent(PINNED.into[3], 0.0, "bulk");
    }
  }
}

/* ----------------------------- Region Boundaries ----------------------------- */

/** @test Drain current is continuous where the linear region meets saturation */
TEST(MosfetLevel2Test, ContinuousAtSaturationVoltage) {
  const MosfetLevel2Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VGS : {1.0, 1.5, 2.0, 3.0}) {
    for (const double VBS : {0.0, -0.5}) {
      const double VTH = MosfetLevel2::thresholdVoltage(VBS, PARAMS);
      const double vdsat = MosfetLevel2::drainSaturationVoltage(VGS, VTH, PARAMS);
      ASSERT_GT(vdsat, 0.0) << "vgs = " << VGS;

      const double BELOW = MosfetLevel2::current(VGS, vdsat - EPS, VBS, PARAMS);
      const double ABOVE = MosfetLevel2::current(VGS, vdsat + EPS, VBS, PARAMS);
      EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE) << "vgs = " << VGS << ", vbs = " << VBS;
    }
  }
}

/** @test Drain current is continuous in the gate voltage at threshold and at turn-on */
TEST(MosfetLevel2Test, ContinuousAtThresholdAndTurnOn) {
  const MosfetLevel2Params PARAMS;
  constexpr double EPS = 1e-9;

  for (const double VBS : {0.0, -0.5}) {
    for (const double VGS :
         {MosfetLevel2::thresholdVoltage(VBS, PARAMS), turnOnVoltage(VBS, PARAMS)}) {
      for (const double VDS : {0.01, 0.5, 5.0}) {
        const double BELOW = MosfetLevel2::current(VGS - EPS, VDS, VBS, PARAMS);
        const double ABOVE = MosfetLevel2::current(VGS + EPS, VDS, VBS, PARAMS);
        EXPECT_GT(ABOVE, 0.0) << "vgs = " << VGS << ", vds = " << VDS << ", vbs = " << VBS;
        EXPECT_NEAR(BELOW, ABOVE, 1e-6 * ABOVE)
            << "vgs = " << VGS << ", vds = " << VDS << ", vbs = " << VBS;
      }
    }
  }
}

/** @test Drain current never falls as the gate voltage rises through threshold */
TEST(MosfetLevel2Test, MonotonicInGateVoltage) {
  const MosfetLevel2Params PARAMS;

  for (const double VDS : {0.01, 0.5, 5.0}) {
    double previous = MosfetLevel2::current(PARAMS.Vth0 - 0.5, VDS, 0.0, PARAMS);
    for (int step = 1; step <= 1000; ++step) {
      const double VGS = PARAMS.Vth0 - 0.5 + 0.001 * step;
      const double ID = MosfetLevel2::current(VGS, VDS, 0.0, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Drain current never falls as the drain voltage rises, below threshold and above */
TEST(MosfetLevel2Test, MonotonicInDrainVoltage) {
  const MosfetLevel2Params PARAMS;

  for (const double VGS : {0.5, 0.7, 1.0, 1.5, 2.0, 3.0}) {
    double previous = MosfetLevel2::current(VGS, 0.0, 0.0, PARAMS);
    for (int step = 1; step <= 500; ++step) {
      const double VDS = 0.01 * step;
      const double ID = MosfetLevel2::current(VGS, VDS, 0.0, PARAMS);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Output conductance is finite and not negative through saturation, the boundary included */
TEST(MosfetLevel2Test, OutputConductanceNotNegativeThroughSaturation) {
  const MosfetLevel2Params PARAMS;

  for (const double VGS : {1.0, 2.0, 3.0}) {
    // On the boundary itself the conductance is a difference taken across it.
    const double VTH = MosfetLevel2::thresholdVoltage(0.0, PARAMS);
    const double vdsat = MosfetLevel2::drainSaturationVoltage(VGS, VTH, PARAMS);
    for (const double VDS : {vdsat - 5e-9, vdsat, vdsat + 5e-9}) {
      const double GDS = MosfetLevel2::outputConductance(VGS, VDS, 0.0, PARAMS);
      EXPECT_TRUE(std::isfinite(GDS)) << "vgs = " << VGS << ", at the boundary";
      EXPECT_GE(GDS, 0.0) << "vgs = " << VGS << ", at the boundary";
    }

    for (int step = 0; step <= 500; ++step) {
      const double VDS = 0.01 * step;
      const double GDS = MosfetLevel2::outputConductance(VGS, VDS, 0.0, PARAMS);
      ASSERT_TRUE(std::isfinite(GDS)) << "vgs = " << VGS << ", vds = " << VDS;
      ASSERT_GE(GDS, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
    }
  }
}

/** @test Transconductance is finite and not negative at threshold and at turn-on */
TEST(MosfetLevel2Test, TransconductanceNotNegativeThroughTurnOn) {
  const MosfetLevel2Params PARAMS;

  for (const double BOUNDARY : {PARAMS.Vth0, turnOnVoltage(0.0, PARAMS)}) {
    // On a boundary the transconductance is a difference taken across it.
    for (const double VGS : {BOUNDARY - 5e-9, BOUNDARY, BOUNDARY + 5e-9}) {
      for (const double VDS : {0.01, 5.0}) {
        const double GM = MosfetLevel2::transconductance(VGS, VDS, 0.0, PARAMS);
        EXPECT_TRUE(std::isfinite(GM)) << "vgs = " << VGS << ", vds = " << VDS;
        EXPECT_GE(GM, 0.0) << "vgs = " << VGS << ", vds = " << VDS;
      }
    }
  }
}

/* ----------------------------- Circuit ----------------------------- */

/** @test A resistor-loaded inverter solved through the stamp follows the reference transfer curve
 */
TEST(MosfetLevel2Test, ResistorLoadedInverterFollowsReferenceTransferCurve) {
  struct Point {
    double vin;
    double vout;
  };

  // Supply -- RD -- drain (the output); the gate is the input; source and
  // bulk grounded. Reference: bisection on (VDD - Vout)/RD = Id(Vin, Vout).
  const MosfetLevel2Params PARAMS;
  constexpr double VDD = 5.0;
  constexpr double RD = 10e3;
  const std::array<Point, 7> CURVE = {{
      Point{0.0, 4.999999999950925},   // off
      Point{0.5, 4.9999818555712086},  // weak inversion
      Point{0.72, 4.99488825257964},   // between threshold and turn-on
      Point{1.0, 4.548743661329839},   // saturation
      Point{1.5, 2.503273679615887},   // saturation
      Point{2.0, 0.4769298324943575},  // linear region
      Point{5.0, 0.16342578306930655}, // linear region
  }};

  const NetID GROUND = 0;
  const NetID SUPPLY = 1;
  const NetID OUTPUT = 2;
  const NetID INPUT = 3;
  for (const Point& point : CURVE) {
    double vOut = VDD;
    bool converged = false;
    for (int iter = 0; iter < 100 && !converged; ++iter) {
      MnaSystem mna(4);
      mna.addVoltageSource(SUPPLY, GROUND, VDD);
      mna.addVoltageSource(INPUT, GROUND, point.vin);
      mna.addConductance(SUPPLY, OUTPUT, 1.0 / RD);
      MosfetLevel2::stamp(mna, OUTPUT, INPUT, GROUND, GROUND, point.vin, vOut, 0.0, PARAMS);

      const auto RESULT = mna.solve();
      ASSERT_TRUE(RESULT.success) << "vin = " << point.vin << ", iteration " << iter;
      const double V_OUT = RESULT.nodeVoltages[OUTPUT];
      converged = std::abs(V_OUT - vOut) < 1e-12;
      vOut = V_OUT;
    }
    ASSERT_TRUE(converged) << "vin = " << point.vin;
    EXPECT_NEAR(vOut, point.vout, 1e-9) << "vin = " << point.vin;
  }
}
