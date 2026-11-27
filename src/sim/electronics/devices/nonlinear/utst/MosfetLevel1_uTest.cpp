/**
 * @file MosfetLevel1_uTest.cpp
 * @brief Unit tests for MosfetLevel1 model.
 *
 * Expected currents and conductances are reference values computed outside
 * the library from the model equations, which each table restates.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/utst/StampProbe.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;
using sim::electronics::devices::nonlinear::NetID;

using sim::electronics::devices::nonlinear::test::expectCurrent;
using sim::electronics::devices::nonlinear::test::pinTerminals;
using sim::electronics::devices::nonlinear::test::terminalNet;

namespace {

/// Relative tolerance on a value against its reference.
constexpr double VALUE_TOL = 1e-12;

/// The device most cases use: channel-length modulation on.
constexpr MosfetLevel1Params DEVICE{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02};

/// Previous-iteration voltages as stampNmos and stampPmos read them: indexed
/// by net, ground first, then the three terminal nets of the stamp probe.
std::vector<double> previousVoltages(double first, double second, double third) {
  return {0.0, first, second, third};
}

} // namespace

/* ----------------------------- I-V Characteristic ----------------------------- */

/** @test Current, gm and gds match the reference in cutoff, the linear region and saturation */
TEST(MosfetLevel1Test, CurrentAndConductancesMatchReference) {
  struct Row {
    MosfetLevel1Params params;
    double vgs;
    double vds;
    double id;
    double gm;
    double gds;
  };

  // With Vgst = Vgs - Vth: zero for Vgs <= Vth;
  // Kp*(Vgst*Vds - Vds^2/2)*(1 + lambda*Vds) for Vds < Vgst;
  // (Kp/2)*Vgst^2*(1 + lambda*Vds) from there on.
  const std::array<Row, 8> ROWS = {{
      Row{DEVICE, 1.5, 2.0, 3.328e-05, 8.32e-05, 6.4e-07},         // saturation
      Row{DEVICE, 1.5, 0.3, 1.9617e-05, 3.018e-05, 5.069e-05},     // linear region
      Row{DEVICE, 3.0, 1.0, 0.0001836, 0.000102, 0.0001362},       // linear region
      Row{DEVICE, 0.65, 2.0, 0.0, 0.0, 0.0},                       // just below threshold
      Row{DEVICE, 0.0, 2.0, 0.0, 0.0, 0.0},                        // far below threshold
      Row{MosfetLevel1Params{}, 2.0, 5.0, 8.45e-05, 0.00013, 0.0}, // the defaults: no modulation
      Row{MosfetLevel1Params{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03}, 3.0, 1.0, 0.0068495, 0.00515,
          0.004474},
      Row{MosfetLevel1Params{.Kp = 100e-6, .Vth = -1.0}, 0.0, 3.0, 5e-05, 0.0001, 0.0}, // depletion
  }};
  for (const Row& row : ROWS) {
    EXPECT_NEAR(MosfetLevel1::current(row.vgs, row.vds, row.params), row.id, VALUE_TOL * row.id)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
    EXPECT_NEAR(MosfetLevel1::transconductance(row.vgs, row.vds, row.params), row.gm,
                VALUE_TOL * row.gm)
        << "vgs = " << row.vgs << ", vds = " << row.vds;
    EXPECT_NEAR(MosfetLevel1::outputConductance(row.vgs, row.vds, row.params), row.gds,
                VALUE_TOL * row.gds)
        << "vgs = " << row.vgs << ", vds = " << row.vds;

    // The combined evaluation returns the same three values.
    const auto COMBINED = MosfetLevel1::stampValues(row.vgs, row.vds, row.params);
    EXPECT_NEAR(COMBINED.id, row.id, VALUE_TOL * row.id) << "vgs = " << row.vgs;
    EXPECT_NEAR(COMBINED.gm, row.gm, VALUE_TOL * row.gm) << "vgs = " << row.vgs;
    EXPECT_NEAR(COMBINED.gds, row.gds, VALUE_TOL * row.gds) << "vgs = " << row.vgs;
  }
}

/* ----------------------------- Region Boundaries ----------------------------- */

