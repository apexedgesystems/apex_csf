/**
 * @file Integration_pTest.cpp
 * @brief Performance tests for ODE integrators.
 *
 * Measures:
 *  - ExplicitEuler scalar step throughput (1 function eval)
 *  - RungeKutta4 scalar / 3D / 6D step throughput (4 function evals)
 *  - RungeKutta45 adaptive step throughput (6-7 function evals)
 *  - Leapfrog scalar / 3D step throughput (symplectic, 2 accel evals)
 *  - RKN4 scalar step throughput (4 evals for 2nd-order ODEs)
 *  - Full-trajectory RK4 sustained throughput
 *
 * Usage:
 *   ./Integration_PTEST --csv results.csv
 *   ./Integration_PTEST --quick
 *   ./Integration_PTEST --profile perf --gtest_filter="*RK4*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/math/integration/inc/ExplicitEuler.hpp"
#include "src/utilities/math/integration/inc/Leapfrog.hpp"
#include "src/utilities/math/integration/inc/RungeKutta4.hpp"
#include "src/utilities/math/integration/inc/RungeKutta45.hpp"
#include "src/utilities/math/integration/inc/RungeKuttaNystrom.hpp"
#include "src/utilities/math/integration/inc/StateVector.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

namespace ub = vernier::bench;

using apex::math::integration::EulerOptions;
using apex::math::integration::ExplicitEuler;
using apex::math::integration::Leapfrog;
using apex::math::integration::RK45Status;
using apex::math::integration::RKN4;
using apex::math::integration::RungeKutta4;
using apex::math::integration::RungeKutta45;
using apex::math::integration::RungeKutta45Options;
using apex::math::integration::RungeKutta4Options;
using apex::math::integration::StateVector;

/* ----------------------------- Scalar ODE Tests ----------------------------- */

/**
 * @brief ExplicitEuler single-step throughput (scalar).
 *
 * Measures the raw step overhead for the simplest integrator.
 * Establishes baseline for CRTP dispatch overhead.
 */
PERF_TEST(ExplicitEuler, ScalarThroughput) {
  UB_PERF_GUARD(perf);

  ExplicitEuler<double> integrator;
  double y = 1.0;

  // Simple exponential decay: dy/dt = -y
  auto f = [](const double& state, double /*t*/) { return -state; };

  integrator.initialize(f, y, 0.0, EulerOptions{});

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(y, 0.01, EulerOptions{});
      // Prevent optimization
      if (y == 0.0) {
        y = 1.0;
      }
    }
    y = 1.0; // Reset
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(y, 0.01, EulerOptions{});
        sink = y;
      },
      "euler_step");

  std::printf("\nExplicitEuler scalar: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);

  // Euler should be extremely fast (1 function eval)
}

/**
 * @brief RungeKutta4 single-step throughput (scalar).
 *
 * Gold standard 4th-order method. Four function evaluations per step.
 */
PERF_TEST(RungeKutta4, ScalarThroughput) {
  UB_PERF_GUARD(perf);

  RungeKutta4<double> integrator;
  double y = 1.0;

  auto f = [](const double& state, double /*t*/) { return -state; };

  integrator.initialize(f, y, 0.0, RungeKutta4Options{});

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(y, 0.01, RungeKutta4Options{});
      if (y == 0.0) {
        y = 1.0;
      }
    }
    y = 1.0;
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(y, 0.01, RungeKutta4Options{});
        sink = y;
      },
      "rk4_step");

  std::printf("\nRungeKutta4 scalar: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);

  // RK4 should still be very fast despite 4 function evals
}

/**
 * @brief Compare Euler vs RK4 step overhead.
 */
