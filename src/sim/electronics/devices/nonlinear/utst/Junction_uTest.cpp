/**
 * @file Junction_uTest.cpp
 * @brief Unit tests for the junction-voltage helpers.
 *
 * Notes:
 *  - Reference operating points come from bisection on the circuit equation
 *    outside this library, not from a Newton iteration.
 *  - Reference currents behind a series resistance come from bisection on the
 *    terminal equation in 60-digit decimal arithmetic, and their conductances
 *    from a central difference of those currents.
 */

#include "src/sim/electronics/devices/nonlinear/inc/Junction.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

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

/* ----------------------------- Series Resistance ----------------------------- */

/**
 * @test Behind a series resistance whose product with the saturation current is large, the
 * current and conductance match the reference in reverse and forward bias. A reverse solve
 * started below its root lands far up the exponential and runs out of iterations there: the
 * first three rows returned +1.3 A, +1.3e6 A and +0.028 A before the start was moved above
 * the root.
 */
TEST(JunctionTest, SeriesSolveMatchesReferenceForLargeResistanceTimesSaturationCurrent) {
  struct Row {
    double v;
    double is;
    double nVt;
    double rs;
    double current;
    double conductance;
    double tolerance; // relative, of the current
  };
  // In the seventh row a millionth of the saturation current flows: the junction's
  // exp(x) - 1 is taken at x = -1e-9, where a double keeps eight digits of it.
  const std::array<Row, 9> ROWS = {{
      Row{-1.0, 1e-9, 0.026, 1e10, -9.972685163484649e-11, 9.971203042147085e-11, 1e-12},
      Row{-1.0, 1e-3, 0.026, 1e4, -9.972685163484649e-05, 9.971203042147085e-05, 1e-12},
      Row{-0.7, 1e-6, 0.026, 2.5e6, -2.766320918412136e-07, 3.9433063240604385e-07, 1e-12},
      Row{-0.05, 1e-6, 0.026, 2.5e6, -1.9792097917714337e-08, 3.958005584336085e-07, 1e-12},
      Row{-20.0, 1e-6, 0.052, 1e3, -1e-06, 0.0, 1e-12},
      Row{-100.0, 1e-12, 0.026, 1e12, -1e-12, 0.0, 1e-12},
      Row{-1e-3, 1e-3, 0.015, 1e9, -9.999999850000003e-13, 9.999999850000003e-10, 1e-7},
      Row{0.7, 1e-6, 0.026, 2.5e6, 2.774533671917712e-07, 3.9676981859427736e-07, 1e-12},
      Row{5.0, 1e-3, 0.026, 1e4, 0.0004989476154922615, 9.982684531791236e-05, 1e-12},
  }};

  for (const Row& row : ROWS) {
    const auto POINT = Junction::withSeriesResistance(row.v, row.is, row.nVt, row.rs);
    EXPECT_NEAR(POINT.current, row.current, row.tolerance * std::abs(row.current))
        << "V = " << row.v << ", Is = " << row.is << ", Rs = " << row.rs;
    // The reference conductance is a difference over 2 uV: good to 1e-9 of its size.
    EXPECT_NEAR(POINT.conductance, row.conductance,
                std::max(row.tolerance, 1e-9) * row.conductance + 1e-30)
        << "V = " << row.v << ", Is = " << row.is << ", Rs = " << row.rs;
  }
}

/**
 * @test Over twelve decades of saturation current and eighteen of series resistance, three
 * thermal voltages and terminal voltages from -100 V to 100 V, the solve converges in at most
 * twelve steps, the junction voltage it returns lies between 0 and the terminal voltage and
 * satisfies the terminal equation, and the current never has the sign opposite to the
 * terminal voltage's.
 */