/** @test Current, gm and gds are continuous at threshold and at the saturation voltage */
TEST(MosfetLevel1Test, ContinuousAtRegionBoundaries) {
  constexpr double EPS = 1e-9;

  // At threshold everything leaves from zero.
  for (const double VDS : {0.01, 5.0}) {
    const auto BELOW = MosfetLevel1::stampValues(DEVICE.Vth - EPS, VDS, DEVICE);
    const auto ABOVE = MosfetLevel1::stampValues(DEVICE.Vth + EPS, VDS, DEVICE);
    EXPECT_EQ(BELOW.id, 0.0) << "vds = " << VDS;
    EXPECT_EQ(BELOW.gm, 0.0) << "vds = " << VDS;
    EXPECT_EQ(BELOW.gds, 0.0) << "vds = " << VDS;
    EXPECT_LT(ABOVE.id, 1e-20) << "vds = " << VDS;
    EXPECT_LT(ABOVE.gm, 1e-12) << "vds = " << VDS;
    EXPECT_LT(ABOVE.gds, 1e-12) << "vds = " << VDS;
  }

  // Where the linear region meets saturation the three values agree.
  for (const double VGS : {1.0, 1.5, 2.0, 3.0}) {
    const double VDSAT = VGS - DEVICE.Vth;
    const auto BELOW = MosfetLevel1::stampValues(VGS, VDSAT - EPS, DEVICE);
    const auto ABOVE = MosfetLevel1::stampValues(VGS, VDSAT + EPS, DEVICE);
    EXPECT_NEAR(BELOW.id, ABOVE.id, 1e-6 * ABOVE.id) << "vgs = " << VGS;
    EXPECT_NEAR(BELOW.gm, ABOVE.gm, 1e-6 * ABOVE.gm) << "vgs = " << VGS;
    EXPECT_NEAR(BELOW.gds, ABOVE.gds, 1e-9 + 1e-6 * ABOVE.gds) << "vgs = " << VGS;
  }
}

/** @test Drain current never falls as the gate voltage rises through threshold */
TEST(MosfetLevel1Test, MonotonicInGateVoltage) {
  for (const double VDS : {0.01, 0.5, 5.0}) {
    double previous = MosfetLevel1::current(DEVICE.Vth - 0.5, VDS, DEVICE);
    for (int step = 1; step <= 1000; ++step) {
      const double VGS = DEVICE.Vth - 0.5 + 0.001 * step;
      const double ID = MosfetLevel1::current(VGS, VDS, DEVICE);
      ASSERT_GE(ID, previous) << "vgs = " << VGS << ", vds = " << VDS;
      previous = ID;
    }
  }
}

/** @test Region code is 0 at or below threshold, 1 for Vds < Vgst, 2 from Vds = Vgst on */
TEST(MosfetLevel1Test, RegionClassifiesBias) {
  EXPECT_EQ(MosfetLevel1::region(0.0, 2.0, DEVICE), 0);
  EXPECT_EQ(MosfetLevel1::region(0.65, 2.0, DEVICE), 0);
  EXPECT_EQ(MosfetLevel1::region(0.7, 2.0, DEVICE), 0); // at threshold

  // Vgs = 1.5 V puts the boundary at Vds = 0.8 V.
  EXPECT_EQ(MosfetLevel1::region(1.5, 0.3, DEVICE), 1);
  EXPECT_EQ(MosfetLevel1::region(1.5, 0.79, DEVICE), 1);
  EXPECT_EQ(MosfetLevel1::region(1.5, 0.8, DEVICE), 2); // on the boundary
  EXPECT_EQ(MosfetLevel1::region(1.5, 2.0, DEVICE), 2);
}

/* ----------------------------- Stamping ----------------------------- */

/** @test Stamp is the model's linearization: exact at the operating point, first order around it */
TEST(MosfetLevel1Test, StampIsLinearizationAtOperatingPoint) {
  struct Bias {
    double vgs;
    double vds;
    const char* region;
  };
  struct Step {
    double gate;
    double drain;
    double source;
  };

  // The source sits off ground so every stamped entry takes part.
  const double V_SOURCE = 0.8;
  const double DV = 0.05;

  for (const Bias& bias :
       {Bias{0.3, 2.0, "cutoff"}, Bias{1.5, 0.3, "linear"}, Bias{1.5, 2.0, "saturation"}}) {
    const auto MODEL = MosfetLevel1::stampValues(bias.vgs, bias.vds, DEVICE);

    // Terminal order: drain, gate, source.
    const auto STAMP = [&](MnaSystem& mna) {
      MosfetLevel1::stamp(mna, terminalNet(0), terminalNet(1), terminalNet(2), bias.vgs, bias.vds,
                          DEVICE);
    };

    for (const Step& step :
         {Step{0.0, 0.0, 0.0}, Step{DV, 0.0, 0.0}, Step{0.0, DV, 0.0}, Step{0.0, 0.0, DV}}) {
      const auto PINNED = pinTerminals({V_SOURCE + bias.vds + step.drain,
                                        V_SOURCE + bias.vgs + step.gate, V_SOURCE + step.source},
                                       STAMP);
      ASSERT_TRUE(PINNED.solved) << bias.region;

      const double D_VGS = step.gate - step.source;
      const double D_VDS = step.drain - step.source;
      const double I_DRAIN = MODEL.id + MODEL.gm * D_VGS + MODEL.gds * D_VDS;
      expectCurrent(PINNED.into[0], I_DRAIN, "drain");
      expectCurrent(PINNED.into[1], 0.0, "gate");
      expectCurrent(PINNED.into[2], -I_DRAIN, "source");
    }
  }
}

