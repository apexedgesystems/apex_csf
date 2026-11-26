/**
 * @file IntegrationOrder_uTest.cpp
 * @brief The integration methods held to closed forms: their order of
 *        accuracy, what they conserve, and how they start.
 *
 * Every reference is a closed form of the circuit's differential equation.
 * The solver is stepped by hand, so each run takes exactly the steps named.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/utilities/math/integration/inc/BackwardEuler.hpp"
#include "src/utilities/math/integration/inc/TrapezoidalRule.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::MnaSystemSparse;
using sim::electronics::algorithms::transient::IntegrationMethod;
using sim::electronics::algorithms::transient::TransientConfig;
using sim::electronics::algorithms::transient::TransientSolver;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;

namespace {

constexpr double DRIVE_VOLTS = 5.0;

/**
 * @brief Response to 5 sin(t / tau) from rest of a first-order circuit of
 *        time constant tau, as a share of the drive: the capacitor's
 *        voltage in an RC, the resistor's voltage (current times R) in an RL.
 *
 * tau y' + y = sin(t / tau), y(0) = 0:
 * y = (sin(t / tau) - cos(t / tau) + exp(-t / tau)) / 2.
 */
double firstOrderResponse(double seconds, double tau) {
  const double PHASE = seconds / tau;
  return 0.5 * (std::sin(PHASE) - std::cos(PHASE) + std::exp(-PHASE));
}

/** @brief A solver stepped by hand, with the state it steps. */
struct Stepped {
  TransientSolver solver;
  TransientState state;

  Stepped(std::size_t nets, IntegrationMethod method) : solver(nets) {
    solver.setIntegrationMethod(method);
    state.resize(nets, 0);
  }

  void steps(std::size_t count, double dt) {
    for (std::size_t i = 0; i < count; ++i) {
      ASSERT_EQ(solver.step(dt, state), TransientStatus::SUCCESS) << "step " << i;
    }
  }
};

/**
 * @brief 5 sin(t / tau) through 1 kohm into 1 uF (tau = 1 ms), from rest.
 * @return The capacitor's voltage less the closed form, after `tailSteps`
 *         more steps of `tailDt` beyond 2 tau taken in steps of tau / stepsPerTau.
 */
double rcError(IntegrationMethod method, std::size_t stepsPerTau, std::size_t tailSteps = 0,
               double tailDt = 0.0) {
  constexpr double OHMS = 1000.0;
  constexpr double FARADS = 1e-6;
  constexpr double TAU = OHMS * FARADS;
  Stepped run(3, method);
  run.solver.companions().addCapacitor(2, 0, FARADS);
  run.solver.setStampCallback([](MnaSystem& mna, double seconds) {
    mna.addVoltageSource(1, 0, DRIVE_VOLTS * std::sin(seconds / TAU));
    mna.addConductance(1, 2, 1.0 / OHMS);
  });
  run.steps(2 * stepsPerTau, TAU / static_cast<double>(stepsPerTau));
  run.steps(tailSteps, tailDt);
  return run.state.voltage(2) - DRIVE_VOLTS * firstOrderResponse(run.state.time, TAU);
}

/**
 * @brief 5 sin(t / tau) through 100 ohm into 10 mH (tau = 100 us), from rest.
 * @return The current less the closed form at 2 tau, in amperes.
 */
double rlError(IntegrationMethod method, std::size_t stepsPerTau) {
  constexpr double OHMS = 100.0;
  constexpr double HENRIES = 10e-3;
  constexpr double TAU = HENRIES / OHMS;
  Stepped run(3, method);
  run.solver.companions().addInductor(2, 0, HENRIES);
  run.solver.setStampCallback([](MnaSystem& mna, double seconds) {
    mna.addVoltageSource(1, 0, DRIVE_VOLTS * std::sin(seconds / TAU));
    mna.addConductance(1, 2, 1.0 / OHMS);
  });
  run.steps(2 * stepsPerTau, TAU / static_cast<double>(stepsPerTau));
  const double DRIVE = DRIVE_VOLTS * std::sin(run.state.time / TAU);
  const double AMPERES = (DRIVE - run.state.voltage(2)) / OHMS;
  return AMPERES - (DRIVE_VOLTS / OHMS) * firstOrderResponse(run.state.time, TAU);
}

/**
 * @brief 1 uF at 5 V discharging through 1 kohm (tau = 1 ms): the voltage
 *        starts with a slope of -5 V per tau.
 * @return The capacitor's voltage less 5 exp(-t / tau) at 2 tau, in volts.
 */
