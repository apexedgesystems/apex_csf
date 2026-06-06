/**
 * @file Mna_pTest.cpp
 * @brief Performance tests for MNA solvers using the benchmarking framework.
 *
 * Measures throughput and latency for:
 *  - Dense LAPACK solve (MnaSystem) at varying circuit sizes
 *  - Sparse KLU factorize + solve (MnaSystemSparse) at varying circuit sizes
 *  - RT-safe cached LU back-substitution
 *  - Stamping throughput (conductance stamp rate)
 *  - Dense vs sparse crossover point
 *
 * The production workload is the Intel 4004 circuit: ~150 nets, ~450 non-zeros
 * (~2% fill). Benchmarks cover 10-500 net circuits to show scaling behavior.
 *
 * Usage:
 *   ./Mna_PTEST --csv results.csv
 *   ./Mna_PTEST --quick --csv results.csv
 *   ./Mna_PTEST --profile perf --gtest_filter="*Sparse*"
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;

/* ----------------------------- Test Data ----------------------------- */

/**
 * @brief Build a resistor ladder circuit of N nets.
 *
 * Creates a chain of resistors from ground to VDD with a voltage source.
 * This produces a tridiagonal conductance matrix -- sparse and representative
 * of real circuit topology (local connectivity, not random fill).
 *
 * @param N Number of nets (including ground).
 * @return Vector of (netA, netB, conductance) triplets.
 */
struct Stamp {
  std::uint32_t a;
  std::uint32_t b;
  double g;
};

static std::vector<Stamp> buildLadder(std::size_t N) {
  std::vector<Stamp> stamps;
  stamps.reserve(N * 2);

  std::mt19937 gen(42);
  std::uniform_real_distribution<double> dist(0.001, 1.0);

  // Chain: net[i] -- R -- net[i+1] for i in [0, N-2]
  for (std::size_t i = 0; i + 1 < N; ++i) {
    stamps.push_back({static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 1), dist(gen)});
  }

  // Add some cross-connections for realistic sparsity (~3% fill)
  for (std::size_t i = 0; i + 3 < N; i += 3) {
    stamps.push_back({static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(i + 3), dist(gen)});
  }

  return stamps;
}

/* ----------------------------- Sparse KLU Solve ----------------------------- */

/**
 * @brief Sparse KLU factorize + solve at production circuit size (150 nets).
 *
 * This is the hot path for the Intel 4004 transistor simulation. Each NR
 * iteration stamps the circuit, factorizes, and solves. Factorize is the
 * dominant cost; solve is O(nnz) back-substitution.
 */
PERF_TEST(MnaSparse, FactorizeAndSolve_150net) {
  UB_PERF_GUARD(perf);

  const std::size_t N = 150;
  const auto STAMPS = buildLadder(N);

  std::printf("\n=== Sparse KLU Factorize + Solve (%zu nets, %zu stamps) ===\n", N, STAMPS.size());

  volatile double sink = 0.0;

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
      for (const auto& s : STAMPS) {
        mna.addConductance(s.a, s.b, s.g);
      }
      mna.addVoltageSource(N - 1, 0, 5.0);
      mna.factorize();
      auto result = mna.solve();
      sink = result.nodeVoltages[1];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
        for (const auto& s : STAMPS) {
          mna.addConductance(s.a, s.b, s.g);
        }
        mna.addVoltageSource(N - 1, 0, 5.0);
        mna.factorize();
        auto r = mna.solve();
        sink = r.nodeVoltages[1];
      },
      "sparse_150");

  std::printf("  Sparse 150-net: %8.0f solves/s  (%.1f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/**
 * @brief Sparse KLU solve-only (cached factorization) at 150 nets.
 *
 * After the first factorize(), subsequent solves with new RHS are O(nnz)
 * back-substitution. This is the RT-safe path used in transient simulation
 * where circuit topology doesn't change between time steps.
 */
PERF_TEST(MnaSparse, CachedSolveOnly_150net) {
  UB_PERF_GUARD(perf);

  const std::size_t N = 150;
  const auto STAMPS = buildLadder(N);

  // Pre-build and factorize once
  sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
  for (const auto& s : STAMPS) {
    mna.addConductance(s.a, s.b, s.g);
  }
  mna.addVoltageSource(N - 1, 0, 5.0);
  mna.factorize();

  std::vector<double> nodeV(N, 0.0);
  std::vector<double> branchI(1, 0.0);
  volatile double sink = 0.0;

  std::printf("\n=== Sparse KLU Cached Solve (back-sub only, %zu nets) ===\n", N);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      mna.solveInto(nodeV.data(), branchI.data());
      sink = nodeV[1];
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        mna.solveInto(nodeV.data(), branchI.data());
        sink = nodeV[1];
      },
      "cached_solve_150");

  std::printf("  Cached solve 150-net: %8.0f solves/s  (%.3f us/solve)\n", result.callsPerSecond,
              result.stats.median);
}

