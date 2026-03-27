#ifndef APEX_SYSTEM_CORE_MONTE_CARLO_RESULTS_HPP
#define APEX_SYSTEM_CORE_MONTE_CARLO_RESULTS_HPP
/**
 * @file MonteCarloResults.hpp
 * @brief Results container and scalar statistics for Monte Carlo runs.
 *
 * Design:
 *   - MonteCarloResults<ResultT> holds per-run results + execution metadata
 *   - ScalarStats provides standard descriptive statistics
 *   - computeStats() extracts statistics from a span of doubles
 *   - extractAndCompute() applies a field extractor to typed results
 *
 * The results container is populated by MonteCarloDriver. Statistics are
 * computed post-hoc by the user on whichever output fields are relevant.
 *
 * Usage:
 * @code
 * MonteCarloResults<MyResult> results = driver.execute(params);
 *
 * // Extract statistics on a specific field
 * auto voltageStats = extractAndCompute<MyResult>(
 *     results.runs, [](const MyResult& r) { return r.peakVoltage; });
 *
 * // Or compute from raw doubles
 * std::vector<double> values = {...};
 * auto stats = computeStats(values);
 * @endcode
 *
 * @note NOT RT-safe: Uses std::vector, std::sort.
 */

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <functional>
#include <numeric>
#include <span>
#include <vector>

namespace apex {
namespace monte_carlo {

/* ----------------------------- ScalarStats ----------------------------- */

/**
 * @struct ScalarStats
 * @brief Descriptive statistics for a scalar output across MC runs.
 */
struct ScalarStats {
  double mean{0.0};
  double stddev{0.0};
  double min{0.0};
  double max{0.0};
  double median{0.0};
  double p05{0.0}; ///< 5th percentile.
  double p25{0.0}; ///< 25th percentile (Q1).
  double p75{0.0}; ///< 75th percentile (Q3).
  double p95{0.0}; ///< 95th percentile.
  std::uint32_t count{0};
};

/* ----------------------------- MonteCarloResults ----------------------------- */

/**
 * @struct MonteCarloResults
 * @brief Container for per-run results and execution metadata.
 *
 * @tparam ResultT Result type from each run.
 */
template <typename ResultT> struct MonteCarloResults {
  /// Per-run results (indexed by run index).
  std::vector<ResultT> runs;

  /// Total runs requested.
  std::uint32_t totalRuns{0};

  /// Runs that completed without exception.
  std::uint32_t completedRuns{0};

  /// Runs that threw an exception (result is default-constructed).
  std::uint32_t failedRuns{0};

  /// Wall-clock execution time in seconds.
  double wallTimeSeconds{0.0};

  /// Number of worker threads used.
  std::uint32_t threadCount{0};

  /**
   * @brief Throughput in runs per second.
   * @return Runs/sec (0 if wallTimeSeconds is zero).
   */
  [[nodiscard]] double runsPerSecond() const noexcept {
    if (wallTimeSeconds <= 0.0) {
      return 0.0;
    }
    return static_cast<double>(completedRuns) / wallTimeSeconds;
  }
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Compute descriptive statistics from a span of doubles.
 * @param values Input values (will be copied and sorted internally).
 * @return ScalarStats with mean, stddev, percentiles.
 *
 * @note NOT RT-safe: Allocates and sorts.
 */
inline ScalarStats computeStats(std::span<const double> values) {
  ScalarStats stats;
  stats.count = static_cast<std::uint32_t>(values.size());

  if (stats.count == 0) {
    return stats;
  }

  // Copy and sort for percentiles
  std::vector<double> sorted(values.begin(), values.end());
  std::sort(sorted.begin(), sorted.end());

  stats.min = sorted.front();
  stats.max = sorted.back();

  // Mean
  const double SUM = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  stats.mean = SUM / static_cast<double>(stats.count);

  // Standard deviation (population)
  double sumSqDiff = 0.0;
  for (const double V : sorted) {
    const double DIFF = V - stats.mean;
    sumSqDiff += DIFF * DIFF;
  }
  stats.stddev = std::sqrt(sumSqDiff / static_cast<double>(stats.count));

  // Percentiles via nearest-rank method
  const auto PERCENTILE = [&](double p) -> double {
    if (stats.count == 1) {
      return sorted[0];
    }
    const double RANK = p * static_cast<double>(stats.count - 1);
    const auto LOWER = static_cast<std::size_t>(std::floor(RANK));
    const auto UPPER = static_cast<std::size_t>(std::ceil(RANK));
    if (LOWER == UPPER) {
      return sorted[LOWER];
    }
    const double FRAC = RANK - static_cast<double>(LOWER);
    return sorted[LOWER] * (1.0 - FRAC) + sorted[UPPER] * FRAC;
  };

  stats.p05 = PERCENTILE(0.05);
  stats.p25 = PERCENTILE(0.25);
  stats.median = PERCENTILE(0.50);
  stats.p75 = PERCENTILE(0.75);
  stats.p95 = PERCENTILE(0.95);

  return stats;
}

/**
 * @brief Extract a scalar field from typed results and compute statistics.
 * @tparam ResultT Result type.
 * @param runs      Span of results.
 * @param extractor Function that extracts a double from a result.
 * @return ScalarStats for the extracted field.
 *
 * @code
 * auto stats = extractAndCompute<MyResult>(
 *     results.runs, [](const MyResult& r) { return r.peakVoltage; });
 * @endcode
 *
 * @note NOT RT-safe: Allocates extraction buffer.
 */
template <typename ResultT>
ScalarStats extractAndCompute(std::span<const ResultT> runs,
                              std::function<double(const ResultT&)> extractor) {
  std::vector<double> values;
  values.reserve(runs.size());
  for (const auto& r : runs) {
    values.push_back(extractor(r));
  }
  return computeStats(values);
}

} // namespace monte_carlo
} // namespace apex

#endif // APEX_SYSTEM_CORE_MONTE_CARLO_RESULTS_HPP