double rcDecayError(IntegrationMethod method, std::size_t stepsPerTau) {
  constexpr double OHMS = 1000.0;
  constexpr double FARADS = 1e-6;
  constexpr double TAU = OHMS * FARADS;
  constexpr double START_VOLTS = 5.0;
  Stepped run(2, method);
  run.solver.companions().addCapacitor(1, 0, FARADS);
  run.solver.companions().capacitor(0).setInitialVoltage(START_VOLTS);
  run.solver.setStampCallback(
      [](MnaSystem& mna, double /*seconds*/) { mna.addConductance(1, 0, 1.0 / OHMS); });
  run.steps(2 * stepsPerTau, TAU / static_cast<double>(stepsPerTau));
  return run.state.voltage(1) - START_VOLTS * std::exp(-run.state.time / TAU);
}

/**
 * @brief 10 mH carrying 50 mA, closed on 100 ohm (tau = 100 us): the
 *        current starts with a slope of -50 mA per tau.
 * @return The inductor's current less 50 mA exp(-t / tau) at 2 tau, in amperes.
 */
double rlDecayError(IntegrationMethod method, std::size_t stepsPerTau) {
  constexpr double OHMS = 100.0;
  constexpr double HENRIES = 10e-3;
  constexpr double TAU = HENRIES / OHMS;
  constexpr double START_AMPERES = 50e-3;
  Stepped run(2, method);
  run.solver.companions().addInductor(1, 0, HENRIES);
  run.solver.companions().inductor(0).setInitialCurrent(START_AMPERES);
  run.solver.setStampCallback(
      [](MnaSystem& mna, double /*seconds*/) { mna.addConductance(1, 0, 1.0 / OHMS); });
  run.steps(2 * stepsPerTau, TAU / static_cast<double>(stepsPerTau));
  return run.solver.companions().inductor(0).prevCurrent -
         START_AMPERES * std::exp(-run.state.time / TAU);
}

/**
 * @brief 1 mH across 1 uF, the capacitor at 5 V and no current, stepped
 *        through ten periods at a hundred steps a period.
 * @return The square root of the energy left as a share of the energy at
 *         the start: the amplitude ratio, without sampling the peaks.
 */
double tankAmplitudeRatio(IntegrationMethod method) {
  constexpr double HENRIES = 1e-3;
  constexpr double FARADS = 1e-6;
  constexpr double START_VOLTS = 5.0;
  const double PERIOD = 2.0 * std::numbers::pi * std::sqrt(HENRIES * FARADS);

  Stepped run(2, method);
  auto& companions = run.solver.companions();
  companions.addInductor(1, 0, HENRIES);
  companions.addCapacitor(1, 0, FARADS);
  // The capacitor at rest at 5 V; the inductor across it has those 5 V and
  // no current yet, which the trapezoidal rule reads as its last voltage.
  companions.capacitor(0).setInitialVoltage(START_VOLTS);
  companions.inductor(0).setInitialCurrent(0.0);
  companions.inductor(0).voltage = START_VOLTS;
  run.solver.setStampCallback([](MnaSystem& /*mna*/, double /*seconds*/) {});
  run.steps(1000, PERIOD / 100.0);

  const double VOLTS = companions.capacitor(0).prevVoltage;
  const double AMPERES = companions.inductor(0).prevCurrent;
  const double ENERGY = 0.5 * FARADS * VOLTS * VOLTS + 0.5 * HENRIES * AMPERES * AMPERES;
  return std::sqrt(ENERGY / (0.5 * FARADS * START_VOLTS * START_VOLTS));
}

/** @brief The RC of rcError, ready to step. */
void buildDrivenRc(Stepped& run) {
  constexpr double OHMS = 1000.0;
  constexpr double FARADS = 1e-6;
  constexpr double TAU = OHMS * FARADS;
  run.solver.companions().addCapacitor(2, 0, FARADS);
  run.solver.setStampCallback([](MnaSystem& mna, double seconds) {
    mna.addVoltageSource(1, 0, DRIVE_VOLTS * std::sin(seconds / TAU));
    mna.addConductance(1, 2, 1.0 / OHMS);
  });
}