/** @test stampNmos carries the model current drain to source, and source to drain when reversed */
TEST(MosfetLevel1Test, StampNmosCarriesModelCurrentInBothDirections) {
  // Terminal order: drain, gate, source. No minimum conductance, so the
  // stamped device is the model alone.
  constexpr double GMIN = 0.0;

  // Forward: Vgs = 2 V, Vds = 3 V, saturation. (Kp/2)*1.3^2*(1 + 0.02*3).
  {
    const std::vector<double> PREV = previousVoltages(3.0, 2.0, 0.0);
    const auto PINNED = pinTerminals({3.0, 2.0, 0.0}, [&](MnaSystem& mna) {
      MosfetLevel1::stampNmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), PREV, DEVICE,
                              GMIN);
    });
    ASSERT_TRUE(PINNED.solved);
    expectCurrent(PINNED.into[0], 8.957e-05, "drain, forward");
    expectCurrent(PINNED.into[1], 0.0, "gate, forward");
    expectCurrent(PINNED.into[2], -8.957e-05, "source, forward");
  }

  // Reversed: the drain net sits below the source net, so the source net
  // acts as the drain. Gate 2 V above the lower net, 1 V across the channel:
  // linear region, Kp*(1.3*1 - 0.5)*(1 + 0.02*1).
  {
    const std::vector<double> PREV = previousVoltages(0.0, 2.0, 1.0);
    const auto STAMP = [&](MnaSystem& mna) {
      MosfetLevel1::stampNmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), PREV, DEVICE,
                              GMIN);
    };
    const auto PINNED = pinTerminals({0.0, 2.0, 1.0}, STAMP);
    ASSERT_TRUE(PINNED.solved);
    expectCurrent(PINNED.into[0], -8.16e-05, "drain, reversed");
    expectCurrent(PINNED.into[1], 0.0, "gate, reversed");
    expectCurrent(PINNED.into[2], 8.16e-05, "source, reversed");

    // First order around it: the gate is referred to the lower (drain) net.
    const auto MODEL = MosfetLevel1::stampValues(2.0, 1.0, DEVICE);
    const double DV = 0.05;
    const auto GATE_STEP = pinTerminals({0.0, 2.0 + DV, 1.0}, STAMP);
    ASSERT_TRUE(GATE_STEP.solved);
    expectCurrent(GATE_STEP.into[2], MODEL.id + MODEL.gm * DV, "source, reversed, gate step");
    const auto CHANNEL_STEP = pinTerminals({0.0, 2.0, 1.0 + DV}, STAMP);
    ASSERT_TRUE(CHANNEL_STEP.solved);
    expectCurrent(CHANNEL_STEP.into[2], MODEL.id + MODEL.gds * DV, "source, reversed, drain step");
  }
}