TEST(JunctionTest, SeriesSolveConvergesToAPointBetweenZeroAndTheTerminalVoltage) {
  int most = 0;
  int points = 0;
  for (const double IS_ROW : {1e-16, 1e-12, 1e-9, 1e-6, 1e-3}) {
    for (const double N_VT_ROW : {0.015, 0.026, 0.052}) {
      for (double rs = 1e-6; rs < 2e12; rs *= 100.0) {
        for (const double V : {-100.0, -20.0, -1.0, -0.7, -0.05, -1e-3, -1e-9, 0.0, 1e-9, 1e-3,
                               0.05, 0.3, 0.7, 1.0, 5.0, 20.0, 100.0}) {
          const auto SOLVED = Junction::seriesJunctionVoltage(V, IS_ROW, N_VT_ROW, rs);
          const double VJ = SOLVED.junctionVolts;
          const auto POINT = Junction::withSeriesResistance(V, IS_ROW, N_VT_ROW, rs);
          SCOPED_TRACE(::testing::Message() << "V = " << V << ", Is = " << IS_ROW
                                            << ", nVt = " << N_VT_ROW << ", Rs = " << rs);
          ASSERT_TRUE(SOLVED.converged);
          most = std::max(most, SOLVED.iterations);
          EXPECT_GE(VJ, std::min(V, 0.0));
          EXPECT_LE(VJ, std::max(V, 0.0));
          // The terminal equation, to what a double resolves of it: the junction's
          // exp(x) - 1 carries an absolute error near 2e-16, which Rs * Is scales, and the
          // stopping tolerance leaves 1e-15 of |Vj| + n*Vt, which the slope scales.
          const double DROP = rs * Junction::current(VJ, IS_ROW, N_VT_ROW);
          const double SLOPE = 1.0 + rs * Junction::conductance(VJ, IS_ROW, N_VT_ROW);
          EXPECT_NEAR(VJ + DROP, V,
                      1e-12 * (std::abs(V) + std::abs(DROP)) + 4e-16 * rs * IS_ROW +
                          4e-15 * SLOPE * (std::abs(VJ) + N_VT_ROW));
          // Never the other sign. It is zero where the true current is under 1e-16 of
          // the saturation current: there exp(x) - 1 is zero in a double.
          EXPECT_GE(POINT.current * V, 0.0);
          if (std::abs(V) >= 1e-3) {
            EXPECT_NE(POINT.current, 0.0);
          }
          EXPECT_GE(POINT.conductance, 0.0); // deep in reverse it is under a double's range
          ++points;
        }
      }
    }
  }
  EXPECT_EQ(points, 5 * 3 * 10 * 17);
  EXPECT_LE(most, 12);
  std::printf("  series solve: %d points, at most %d steps\n", points, most);
}

/**
 * @test A solve outside its domain returns no number, where the point it stopped at would
 * have been one. A saturation current or a thermal voltage that is not positive is refused
 * before any evaluation; a terminal voltage that is not a number runs to the iteration
 * limit. In both the junction voltage reported is not called converged, and current and
 * conductance are NaN.
 */
TEST(JunctionTest, SeriesSolveOutsideItsDomainReturnsNoNumber) {
  struct Refused {
    double is;
    double nVt;
  };
  for (const Refused& row :
       {Refused{-1e-14, 0.026}, Refused{0.0, 0.026}, Refused{1e-14, 0.0}, Refused{1e-14, -0.026}}) {
    const auto SOLVED = Junction::seriesJunctionVoltage(1.0, row.is, row.nVt, 1.0);
    EXPECT_FALSE(SOLVED.converged) << "Is = " << row.is << ", nVt = " << row.nVt;
    EXPECT_EQ(SOLVED.iterations, 0) << "Is = " << row.is << ", nVt = " << row.nVt;
    // At the junction voltage reported, 0 V, a negative saturation current gives a current
    // of zero: a number, and not the answer to anything.
    const auto POINT = Junction::withSeriesResistance(1.0, row.is, row.nVt, 1.0);
    EXPECT_TRUE(std::isnan(POINT.current)) << "Is = " << row.is << ", nVt = " << row.nVt;
    EXPECT_TRUE(std::isnan(POINT.conductance)) << "Is = " << row.is << ", nVt = " << row.nVt;
  }

  const double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();
  const auto UNSOLVED = Junction::seriesJunctionVoltage(NOT_A_NUMBER, 1e-14, 0.026, 1.0);
  EXPECT_FALSE(UNSOLVED.converged);
  EXPECT_EQ(UNSOLVED.iterations, Junction::SERIES_MAX_ITER);
  EXPECT_TRUE(std::isnan(Junction::withSeriesResistance(NOT_A_NUMBER, 1e-14, 0.026, 1.0).current));
}
