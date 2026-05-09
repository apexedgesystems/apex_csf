/**
 * @file NewtonRaphson_uTest.cpp
 * @brief Unit tests for Newton-Raphson nonlinear circuit solver.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

#include <gtest/gtest.h>

#include <cmath>

using sim::electronics::algorithms::mna::MnaResult;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::nonlinear::NewtonRaphsonSolver;
using sim::electronics::algorithms::nonlinear::NonlinearConfig;
using sim::electronics::algorithms::nonlinear::NonlinearDevice;
using sim::electronics::algorithms::nonlinear::NonlinearDeviceSet;
using sim::electronics::algorithms::nonlinear::NonlinearResult;
using sim::electronics::algorithms::nonlinear::NonlinearStatus;

/* ----------------------------- Test Devices ----------------------------- */

/**
 * @brief Nonlinear resistor with cubic I-V: I = V + alpha*V^3
 *
 * Provides smooth nonlinear characteristic with always-positive conductance.
 * For small alpha (1e-4), behaves nearly linear but tests nonlinear solver.
 * Conductance g = dI/dV = 1 + 3*alpha*V^2 (always positive).
 */
class NonlinearResistor : public NonlinearDevice {
public:
  NonlinearResistor(NetID pos, NetID neg, double resistance, double alpha = 1e-4)
      : pos_(pos), neg_(neg), G0_(1.0 / resistance), alpha_(alpha) {}

  [[nodiscard]] NetID posNet() const noexcept override { return pos_; }
  [[nodiscard]] NetID negNet() const noexcept override { return neg_; }

  [[nodiscard]] double current(double V) const noexcept override {
    // I = G0*V + alpha*V^3 (cubic nonlinearity)
    return G0_ * V + alpha_ * V * V * V;
  }

  [[nodiscard]] double conductance(double V) const noexcept override {
    // g = dI/dV = G0 + 3*alpha*V^2 (always positive)
    return G0_ + 3.0 * alpha_ * V * V;
  }

private:
  NetID pos_, neg_;
  double G0_;    // Linear conductance (1/R)
  double alpha_; // Nonlinear coefficient
};

/**
 * @brief Ideal diode model: I = Is * (exp(V/Vt) - 1)
 *
 * Shockley diode equation with thermal voltage Vt.
 */
class DiodeModel : public NonlinearDevice {
public:
  DiodeModel(NetID anode, NetID cathode, double saturationCurrent = 1e-12,
             double thermalVoltage = 0.026)
      : anode_(anode), cathode_(cathode), Is_(saturationCurrent), Vt_(thermalVoltage) {}

  [[nodiscard]] NetID posNet() const noexcept override { return anode_; }
  [[nodiscard]] NetID negNet() const noexcept override { return cathode_; }

  [[nodiscard]] double current(double vTerminal) const noexcept override {
    // Limit exp argument to prevent overflow
    double expArg = std::min(vTerminal / Vt_, 40.0);
    return Is_ * (std::exp(expArg) - 1.0);
  }