/** @test stampPmos carries the model current source to drain, and drain to source when reversed */
TEST(MosfetLevel1Test, StampPmosCarriesModelCurrentInBothDirections) {
  // Terminal order: source, gate, drain. No minimum conductance.
  constexpr double GMIN = 0.0;

  // Forward: Vsg = 2 V, Vsd = 4 V, saturation. (Kp/2)*1.3^2*(1 + 0.02*4).
  {
    const std::vector<double> PREV = previousVoltages(5.0, 3.0, 1.0);
    const auto PINNED = pinTerminals({5.0, 3.0, 1.0}, [&](MnaSystem& mna) {
      MosfetLevel1::stampPmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), PREV, DEVICE,
                              GMIN);
    });
    ASSERT_TRUE(PINNED.solved);
    expectCurrent(PINNED.into[0], 9.126e-05, "source, forward");
    expectCurrent(PINNED.into[1], 0.0, "gate, forward");
    expectCurrent(PINNED.into[2], -9.126e-05, "drain, forward");
  }

  // Reversed: the source net sits below the drain net, so the drain net
  // acts as the source. 3 V from it to the gate, 2 V across the channel:
  // linear region, Kp*(2.3*2 - 2)*(1 + 0.02*2).
  {
    const std::vector<double> PREV = previousVoltages(1.0, 0.0, 3.0);
    const auto STAMP = [&](MnaSystem& mna) {
      MosfetLevel1::stampPmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), PREV, DEVICE,
                              GMIN);
    };
    const auto PINNED = pinTerminals({1.0, 0.0, 3.0}, STAMP);
    ASSERT_TRUE(PINNED.solved);
    expectCurrent(PINNED.into[0], -0.0002704, "source, reversed");
    expectCurrent(PINNED.into[1], 0.0, "gate, reversed");
    expectCurrent(PINNED.into[2], 0.0002704, "drain, reversed");

    // First order around it: the gate is referred to the upper (drain) net.
    const auto MODEL = MosfetLevel1::stampValues(3.0, 2.0, DEVICE);
    const double DV = 0.05;
    const auto GATE_STEP = pinTerminals({1.0, 0.0 - DV, 3.0}, STAMP);
    ASSERT_TRUE(GATE_STEP.solved);
    expectCurrent(GATE_STEP.into[2], MODEL.id + MODEL.gm * DV, "drain, reversed, gate step");
  }
}

/** @test The minimum conductance is stamped beside the device and does not change its current */
TEST(MosfetLevel1Test, MinimumConductanceAddsToACutOffDevice) {
  // Terminal order: drain, gate, source. Gate at 0 V: the device is cut off
  // and carries nothing, so what flows is the minimum conductance times Vds.
  constexpr double GMIN = 1e-9;
  const std::vector<double> PREV = previousVoltages(3.0, 0.0, 0.0);
  const auto PINNED = pinTerminals({3.0, 0.0, 0.0}, [&](MnaSystem& mna) {
    MosfetLevel1::stampNmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), PREV, DEVICE,
                            GMIN);
  });
  ASSERT_TRUE(PINNED.solved);
  expectCurrent(PINNED.into[0], 3e-9, "drain");
  expectCurrent(PINNED.into[2], -3e-9, "source");
}

/** @test A net beyond the previous-voltage vector is read as 0 V */
TEST(MosfetLevel1Test, StampNmosReadsMissingPreviousVoltagesAsZero) {
  // With no previous voltages every terminal reads 0 V: the linearization
  // point is a cut-off device, whatever the terminals are then held at.
  constexpr double GMIN = 1e-9;
  const std::vector<double> EMPTY;
  const auto PINNED = pinTerminals({3.0, 2.0, 0.0}, [&](MnaSystem& mna) {
    MosfetLevel1::stampNmos(mna, terminalNet(0), terminalNet(1), terminalNet(2), EMPTY, DEVICE,
                            GMIN);
  });
  ASSERT_TRUE(PINNED.solved);
  expectCurrent(PINNED.into[0], 3e-9, "drain");
  expectCurrent(PINNED.into[1], 0.0, "gate");
}

/* ----------------------------- Voltage Limiting ----------------------------- */

/** @test fetlim limits a gate-voltage step: one row for each branch a step can take */
TEST(MosfetLevel1Test, FetlimMatchesReference) {
  struct Row {
    double vold;
    double vnew;
    double limited;
    const char* branch;
  };

  // Threshold 0.7 V. The breakpoints are vto - 0.5 = 0.2, vto + 0.5 = 1.2,
  // vto + 2 = 2.7, vto + 3.5 = 4.2 and vto + 4 = 4.7; the step bounds are
  // |2*(vold - vto)| + 2 upward and |vold - vto| + 1 downward.
  constexpr double VTO = 0.7;
  const std::array<Row, 13> ROWS = {{
      Row{6.0, 5.5, 5.5, "well on, small step down: unchanged"},
      Row{6.0, 3.0, 3.0, "well on, down past vto + 3.5 but above vto + 2: unchanged"},
      Row{6.0, 1.0, 2.7, "well on, down below vto + 2: held at vto + 2"},
      Row{5.0, 6.0, 6.0, "well on, small step up: unchanged"},
      Row{5.0, 20.0, 15.6, "well on, large step up: bounded by 2*(vold - vto) + 2"},
      Row{2.0, 1.5, 1.5, "on, small step down: unchanged"},
      Row{2.0, -3.0, 0.2, "on, large step down: held at vto - 0.5"},
      Row{2.0, 3.0, 3.0, "on, small step up: unchanged"},
      Row{2.0, 10.0, 4.7, "on, large step up: held at vto + 4"},
      Row{0.0, -1.0, -1.0, "off, small step down: unchanged"},
      Row{0.0, -10.0, -3.4, "off, large step down: bounded by 2*(vto - vold) + 2"},
      Row{0.0, 1.0, 1.0, "off, step up to below vto + 0.5: unchanged"},
      Row{0.0, 5.0, 1.2, "off, step up past vto + 0.5: held at vto + 0.5"},
  }};
  for (const Row& row : ROWS) {
    EXPECT_DOUBLE_EQ(MosfetLevel1::fetlim(row.vnew, row.vold, VTO), row.limited) << row.branch;
  }
}

