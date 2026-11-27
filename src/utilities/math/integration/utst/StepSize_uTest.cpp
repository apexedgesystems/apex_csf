/**
 * @file StepSize_uTest.cpp
 * @brief Unit tests for apex::math::integration::isValidStep and every step guard built on it.
 *
 * Notes:
 *  - Each integrator and accumulator must refuse a step size that is zero,
 *    negative, infinite or NaN, report its invalid-step status, and leave its
 *    state, time and counters as they were.
 *  - The derivative and solver callbacks count their calls: a refused step
 *    makes none.
 */

#include "src/utilities/math/integration/inc/StepSize.hpp"

#include "src/utilities/math/integration/inc/Accumulator.hpp"
#include "src/utilities/math/integration/inc/AdamsBashforth.hpp"
#include "src/utilities/math/integration/inc/AdamsMoulton.hpp"
#include "src/utilities/math/integration/inc/BDF.hpp"
#include "src/utilities/math/integration/inc/BDF2.hpp"
#include "src/utilities/math/integration/inc/BackwardEuler.hpp"
#include "src/utilities/math/integration/inc/ExplicitEuler.hpp"
#include "src/utilities/math/integration/inc/Leapfrog.hpp"
#include "src/utilities/math/integration/inc/Rosenbrock.hpp"
#include "src/utilities/math/integration/inc/RungeKutta4.hpp"
#include "src/utilities/math/integration/inc/RungeKutta45.hpp"
#include "src/utilities/math/integration/inc/RungeKuttaNystrom.hpp"
#include "src/utilities/math/integration/inc/SDIRK2.hpp"
#include "src/utilities/math/integration/inc/TrapezoidalRule.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include <array>
#include <limits>

using apex::math::integration::Accumulator;
using apex::math::integration::AccumulatorStatus;
using apex::math::integration::AdamsBashforth;
using apex::math::integration::AdamsMoulton;
using apex::math::integration::BackwardEuler;
using apex::math::integration::BDF;
using apex::math::integration::BDF2;
using apex::math::integration::ExplicitEuler;
using apex::math::integration::isValidStep;
using apex::math::integration::Leapfrog;
using apex::math::integration::LeapfrogStatus;
using apex::math::integration::MultiRateAccumulator;
using apex::math::integration::RK45Status;
using apex::math::integration::RKN34;
using apex::math::integration::RKN4;
using apex::math::integration::RKN6;
using apex::math::integration::RKNStatus;
using apex::math::integration::ROS2;
using apex::math::integration::ROS3;
using apex::math::integration::RosenbrockOptions;
using apex::math::integration::RosenbrockStatus;
using apex::math::integration::RungeKutta4;
using apex::math::integration::RungeKutta45;
using apex::math::integration::RungeKutta45Options;
using apex::math::integration::SDIRK2;
using apex::math::integration::Status;
using apex::math::integration::TrapezoidalRule;

namespace {

constexpr double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();
constexpr double INFINITE = std::numeric_limits<double>::infinity();

/** Step sizes no stepper may take. */
constexpr std::array<double, 5> REFUSED_STEPS = {NOT_A_NUMBER, INFINITE, -INFINITE, 0.0, -0.1};

constexpr double START_VALUE = 1.0;
constexpr double START_RATE = 0.5;
constexpr double START_TIME = 2.0;

/** Options type of a CRTP integrator `Integrator<State, Options>`. */
template <typename Integrator> struct OptionsOf;
template <template <typename, typename> class Integrator, typename State, typename Options>
struct OptionsOf<Integrator<State, Options>> {
  using Type = Options;
};

/**
 * Options whose solver callbacks, where the type has them, count their calls
 * in `solverCalls`.
 */
template <typename Options> Options countingOptions(int& solverCalls) {
  Options opts{};
  if constexpr (requires { opts.linearSolve; }) {
    opts.computeJacobian = [&solverCalls](const double&, double) {
      ++solverCalls;
      return 1.0;
    };
    opts.linearSolve = [&solverCalls](const double&, const double& rhs) {
      ++solverCalls;
      return rhs;
    };
  }
  return opts;
}

/** Acceleration callable for every second-order stepper; counts its calls. */
struct CountingAccel {
  int* calls;
  double operator()(const double&) const {
    ++*calls;
    return 0.0;
  }
  double operator()(const double&, const double&, const double&) const {
    ++*calls;
    return 0.0;
  }
};

} // namespace