/** @brief The RC of rcError on the sparse solver, ready to step. */
void buildDrivenRcSparse(Stepped& run) {
  constexpr double OHMS = 1000.0;
  constexpr double FARADS = 1e-6;
  constexpr double TAU = OHMS * FARADS;
  run.solver.companions().addCapacitor(2, 0, FARADS);
  run.solver.setSparse(true);
  run.solver.setStatefulStampCallbackSparse(
      [](MnaSystemSparse& mna, double seconds, const std::vector<double>& /*previous*/) {
        mna.addVoltageSource(1, 0, DRIVE_VOLTS * std::sin(seconds / TAU));
        mna.addConductance(1, 2, 1.0 / OHMS);
      });
}

/**
 * @brief Steps the driven RC with a companion method and its differential
 *        equation, tau v' = vin(t) - v, with an integrator of the
 *        integration library, and expects the two voltages equal at every
 *        step.
 *
 * @tparam Integrator The library's integrator of the same method.
 * @param implicitShare The share of the step the method puts on the new
 *        derivative: 1, 1/2 or 2/3. The equation is linear, so one Newton
 *        iteration with the Jacobian 1 + share * dt / tau is its solution.
 */
template <typename Integrator>
void expectCompanionMatchesLibrary(IntegrationMethod method, double implicitShare) {
  constexpr double TAU = 1e-3;
  constexpr double DT = TAU / 50.0;
  Stepped run(3, method);
  buildDrivenRc(run);

  apex::math::integration::ImplicitOptions<double> options{};
  options.computeJacobian = [implicitShare](const double& /*volts*/, double /*seconds*/) {
    return 1.0 + implicitShare * DT / TAU;
  };
  options.linearSolve = [](const double& jacobian, const double& rhs) { return rhs / jacobian; };
  options.converged = [](const double& /*delta*/, const double& /*residual*/) { return true; };

  constexpr auto SUCCESS = static_cast<std::uint8_t>(apex::math::integration::Status::SUCCESS);
  Integrator library;
  ASSERT_EQ(library.initialize(
                [](double volts, double seconds) {
                  return (DRIVE_VOLTS * std::sin(seconds / TAU) - volts) / TAU;
                },
                0.0, 0.0, options),
            SUCCESS);

  double volts = 0.0;
  for (int step = 0; step < 200; ++step) {
    run.steps(1, DT);
    ASSERT_EQ(library.step(volts, DT, options), SUCCESS);
    ASSERT_NEAR(run.state.voltage(2), volts, 1e-11)
        << "method " << static_cast<int>(method) << ", step " << step;
  }
}

} // namespace

/* ----------------------------- Order of Accuracy ----------------------------- */

/** @test Backward Euler: halving the step halves the error */
TEST(IntegrationOrderTest, BackwardEulerIsFirstOrder) {
  const double RC_RATIO = rcError(IntegrationMethod::BACKWARD_EULER, 100) /
                          rcError(IntegrationMethod::BACKWARD_EULER, 200);
  const double RL_RATIO = rlError(IntegrationMethod::BACKWARD_EULER, 100) /
                          rlError(IntegrationMethod::BACKWARD_EULER, 200);
  EXPECT_NEAR(RC_RATIO, 2.0, 0.1);
  EXPECT_NEAR(RL_RATIO, 2.0, 0.1);
}

/** @test The trapezoidal rule: halving the step quarters the error */
TEST(IntegrationOrderTest, TrapezoidalIsSecondOrder) {
  const double RC_COARSE = rcError(IntegrationMethod::TRAPEZOIDAL, 100);
  const double RL_COARSE = rlError(IntegrationMethod::TRAPEZOIDAL, 100);
  EXPECT_NEAR(RC_COARSE / rcError(IntegrationMethod::TRAPEZOIDAL, 200), 4.0, 0.2);
  EXPECT_NEAR(RL_COARSE / rlError(IntegrationMethod::TRAPEZOIDAL, 200), 4.0, 0.2);
  // On signals of 5 V and 35 mA.
  EXPECT_LT(std::fabs(RC_COARSE), 1e-4);
  EXPECT_LT(std::fabs(RL_COARSE), 1e-6);
}

/** @test Gear's second-order formula: halving the step quarters the error */
TEST(IntegrationOrderTest, Gear2IsSecondOrder) {
  const double RC_COARSE = rcError(IntegrationMethod::GEAR2, 100);
  const double RL_COARSE = rlError(IntegrationMethod::GEAR2, 100);
  EXPECT_NEAR(RC_COARSE / rcError(IntegrationMethod::GEAR2, 200), 4.0, 0.3);
  EXPECT_NEAR(RL_COARSE / rlError(IntegrationMethod::GEAR2, 200), 4.0, 0.3);
  EXPECT_LT(std::fabs(RC_COARSE), 5e-4);
  EXPECT_LT(std::fabs(RL_COARSE), 1e-4);
}