/* ----------------------------- Dense LAPACK Solve ----------------------------- */

/**
 * @brief Dense LAPACK solve at small circuit sizes (10, 50 nets).
 *
 * Dense solve uses dgesv (LU factorization). O(n^3) scaling means it's only
 * competitive for small circuits. Provides the baseline for sparse comparison.
 */
PERF_TEST(MnaDense, LapackSolve_Small) {
  UB_PERF_GUARD(perf);

  const std::size_t SIZES[] = {10, 50};
  volatile double sink = 0.0;

  std::printf("\n=== Dense LAPACK Solve (small circuits) ===\n");

  for (std::size_t N : SIZES) {
    const auto STAMPS = buildLadder(N);

    auto result = perf.throughputLoop(
        [&] {
          sim::electronics::algorithms::mna::MnaSystem mna(N);
          for (const auto& s : STAMPS) {
            mna.addConductance(s.a, s.b, s.g);
          }
          mna.addVoltageSource(static_cast<sim::electronics::algorithms::mna::NetID>(N - 1), 0, 5.0);
          auto r = mna.solve();
          sink = r.nodeVoltages[1];
        },
        std::string("dense_") + std::to_string(N));

    std::printf("  Dense %3zu-net: %8.0f solves/s  (%.1f us/solve)\n", N, result.callsPerSecond,
                result.stats.median);
  }
}

/* ----------------------------- Scaling ----------------------------- */

/**
 * @brief Sparse solve scaling across circuit sizes (10 to 500 nets).
 *
 * Shows how KLU factorize+solve scales with circuit size. Expected: roughly
 * linear in nnz (number of non-zeros), which grows linearly with net count
 * for ladder circuits.
 */
PERF_TEST(MnaSparse, Scaling) {
  UB_PERF_GUARD(perf);

  const std::size_t SIZES[] = {10, 50, 100, 150, 250, 500};
  volatile double sink = 0.0;

  std::printf("\n=== Sparse KLU Scaling (factorize + solve) ===\n");

  for (std::size_t N : SIZES) {
    const auto STAMPS = buildLadder(N);

    auto result = perf.throughputLoop(
        [&] {
          sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
          for (const auto& s : STAMPS) {
            mna.addConductance(s.a, s.b, s.g);
          }
          mna.addVoltageSource(static_cast<sim::electronics::algorithms::mna::NetID>(N - 1), 0, 5.0);
          mna.factorize();
          auto r = mna.solve();
          sink = r.nodeVoltages[1];
        },
        std::string("sparse_") + std::to_string(N));

    std::printf("  Sparse %3zu-net (%3zu nnz): %8.0f solves/s  (%.1f us/solve)\n", N, STAMPS.size(),
                result.callsPerSecond, result.stats.median);
  }
}

/* ----------------------------- Stamping Throughput ----------------------------- */

/**
 * @brief Raw stamping throughput for sparse MNA.
 *
 * Measures how fast conductances can be stamped into the triplet list.
 * This is the per-transistor cost in the NR loop (each transistor stamps
 * ~3-5 entries per iteration).
 */
PERF_TEST(MnaSparse, StampThroughput) {
  UB_PERF_GUARD(perf);

  const std::size_t N = 150;
  const std::size_t NUM_STAMPS = 2000; // ~4 stamps per transistor x 500 transistors
  volatile double sink = 0.0;

  std::printf("\n=== Sparse Stamp Throughput (%zu stamps into %zu-net system) ===\n", NUM_STAMPS,
              N);

  std::mt19937 gen(42);
  std::uniform_int_distribution<std::uint32_t> netDist(0, N - 1);
  std::uniform_real_distribution<double> gDist(0.001, 1.0);

  // Pre-generate random stamps
  std::vector<Stamp> stamps(NUM_STAMPS);
  for (auto& s : stamps) {
    s.a = netDist(gen);
    s.b = netDist(gen);
    s.g = gDist(gen);
  }

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
      for (const auto& s : stamps) {
        mna.addConductance(s.a, s.b, s.g);
      }
      sink = static_cast<double>(mna.nnz());
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
        for (const auto& s : stamps) {
          mna.addConductance(s.a, s.b, s.g);
        }
        sink = static_cast<double>(mna.nnz());
      },
      "stamp_2000");

  double stampsPerSec = NUM_STAMPS * result.callsPerSecond;
  std::printf("  %zu stamps: %8.0f batches/s  (%.0f Mstamps/s)\n", NUM_STAMPS,
              result.callsPerSecond, stampsPerSec / 1e6);
}