/** @test limvds limits a drain-voltage step: one row for each branch a step can take */
TEST(MosfetLevel1Test, LimvdsMatchesReference) {
  struct Row {
    double vold;
    double vnew;
    double limited;
    const char* branch;
  };

  const std::array<Row, 9> ROWS = {{
      Row{5.0, 6.0, 6.0, "high, small step up: unchanged"},
      Row{5.0, 100.0, 17.0, "high, large step up: bounded by 3*vold + 2"},
      Row{5.0, 4.0, 4.0, "high, step down staying above 3.5: unchanged"},
      Row{5.0, 3.0, 3.0, "high, step down below 3.5 but above 2: unchanged"},
      Row{5.0, 0.0, 2.0, "high, step down below 2: held at 2"},
      Row{1.0, 2.0, 2.0, "low, small step up: unchanged"},
      Row{1.0, 10.0, 4.0, "low, large step up: held at 4"},
      Row{1.0, 0.5, 0.5, "low, small step down: unchanged"},
      Row{1.0, -5.0, -0.5, "low, large step down: held at -0.5"},
  }};
  for (const Row& row : ROWS) {
    EXPECT_DOUBLE_EQ(MosfetLevel1::limvds(row.vnew, row.vold), row.limited) << row.branch;
  }
}

/* ----------------------------- Circuit ----------------------------- */

/** @test A resistor-loaded inverter solved through stampNmos follows the reference transfer curve
 */
TEST(MosfetLevel1Test, ResistorLoadedInverterFollowsReferenceTransferCurve) {
  struct Point {
    double vin;
    double vout;
  };

  // Supply -- RD -- drain (the output); the gate is the input; source
  // grounded. Reference: bisection on (VDD - Vout)/RD = Id(Vin, Vout).
  const MosfetLevel1Params PARAMS{.Kp = 1e-3, .Vth = 0.7, .lambda = 0.02};
  constexpr double VDD = 5.0;
  constexpr double RD = 10e3;
  const std::array<Point, 6> CURVE = {{
      Point{0.0, 5.0},                 // off
      Point{0.7, 5.0},                 // at threshold: still off
      Point{1.0, 4.509415262636273},   // saturation
      Point{1.5, 1.6917293233082706},  // saturation
      Point{2.0, 0.41634836260691266}, // linear region
      Point{5.0, 0.11488160083141971}, // linear region
  }};

  const NetID GROUND = 0;
  const NetID SUPPLY = 1;
  const NetID OUTPUT = 2;
  const NetID INPUT = 3;
  for (const Point& point : CURVE) {
    // Previous voltages by net; the drain step goes through the model's own
    // limiter, as a solver using this model would apply it.
    std::vector<double> prev = {0.0, VDD, VDD, point.vin};
    bool converged = false;
    for (int iter = 0; iter < 100 && !converged; ++iter) {
      MnaSystem mna(4);
      mna.addVoltageSource(SUPPLY, GROUND, VDD);
      mna.addVoltageSource(INPUT, GROUND, point.vin);
      mna.addConductance(SUPPLY, OUTPUT, 1.0 / RD);
      MosfetLevel1::stampNmos(mna, OUTPUT, INPUT, GROUND, prev, PARAMS, 0.0);

      const auto RESULT = mna.solve();
      ASSERT_TRUE(RESULT.success) << "vin = " << point.vin << ", iteration " << iter;
      const double V_OUT = MosfetLevel1::limvds(RESULT.nodeVoltages[OUTPUT], prev[OUTPUT]);
      converged = std::abs(V_OUT - prev[OUTPUT]) < 1e-12;
      prev[OUTPUT] = V_OUT;
    }
    ASSERT_TRUE(converged) << "vin = " << point.vin;
    EXPECT_NEAR(prev[OUTPUT], point.vout, 1e-9) << "vin = " << point.vin;
  }
}