/**
 * @test Gear's formula is second order from a start with a slope: a
 *       discharge, where the first step already moves.
 *
 * The first step is backward Euler, whose one step costs an error of second
 * order; a first step of Gear's formula on an invented earlier value costs
 * one of first order (3.4 mV here at a hundred steps a time constant).
 */
TEST(IntegrationOrderTest, Gear2IsSecondOrderFromANonzeroSlope) {
  const double RC_COARSE = rcDecayError(IntegrationMethod::GEAR2, 100);
  const double RC_MIDDLE = rcDecayError(IntegrationMethod::GEAR2, 200);
  const double RC_FINE = rcDecayError(IntegrationMethod::GEAR2, 400);
  EXPECT_NEAR(RC_COARSE / RC_MIDDLE, 4.0, 0.3);
  EXPECT_NEAR(RC_MIDDLE / RC_FINE, 4.0, 0.3);
  EXPECT_LT(std::fabs(RC_COARSE), 1e-5);

  const double RL_COARSE = rlDecayError(IntegrationMethod::GEAR2, 100);
  const double RL_MIDDLE = rlDecayError(IntegrationMethod::GEAR2, 200);
  const double RL_FINE = rlDecayError(IntegrationMethod::GEAR2, 400);
  EXPECT_NEAR(RL_COARSE / RL_MIDDLE, 4.0, 0.3);
  EXPECT_NEAR(RL_MIDDLE / RL_FINE, 4.0, 0.3);
  EXPECT_LT(std::fabs(RL_COARSE), 1e-7);
}

/** @test Backward Euler is first order on the same discharges */
TEST(IntegrationOrderTest, BackwardEulerIsFirstOrderFromANonzeroSlope) {
  EXPECT_NEAR(rcDecayError(IntegrationMethod::BACKWARD_EULER, 100) /
                  rcDecayError(IntegrationMethod::BACKWARD_EULER, 200),
              2.0, 0.1);
  EXPECT_NEAR(rlDecayError(IntegrationMethod::BACKWARD_EULER, 100) /
                  rlDecayError(IntegrationMethod::BACKWARD_EULER, 200),
              2.0, 0.1);
}

/* ----------------------------- Conservation ----------------------------- */

/** @test The trapezoidal rule keeps the energy of a lossless tank */
TEST(IntegrationOrderTest, TrapezoidalConservesTankEnergy) {
  EXPECT_NEAR(tankAmplitudeRatio(IntegrationMethod::TRAPEZOIDAL), 1.0, 1e-9);
}

/** @test Gear's formula damps a lossless tank, and only slightly at a hundred steps a period */
TEST(IntegrationOrderTest, Gear2DampsTankSlightly) {
  const double RATIO = tankAmplitudeRatio(IntegrationMethod::GEAR2);
  EXPECT_GE(RATIO, 0.99);
  EXPECT_LT(RATIO, 1.0);
}

/* ----------------------------- Starting ----------------------------- */

/** @test A divider at its operating point stays there under every method */
TEST(IntegrationOrderTest, OperatingPointIsHeldByEveryMethod) {
  for (const IntegrationMethod METHOD :
       {IntegrationMethod::BACKWARD_EULER, IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2}) {
    // 5 V over 1 kohm and 1 kohm, with 1 uF across the lower one.
    Stepped run(3, METHOD);
    run.solver.companions().addCapacitor(2, 0, 1e-6);
    run.solver.setStampCallback([](MnaSystem& mna, double /*seconds*/) {
      mna.addVoltageSource(1, 0, 5.0);
      mna.addConductance(1, 2, 1e-3);
      mna.addConductance(2, 0, 1e-3);
    });
    ASSERT_EQ(run.solver.computeDC(run.state), TransientStatus::SUCCESS);
    ASSERT_NEAR(run.state.voltage(2), 2.5, 1e-12);
    for (int step = 0; step < 50; ++step) {
      run.steps(1, 1e-5);
      ASSERT_NEAR(run.state.voltage(2), 2.5, 1e-12)
          << "method " << static_cast<int>(METHOD) << ", step " << step;
    }
  }
}

