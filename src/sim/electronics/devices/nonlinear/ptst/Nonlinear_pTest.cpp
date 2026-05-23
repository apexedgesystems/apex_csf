/**
 * @file Nonlinear_pTest.cpp
 * @brief Performance tests for nonlinear device models.
 *
 * Measures per-device evaluation throughput for the physics functions used in
 * Newton-Raphson iteration. The Intel 4004 L1 simulation calls MosfetLevel1
 * current/gm/gds 2242 times per NR iteration -- this is the production hotpath.
 *
 * Usage:
 *   ./Nonlinear_PTEST --csv results.csv
 *   ./Nonlinear_PTEST --quick
 *   ./Nonlinear_PTEST --profile gperf --gtest_filter="*MosfetLevel1*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetBsim3.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;
using sim::electronics::devices::nonlinear::BjtEbersMoll;
using sim::electronics::devices::nonlinear::BjtEbersMollParams;
using sim::electronics::devices::nonlinear::DiodeShockley;
using sim::electronics::devices::nonlinear::DiodeShockleyParams;
using sim::electronics::devices::nonlinear::MosfetBsim3;
using sim::electronics::devices::nonlinear::MosfetBsim3Params;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Test Data ----------------------------- */

struct MosfetBias {
  double vgs;
  double vds;
};

static std::vector<MosfetBias> generateMosfetBias(std::size_t count) {
  std::vector<MosfetBias> biases(count);
  std::mt19937 gen(42);
  std::uniform_real_distribution<double> vgsDist(0.0, 5.0);
  std::uniform_real_distribution<double> vdsDist(0.0, 5.0);
  for (auto& b : biases) {
    b.vgs = vgsDist(gen);
    b.vds = vdsDist(gen);
  }
  return biases;
}

/* ----------------------------- MosfetLevel1 ----------------------------- */

/**
 * @brief MosfetLevel1 current evaluation throughput (2242 devices).
 *
 * This is the innermost loop of the Intel 4004 L1 NR iteration:
 * for each transistor, evaluate Id(Vgs, Vds) to compute the Jacobian entry.
 */
PERF_TEST(NonlinearPerf, MosfetLevel1Current_2242) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 2242;
  const auto BIASES = generateMosfetBias(COUNT);
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03};
  volatile double sink = 0.0;

  std::printf("\n=== MosfetLevel1::current() (%zu evaluations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (const auto& b : BIASES) {
        sum += MosfetLevel1::current(b.vgs, b.vds, params);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (const auto& b : BIASES) {
          sum += MosfetLevel1::current(b.vgs, b.vds, params);
        }
        sink = sum;
      },
      "mosfet_current_2242");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu devices: %8.0f batches/s  (%.1f ns/device)\n", COUNT, result.callsPerSecond,
              perDevice);
}

/**
 * @brief Full NR stamp evaluation (current + gm + gds + ieq) per device.
 *
 * Each NR iteration needs all four values per transistor. This measures
 * the combined cost of computing the linearized device model.
 */
PERF_TEST(NonlinearPerf, MosfetLevel1FullStamp_2242) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 2242;
  const auto BIASES = generateMosfetBias(COUNT);
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03};
  volatile double sink = 0.0;

  std::printf("\n=== MosfetLevel1 Full NR Stamp (current+gm+gds+ieq, %zu devices) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (const auto& b : BIASES) {
        double id = MosfetLevel1::current(b.vgs, b.vds, params);
        double gm = MosfetLevel1::transconductance(b.vgs, b.vds, params);
        double gds = MosfetLevel1::outputConductance(b.vgs, b.vds, params);
        double ieq = id - gm * b.vgs - gds * b.vds;
        sum += ieq;
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (const auto& b : BIASES) {
          double id = MosfetLevel1::current(b.vgs, b.vds, params);
          double gm = MosfetLevel1::transconductance(b.vgs, b.vds, params);
          double gds = MosfetLevel1::outputConductance(b.vgs, b.vds, params);
          double ieq = id - gm * b.vgs - gds * b.vds;
          sum += ieq;
        }
        sink = sum;
      },
      "mosfet_full_stamp_2242");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu devices: %8.0f batches/s  (%.1f ns/device)\n", COUNT, result.callsPerSecond,
              perDevice);
}

/**
 * @brief Combined (id, gm, gds) via MosfetLevel1::stampValues for 2242 devices.
 *
 * Same workload as MosfetLevel1FullStamp_2242 but using the combined
 * `stampValues` entry point which evaluates the region branch and shared
 * subexpressions exactly once instead of three times.
 */
