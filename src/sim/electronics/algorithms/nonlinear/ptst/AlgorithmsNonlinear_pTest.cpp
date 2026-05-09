/**
 * @file AlgorithmsNonlinear_pTest.cpp
 * @brief Performance tests for Newton-Raphson nonlinear circuit solver.
 *
 * Measures NR iteration throughput and convergence behavior for small nonlinear
 * circuits representative of production workloads. Two categories:
 *
 * 1. NR iteration throughput: full solve() calls on small circuits with diodes
 *    and nonlinear resistors, measuring end-to-end NR solve rate.
 * 2. Convergence: iteration count to converge for standard test circuits
 *    (diode voltage divider, multi-diode chain, resistor divider with
 *    nonlinear load).
 *
 * Usage:
 *   ./AlgorithmsNonlinear_PTEST --csv results.csv
 *   ./AlgorithmsNonlinear_PTEST --quick
 *   ./AlgorithmsNonlinear_PTEST --profile gperf --gtest_filter="*Diode*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::NetID;
using sim::electronics::algorithms::nonlinear::NewtonRaphsonSolver;
using sim::electronics::algorithms::nonlinear::NonlinearConfig;
using sim::electronics::algorithms::nonlinear::NonlinearDevice;
using sim::electronics::algorithms::nonlinear::NonlinearResult;

/* ----------------------------- Test Devices ----------------------------- */

/**
 * @brief Shockley diode model: I = Is * (exp(V/Vt) - 1).
 *
 * Standard exponential diode for NR convergence testing.
 */
class PerfDiodeModel : public NonlinearDevice {
public:
  PerfDiodeModel(NetID anode, NetID cathode, double saturationCurrent = 1e-12,
                 double thermalVoltage = 0.026)
      : anode_(anode), cathode_(cathode), Is_(saturationCurrent), Vt_(thermalVoltage) {}

  [[nodiscard]] NetID posNet() const noexcept override { return anode_; }
  [[nodiscard]] NetID negNet() const noexcept override { return cathode_; }

  [[nodiscard]] double current(double vTerminal) const noexcept override {
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
  double Is_;
  double Vt_;
};

/**
 * @brief Nonlinear resistor with cubic I-V: I = G0*V + alpha*V^3.
 *
 * Always-positive conductance for well-conditioned NR testing.
 */
class PerfNonlinearResistor : public NonlinearDevice {
public:
  PerfNonlinearResistor(NetID pos, NetID neg, double resistance, double alpha = 1e-4)
      : pos_(pos), neg_(neg), G0_(1.0 / resistance), alpha_(alpha) {}

  [[nodiscard]] NetID posNet() const noexcept override { return pos_; }
  [[nodiscard]] NetID negNet() const noexcept override { return neg_; }

  [[nodiscard]] double current(double V) const noexcept override {
    return G0_ * V + alpha_ * V * V * V;
  }