/* -------------------------------- API Tests ------------------------------- */

/** @test Accepts positive finite step sizes, the smallest and the largest included */
TEST(IsValidStepTest, AcceptsPositiveFinite) {
  EXPECT_TRUE(isValidStep(1.0e-3));
  EXPECT_TRUE(isValidStep(std::numeric_limits<double>::denorm_min()));
  EXPECT_TRUE(isValidStep(std::numeric_limits<double>::max()));
}

/** @test Refuses zero, negative, infinite and NaN step sizes */
TEST(IsValidStepTest, RefusesZeroNegativeAndNonFinite) {
  for (const double dt : REFUSED_STEPS) {
    EXPECT_FALSE(isValidStep(dt)) << "dt " << dt;
  }
  EXPECT_FALSE(isValidStep(-0.0));
}

/** @test Evaluates in a constant expression */
TEST(IsValidStepTest, UsableInConstantExpression) {
  static_assert(isValidStep(0.25));
  static_assert(!isValidStep(0.0));
  static_assert(!isValidStep(std::numeric_limits<double>::infinity()));
  static_assert(!isValidStep(std::numeric_limits<double>::quiet_NaN()));
  SUCCEED();
}

/* ------------------------- First-Order Stepper Tests ---------------------- */

template <typename Integrator> class FirstOrderStepGuardTest : public ::testing::Test {};

using FirstOrderSteppers =
    ::testing::Types<ExplicitEuler<double>, RungeKutta4<double>, BackwardEuler<double>,
                     TrapezoidalRule<double>, BDF2<double>, AdamsBashforth<double>,
                     AdamsMoulton<double>, SDIRK2<double>>;
TYPED_TEST_SUITE(FirstOrderStepGuardTest, FirstOrderSteppers);