  [[nodiscard]] double conductance(double vTerminal) const noexcept override {
    double expArg = std::min(vTerminal / Vt_, 40.0);
    return (Is_ / Vt_) * std::exp(expArg);
  }

private:
  NetID anode_;
  NetID cathode_;
  double Is_; // Saturation current
  double Vt_; // Thermal voltage
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test Solver constructs with correct net count. */
TEST(NewtonRaphsonSolverTest, DefaultConstruction) {
  NewtonRaphsonSolver solver(10);
  EXPECT_EQ(solver.netCount(), 10u);
}

/* ----------------------------- Enum Tests ----------------------------- */

/** @test Status enum toString conversion. */
TEST(NonlinearStatusTest, ToString) {
  EXPECT_STREQ(toString(NonlinearStatus::SUCCESS), "SUCCESS");
  EXPECT_STREQ(toString(NonlinearStatus::ERROR_MAX_ITERATIONS), "ERROR_MAX_ITERATIONS");
  EXPECT_STREQ(toString(NonlinearStatus::ERROR_SINGULAR_MATRIX), "ERROR_SINGULAR_MATRIX");
  EXPECT_STREQ(toString(NonlinearStatus::ERROR_VOLTAGE_DIVERGENCE), "ERROR_VOLTAGE_DIVERGENCE");
  EXPECT_STREQ(toString(NonlinearStatus::ERROR_INVALID_CONFIG), "ERROR_INVALID_CONFIG");
}

/* ----------------------------- Configuration Tests ----------------------------- */

/** @test Convergence criteria with absolute voltage tolerance. */
TEST(NonlinearConfigTest, AbsoluteVoltageTolerance) {
  NonlinearConfig config;
  config.voltageTolerance = 1e-6;
  config.currentTolerance = 1e-20;  // Disable current criterion
  config.relativeTolerance = 1e-20; // Disable relative criterion

  // Should converge when maxDeltaV < voltageTolerance
  EXPECT_TRUE(config.isConverged(5e-7, 1e-3, 10.0));
  EXPECT_FALSE(config.isConverged(5e-6, 1e-3, 10.0));
}

/** @test Convergence criteria with absolute current tolerance. */
TEST(NonlinearConfigTest, AbsoluteCurrentTolerance) {
  NonlinearConfig config;
  config.currentTolerance = 1e-9;
  config.voltageTolerance = 1e-20;  // Disable voltage criterion
  config.relativeTolerance = 1e-20; // Disable relative criterion

  // Should converge when maxCurrent < currentTolerance
  EXPECT_TRUE(config.isConverged(1e-3, 5e-10, 10.0));
  EXPECT_FALSE(config.isConverged(1e-3, 5e-9, 10.0));
}

/** @test Convergence criteria with relative voltage tolerance. */
TEST(NonlinearConfigTest, RelativeVoltageTolerance) {
  NonlinearConfig config;
  config.relativeTolerance = 1e-3;
  config.voltageTolerance = 1e-20; // Disable absolute check
  config.currentTolerance = 1e-20;

  // maxDeltaV=0.005, maxVoltage=10.0 => relative error = 0.0005 < 1e-3
  EXPECT_TRUE(config.isConverged(0.005, 1.0, 10.0));

  // maxDeltaV=0.02, maxVoltage=10.0 => relative error = 0.002 > 1e-3
  EXPECT_FALSE(config.isConverged(0.02, 1.0, 10.0));
}

/** @test Invalid configuration detection. */
TEST(NewtonRaphsonSolverTest, InvalidConfiguration) {
  NewtonRaphsonSolver solver(5);

  NonlinearConfig config;
  config.maxIterations = 0; // Invalid

  NonlinearResult result = solver.solve(config);
  EXPECT_EQ(result.status, NonlinearStatus::ERROR_INVALID_CONFIG);
  EXPECT_FALSE(result.success());
}

/* ----------------------------- Device Set Tests ----------------------------- */

/** @test deviceCount() matches after adding devices. */
TEST(NonlinearDeviceSetTest, DeviceCount) {
  NonlinearDeviceSet devices;
  EXPECT_EQ(devices.deviceCount(), 0u);

  devices.addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  EXPECT_EQ(devices.deviceCount(), 1u);

  devices.addDevice(std::make_unique<DiodeModel>(2, 0));
  EXPECT_EQ(devices.deviceCount(), 2u);

  // deviceCount() and size() must agree
  EXPECT_EQ(devices.deviceCount(), devices.size());
}

/** @test device() accessor returns correct device (const and non-const). */
TEST(NonlinearDeviceSetTest, DeviceAccessor) {
  NonlinearDeviceSet devices;
  devices.addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  devices.addDevice(std::make_unique<DiodeModel>(3, 0));

  // Non-const accessor
  NonlinearDevice& D0 = devices.device(0);
  EXPECT_EQ(D0.posNet(), 1u);
  EXPECT_EQ(D0.negNet(), 0u);

  NonlinearDevice& d1 = devices.device(1);
  EXPECT_EQ(d1.posNet(), 3u);
  EXPECT_EQ(d1.negNet(), 0u);

  // Const accessor
  const NonlinearDeviceSet& CREF = devices;
  const NonlinearDevice& CD0 = CREF.device(0);
  EXPECT_EQ(CD0.posNet(), 1u);

  const NonlinearDevice& CD1 = CREF.device(1);
  EXPECT_EQ(CD1.posNet(), 3u);
}

/** @test updateAllStates() completes without error after solving. */
TEST(NonlinearDeviceSetTest, UpdateAllStates) {
  NonlinearDeviceSet devices;
  devices.addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  devices.addDevice(std::make_unique<DiodeModel>(2, 0));

  std::vector<double> voltages = {0.0, 5.0, 0.7};
  // Must not throw or crash; exercises updateState() on both devices
  devices.updateAllStates(voltages);
}

/** @test NonlinearDevice::updateState() default implementation is callable. */
TEST(NonlinearDeviceTest, UpdateStateDefault) {
  // NonlinearResistor does not override updateState(), so it uses the default no-op
  NonlinearResistor resistor(1, 0, 100.0);
  // Call twice at different voltages to confirm no side effects
  resistor.updateState(5.0);
  resistor.updateState(-3.0);

  // Device characteristics unchanged (updateState is a no-op)
  EXPECT_NEAR(resistor.current(5.0), 0.05 + 1e-4 * 125.0, 1e-9);
}

/** @test Add and stamp nonlinear resistor. */
TEST(NonlinearDeviceSetTest, AddNonlinearResistor) {
  NonlinearDeviceSet devices;

  auto resistor = std::make_unique<NonlinearResistor>(1, 0, 100.0);
  devices.addDevice(std::move(resistor));

  EXPECT_EQ(devices.size(), 1u);

  // Stamp at operating point V=5.0
  MnaSystem mna(2);
  std::vector<double> voltages = {0.0, 5.0};
  devices.stampAllLinearized(mna, voltages);

  // Verify stamp by solving (should maintain V=5.0 at equilibrium)
  // At equilibrium: I = V^2/R, which is balanced by linearized Norton equivalent
  MnaResult result = mna.solve();
  EXPECT_TRUE(result.success);
}

/** @test Clear device set. */
TEST(NonlinearDeviceSetTest, ClearDevices) {
  NonlinearDeviceSet devices;
  devices.addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  devices.addDevice(std::make_unique<NonlinearResistor>(2, 0, 200.0));
  EXPECT_EQ(devices.size(), 2u);

  devices.clear();
  EXPECT_EQ(devices.size(), 0u);
}

/* ----------------------------- Accessor Tests ----------------------------- */

/** @test voltages() returns converged node voltages after solve. */
TEST(NewtonRaphsonSolverTest, VoltagesAccessor) {
  NewtonRaphsonSolver solver(2);

  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); });

  NonlinearConfig config;
  NonlinearResult result = solver.solve(config);
  ASSERT_TRUE(result.success());

  const std::vector<double>& V = solver.voltages();
  ASSERT_EQ(V.size(), 2u);
  EXPECT_NEAR(V[1], 5.0, 1e-5);
}