PERF_TEST(Comparison, EulerVsRK4) {
  UB_PERF_GUARD(perf);

  ExplicitEuler<double> euler;
  RungeKutta4<double> rk4;
  double y = 1.0;

  auto f = [](const double& state, double /*t*/) { return -state; };

  euler.initialize(f, y, 0.0, EulerOptions{});
  rk4.initialize(f, y, 0.0, RungeKutta4Options{});

  std::printf("\n=== Euler vs RK4 Comparison ===\n");

  volatile double sink = 0.0;

  // Euler
  auto eulerResult = perf.throughputLoop(
      [&] {
        euler.step(y, 0.01, EulerOptions{});
        sink = y;
      },
      "euler");

  std::printf("Euler: %.0f steps/s\n", eulerResult.callsPerSecond);

  // RK4
  y = 1.0;
  auto rk4Result = perf.throughputLoop(
      [&] {
        rk4.step(y, 0.01, RungeKutta4Options{});
        sink = y;
      },
      "rk4");

  std::printf("RK4:   %.0f steps/s\n", rk4Result.callsPerSecond);

  double ratio = eulerResult.callsPerSecond / rk4Result.callsPerSecond;
  std::printf("Euler is %.2fx faster (expected ~4x due to 4 evals)\n", ratio);

  // Euler should be faster, but not by more than 6x (overhead matters)
}

/* ----------------------------- Vector ODE Tests ----------------------------- */

/**
 * @brief RungeKutta4 with 3D state vector.
 *
 * Tests performance with realistic multi-dimensional state.
 */
PERF_TEST(RungeKutta4, Vector3DThroughput) {
  UB_PERF_GUARD(perf);

  RungeKutta4<StateVector<3>> integrator;
  StateVector<3> y{1.0, 0.0, 0.0};

  // Lorenz system-like dynamics
  auto f = [](const StateVector<3>& s, double /*t*/) {
    return StateVector<3>{10.0 * (s[1] - s[0]), s[0] * (28.0 - s[2]) - s[1],
                          s[0] * s[1] - 2.667 * s[2]};
  };

  integrator.initialize(f, y, 0.0, RungeKutta4Options{});

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(y, 0.001, RungeKutta4Options{});
    }
    y = StateVector<3>{1.0, 0.0, 0.0};
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(y, 0.001, RungeKutta4Options{});
        sink = y[0];
      },
      "rk4_3d");

  std::printf("\nRungeKutta4 3D vector: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);
}

/**
 * @brief RungeKutta4 with 6D state vector (typical 6DOF state).
 */
PERF_TEST(RungeKutta4, Vector6DThroughput) {
  UB_PERF_GUARD(perf);

  RungeKutta4<StateVector<6>> integrator;
  StateVector<6> y{1.0, 0.0, 0.0, 0.1, 0.0, 0.0};

  // Simple 6DOF dynamics
  auto f = [](const StateVector<6>& s, double /*t*/) {
    return StateVector<6>{s[3], s[4], s[5], -0.1 * s[0], -0.1 * s[1], -9.81 - 0.1 * s[2]};
  };

  integrator.initialize(f, y, 0.0, RungeKutta4Options{});

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(y, 0.001, RungeKutta4Options{});
    }
    y = StateVector<6>{1.0, 0.0, 0.0, 0.1, 0.0, 0.0};
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(y, 0.001, RungeKutta4Options{});
        sink = y[0];
      },
      "rk4_6d");

  std::printf("\nRungeKutta4 6D vector: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);
}

/* ----------------------------- Adaptive RK45 Tests ----------------------------- */

/**
 * @brief RungeKutta45 adaptive integration throughput.
 *
 * Tests the overhead of error estimation and step size control.
 */
PERF_TEST(RungeKutta45, AdaptiveThroughput) {
  UB_PERF_GUARD(perf);

  RungeKutta45<double> integrator;
  RungeKutta45Options opts;
  opts.absTol = 1e-6;
  opts.relTol = 1e-6;

  double y = 1.0;
  double dt = 0.01;

  auto f = [](const double& state, double /*t*/) { return -state; };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto result = integrator.step(y, 0.0, dt, f, opts);
      if (result.status == RK45Status::SUCCESS) {
        y = result.y5;
      }
    }
    y = 1.0;
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        auto stepResult = integrator.step(y, 0.0, dt, f, opts);
        if (stepResult.status == RK45Status::SUCCESS) {
          y = stepResult.y5;
        }
        sink = y;
      },
      "rk45_step");

  std::printf("\nRungeKutta45 adaptive: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);

  // RK45 has more overhead (7 evals, error check, step control)
}