/** @test Refuses the step, counts one rejection, and leaves state, time and call counts alone */
TYPED_TEST(FirstOrderStepGuardTest, RefusesInvalidStep) {
  using Options = typename OptionsOf<TypeParam>::Type;

  for (const double dt : REFUSED_STEPS) {
    int derivativeCalls = 0;
    int solverCalls = 0;
    const Options opts = countingOptions<Options>(solverCalls);
    TypeParam integrator;
    ASSERT_EQ(integrator.initialize(
                  [&derivativeCalls](const double& y, double) {
                    ++derivativeCalls;
                    return -y;
                  },
                  START_VALUE, START_TIME, opts),
              static_cast<std::uint8_t>(Status::SUCCESS));
    const int callsAfterInitialize = derivativeCalls;

    double y = START_VALUE;
    EXPECT_EQ(integrator.step(y, dt, opts), static_cast<std::uint8_t>(Status::ERROR_INVALID_STEP))
        << "dt " << dt;
    EXPECT_EQ(y, START_VALUE) << "dt " << dt;
    EXPECT_EQ(integrator.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(integrator.stats().stepRejections, 1U) << "dt " << dt;
    EXPECT_EQ(derivativeCalls, callsAfterInitialize) << "dt " << dt;
    EXPECT_EQ(solverCalls, 0) << "dt " << dt;
  }
}

/** @test Refuses the step in the variable-order BDF integrator */
TEST(BdfStepGuardTest, RefusesInvalidStep) {
  for (const double dt : REFUSED_STEPS) {
    int derivativeCalls = 0;
    int solverCalls = 0;
    const auto opts =
        countingOptions<apex::math::integration::ImplicitOptions<double>>(solverCalls);
    BDF<double, 3> integrator;
    integrator.initialize(
        [&derivativeCalls](const double& y, double) {
          ++derivativeCalls;
          return -y;
        },
        START_VALUE, START_TIME);

    double y = START_VALUE;
    EXPECT_EQ(integrator.step(y, dt, opts), static_cast<std::uint8_t>(Status::ERROR_INVALID_STEP))
        << "dt " << dt;
    EXPECT_EQ(y, START_VALUE) << "dt " << dt;
    EXPECT_EQ(integrator.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(integrator.stats().steps, 0U) << "dt " << dt;
    EXPECT_EQ(derivativeCalls, 0) << "dt " << dt;
    EXPECT_EQ(solverCalls, 0) << "dt " << dt;
  }
}

/** @test Refuses the step in the adaptive RK45 integrator and suggests the minimum step */
TEST(Rk45StepGuardTest, RefusesInvalidStep) {
  const RungeKutta45Options opts{};
  for (const double dt : REFUSED_STEPS) {
    int derivativeCalls = 0;
    RungeKutta45<double> integrator;
    const auto result = integrator.step(
        START_VALUE, START_TIME, dt,
        [&derivativeCalls](const double& y, double) {
          ++derivativeCalls;
          return -y;
        },
        opts);

    EXPECT_EQ(result.status, RK45Status::ERROR_INVALID_STEP) << "dt " << dt;
    EXPECT_EQ(result.dtNext, opts.dtMin) << "dt " << dt;
    EXPECT_EQ(derivativeCalls, 0) << "dt " << dt;
    EXPECT_EQ(integrator.stats().functionEvals, 0U) << "dt " << dt;
    EXPECT_EQ(integrator.stats().rejectedSteps, 0U) << "dt " << dt;
  }
}

/** @test Refuses the step in both Rosenbrock integrators */
TEST(RosenbrockStepGuardTest, RefusesInvalidStep) {
  for (const double dt : REFUSED_STEPS) {
    int calls = 0;
    RosenbrockOptions<double> opts;
    opts.computeMatrix = [&calls](const double&, double, double) {
      ++calls;
      return 1.0;
    };
    opts.linearSolve = [&calls](const double&, const double& rhs) {
      ++calls;
      return rhs;
    };
    const auto f = [&calls](const double& y, double) {
      ++calls;
      return -y;
    };

    ROS2<double> ros2;
    ros2.initialize(f, START_VALUE, START_TIME);
    double y = START_VALUE;
    EXPECT_EQ(ros2.step(y, dt, opts),
              static_cast<std::uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP))
        << "dt " << dt;
    EXPECT_EQ(y, START_VALUE) << "dt " << dt;
    EXPECT_EQ(ros2.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(ros2.stats().steps, 0U) << "dt " << dt;

    ROS3<double> ros3;
    ros3.initialize(f, START_VALUE, START_TIME);
    EXPECT_EQ(ros3.step(y, dt, opts),
              static_cast<std::uint8_t>(RosenbrockStatus::ERROR_INVALID_STEP))
        << "dt " << dt;
    EXPECT_EQ(y, START_VALUE) << "dt " << dt;
    EXPECT_EQ(ros3.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(ros3.stats().steps, 0U) << "dt " << dt;

    EXPECT_EQ(calls, 0) << "dt " << dt;
  }
}

/* ------------------------ Second-Order Stepper Tests ---------------------- */

template <typename Integrator> class SecondOrderStepGuardTest : public ::testing::Test {};

using SecondOrderSteppers =
    ::testing::Types<apex::math::integration::Leapfrog<double>,
                     apex::math::integration::VelocityVerlet<double>, RKN4<double>, RKN6<double>>;
TYPED_TEST_SUITE(SecondOrderStepGuardTest, SecondOrderSteppers);

/** @test Refuses the step and leaves position, velocity, time and call counts alone */
TYPED_TEST(SecondOrderStepGuardTest, RefusesInvalidStep) {
  // The four steppers share one value for their invalid-step status.
  static_assert(static_cast<int>(LeapfrogStatus::ERROR_INVALID_STEP) ==
                static_cast<int>(RKNStatus::ERROR_INVALID_STEP));

  for (const double dt : REFUSED_STEPS) {
    int accelCalls = 0;
    const CountingAccel accel{&accelCalls};
    TypeParam integrator;
    integrator.reset(START_TIME);

    double x = START_VALUE;
    double v = START_RATE;
    EXPECT_EQ(integrator.step(x, v, dt, accel),
              static_cast<std::uint8_t>(RKNStatus::ERROR_INVALID_STEP))
        << "dt " << dt;
    EXPECT_EQ(x, START_VALUE) << "dt " << dt;
    EXPECT_EQ(v, START_RATE) << "dt " << dt;
    EXPECT_EQ(integrator.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(integrator.stats().steps, 0U) << "dt " << dt;
    EXPECT_EQ(accelCalls, 0) << "dt " << dt;
  }
}

/** @test Refuses the step in the adaptive RKN34 integrator and suggests the minimum step */
TEST(Rkn34StepGuardTest, RefusesInvalidStep) {
  const RKN34<double>::Options opts{};
  for (const double dt : REFUSED_STEPS) {
    int accelCalls = 0;
    const CountingAccel accel{&accelCalls};
    RKN34<double> integrator;
    integrator.reset(START_TIME);

    double y = START_VALUE;
    double v = START_RATE;
    const auto result = integrator.step(y, v, dt, accel, opts);

    EXPECT_EQ(result.status, RKNStatus::ERROR_INVALID_STEP) << "dt " << dt;
    EXPECT_EQ(result.dtNext, opts.dtMin) << "dt " << dt;
    EXPECT_EQ(y, START_VALUE) << "dt " << dt;
    EXPECT_EQ(v, START_RATE) << "dt " << dt;
    EXPECT_EQ(integrator.time(), START_TIME) << "dt " << dt;
    EXPECT_EQ(integrator.stats().functionEvals, 0U) << "dt " << dt;
    EXPECT_EQ(accelCalls, 0) << "dt " << dt;
  }
}

/* ----------------------------- Accumulator Tests -------------------------- */

/** @test Refuses the step in both accumulation rules and keeps state, time and sample count */
TEST(AccumulatorStepGuardTest, RefusesInvalidStep) {
  for (const double dt : REFUSED_STEPS) {
    Accumulator<double> accumulator{START_VALUE};

    EXPECT_EQ(accumulator.accumulate(START_RATE, dt),
              static_cast<std::uint8_t>(AccumulatorStatus::ERROR_INVALID_DT))
        << "dt " << dt;
    EXPECT_EQ(accumulator.accumulateTrapezoidal(START_RATE, START_RATE, dt),
              static_cast<std::uint8_t>(AccumulatorStatus::ERROR_INVALID_DT))
        << "dt " << dt;
    EXPECT_EQ(accumulator.state(), START_VALUE) << "dt " << dt;
    EXPECT_EQ(accumulator.time(), 0.0) << "dt " << dt;
    EXPECT_EQ(accumulator.sampleCount(), 0U) << "dt " << dt;
  }
}

/** @test Refuses the step for a valid source and keeps the state and that source's totals */
TEST(MultiRateAccumulatorStepGuardTest, RefusesInvalidStep) {
  for (const double dt : REFUSED_STEPS) {
    MultiRateAccumulator<double, 2> accumulator{START_VALUE};

    EXPECT_EQ(accumulator.accumulate(1, START_RATE, dt),
              static_cast<std::uint8_t>(AccumulatorStatus::ERROR_INVALID_DT))
        << "dt " << dt;
    EXPECT_EQ(accumulator.state(), START_VALUE) << "dt " << dt;
    EXPECT_EQ(accumulator.sourceStats(1).sampleCount, 0U) << "dt " << dt;
    EXPECT_EQ(accumulator.sourceStats(1).totalTime, 0.0) << "dt " << dt;
  }
}