  [[nodiscard]] double conductance(double V) const noexcept override {
    return G0_ + 3.0 * alpha_ * V * V;
  }

private:
  NetID pos_, neg_;
  double G0_;
  double alpha_;
};

/* ----------------------------- NewtonRaphson Throughput ----------------------------- */

/**
 * @brief NR solve throughput for single-diode circuit.
 *
 * Circuit: 5V source -- 1k resistor -- diode -- GND.
 * This is the simplest nonlinear circuit requiring NR iteration.
 * Measures full solve() call rate including MNA assembly and factorization.
 */
PERF_TEST(AlgorithmsNonlinearPerf, NR_SingleDiodeSolve) {
  UB_PERF_GUARD(perf);

  const std::size_t NET_COUNT = 3; // GND=0, Vsrc=1, DiodeAnode=2
  volatile double sink = 0.0;

  std::printf("\n=== NR Solve: Single Diode (Vsrc + R + D) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NewtonRaphsonSolver solver(NET_COUNT);
      solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 0));
      solver.setLinearStampCallback([](MnaSystem& mna) {
        mna.addVoltageSource(1, 0, 5.0);
        mna.addConductance(1, 2, 1.0 / 1000.0); // 1k resistor
      });
      NonlinearConfig config;
      NonlinearResult result = solver.solve(config);
      sink = result.nodeVoltages[2];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NewtonRaphsonSolver solver(NET_COUNT);
        solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 0));
        solver.setLinearStampCallback([](MnaSystem& mna) {
          mna.addVoltageSource(1, 0, 5.0);
          mna.addConductance(1, 2, 1.0 / 1000.0);
        });
        NonlinearConfig config;
        NonlinearResult r = solver.solve(config);
        sink = r.nodeVoltages[2];
      },
      "nr_single_diode");

  std::printf("  Single diode: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief NR solve throughput for diode voltage divider.
 *
 * Circuit: 5V source -- R1(1k) -- node2 -- R2(1k) -- diode -- GND.
 * Two-node nonlinear circuit with resistive divider feeding a diode.
 */
PERF_TEST(AlgorithmsNonlinearPerf, NR_DiodeVoltageDivider) {
  UB_PERF_GUARD(perf);

  const std::size_t NET_COUNT = 4; // GND=0, Vsrc=1, Mid=2, DiodeAnode=3
  volatile double sink = 0.0;

  std::printf("\n=== NR Solve: Diode Voltage Divider (Vsrc + R1 + R2 + D) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NewtonRaphsonSolver solver(NET_COUNT);
      solver.devices().addDevice(std::make_unique<PerfDiodeModel>(3, 0));
      solver.setLinearStampCallback([](MnaSystem& mna) {
        mna.addVoltageSource(1, 0, 5.0);
        mna.addConductance(1, 2, 1.0 / 1000.0); // R1 = 1k
        mna.addConductance(2, 3, 1.0 / 1000.0); // R2 = 1k
      });
      NonlinearConfig config;
      NonlinearResult result = solver.solve(config);
      sink = result.nodeVoltages[3];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NewtonRaphsonSolver solver(NET_COUNT);
        solver.devices().addDevice(std::make_unique<PerfDiodeModel>(3, 0));
        solver.setLinearStampCallback([](MnaSystem& mna) {
          mna.addVoltageSource(1, 0, 5.0);
          mna.addConductance(1, 2, 1.0 / 1000.0);
          mna.addConductance(2, 3, 1.0 / 1000.0);
        });
        NonlinearConfig config;
        NonlinearResult r = solver.solve(config);
        sink = r.nodeVoltages[3];
      },
      "nr_diode_divider");

  std::printf("  Diode divider: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief NR solve throughput for multi-diode chain.
 *
 * Circuit: 5V source -- R(1k) -- D1 -- D2 -- D3 -- GND.
 * Three series diodes require more NR iterations due to coupled exponentials.
 * Stresses convergence with multiple strongly-nonlinear devices.
 */
PERF_TEST(AlgorithmsNonlinearPerf, NR_ThreeDiodeChain) {
  UB_PERF_GUARD(perf);

  const std::size_t NET_COUNT = 5; // GND=0, Vsrc=1, D1a=2, D2a=3, D3a=4
  volatile double sink = 0.0;

  std::printf("\n=== NR Solve: Three-Diode Chain (Vsrc + R + D1 + D2 + D3) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NewtonRaphsonSolver solver(NET_COUNT);
      solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 3)); // D1
      solver.devices().addDevice(std::make_unique<PerfDiodeModel>(3, 4)); // D2
      solver.devices().addDevice(std::make_unique<PerfDiodeModel>(4, 0)); // D3
      solver.setLinearStampCallback([](MnaSystem& mna) {
        mna.addVoltageSource(1, 0, 5.0);
        mna.addConductance(1, 2, 1.0 / 1000.0); // R = 1k
      });
      NonlinearConfig config;
      NonlinearResult result = solver.solve(config);
      sink = result.nodeVoltages[2];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NewtonRaphsonSolver solver(NET_COUNT);
        solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 3));
        solver.devices().addDevice(std::make_unique<PerfDiodeModel>(3, 4));
        solver.devices().addDevice(std::make_unique<PerfDiodeModel>(4, 0));
        solver.setLinearStampCallback([](MnaSystem& mna) {
          mna.addVoltageSource(1, 0, 5.0);
          mna.addConductance(1, 2, 1.0 / 1000.0);
        });
        NonlinearConfig config;
        NonlinearResult r = solver.solve(config);
        sink = r.nodeVoltages[2];
      },
      "nr_three_diode_chain");

  std::printf("  Three-diode chain: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief NR solve throughput for nonlinear resistor network.
 *
 * Circuit: 10V source -- NLR1(50 ohm) -- node2 -- NLR2(75 ohm) -- GND
 *                                         node2 -- R_lin(10 ohm) -- GND
 * Mixed linear and nonlinear elements test MNA assembly efficiency.
 */
PERF_TEST(AlgorithmsNonlinearPerf, NR_NonlinearResistorNetwork) {
  UB_PERF_GUARD(perf);

  const std::size_t NET_COUNT = 3; // GND=0, Vsrc=1, Mid=2
  volatile double sink = 0.0;

  std::printf("\n=== NR Solve: Nonlinear Resistor Network (Vsrc + NLR1 + NLR2 + Rlin) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      NewtonRaphsonSolver solver(NET_COUNT);
      solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(1, 2, 50.0));
      solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(2, 0, 75.0));
      solver.setLinearStampCallback([](MnaSystem& mna) {
        mna.addVoltageSource(1, 0, 10.0);
        mna.addConductance(2, 0, 1.0 / 10.0); // 10 ohm linear
      });
      NonlinearConfig config;
      NonlinearResult result = solver.solve(config);
      sink = result.nodeVoltages[2];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        NewtonRaphsonSolver solver(NET_COUNT);
        solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(1, 2, 50.0));
        solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(2, 0, 75.0));
        solver.setLinearStampCallback([](MnaSystem& mna) {
          mna.addVoltageSource(1, 0, 10.0);
          mna.addConductance(2, 0, 1.0 / 10.0);
        });
        NonlinearConfig config;
        NonlinearResult r = solver.solve(config);
        sink = r.nodeVoltages[2];
      },
      "nr_nonlinear_resistor_network");

  std::printf("  NL resistor network: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- Convergence ----------------------------- */

/**
 * @brief Convergence: iterations for single diode circuit.
 *
 * Measures how many NR iterations are needed for a forward-biased diode.
 * Reports iteration count as the primary metric -- fewer is better.
 */
PERF_TEST(AlgorithmsNonlinearPerf, Convergence_SingleDiode) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Convergence: Single Diode ===\n");

  NewtonRaphsonSolver solver(3);
  solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 0));
  solver.setLinearStampCallback([](MnaSystem& mna) {
    mna.addVoltageSource(1, 0, 5.0);
    mna.addConductance(1, 2, 1.0 / 1000.0);
  });

  NonlinearConfig config;
  config.maxIterations = 50;

  NonlinearResult result = solver.solve(config);

  ASSERT_TRUE(result.success()) << "Single diode failed to converge";
  std::printf("  Iterations: %zu  Final error: %.2e  V_diode: %.6f V\n", result.iterations,
              result.finalError, result.nodeVoltages[2]);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      NonlinearResult r = solver.solve(config);
      (void)r;
    }
  });

  auto perfResult = perf.throughputLoop(
      [&] {
        solver.reset();
        NonlinearResult r = solver.solve(config);
        (void)r;
      },
      "convergence_single_diode");

  std::printf("  Re-solve rate: %8.0f solves/s  (%.1f us/solve)\n", perfResult.callsPerSecond,
              perfResult.stats.median);
}