/**
 * @brief Compare fixed-step RK4 vs adaptive RK45.
 */
PERF_TEST(Comparison, RK4VsRK45) {
  UB_PERF_GUARD(perf);

  RungeKutta4<double> rk4;
  RungeKutta45<double> rk45;
  RungeKutta45Options opts;

  double y = 1.0;
  auto f = [](const double& state, double /*t*/) { return -state; };

  rk4.initialize(f, y, 0.0, RungeKutta4Options{});

  std::printf("\n=== RK4 (fixed) vs RK45 (adaptive) ===\n");

  volatile double sink = 0.0;

  // RK4
  auto rk4Result = perf.throughputLoop(
      [&] {
        rk4.step(y, 0.01, RungeKutta4Options{});
        sink = y;
      },
      "rk4");

  std::printf("RK4 (fixed):    %.0f steps/s\n", rk4Result.callsPerSecond);

  // RK45
  y = 1.0;
  auto rk45Result = perf.throughputLoop(
      [&] {
        auto result = rk45.step(y, 0.0, 0.01, f, opts);
        if (result.status == RK45Status::SUCCESS) {
          y = result.y5;
        }
        sink = y;
      },
      "rk45");

  std::printf("RK45 (adaptive): %.0f steps/s\n", rk45Result.callsPerSecond);

  double ratio = rk4Result.callsPerSecond / rk45Result.callsPerSecond;
  std::printf("RK4 is %.2fx faster per step (but RK45 can take larger steps)\n", ratio);
}

/* ----------------------------- Symplectic Tests ----------------------------- */

/**
 * @brief Leapfrog single-step throughput.
 *
 * Tests symplectic integrator for Hamiltonian systems.
 */
PERF_TEST(Leapfrog, ThroughputScalar) {
  UB_PERF_GUARD(perf);

  Leapfrog<double> integrator;
  double x = 1.0;
  double v = 0.0;

  // Simple harmonic oscillator
  auto accel = [](const double& pos) { return -pos; };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(x, v, 0.01, accel);
    }
    x = 1.0;
    v = 0.0;
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(x, v, 0.01, accel);
        sink = x;
      },
      "leapfrog_step");

  std::printf("\nLeapfrog scalar: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);

  // Leapfrog with 2 accel evals should be fast
}

/**
 * @brief Leapfrog with 3D vectors (orbital mechanics).
 */
PERF_TEST(Leapfrog, Throughput3D) {
  UB_PERF_GUARD(perf);

  Leapfrog<StateVector<3>> integrator;
  StateVector<3> pos{1.0, 0.0, 0.0};
  StateVector<3> vel{0.0, 1.0, 0.0};

  // Gravitational acceleration (simplified)
  auto accel = [](const StateVector<3>& r) {
    double rMag = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    double r3 = rMag * rMag * rMag;
    return StateVector<3>{-r[0] / r3, -r[1] / r3, -r[2] / r3};
  };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(pos, vel, 0.001, accel);
    }
    pos = StateVector<3>{1.0, 0.0, 0.0};
    vel = StateVector<3>{0.0, 1.0, 0.0};
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(pos, vel, 0.001, accel);
        sink = pos[0];
      },
      "leapfrog_3d");

  std::printf("\nLeapfrog 3D orbital: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);
}

/* ----------------------------- RKN Tests ----------------------------- */

/**
 * @brief RKN4 throughput for second-order ODEs.
 *
 * Specialized integrator for y'' = f(t, y, y').
 */