/* ----------------------------- Multi-threaded Parallel KLU ----------------------------- */

/**
 * @brief K parallel `MnaSystemSparse` solves at Intel-4004-scale (Dim1081).
 *
 * Realistic workload: K independent 4004 simulations (Monte Carlo,
 * parameter sweep, regression suite). Each instance runs its own KLU
 * factorize + solve at Dim1081 -- the actual 4004 net count.
 *
 * Each circuit gets its own `MnaSystemSparse` instance; KLU handles
 * are per-instance so threads don't contend. The work is split round-
 * robin across `numThreads` `std::thread`s.
 *
 * Per pass-13 baseline single-threaded: 543 us per
 * stamp+factor+solve at this dim. At full thread parallelism with K
 * circuits, expected wall time is `(K / numThreads) * 543 us`.
 *
 * Reports per-circuit cost both serial and parallel for the same K,
 * giving the practical batched-MC throughput on the host machine.
 */
static void runParallelKluCase(std::size_t numCircuits, unsigned numThreads, const char* label) {
  UB_PERF_GUARD(perf);
  constexpr std::size_t N = 1081; // Intel 4004 net count.
  const auto STAMPS = buildLadder(N);

  std::printf("\n=== Parallel KLU %s (K=%zu circuits, %u threads, Dim%zu) ===\n", label,
              numCircuits, numThreads, N);

  // Pre-build per-circuit conductance perturbations so the lambda
  // body does only the build + factor + solve work (no allocation
  // overhead skewing measurement).
  std::vector<std::vector<Stamp>> perCircuitStamps(numCircuits, STAMPS);
  for (std::size_t k = 1; k < numCircuits; ++k) {
    for (auto& s : perCircuitStamps[k]) {
      s.g *= 1.0 + 0.001 * static_cast<double>(k); // tiny per-circuit jitter
    }
  }

  std::atomic<int> sinkBits{0};

  auto solveOne = [&](std::size_t k) {
    sim::electronics::algorithms::mna::MnaSystemSparse mna(N);
    for (const auto& s : perCircuitStamps[k]) {
      mna.addConductance(s.a, s.b, s.g);
    }
    mna.addVoltageSource(N - 1, 0, 5.0);
    mna.factorize();
    auto r = mna.solve();
    // Touch result so the compiler doesn't optimise it away.
    int bits;
    std::memcpy(&bits, &r.nodeVoltages[1], sizeof(int));
    sinkBits.fetch_add(bits, std::memory_order_relaxed);
  };

  auto runFn = [&] {
    if (numThreads <= 1) {
      for (std::size_t k = 0; k < numCircuits; ++k)
        solveOne(k);
      return;
    }
    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    std::atomic<std::size_t> nextIdx{0};
    for (unsigned t = 0; t < numThreads; ++t) {
      workers.emplace_back([&] {
        while (true) {
          std::size_t k = nextIdx.fetch_add(1, std::memory_order_relaxed);
          if (k >= numCircuits)
            break;
          solveOne(k);
        }
      });
    }
    for (auto& w : workers)
      w.join();
  };

  perf.warmup(runFn);
  auto result = perf.measured(runFn, "parallel_klu");

  const double TOTAL_US = result.stats.median;
  const double PER_CIRCUIT_US = TOTAL_US / static_cast<double>(numCircuits);
  std::printf("  total wall: %.1f us  (%.1f us / circuit)\n", TOTAL_US, PER_CIRCUIT_US);
  if (numThreads > 1) {
    std::printf("  CV: %.1f%%  (lower is better; see RESULTS_REPORT for serial baseline)\n",
                result.stats.cv * 100.0);
  }
}

PERF_TEST(MnaParallelKlu, K8_T1) { runParallelKluCase(8, 1, "K8_T1"); }
PERF_TEST(MnaParallelKlu, K8_T8) { runParallelKluCase(8, 8, "K8_T8"); }
PERF_TEST(MnaParallelKlu, K16_T1) { runParallelKluCase(16, 1, "K16_T1"); }
PERF_TEST(MnaParallelKlu, K16_T8) { runParallelKluCase(16, 8, "K16_T8"); }
PERF_TEST(MnaParallelKlu, K64_T1) { runParallelKluCase(64, 1, "K64_T1"); }
PERF_TEST(MnaParallelKlu, K64_T8) { runParallelKluCase(64, 8, "K64_T8"); }

PERF_MAIN()
