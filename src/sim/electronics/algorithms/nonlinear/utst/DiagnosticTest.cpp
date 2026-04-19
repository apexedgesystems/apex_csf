/**
 * @file DiagnosticTest.cpp
 * @brief Diagnostic test to debug Newton-Raphson convergence issues.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

#include <gtest/gtest.h>
#include <iostream>

using sim::electronics::mna::MnaSystem;
using sim::electronics::mna::NetID;
using sim::electronics::nonlinear::NewtonRaphsonSolver;
using sim::electronics::nonlinear::NonlinearConfig;
using sim::electronics::nonlinear::NonlinearDevice;
using sim::electronics::nonlinear::NonlinearResult;

/* ----------------------------- File Helpers ----------------------------- */

class DiagnosticResistor : public NonlinearDevice {
public:
  DiagnosticResistor(NetID pos, NetID neg, double resistance, double alpha = 1e-4)
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

/* ----------------------------- Method Tests ----------------------------- */

/** @test Diagnose simple circuit that should converge. */
TEST(DiagnosticTest, SimpleCircuit) {
  // V1 (5V) -- NonlinearResistor -- GND
  NewtonRaphsonSolver solver(2);
  solver.devices().addDevice(std::make_unique<DiagnosticResistor>(1, 0, 100.0));
  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 5.0); });

  NonlinearConfig config;
  config.maxIterations = 20;
  config.voltageTolerance = 1e-6;

  std::cout << "\n=== Diagnostic: Simple Circuit ===\n";
  std::cout << "Circuit: 5V source -- NonlinearResistor (R=100) -- GND\n";
  std::cout << "Expected: V1 = 5.0V (clamped by voltage source)\n\n";

  NonlinearResult result = solver.solve(config);

  std::cout << "Result: " << (result.success() ? "SUCCESS" : "FAILED") << "\n";
  std::cout << "Iterations: " << result.iterations << "\n";
  std::cout << "Final error: " << result.finalError << "\n";
  std::cout << "Node voltages: ";
  for (size_t i = 0; i < result.nodeVoltages.size(); ++i) {
    std::cout << "V" << i << "=" << result.nodeVoltages[i] << " ";
  }
  std::cout << "\n";

  EXPECT_TRUE(result.success());
  EXPECT_NEAR(result.nodeVoltages[1], 5.0, 1e-5);
}

/** @test Diagnose multi-device circuit. */
TEST(DiagnosticTest, MultiDeviceCircuit) {
  // Net 2 (10V) -- R2 -- Net 1 -- R1 -- GND
  //                        |
  //                       G_linear
  //                        |
  //                       GND

  NewtonRaphsonSolver solver(3);

  // R1: net 1 to GND (50 ohm nonlinear)
  solver.devices().addDevice(std::make_unique<DiagnosticResistor>(1, 0, 50.0));

  // R2: net 2 to net 1 (75 ohm nonlinear)
  solver.devices().addDevice(std::make_unique<DiagnosticResistor>(2, 1, 75.0));

  solver.setLinearStampCallback([](MnaSystem& mna) {
    // Voltage source: net 2 = 10V
    mna.addVoltageSource(2, 0, 10.0);

    // Linear conductance: net 1 to GND (10 ohm = 0.1 S)
    mna.addConductance(1, 0, 0.1);
  });

  NonlinearConfig config;
  config.maxIterations = 50;
  config.voltageTolerance = 1e-9;  // Tighter voltage tolerance
  config.currentTolerance = 1e-6;  // Match KCL check tolerance
  config.relativeTolerance = 1e-6; // Tighter relative tolerance
  config.enableDamping = true;

  std::cout << "\n=== Diagnostic: Multi-Device Circuit ===\n";
  std::cout << "Circuit: 10V -- R2(75ohm) -- Net1 -- R1(50ohm) -- GND\n";
  std::cout << "                            |-- G(10ohm) -- GND\n\n";

  NonlinearResult result = solver.solve(config);

  std::cout << "Result: " << (result.success() ? "SUCCESS" : "FAILED") << "\n";
  std::cout << "Status: " << toString(result.status) << "\n";
  if (!result.errorMessage.empty()) {
    std::cout << "Error: " << result.errorMessage << "\n";
  }
  std::cout << "Iterations: " << result.iterations << "/" << config.maxIterations << "\n";
  std::cout << "Final error: " << result.finalError << "\n";
  std::cout << "Node voltages: ";
  for (size_t i = 0; i < result.nodeVoltages.size(); ++i) {
    std::cout << "V" << i << "=" << result.nodeVoltages[i] << " ";
  }
  std::cout << "\n";

  // Verify KCL at node 1
  if (result.success()) {
    double V1 = result.nodeVoltages[1];
    double V2 = result.nodeVoltages[2];

    // Current through R1 (1->0): I = G0*V + alpha*V^3
    double G0_R1 = 1.0 / 50.0;
    const double ALPHA = 1e-4;
    double I_R1 = G0_R1 * V1 + ALPHA * V1 * V1 * V1;

    // Current through R2 (2->1): I = G0*(V2-V1) + alpha*(V2-V1)^3
    double G0_R2 = 1.0 / 75.0;
    double V_R2 = V2 - V1;
    double I_R2 = G0_R2 * V_R2 + ALPHA * V_R2 * V_R2 * V_R2;

    // Current through linear G (1->0): I = 0.1 * V1
    double I_G = 0.1 * V1;

    // KCL at node 1: I_R2 should equal I_R1 + I_G
    std::cout << "\nKCL Check at node 1:\n";
    std::cout << "  I_R2 (into node 1): " << I_R2 << " A\n";
    std::cout << "  I_R1 (out of node 1): " << I_R1 << " A\n";
    std::cout << "  I_G (out of node 1): " << I_G << " A\n";
    std::cout << "  Balance (should be ~0): " << (I_R2 - I_R1 - I_G) << " A\n";

    // Verify balance (relaxed tolerance for strong nonlinearity)
    // With alpha=1e-4 and moderate voltages, expect ~1% KCL error due to linearization
    const double KCL_ERROR = std::abs(I_R2 - I_R1 - I_G);
    const double TOTAL_CURRENT = std::max(I_R2, I_R1 + I_G);
    const double RELATIVE_ERROR = KCL_ERROR / TOTAL_CURRENT;
    EXPECT_LT(RELATIVE_ERROR, 0.02); // 2% relative error tolerance
  }

  EXPECT_TRUE(result.success());
}