PERF_TEST(RKN4, ThroughputScalar) {
  UB_PERF_GUARD(perf);

  RKN4<double> integrator;
  double y = 1.0;
  double v = 0.0;

  // Simple harmonic oscillator: y'' = -y
  auto accel = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      integrator.step(y, v, 0.01, accel);
    }
    y = 1.0;
    v = 0.0;
  });

  volatile double sink = 0.0;
  auto result = perf.throughputLoop(
      [&] {
        integrator.step(y, v, 0.01, accel);
        sink = y;
      },
      "rkn4_step");

  std::printf("\nRKN4 scalar: %.0f steps/s (%.1f ns/step)\n", result.callsPerSecond,
              result.stats.median * 1000);

  // RKN4 has 4 function evals optimized for second-order
}

/**
 * @brief Compare Leapfrog vs RKN4 for second-order ODEs.
 */
PERF_TEST(Comparison, LeapfrogVsRKN4) {
  UB_PERF_GUARD(perf);

  Leapfrog<double> leapfrog;
  RKN4<double> rkn4;

  double y = 1.0;
  double v = 0.0;

  auto accelLeapfrog = [](const double& pos) { return -pos; };
  auto accelRKN = [](double /*t*/, const double& pos, const double& /*vel*/) { return -pos; };

  std::printf("\n=== Leapfrog vs RKN4 (2nd-order ODE) ===\n");

  volatile double sink = 0.0;

  // Leapfrog
  auto leapfrogResult = perf.throughputLoop(
      [&] {
        leapfrog.step(y, v, 0.01, accelLeapfrog);
        sink = y;
      },
      "leapfrog");

  std::printf("Leapfrog (2 evals, 2nd-order):  %.0f steps/s\n", leapfrogResult.callsPerSecond);

  // RKN4
  y = 1.0;
  v = 0.0;
  auto rkn4Result = perf.throughputLoop(
      [&] {
        rkn4.step(y, v, 0.01, accelRKN);
        sink = y;
      },
      "rkn4");

  std::printf("RKN4 (4 evals, 4th-order):       %.0f steps/s\n", rkn4Result.callsPerSecond);

  double ratio = leapfrogResult.callsPerSecond / rkn4Result.callsPerSecond;
  std::printf("Leapfrog is %.2fx faster (but RKN4 is 4th-order accurate)\n", ratio);
}

/* ----------------------------- Long Integration Tests ----------------------------- */

/**
 * @brief Measure full integration throughput (many steps).
 *
 * Tests realistic workload of integrating over an interval.
 */
PERF_TEST(Integration, FullTrajectoryRK4) {
  UB_PERF_GUARD(perf);

  std::printf("\n=== Full Trajectory Integration ===\n");

  RungeKutta4<double> integrator;
  const double T_END = 10.0;
  const double DT = 0.001;
  const int STEPS = static_cast<int>(T_END / DT);

  auto f = [](const double& state, double /*t*/) { return -state; };

  perf.warmup([&] {
    double y = 1.0;
    integrator.initialize(f, y, 0.0, RungeKutta4Options{});
    for (int i = 0; i < STEPS; ++i) {
      integrator.step(y, DT, RungeKutta4Options{});
    }
  });

  volatile double finalY = 0.0;
  auto result = perf.measured(
      [&] {
        double y = 1.0;
        integrator.initialize(f, y, 0.0, RungeKutta4Options{});
        for (int i = 0; i < STEPS; ++i) {
          integrator.step(y, DT, RungeKutta4Options{});
        }
        finalY = y;
      },
      "trajectory");

  double trajectoriesPerSec = result.callsPerSecond;
  double stepsPerSec = trajectoriesPerSec * STEPS;

  std::printf("RK4 trajectory (10s, dt=0.001): %.0f trajectories/s (%.1f M steps/s)\n",
              trajectoriesPerSec, stepsPerSec / 1e6);

  // Should maintain high throughput even in sustained integration
}

PERF_MAIN()