/** @test An inductor starts with the current of the operating point, on every run */
TEST(IntegrationOrderTest, InductorStartsWithItsOperatingPointCurrent) {
  for (const IntegrationMethod METHOD :
       {IntegrationMethod::BACKWARD_EULER, IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2}) {
    // 5 V through 100 ohm into 10 mH: 50 mA at rest.
    Stepped run(3, METHOD);
    run.solver.companions().addInductor(2, 0, 10e-3);
    run.solver.setStampCallback([](MnaSystem& mna, double /*seconds*/) {
      mna.addVoltageSource(1, 0, 5.0);
      mna.addConductance(1, 2, 1.0 / 100.0);
    });
    const auto AMPERES = [&run] { return (5.0 - run.state.voltage(2)) / 100.0; };

    // Twice: the second start is from the operating point again, not from
    // where the first run stopped.
    for (int start = 0; start < 2; ++start) {
      ASSERT_EQ(run.solver.computeDC(run.state), TransientStatus::SUCCESS);
      run.steps(1, 1e-6);
      EXPECT_NEAR(AMPERES(), 50e-3, 1e-6) << "method " << static_cast<int>(METHOD);
      run.steps(20, 1e-6);
      EXPECT_NEAR(AMPERES(), 50e-3, 1e-6) << "method " << static_cast<int>(METHOD);
    }
  }
}

/* ----------------------------- Changing Step ----------------------------- */

/** @test A last step half as long leaves Gear's formula as accurate as the steps before it */
TEST(IntegrationOrderTest, Gear2KeepsItsAccuracyOverAShorterStep) {
  // 205 steps of 10 us, then one of 5 us, against the error after the 205.
  const double BEFORE = rcError(IntegrationMethod::GEAR2, 100, 5, 1e-5);
  constexpr double OHMS = 1000.0;
  constexpr double FARADS = 1e-6;
  constexpr double TAU = OHMS * FARADS;
  Stepped run(3, IntegrationMethod::GEAR2);
  run.solver.companions().addCapacitor(2, 0, FARADS);
  run.solver.setStampCallback([](MnaSystem& mna, double seconds) {
    mna.addVoltageSource(1, 0, DRIVE_VOLTS * std::sin(seconds / TAU));
    mna.addConductance(1, 2, 1.0 / OHMS);
  });
  run.steps(205, 1e-5);
  run.steps(1, 5e-6);
  const double SHORTENED =
      run.state.voltage(2) - DRIVE_VOLTS * firstOrderResponse(run.state.time, TAU);
  EXPECT_LE(std::fabs(SHORTENED), 2.0 * std::fabs(BEFORE));
}

/**
 * @test A solver that keeps its factors takes the same steps as one that does
 *       not, across a change of step length under Gear's formula: by the
 *       dense and by the two-state way of keeping them.
 *
 * After a shorter step the formula weighs its history differently for one
 * step more, so the conductances of two steps of equal length differ.
 */
TEST(IntegrationOrderTest, KeptFactorsFollowGear2AcrossAChangedStep) {
  enum class Keeping { DENSE, DUAL };
  for (const Keeping KEEPING : {Keeping::DENSE, Keeping::DUAL}) {
    Stepped plain(3, IntegrationMethod::GEAR2);
    Stepped kept(3, IntegrationMethod::GEAR2);
    buildDrivenRc(plain);
    buildDrivenRc(kept);
    switch (KEEPING) {
    case Keeping::DENSE:
      kept.solver.setCachedLU(true);
      break;
    case Keeping::DUAL:
      kept.solver.setDualLU(true);
      break;
    }

    const auto BOTH = [&plain, &kept, KEEPING](std::size_t count, double dt) {
      for (std::size_t i = 0; i < count; ++i) {
        plain.steps(1, dt);
        if (KEEPING == Keeping::DUAL) {
          ASSERT_EQ(kept.solver.stepDual(dt, 0, kept.state), TransientStatus::SUCCESS);
        } else {
          kept.steps(1, dt);
        }
        ASSERT_NEAR(kept.state.voltage(2), plain.state.voltage(2), 1e-12)
            << "keeping " << static_cast<int>(KEEPING) << ", at " << plain.state.time << " s";
      }
    };
    BOTH(100, 1e-5);
    BOTH(1, 5e-6);
    BOTH(50, 1e-5);
  }
}

/**
 * @test The sparse solver's kept factors serve steps of the length they were
 *       built for: steps of 10, 20, 40 and 80 us, each twice the one before.
 *
 * Gear's factor is 5/3 for each of the last three while C/dt halves from one
 * to the next, so the factor alone does not tell the matrices apart. The
 * steps of 80 us that follow reach the same factor and length, where the
 * kept factors do serve.
 */