/**
 * @brief Convergence: iterations for three-diode chain.
 *
 * Three series diodes are harder to converge than one. Reports iteration
 * count and per-node voltages to verify physical correctness.
 */
PERF_TEST(AlgorithmsNonlinearPerf, Convergence_ThreeDiodeChain) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Convergence: Three-Diode Chain ===\n");

  NewtonRaphsonSolver solver(5);
  solver.devices().addDevice(std::make_unique<PerfDiodeModel>(2, 3)); // D1
  solver.devices().addDevice(std::make_unique<PerfDiodeModel>(3, 4)); // D2
  solver.devices().addDevice(std::make_unique<PerfDiodeModel>(4, 0)); // D3
  solver.setLinearStampCallback([](MnaSystem& mna) {
    mna.addVoltageSource(1, 0, 5.0);
    mna.addConductance(1, 2, 1.0 / 1000.0);
  });

  NonlinearConfig config;
  config.maxIterations = 50;

  NonlinearResult result = solver.solve(config);

  ASSERT_TRUE(result.success()) << "Three-diode chain failed to converge";
  std::printf("  Iterations: %zu  Final error: %.2e\n", result.iterations, result.finalError);
  for (std::size_t n = 1; n < result.nodeVoltages.size(); ++n) {
    std::printf("    Node %zu: %.6f V\n", n, result.nodeVoltages[n]);
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      NonlinearResult r = solver.solve(config);
      (void)r;
    }
  });

  auto perfResult = perf.throughputLoop(
      [&] {
        solver.reset();
        NonlinearResult r = solver.solve(config);
        (void)r;
      },
      "convergence_three_diode_chain");

  std::printf("  Re-solve rate: %8.0f solves/s  (%.1f us/solve)\n", perfResult.callsPerSecond,
              perfResult.stats.median);
}