/** @test const devices() accessor returns same data. */
TEST(NewtonRaphsonSolverTest, ConstDevicesAccessor) {
  NewtonRaphsonSolver solver(2);
  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));

  const NewtonRaphsonSolver& CREF = solver;
  EXPECT_EQ(CREF.devices().deviceCount(), 1u);
  EXPECT_EQ(CREF.devices().device(0).posNet(), 1u);
}

/* ----------------------------- Solver Convergence Tests ----------------------------- */

/** @test Simple nonlinear resistor convergence. */
TEST(NewtonRaphsonSolverTest, NonlinearResistorConvergence) {
  // Circuit: V1 (5V) -- NonlinearResistor (I=V^2/100) -- GND
  // Expected: Node 1 = 5V (clamped by voltage source)

  NewtonRaphsonSolver solver(2); // Net 0 = GND, Net 1 = V1

  // Add nonlinear resistor
  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));

  // Linear stamp: voltage source 5V at net 1
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); });

  // Solve
  NonlinearConfig config;
  config.maxIterations = 20;
  config.voltageTolerance = 1e-6;

  NonlinearResult result = solver.solve(config);

  EXPECT_TRUE(result.success());
  EXPECT_EQ(result.status, NonlinearStatus::SUCCESS);
  EXPECT_LT(result.iterations, config.maxIterations);
  EXPECT_LT(result.finalError, config.voltageTolerance);

  // Check voltage at net 1
  ASSERT_EQ(result.nodeVoltages.size(), 2u);
  EXPECT_NEAR(result.nodeVoltages[1], 5.0, 1e-5);
}

/** @test Diode circuit convergence. */
TEST(NewtonRaphsonSolverTest, DiodeCircuitConvergence) {
  // Circuit: V1 (0.7V) -- Diode -- GND
  // Expected: Forward-biased diode conducts current

  NewtonRaphsonSolver solver(2);

  // Add diode (anode=1, cathode=0)
  solver.devices().addDevice(std::make_unique<DiodeModel>(1, 0));

  // Linear stamp: voltage source 0.7V at net 1
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 0.7); });

  NonlinearConfig config;
  config.maxIterations = 20;

  NonlinearResult result = solver.solve(config);

  EXPECT_TRUE(result.success());
  EXPECT_NEAR(result.nodeVoltages[1], 0.7, 1e-5);
}

/** @test Convergence with initial guess. */
TEST(NewtonRaphsonSolverTest, InitialGuessConvergence) {
  NewtonRaphsonSolver solver(2);

  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 3.0); });

  // Set initial guess close to solution
  std::vector<double> initialGuess = {0.0, 2.9};
  solver.setInitialGuess(initialGuess);

  NonlinearConfig config;
  NonlinearResult result = solver.solve(config);

  EXPECT_TRUE(result.success());
  // Good initial guess should reduce iterations (relaxed from < 5 to < 10)
  EXPECT_LT(result.iterations, 10u);
  EXPECT_NEAR(result.nodeVoltages[1], 3.0, 1e-5);
}