TEST(IntegrationOrderTest, KeptSparseFactorsFollowTheStepLength) {
  Stepped plain(3, IntegrationMethod::GEAR2);
  Stepped sparse(3, IntegrationMethod::GEAR2);
  buildDrivenRc(plain);
  buildDrivenRcSparse(sparse);
  for (const double DT : {10e-6, 20e-6, 40e-6, 80e-6, 80e-6, 80e-6, 80e-6}) {
    plain.steps(1, DT);
    sparse.steps(1, DT);
    ASSERT_NEAR(sparse.state.voltage(2), plain.state.voltage(2), 1e-12)
        << "after a step of " << DT << " s, at " << plain.state.time << " s";
  }
}

/** @test run() integrates with the method its configuration names */
TEST(IntegrationOrderTest, RunUsesTheConfiguredMethod) {
  // The tank of TrapezoidalConservesTankEnergy through run(): backward Euler
  // would leave a fraction of the energy.
  constexpr double HENRIES = 1e-3;
  constexpr double FARADS = 1e-6;
  const double PERIOD = 2.0 * std::numbers::pi * std::sqrt(HENRIES * FARADS);

  TransientSolver solver(2);
  auto& companions = solver.companions();
  companions.addInductor(1, 0, HENRIES);
  companions.addCapacitor(1, 0, FARADS);
  companions.capacitor(0).setInitialVoltage(5.0);
  companions.inductor(0).voltage = 5.0;
  solver.setStampCallback([](MnaSystem& /*mna*/, double /*seconds*/) {});

  TransientConfig config;
  config.tStart = 0.0;
  config.tEnd = 5.0 * PERIOD;
  config.tStep = PERIOD / 100.0;
  config.dcOpPoint = false;
  config.method = IntegrationMethod::TRAPEZOIDAL;
  ASSERT_TRUE(solver.run(config, false).success);
  EXPECT_EQ(solver.integrationMethod(), IntegrationMethod::TRAPEZOIDAL);

  const double VOLTS = companions.capacitor(0).prevVoltage;
  const double AMPERES = companions.inductor(0).prevCurrent;
  const double ENERGY = 0.5 * FARADS * VOLTS * VOLTS + 0.5 * HENRIES * AMPERES * AMPERES;
  EXPECT_NEAR(ENERGY / (0.5 * FARADS * 25.0), 1.0, 1e-9);
}

/* ----------------------------- Integration Library ----------------------------- */

/** @test Backward Euler is the integration library's backward Euler on the circuit's equation */
TEST(IntegrationOrderTest, BackwardEulerMatchesTheIntegrationLibrary) {
  expectCompanionMatchesLibrary<apex::math::integration::BackwardEuler<double>>(
      IntegrationMethod::BACKWARD_EULER, 1.0);
}

/** @test The trapezoidal companions are the integration library's trapezoidal rule */
TEST(IntegrationOrderTest, TrapezoidalMatchesTheIntegrationLibrary) {
  expectCompanionMatchesLibrary<apex::math::integration::TrapezoidalRule<double>>(
      IntegrationMethod::TRAPEZOIDAL, 0.5);
}

/**
 * @test The GEAR2 companions take the integration library's backward-Euler
 *       step first and Gear's recurrence from then on.
 *
 * The recurrence is written out for tau v' = vin(t) - v with equal steps:
 * (3 v - 4 v1 + v2) / (2 dt) = (vin - v) / tau. The library's BDF2 starts on
 * an invented earlier value, so it is not the reference past the start.
 */
TEST(IntegrationOrderTest, Gear2StartsAsBackwardEulerThenFollowsItsRecurrence) {
  constexpr double TAU = 1e-3;
  constexpr double DT = TAU / 50.0;
  Stepped gear(3, IntegrationMethod::GEAR2);
  Stepped euler(3, IntegrationMethod::BACKWARD_EULER);
  buildDrivenRc(gear);
  buildDrivenRc(euler);

  gear.steps(1, DT);
  euler.steps(1, DT);
  ASSERT_DOUBLE_EQ(gear.state.voltage(2), euler.state.voltage(2));

  double before = 0.0;
  double last = gear.state.voltage(2);
  for (int step = 2; step <= 200; ++step) {
    gear.steps(1, DT);
    const double DRIVE = DRIVE_VOLTS * std::sin(gear.state.time / TAU);
    const double NEXT = (4.0 * last - before + 2.0 * DT / TAU * DRIVE) / (3.0 + 2.0 * DT / TAU);
    ASSERT_NEAR(gear.state.voltage(2), NEXT, 1e-11) << "step " << step;
    before = last;
    last = gear.state.voltage(2);
  }
}
