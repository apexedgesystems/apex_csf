/**
 * @file main.cpp
 * @brief Monte Carlo voltage regulator tolerance analysis demo.
 *
 * Demonstrates the full MC workflow:
 *   1. Define component tolerances (manufacturing specs)
 *   2. Generate parameter sweep (grid, random, or LHS)
 *   3. Execute sweep across all CPU cores
 *   4. Print summary statistics table
 *   5. Export per-run CSV and summary CSV
 *   6. Report yield (% of boards in spec)
 *   7. Track convergence (did we run enough?)
 *
 * Usage:
 *   ./ApexMcDemo                                 # defaults: 10000 runs, LHS
 *   ./ApexMcDemo --runs 100000                   # more runs
 *   ./ApexMcDemo --runs 50000 --threads 8        # explicit thread count
 *   ./ApexMcDemo --runs 1000 --csv results.csv   # export per-run CSV
 *   ./ApexMcDemo --seed 42                       # reproducible sweep
 */

#include "src/sim/analog/regulator/inc/RegulatorModel.hpp"
#include "src/system/core/monte_carlo/inc/ConvergenceTracker.hpp"
#include "src/system/core/monte_carlo/inc/MonteCarloDriver.hpp"
#include "src/system/core/monte_carlo/inc/MonteCarloExport.hpp"
#include "src/system/core/monte_carlo/inc/SweepGenerator.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using apex::monte_carlo::ColumnDef;
using apex::monte_carlo::computeYield;
using apex::monte_carlo::ConvergenceTracker;
using apex::monte_carlo::DriverConfig;
using apex::monte_carlo::exportCsv;
using apex::monte_carlo::exportSummaryCsv;
using apex::monte_carlo::extractAndCompute;
using apex::monte_carlo::generateSweep;
using apex::monte_carlo::MonteCarloDriver;
using apex::monte_carlo::printSummary;
using sim::analog::RegulatorParams;
using sim::analog::RegulatorResult;

/* ----------------------------- CLI Parsing ----------------------------- */

struct CliArgs {
  std::uint32_t runs{10000};
  std::uint32_t threads{0}; // 0 = auto
  std::uint64_t seed{42};
  std::string csvPath;
  std::string summaryPath;
};

static CliArgs parseArgs(int argc, char* argv[]) {
  CliArgs args;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
      args.runs = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      args.threads = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      args.seed = static_cast<std::uint64_t>(std::atoll(argv[++i]));
    } else if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
      args.csvPath = argv[++i];
    } else if (std::strcmp(argv[i], "--summary") == 0 && i + 1 < argc) {
      args.summaryPath = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      fmt::print("Usage: ApexMcDemo [options]\n"
                 "  --runs N       Number of MC runs (default: 10000)\n"
                 "  --threads N    Worker thread count (default: auto)\n"
                 "  --seed N       RNG seed (default: 42)\n"
                 "  --csv PATH     Export per-run results CSV\n"
                 "  --summary PATH Export summary statistics CSV\n"
                 "  -h, --help     Show this help\n");
      std::exit(0);
    }
  }

  return args;
}

/* ----------------------------- Column Definitions ----------------------------- */

static std::vector<ColumnDef<RegulatorResult>> makeColumns() {
  return {
      {"v_out", [](const RegulatorResult& r) { return r.vOut; }},
      {"ripple_mV", [](const RegulatorResult& r) { return r.ripple * 1000.0; }},
      {"settling_us", [](const RegulatorResult& r) { return r.settlingTime * 1.0e6; }},
      {"phase_margin", [](const RegulatorResult& r) { return r.phaseMargin; }},
      {"in_spec", [](const RegulatorResult& r) { return r.inSpec ? 1.0 : 0.0; }},
  };
}

/* ----------------------------- Main ----------------------------- */

