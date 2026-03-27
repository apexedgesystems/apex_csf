#ifndef APEX_SYSTEM_CORE_MONTE_CARLO_SWEEP_GENERATOR_HPP
#define APEX_SYSTEM_CORE_MONTE_CARLO_SWEEP_GENERATOR_HPP
/**
 * @file SweepGenerator.hpp
 * @brief Parameter sweep generation utilities for Monte Carlo runs.
 *
 * Design:
 *   - generateSweep() applies a user-provided mutator to a base config
 *   - Each run gets a per-run RNG seeded deterministically
 *   - Built-in 1D utilities: uniform, grid, Latin Hypercube
 *   - User composes multi-dimensional sweeps via the mutator callback
 *
 * The mutator pattern keeps the generator generic. The user knows their
 * parameter struct layout and which fields to vary. The generator just
 * handles seeding and repetition.
 *
 * Usage:
 * @code
 * struct MyParams { double resistance; double capacitance; };
 *
 * MyParams base{100.0, 1e-6};
 * auto params = apex::monte_carlo::generateSweep<MyParams>(
 *     base, 10000,
 *     [](MyParams& p, std::uint32_t, std::mt19937_64& rng) {
 *         p.resistance = std::normal_distribution<>(100.0, 5.0)(rng);
 *         p.capacitance = std::uniform_real_distribution<>(1e-9, 1e-6)(rng);
 *     },
 *     42);  // seed for reproducibility
 *
 * // Or use grid sweep for a single parameter
 * auto resistances = apex::monte_carlo::gridSweep(50.0, 200.0, 100);
 * @endcode
 *
 * @note NOT RT-safe: Allocates vectors.
 */

#include <cstdint>

#include <algorithm>
#include <functional>
#include <numeric>
#include <random>
#include <vector>