/**
 * @brief Convergence: nonlinear resistor network with good initial guess.
 *
 * Tests that providing a close initial guess reduces iteration count.
 * Compares cold-start vs warm-start convergence.
 */
PERF_TEST(AlgorithmsNonlinearPerf, Convergence_WarmStart) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Convergence: Warm Start vs Cold Start ===\n");

  NonlinearConfig config;
  config.maxIterations = 50;

  // Cold start (zero initial guess)
  {
    NewtonRaphsonSolver solver(3);
    solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(1, 2, 50.0));
    solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(2, 0, 75.0));
    solver.setLinearStampCallback([](MnaSystem& mna) {
      mna.addVoltageSource(1, 0, 10.0);
      mna.addConductance(2, 0, 1.0 / 10.0);
    });

    NonlinearResult result = solver.solve(config);
    ASSERT_TRUE(result.success()) << "Cold start failed to converge";
    std::printf("  Cold start: %zu iterations  error=%.2e  V2=%.6f V\n", result.iterations,
                result.finalError, result.nodeVoltages[2]);
  }

  // Warm start (initial guess near solution)
  {
    NewtonRaphsonSolver solver(3);
    solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(1, 2, 50.0));
    solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(2, 0, 75.0));
    solver.setLinearStampCallback([](MnaSystem& mna) {
      mna.addVoltageSource(1, 0, 10.0);
      mna.addConductance(2, 0, 1.0 / 10.0);
    });

    // Approximate solution: V1=10V, V2 ~ 5V (rough divider estimate)
    solver.setInitialGuess({0.0, 10.0, 5.0});

    NonlinearResult result = solver.solve(config);
    ASSERT_TRUE(result.success()) << "Warm start failed to converge";
    std::printf("  Warm start: %zu iterations  error=%.2e  V2=%.6f V\n", result.iterations,
                result.finalError, result.nodeVoltages[2]);
  }

  // Throughput for warm-start re-solve (typical transient simulation pattern)
  NewtonRaphsonSolver solver(3);
  solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(1, 2, 50.0));
  solver.devices().addDevice(std::make_unique<PerfNonlinearResistor>(2, 0, 75.0));
  solver.setLinearStampCallback([](MnaSystem& mna) {
    mna.addVoltageSource(1, 0, 10.0);
    mna.addConductance(2, 0, 1.0 / 10.0);
  });
  solver.setInitialGuess({0.0, 10.0, 5.0});

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      solver.reset();
      solver.setInitialGuess({0.0, 10.0, 5.0});
      NonlinearResult r = solver.solve(config);
      (void)r;
    }
  });

  auto perfResult = perf.throughputLoop(
      [&] {
        solver.reset();
        solver.setInitialGuess({0.0, 10.0, 5.0});
        NonlinearResult r = solver.solve(config);
        (void)r;
      },
      "convergence_warm_start");

  std::printf("  Warm re-solve rate: %8.0f solves/s  (%.1f us/solve)\n", perfResult.callsPerSecond,
              perfResult.stats.median);
}

PERF_MAIN()