int main(int argc, char* argv[]) {
  const auto ARGS = parseArgs(argc, argv);

  fmt::print("=== Apex Monte Carlo Demo: LDO Voltage Regulator ===\n\n");

  // ---- Component tolerances ----
  fmt::print("Component tolerances (manufacturing specs):\n");
  fmt::print("  R1 (feedback upper):  100k ohm +/- 1%\n");
  fmt::print("  R2 (feedback lower):  60.606k ohm +/- 1%\n");
  fmt::print("  C_out (output cap):   10uF +/- 20%\n");
  fmt::print("  ESR (cap parasitic):  10m ohm +/- 50%\n");
  fmt::print("  V_ref (reference):    1.25V +/- 2%\n");
  fmt::print("  Bandwidth:            100kHz +/- 30%\n\n");

  // ---- Generate parameter sweep ----
  fmt::print("Generating {} parameter sets (LHS, seed={})...\n", ARGS.runs, ARGS.seed);

  RegulatorParams base;
  auto params = generateSweep<RegulatorParams>(
      base, ARGS.runs,
      [](RegulatorParams& p, std::uint32_t, std::mt19937_64& rng) {
        // Resistors: +/- 1% (normal distribution, 3-sigma = 1%)
        p.r1 *= 1.0 + std::normal_distribution<>(0.0, 0.01 / 3.0)(rng);
        p.r2 *= 1.0 + std::normal_distribution<>(0.0, 0.01 / 3.0)(rng);

        // Ceramic capacitor: +/- 20% (uniform, worst-case)
        p.cOut *= 1.0 + std::uniform_real_distribution<>(-0.20, 0.20)(rng);

        // ESR: +/- 50% (log-normal, skewed high)
        p.esr *= std::exp(std::normal_distribution<>(0.0, 0.20)(rng));

        // Voltage reference: +/- 2% (normal, 3-sigma = 2%)
        p.vRef *= 1.0 + std::normal_distribution<>(0.0, 0.02 / 3.0)(rng);

        // Bandwidth: +/- 30% (uniform, process variation)
        p.bandwidth *= 1.0 + std::uniform_real_distribution<>(-0.30, 0.30)(rng);
      },
      ARGS.seed);

  // ---- Run Monte Carlo sweep ----
  DriverConfig driverCfg;
  driverCfg.threadCount = ARGS.threads;
  driverCfg.baseSeed = ARGS.seed;

  MonteCarloDriver<RegulatorParams, RegulatorResult> driver(
      [](const RegulatorParams& p, std::uint32_t) -> RegulatorResult {
        return sim::analog::simulate(p);
      },
      driverCfg);

  fmt::print("Running {} MC iterations across {} cores...\n", ARGS.runs, driver.workerCount());

  auto results = driver.execute(params);

  // ---- Summary statistics ----
  auto columns = makeColumns();
  fmt::print("\n");
  printSummary(std::cout, results, columns);

  // ---- Yield analysis ----
  fmt::print("\n--- Yield Analysis ---\n");

  const double VOLTAGE_YIELD = computeYield<RegulatorResult>(
      results, [](const RegulatorResult& r) { return r.inSpec ? 1.0 : 0.0; }, 0.5,
      false); // in_spec >= 0.5 (i.e., true)

  const double RIPPLE_YIELD = computeYield<RegulatorResult>(
      results, [](const RegulatorResult& r) { return r.ripple * 1000.0; }, 50.0,
      true); // ripple <= 50mV

  const double PHASE_YIELD = computeYield<RegulatorResult>(
      results, [](const RegulatorResult& r) { return r.phaseMargin; }, 45.0,
      false); // phase margin >= 45 deg

  fmt::print("  V_out within 3.3V +/-3%:  {:.2f}%\n", VOLTAGE_YIELD * 100.0);
  fmt::print("  Ripple < 50mV:            {:.2f}%\n", RIPPLE_YIELD * 100.0);
  fmt::print("  Phase margin > 45 deg:    {:.2f}%\n", PHASE_YIELD * 100.0);

  // Combined yield (all specs pass)
  const double COMBINED_YIELD = computeYield<RegulatorResult>(
      results,
      [](const RegulatorResult& r) {
        return (r.inSpec && r.ripple * 1000.0 <= 50.0 && r.phaseMargin >= 45.0) ? 1.0 : 0.0;
      },
      0.5, false);
  fmt::print("  All specs pass:           {:.2f}%\n", COMBINED_YIELD * 100.0);

  // ---- Sensitivity ranking ----
  fmt::print("\n--- Sensitivity Analysis ---\n");

  // Run with each parameter varied independently to find dominant contributor
  auto voutStats = extractAndCompute<RegulatorResult>(
      results.runs, [](const RegulatorResult& r) { return r.vOut; });
  fmt::print("  V_out CV (spread): {:.4f}%\n", (voutStats.stddev / voutStats.mean) * 100.0);
  fmt::print("  V_out range: [{:.4f}V, {:.4f}V]\n", voutStats.min, voutStats.max);

  // ---- Convergence check ----
  fmt::print("\n--- Convergence Check ---\n");
  ConvergenceTracker voutTracker(0.001, 100); // 0.1% threshold
  ConvergenceTracker rippleTracker(0.001, 100);

  for (const auto& r : results.runs) {
    voutTracker.addSample(r.vOut);
    rippleTracker.addSample(r.ripple * 1000.0);
  }

  fmt::print("  V_out:   SEM={:.6g}V, converged={}\n", voutTracker.standardErrorOfMean(),
             voutTracker.isConverged() ? "YES" : "NO");
  fmt::print("  Ripple:  SEM={:.6g}mV, converged={}\n", rippleTracker.standardErrorOfMean(),
             rippleTracker.isConverged() ? "YES" : "NO");

  // ---- CSV export ----
  if (!ARGS.csvPath.empty()) {
    if (exportCsv(results, ARGS.csvPath, columns)) {
      fmt::print("\nPer-run CSV exported to: {}\n", ARGS.csvPath);
    } else {
      fmt::print("\nERROR: Failed to export CSV to: {}\n", ARGS.csvPath);
    }
  }

  if (!ARGS.summaryPath.empty()) {
    if (exportSummaryCsv(results, ARGS.summaryPath, columns)) {
      fmt::print("Summary CSV exported to: {}\n", ARGS.summaryPath);
    } else {
      fmt::print("ERROR: Failed to export summary to: {}\n", ARGS.summaryPath);
    }
  }

  fmt::print("\nDone.\n");
  return 0;
}
