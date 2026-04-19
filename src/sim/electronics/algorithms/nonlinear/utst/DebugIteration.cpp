/**
 * @file DebugIteration.cpp
 * @brief Debug Newton-Raphson iteration step-by-step.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>

using sim::electronics::mna::MnaSystem;
using sim::electronics::mna::NetID;
using sim::electronics::nonlinear::NewtonRaphsonSolver;
using sim::electronics::nonlinear::NonlinearConfig;
using sim::electronics::nonlinear::NonlinearDevice;
using sim::electronics::nonlinear::NonlinearResult;

/* ----------------------------- File Helpers ----------------------------- */

class DebugResistor : public NonlinearDevice {
public:
  DebugResistor(NetID pos, NetID neg, double resistance, const char* name, double alpha = 1e-4)
      : pos_(pos), neg_(neg), G0_(1.0 / resistance), alpha_(alpha), name_(name) {}

  [[nodiscard]] NetID posNet() const noexcept override { return pos_; }
  [[nodiscard]] NetID negNet() const noexcept override { return neg_; }

  [[nodiscard]] double current(double V) const noexcept override {
    return G0_ * V + alpha_ * V * V * V;
  }

  [[nodiscard]] double conductance(double V) const noexcept override {
    return G0_ + 3.0 * alpha_ * V * V;
  }

  const char* name() const { return name_; }

private:
  NetID pos_, neg_;
  double G0_;
  double alpha_;
  const char* name_;
};

/* ----------------------------- Method Tests ----------------------------- */

/** @test Debug detailed iteration behavior. */
TEST(DebugIteration, ManualIteration) {
  std::cout << "\n=== MANUAL NEWTON-RAPHSON DEBUG ===\n\n";

  // Simple circuit: V1 (1V) -- R1 -- GND
  std::cout << "Circuit: V1 (1V) -- R1(100ohm, cubic) -- GND\n";
  std::cout << "Expected: V2 = 1V (enforced), should converge immediately\n\n";

  NewtonRaphsonSolver solver(2); // 0=GND, 1=unused, 1=V1

  // Add one device
  solver.devices().addDevice(std::make_unique<DebugResistor>(1, 0, 100.0, "R1"));

  solver.setLinearStampCallback([](MnaSystem& mna) { mna.addVoltageSource(1, 0, 1.0); });

  // Manually iterate and print details
  NonlinearConfig config;
  config.maxIterations = 5;
  config.voltageTolerance = 1e-6;
  config.enableDamping = false;

  NonlinearResult result = solver.solve(config);

  std::cout << "\nFinal result:\n";
  std::cout << "  Status: " << (result.success() ? "SUCCESS" : "FAILED") << "\n";
  std::cout << "  Iterations: " << result.iterations << "\n";
  std::cout << "  Final error: " << result.finalError << "\n";
  std::cout << "  Voltages: ";
  for (size_t i = 0; i < result.nodeVoltages.size(); ++i) {
    std::cout << "V" << i << "=" << std::fixed << std::setprecision(6) << result.nodeVoltages[i]
              << " ";
  }
  std::cout << "\n";

  EXPECT_TRUE(result.success());
  EXPECT_NEAR(result.nodeVoltages[1], 1.0, 1e-5);
}