PERF_TEST(NonlinearPerf, MosfetLevel1StampValues_2242) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 2242;
  const auto BIASES = generateMosfetBias(COUNT);
  MosfetLevel1Params params{.Kp = 5e-3, .Vth = 1.17, .lambda = 0.03};
  volatile double sink = 0.0;

  std::printf("\n=== MosfetLevel1 stampValues (combined id+gm+gds, %zu devices) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (const auto& b : BIASES) {
        const auto SV = MosfetLevel1::stampValues(b.vgs, b.vds, params);
        sum += SV.id - SV.gm * b.vgs - SV.gds * b.vds;
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (const auto& b : BIASES) {
          const auto SV = MosfetLevel1::stampValues(b.vgs, b.vds, params);
          sum += SV.id - SV.gm * b.vgs - SV.gds * b.vds;
        }
        sink = sum;
      },
      "mosfet_stampvalues_2242");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu devices: %8.0f batches/s  (%.1f ns/device)\n", COUNT, result.callsPerSecond,
              perDevice);
}

/* ----------------------------- MosfetBsim3 ----------------------------- */

/**
 * @brief MosfetBsim3 stampValues throughput on the realistic L2
 *        latch-core slice (338 transistors).
 *
 * `Intel4004GridLevel2` overrides `stampLatchFeedbackTransistor` to
 * call the BSIM3 weak-inversion model (`n_factor=2.5`) on the ~338
 * cross-coupled latch transistors per NR iter. This ptest measures
 * the per-iteration cost of that override path with the same
 * production parameter template.
 */
PERF_TEST(NonlinearPerf, MosfetBsim3StampValues_338) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 338;
  const auto BIASES = generateMosfetBias(COUNT);
  // Match Intel4004GridLevel2::bsim3LatchParams_ exactly (the L2
  // production template).
  MosfetBsim3Params params{.Kp = 5e-3,
                           .Vth0 = 1.17,
                           .lambda = 0.03,
                           .W = 1.0,
                           .L = 1.0,
                           .n_factor = 2.5,
                           .Vt = 0.026,
                           .eta0 = 0.0,
                           .K1 = 0.0,
                           .K2 = 0.0,
                           .phi = 0.7,
                           .ua = 0.0,
                           .ub = 0.0,
                           .tox = 50e-9,
                           .delta = 0.01};
  volatile double sink = 0.0;

  std::printf("\n=== MosfetBsim3 stampValues (n=2.5 weak-inv, %zu devices) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (const auto& b : BIASES) {
        const auto SV = MosfetBsim3::stampValues(b.vgs, b.vds, /*vbs=*/0.0, params);
        sum += SV.id - SV.gm * b.vgs - SV.gds * b.vds;
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (const auto& b : BIASES) {
          const auto SV = MosfetBsim3::stampValues(b.vgs, b.vds, /*vbs=*/0.0, params);
          sum += SV.id - SV.gm * b.vgs - SV.gds * b.vds;
        }
        sink = sum;
      },
      "mosfet_bsim3_stampvalues_338");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu devices: %8.0f batches/s  (%.1f ns/device)\n", COUNT, result.callsPerSecond,
              perDevice);
}

/* ----------------------------- DiodeShockley ----------------------------- */

/**
 * @brief Diode Shockley evaluation throughput.
 */
PERF_TEST(NonlinearPerf, DiodeShockleyEval_1000) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 1000;
  DiodeShockleyParams params;
  volatile double sink = 0.0;

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> vDist(-1.0, 0.8);
  std::vector<double> voltages(COUNT);
  for (auto& v : voltages)
    v = vDist(gen);

  std::printf("\n=== DiodeShockley::current() (%zu evaluations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (double v : voltages) {
        sum += DiodeShockley::current(v, params);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (double v : voltages) {
          sum += DiodeShockley::current(v, params);
        }
        sink = sum;
      },
      "diode_1000");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu diodes: %8.0f batches/s  (%.1f ns/diode)\n", COUNT, result.callsPerSecond,
              perDevice);
}

/* ----------------------------- BjtEbersMoll ----------------------------- */

/**
 * @brief BJT Ebers-Moll collector current evaluation.
 */
PERF_TEST(NonlinearPerf, BjtEbersMollEval_500) {
  UB_PERF_GUARD(perf);

  const std::size_t COUNT = 500;
  BjtEbersMollParams params;
  volatile double sink = 0.0;

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> vbeDist(0.0, 0.8);
  std::uniform_real_distribution<double> vbcDist(-5.0, 0.3);

  struct BjtBias {
    double vbe;
    double vbc;
  };
  std::vector<BjtBias> biases(COUNT);
  for (auto& b : biases) {
    b.vbe = vbeDist(gen);
    b.vbc = vbcDist(gen);
  }

  std::printf("\n=== BjtEbersMoll::collectorCurrent() (%zu evaluations) ===\n", COUNT);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      double sum = 0.0;
      for (const auto& b : biases) {
        sum += BjtEbersMoll::collectorCurrent(b.vbe, b.vbc, params);
      }
      sink = sum;
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        double sum = 0.0;
        for (const auto& b : biases) {
          sum += BjtEbersMoll::collectorCurrent(b.vbe, b.vbc, params);
        }
        sink = sum;
      },
      "bjt_500");

  double perDevice = result.stats.median * 1000.0 / COUNT;
  std::printf("  %zu BJTs: %8.0f batches/s  (%.1f ns/BJT)\n", COUNT, result.callsPerSecond,
              perDevice);
}

PERF_MAIN()