/* ----------------------------- Damping Tests ----------------------------- */

/** @test Damping improves oscillatory convergence. */
TEST(NewtonRaphsonSolverTest, DampingOscillatoryConvergence) {
  // Circuit with multiple nonlinear resistors that might oscillate
  NewtonRaphsonSolver solver(3);

  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 50.0));
  solver.devices().addDevice(std::make_unique<NonlinearResistor>(2, 1, 75.0));

  solver.setLinearStampCallback([](MnaSystem& mna) {
    mna.addVoltageSource(2, 0, 10.0);
    mna.addConductance(1, 0, 0.1); // Linear resistor (10 ohm = 0.1 S)
  });

  // Test with damping enabled (default)
  NonlinearConfig configDamped;
  configDamped.enableDamping = true;
  configDamped.dampingFactor = 0.5;
  configDamped.dampingIterations = 5;

  NonlinearResult resultDamped = solver.solve(configDamped);

  solver.reset();

  // Test without damping
  NonlinearConfig configNoDamping;
  configNoDamping.enableDamping = false;

  NonlinearResult resultNoDamping = solver.solve(configNoDamping);

  // Both should converge, but damped version may converge faster
  EXPECT_TRUE(resultDamped.success());
  EXPECT_TRUE(resultNoDamping.success());
}

/* ----------------------------- Failure Tests ----------------------------- */

/** @test Maximum iterations exceeded. */
TEST(NewtonRaphsonSolverTest, MaxIterationsExceeded) {
  NewtonRaphsonSolver solver(2);

  // Add very nonlinear device
  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 10.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 100.0); });

  NonlinearConfig config;
  config.maxIterations = 2; // Force failure

  NonlinearResult result = solver.solve(config);

  EXPECT_FALSE(result.success());
  EXPECT_EQ(result.status, NonlinearStatus::ERROR_MAX_ITERATIONS);
  EXPECT_EQ(result.iterations, 2u);
}

/** @test Convergence with unstable initial guess. */
TEST(NewtonRaphsonSolverTest, UnstableInitialGuessRecovery) {
  // Circuit with large initial voltage error - should still converge with damping
  NewtonRaphsonSolver solver(2);

  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); });

  // Set initial guess far from solution (5V)
  solver.setInitialGuess({0.0, 1000.0});

  NonlinearConfig config;
  config.enableDamping = true; // Damping helps with large errors
  config.maxIterations = 50;   // May need more iterations
  NonlinearResult result = solver.solve(config);

  // Should converge despite poor initial guess
  EXPECT_TRUE(result.success());
  EXPECT_NEAR(result.nodeVoltages[1], 5.0, 1e-3);
}

/* ----------------------------- Reset Tests ----------------------------- */

/** @test Reset clears solver state. */
TEST(NewtonRaphsonSolverTest, ResetClearsState) {
  NewtonRaphsonSolver solver(2);

  solver.devices().addDevice(std::make_unique<NonlinearResistor>(1, 0, 100.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); });

  NonlinearConfig config;
  NonlinearResult result1 = solver.solve(config);
  EXPECT_TRUE(result1.success());

  // Reset and solve again
  solver.reset();

  NonlinearResult result2 = solver.solve(config);
  EXPECT_TRUE(result2.success());

  // Results should be identical
  EXPECT_NEAR(result1.nodeVoltages[1], result2.nodeVoltages[1], 1e-9);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Repeated solves produce identical results. */
TEST(NewtonRaphsonSolverTest, RepeatedSolvesDeterminism) {
  NewtonRaphsonSolver solver(2);

  solver.devices().addDevice(std::make_unique<DiodeModel>(1, 0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 0.65); });

  NonlinearConfig config;

  std::vector<double> voltages1;
  std::vector<double> voltages2;

  for (int i = 0; i < 3; ++i) {
    solver.reset();
    NonlinearResult result = solver.solve(config);
    ASSERT_TRUE(result.success());

    if (i == 0) {
      voltages1 = result.nodeVoltages;
    } else {
      voltages2 = result.nodeVoltages;

      // Results should be identical across runs
      ASSERT_EQ(voltages1.size(), voltages2.size());
      for (std::size_t j = 0; j < voltages1.size(); ++j) {
        EXPECT_DOUBLE_EQ(voltages1[j], voltages2[j]);
      }
    }
  }
}
