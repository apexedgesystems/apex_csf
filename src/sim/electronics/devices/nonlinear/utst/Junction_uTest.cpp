/**
 * @file Junction_uTest.cpp
 * @brief Unit tests for the junction-voltage helpers.
 *
 * Notes:
 *  - Reference operating points come from bisection on the circuit equation
 *    outside this library, not from a Newton iteration.
 */

#include "src/sim/electronics/devices/nonlinear/inc/Junction.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::Junction;
using sim::electronics::devices::nonlinear::MnaSystem;
using sim::electronics::devices::nonlinear::NetID;

/* ----------------------------- Constants ----------------------------- */

static constexpr double N_VT = 0.026;
static constexpr double IS = 1e-14;

/// n*Vt * ln(n*Vt / (sqrt(2) * Is)) for the values above.
static constexpr double V_CRIT_REFERENCE = 0.7342389332375763;

/**
 * Newton iterations for a resistor and a default diode in series across a
 * voltage source, starting from zero volts across the diode.
 *
 * @return Iterations until the diode voltage moves by less than 1e-9 V, or
 *         maxIter when it never does. The final diode voltage is written to
 *         vDiodeOut.
 */
static int solveResistorDiode(double vSource, double rOhms, bool limit, int maxIter,
                              double& vDiodeOut) {
  const DiodeShockleyParams PARAMS{.Is = IS, .n = 1.0, .Vt = N_VT};
  const double V_CRIT = Junction::criticalVoltage(IS, N_VT);
  const NetID SOURCE = 1;
  const NetID ANODE = 2;

  double vDiode = 0.0;
  for (int iter = 0; iter < maxIter; ++iter) {
    MnaSystem mna(3);
    mna.addVoltageSource(SOURCE, 0, vSource);
    mna.addConductance(SOURCE, ANODE, 1.0 / rOhms);
    DiodeShockley::stamp(mna, ANODE, 0, vDiode, PARAMS);
    const auto RESULT = mna.solve();
    if (!RESULT.success) {
      break;
    }

    const double V_SOLVED = RESULT.nodeVoltages[ANODE];
    if (std::abs(V_SOLVED - vDiode) < 1e-9) {
      vDiodeOut = V_SOLVED;
      return iter;
    }
    vDiode = limit ? Junction::limitVoltage(V_SOLVED, vDiode, N_VT, V_CRIT) : V_SOLVED;
  }
  vDiodeOut = vDiode;
  return maxIter;
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Limited exponential is the exponential up to its cap and its tangent line above */
TEST(JunctionTest, LimitedExpIsExponentialThenTangent) {
  constexpr double CAP = Junction::EXP_LINEAR_FROM;

  // Below the cap: the exponential itself.
  for (const double X : {-50.0, 0.0, 1.0, 26.9, CAP}) {
    EXPECT_DOUBLE_EQ(Junction::limitedExp(X), std::exp(X)) << "x = " << X;
    EXPECT_DOUBLE_EQ(Junction::limitedExpSlope(X), std::exp(X)) << "x = " << X;
  }

  // Above: value on the tangent at the cap, slope constant.
  const double AT_CAP = std::exp(CAP);
  EXPECT_DOUBLE_EQ(Junction::limitedExp(CAP + 10.0), AT_CAP * 11.0);
  EXPECT_DOUBLE_EQ(Junction::limitedExp(1e6), AT_CAP * (1.0 + 1e6 - CAP));
  EXPECT_DOUBLE_EQ(Junction::limitedExpSlope(CAP + 10.0), AT_CAP);
  EXPECT_DOUBLE_EQ(Junction::limitedExpSlope(1e6), AT_CAP);
}

/** @test Junction current and conductance are the Shockley values, exactly, below the cap */
TEST(JunctionTest, CurrentAndConductanceMatchShockleyReference) {
  // Is * (exp(0.7 / 0.026) - 1) and (Is / 0.026) * exp(0.7 / 0.026), evaluated
  // outside this library.
  EXPECT_NEAR(Junction::current(0.7, IS, N_VT), 0.00492655962756388, 1e-17);
  EXPECT_NEAR(Junction::conductance(0.7, IS, N_VT), 0.18948306259899542, 1e-15);
  EXPECT_DOUBLE_EQ(Junction::current(0.0, IS, N_VT), 0.0);
  EXPECT_NEAR(Junction::current(-5.0, IS, N_VT), -IS, 1e-28);
}

/** @test Critical voltage matches n*Vt * ln(n*Vt / (sqrt(2) * Is)) */
TEST(JunctionTest, CriticalVoltageMatchesReference) {
  EXPECT_NEAR(Junction::criticalVoltage(IS, N_VT), V_CRIT_REFERENCE, 1e-15);
}

/** @test Proposals below the critical voltage or within two thermal voltages pass unchanged */
TEST(JunctionTest, SmallOrSubcriticalStepsPassUnchanged) {
  const double V_CRIT = V_CRIT_REFERENCE;

  EXPECT_DOUBLE_EQ(Junction::limitVoltage(0.70, 0.10, N_VT, V_CRIT), 0.70); // below Vcrit
  EXPECT_DOUBLE_EQ(Junction::limitVoltage(-3.0, 0.60, N_VT, V_CRIT), -3.0); // reverse
  EXPECT_DOUBLE_EQ(Junction::limitVoltage(0.79, 0.74, N_VT, V_CRIT), 0.79); // within 2*n*Vt
}

/** @test Large forward steps are taken logarithmically from the previous voltage */
TEST(JunctionTest, LargeForwardStepsAreCompressed) {
  const double V_CRIT = V_CRIT_REFERENCE;

  // From rest: n*Vt * ln(5 / n*Vt).
  EXPECT_NEAR(Junction::limitVoltage(5.0, 0.0, N_VT, V_CRIT), 0.13673651298826364, 1e-15);
  // From a forward bias: 0.6 + n*Vt * ln(1 + 4.4 / n*Vt).
  EXPECT_NEAR(Junction::limitVoltage(5.0, 0.6, N_VT, V_CRIT), 0.7335660295473273, 1e-15);
  // A step down from far above the critical voltage, past where the
  // extrapolated current would change sign, lands on the critical voltage.
  EXPECT_DOUBLE_EQ(Junction::limitVoltage(0.9, 5.0, N_VT, V_CRIT), V_CRIT);
}

/** @test Limited Newton reaches the resistor-diode operating point in a fraction of the iterations
 */
TEST(JunctionTest, LimitedNewtonConvergesOnResistorDiode) {
  // Reference: bisection on (V - Vd) / R = Is * (exp(Vd / Vt) - 1).
  struct Row {
    double vSource;
    double rOhms;
    double vDiode;
  };
  const std::array<Row, 3> ROWS = {{
      Row{5.0, 1e3, 0.6964845745632098},
      Row{0.5, 1e3, 0.49792438569095343},
      Row{12.0, 100.0, 0.7812637657392556},
  }};

  for (const Row& row : ROWS) {
    double vDiode = 0.0;
    const int ITERATIONS = solveResistorDiode(row.vSource, row.rOhms, /*limit=*/true, 100, vDiode);
    EXPECT_LT(ITERATIONS, 15) << "V = " << row.vSource;
    EXPECT_NEAR(vDiode, row.vDiode, 1e-9) << "V = " << row.vSource;
  }

  // Without the limiter the 5 V case reaches the same point, but only after
  // walking down one thermal voltage per iteration from where the limited
  // exponential turns linear (2.08 V): about (2.08 - 0.70) / 0.026 steps.
  double vUnlimited = 0.0;
  const int UNLIMITED = solveResistorDiode(5.0, 1e3, /*limit=*/false, 100, vUnlimited);
  EXPECT_GT(UNLIMITED, 40);
  EXPECT_LT(UNLIMITED, 100);
  EXPECT_NEAR(vUnlimited, 0.6964845745632098, 1e-9);
}