namespace apex {
namespace monte_carlo {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Generate N parameter sets by mutating a base configuration.
 * @tparam ParamT Parameter struct type (must be copy-constructible).
 * @param base     Base configuration (copied for each run before mutation).
 * @param count    Number of parameter sets to generate.
 * @param mutator  Callable: (ParamT& params, runIndex, rng) -> void.
 * @param seed     Base RNG seed (run i uses seed + i for reproducibility).
 * @return Vector of mutated parameter sets.
 *
 * Each run gets an independent RNG seeded with (seed + runIndex). This
 * ensures reproducible results regardless of generation order.
 *
 * @note NOT RT-safe: Allocates vector of ParamT.
 */
template <typename ParamT>
std::vector<ParamT>
generateSweep(const ParamT& base, std::uint32_t count,
              std::function<void(ParamT&, std::uint32_t, std::mt19937_64&)> mutator,
              std::uint64_t seed = 0) {
  std::vector<ParamT> params;
  params.reserve(count);

  for (std::uint32_t i = 0; i < count; ++i) {
    ParamT p = base;
    std::mt19937_64 rng(seed + static_cast<std::uint64_t>(i));
    mutator(p, i, rng);
    params.push_back(std::move(p));
  }

  return params;
}

/**
 * @brief Generate a uniform grid of values between min and max (inclusive).
 * @param minVal  Minimum value.
 * @param maxVal  Maximum value.
 * @param steps   Number of grid points (must be >= 2).
 * @return Vector of evenly-spaced values from minVal to maxVal.
 *
 * Returns {minVal} if steps < 2. With steps=2, returns {minVal, maxVal}.
 *
 * @note NOT RT-safe: Allocates vector.
 */
inline std::vector<double> gridSweep(double minVal, double maxVal, std::uint32_t steps) {
  std::vector<double> values;
  if (steps == 0) {
    return values;
  }
  values.reserve(steps);

  if (steps == 1) {
    values.push_back(minVal);
    return values;
  }

  const double STEP_SIZE = (maxVal - minVal) / static_cast<double>(steps - 1);
  for (std::uint32_t i = 0; i < steps; ++i) {
    values.push_back(minVal + static_cast<double>(i) * STEP_SIZE);
  }

  return values;
}

/**
 * @brief Generate N uniformly-distributed random values in [min, max].
 * @param minVal Minimum value (inclusive).
 * @param maxVal Maximum value (inclusive).
 * @param count  Number of values to generate.
 * @param seed   RNG seed for reproducibility.
 * @return Vector of random doubles in [minVal, maxVal].
 *
 * @note NOT RT-safe: Allocates vector.
 */
inline std::vector<double> uniformSweep(double minVal, double maxVal, std::uint32_t count,
                                        std::uint64_t seed = 0) {
  std::vector<double> values;
  values.reserve(count);

  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<double> dist(minVal, maxVal);

  for (std::uint32_t i = 0; i < count; ++i) {
    values.push_back(dist(rng));
  }

  return values;
}

/**
 * @brief Generate N values using Latin Hypercube Sampling (1D).
 * @param minVal Minimum value (inclusive).
 * @param maxVal Maximum value (inclusive).
 * @param count  Number of samples.
 * @param seed   RNG seed for reproducibility.
 * @return Vector of LHS-distributed doubles in [minVal, maxVal].
 *
 * Divides [min, max] into N equal strata and draws one random sample
 * from each stratum. Provides better coverage than pure random sampling
 * with the same number of points.
 *
 * @note NOT RT-safe: Allocates vector, shuffles.
 */
inline std::vector<double> latinHypercubeSweep(double minVal, double maxVal, std::uint32_t count,
                                               std::uint64_t seed = 0) {
  std::vector<double> values;
  if (count == 0) {
    return values;
  }
  values.reserve(count);

  std::mt19937_64 rng(seed);
  const double STRATUM_WIDTH = (maxVal - minVal) / static_cast<double>(count);

  // One sample per stratum
  std::uniform_real_distribution<double> unitDist(0.0, 1.0);
  for (std::uint32_t i = 0; i < count; ++i) {
    const double STRATUM_MIN = minVal + static_cast<double>(i) * STRATUM_WIDTH;
    values.push_back(STRATUM_MIN + unitDist(rng) * STRATUM_WIDTH);
  }

  // Shuffle to remove positional correlation
  std::shuffle(values.begin(), values.end(), rng);

  return values;
}

/**
 * @brief Generate Cartesian product of two 1D sweeps.
 * @tparam ParamT Parameter struct type.
 * @param base   Base configuration.
 * @param sweepA First dimension values.
 * @param sweepB Second dimension values.
 * @param applyA Callable: (ParamT& p, double valueA) -> void.
 * @param applyB Callable: (ParamT& p, double valueB) -> void.
 * @return Vector of size |sweepA| * |sweepB| parameter sets.
 *
 * @code
 * auto resistances = gridSweep(50.0, 200.0, 10);
 * auto capacitances = gridSweep(1e-9, 1e-6, 10);
 * auto params = cartesianProduct<MyParams>(
 *     base, resistances, capacitances,
 *     [](MyParams& p, double v) { p.resistance = v; },
 *     [](MyParams& p, double v) { p.capacitance = v; });
 * // 100 parameter sets
 * @endcode
 *
 * @note NOT RT-safe: Allocates vector.
 */
template <typename ParamT>
std::vector<ParamT> cartesianProduct(const ParamT& base, const std::vector<double>& sweepA,
                                     const std::vector<double>& sweepB,
                                     std::function<void(ParamT&, double)> applyA,
                                     std::function<void(ParamT&, double)> applyB) {
  std::vector<ParamT> params;
  params.reserve(sweepA.size() * sweepB.size());

  for (const double A : sweepA) {
    for (const double B : sweepB) {
      ParamT p = base;
      applyA(p, A);
      applyB(p, B);
      params.push_back(std::move(p));
    }
  }

  return params;
}

} // namespace monte_carlo
} // namespace apex

#endif // APEX_SYSTEM_CORE_MONTE_CARLO_SWEEP_GENERATOR_HPP
