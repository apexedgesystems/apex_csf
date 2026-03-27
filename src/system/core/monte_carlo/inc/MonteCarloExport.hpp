#ifndef APEX_SYSTEM_CORE_MONTE_CARLO_EXPORT_HPP
#define APEX_SYSTEM_CORE_MONTE_CARLO_EXPORT_HPP
/**
 * @file MonteCarloExport.hpp
 * @brief CSV export and summary reporting for Monte Carlo results.
 *
 * Design:
 *   - exportCsv() writes per-run results with user-defined column extractors
 *   - printSummary() outputs a statistics table to any ostream
 *   - Column definitions decouple export format from result type
 *   - Uses fmt for formatting (consistent with project conventions)
 *
 * Two export modes:
 *   1. Per-run CSV: One row per simulation run, user-defined columns
 *   2. Summary table: ScalarStats per output field, printed or written
 *
 * Usage:
 * @code
 * // Define columns
 * std::vector<ColumnDef<MyResult>> columns = {
 *     {"peak_voltage", [](const MyResult& r) { return r.peakVoltage; }},
 *     {"settling_time", [](const MyResult& r) { return r.settlingTime; }},
 *     {"converged", [](const MyResult& r) { return r.converged ? 1.0 : 0.0; }},
 * };
 *
 * // Export per-run CSV
 * exportCsv(results, "mc_results.csv", columns);
 *
 * // Print summary to stdout
 * printSummary(std::cout, results, columns);
 * @endcode
 *
 * @note NOT RT-safe: File I/O, allocations.
 */

#include "src/system/core/monte_carlo/inc/MonteCarloResults.hpp"

#include <cstdint>

#include <filesystem>
#include <fstream>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace apex {
namespace monte_carlo {

/* ----------------------------- ColumnDef ----------------------------- */

/**
 * @struct ColumnDef
 * @brief Defines a named column extracted from a result type.
 *
 * @tparam ResultT The result struct type.
 *
 * Each column has a name (CSV header) and an extractor that pulls a
 * double from the result. This decouples the export format from the
 * result type — the caller defines what to export.
 */
template <typename ResultT> struct ColumnDef {
  std::string name;
  std::function<double(const ResultT&)> extractor;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Export per-run results to CSV file.
 * @tparam ResultT Result type.
 * @param results    Monte Carlo results to export.
 * @param path       Output file path.
 * @param columns    Column definitions (name + extractor).
 * @return true on success, false on file open failure.
 *
 * Writes a header row followed by one data row per run.
 * Columns are: run_index, followed by user-defined columns.
 *
 * @note NOT RT-safe: File I/O.
 */
template <typename ResultT>
bool exportCsv(const MonteCarloResults<ResultT>& results, const std::filesystem::path& path,
               const std::vector<ColumnDef<ResultT>>& columns) {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  // Header row
  out << "run_index";
  for (const auto& col : columns) {
    out << "," << col.name;
  }
  out << "\n";

  // Data rows
  for (std::uint32_t i = 0; i < results.runs.size(); ++i) {
    out << i;
    for (const auto& col : columns) {
      out << fmt::format(",{:.8g}", col.extractor(results.runs[i]));
    }
    out << "\n";
  }

  return true;
}

/**
 * @brief Export summary statistics to CSV file.
 * @tparam ResultT Result type.
 * @param results    Monte Carlo results.
 * @param path       Output file path.
 * @param columns    Column definitions.
 * @return true on success, false on file open failure.
 *
 * Writes one row per column with statistics (mean, stddev, min, max,
 * percentiles). Useful for quick comparison across sweeps.
 *
 * @note NOT RT-safe: File I/O, sorting.
 */
template <typename ResultT>
bool exportSummaryCsv(const MonteCarloResults<ResultT>& results, const std::filesystem::path& path,
                      const std::vector<ColumnDef<ResultT>>& columns) {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out << "field,count,mean,stddev,min,p05,p25,median,p75,p95,max\n";

  for (const auto& col : columns) {
    const auto STATS = extractAndCompute<ResultT>(results.runs, col.extractor);
    out << fmt::format("{},{},{:.8g},{:.8g},{:.8g},{:.8g},{:.8g},{:.8g},{:.8g},"
                       "{:.8g},{:.8g}\n",
                       col.name, STATS.count, STATS.mean, STATS.stddev, STATS.min, STATS.p05,
                       STATS.p25, STATS.median, STATS.p75, STATS.p95, STATS.max);
  }

  return true;
}

/**
 * @brief Print a formatted statistics summary table to an ostream.
 * @tparam ResultT Result type.
 * @param os         Output stream (cout, file stream, etc.).
 * @param results    Monte Carlo results.
 * @param columns    Column definitions.
 *
 * Prints a human-readable table with aligned columns showing
 * mean, stddev, min, median, max, and p05/p95 for each output field.
 * Also prints execution metadata (runs, threads, wall time, throughput).
 *
 * @note NOT RT-safe: Stream I/O, sorting.
 */
template <typename ResultT>
void printSummary(std::ostream& os, const MonteCarloResults<ResultT>& results,
                  const std::vector<ColumnDef<ResultT>>& columns) {
  // Execution metadata
  os << fmt::format("Monte Carlo Results: {} runs ({} completed, {} failed)\n", results.totalRuns,
                    results.completedRuns, results.failedRuns);
  os << fmt::format("Execution: {:.3f}s wall, {} threads, {:.0f} runs/sec\n\n",
                    results.wallTimeSeconds, results.threadCount, results.runsPerSecond());

  // Find longest column name for alignment
  std::size_t maxNameLen = 5; // "Field" header minimum
  for (const auto& col : columns) {
    if (col.name.size() > maxNameLen) {
      maxNameLen = col.name.size();
    }
  }

  // Table header
  const auto HDR =
      fmt::format("  {:<{}}  {:>12}  {:>12}  {:>12}  {:>12}  "
                  "{:>12}  {:>12}  {:>12}\n",
                  "Field", maxNameLen, "Mean", "Stddev", "Min", "Median", "Max", "P05", "P95");
  os << HDR;
  os << std::string(HDR.size() - 1, '-') << "\n";

  // Per-column statistics
  for (const auto& col : columns) {
    const auto S = extractAndCompute<ResultT>(results.runs, col.extractor);
    os << fmt::format("  {:<{}}  {:>12.6g}  {:>12.6g}  {:>12.6g}  {:>12.6g}  "
                      "{:>12.6g}  {:>12.6g}  {:>12.6g}\n",
                      col.name, maxNameLen, S.mean, S.stddev, S.min, S.median, S.max, S.p05, S.p95);
  }

  // Failure rate if applicable
  if (results.failedRuns > 0) {
    const double FAIL_RATE =
        100.0 * static_cast<double>(results.failedRuns) / static_cast<double>(results.totalRuns);
    os << fmt::format("\nFailure rate: {:.2f}% ({}/{})\n", FAIL_RATE, results.failedRuns,
                      results.totalRuns);
  }
}

/**
 * @brief Compute yield (pass rate) against a threshold.
 * @tparam ResultT Result type.
 * @param results   Monte Carlo results.
 * @param extractor Field extractor.
 * @param limit     Threshold value.
 * @param upper     If true, pass = (value <= limit). If false, pass = (value >= limit).
 * @return Fraction of runs that pass [0.0, 1.0].
 *
 * @note NOT RT-safe: Iterates all results.
 */
template <typename ResultT>
double computeYield(const MonteCarloResults<ResultT>& results,
                    std::function<double(const ResultT&)> extractor, double limit,
                    bool upper = true) {
  if (results.runs.empty()) {
    return 0.0;
  }

  std::uint32_t passCount = 0;
  for (const auto& r : results.runs) {
    const double VAL = extractor(r);
    if (upper ? (VAL <= limit) : (VAL >= limit)) {
      ++passCount;
    }
  }

  return static_cast<double>(passCount) / static_cast<double>(results.runs.size());
}

} // namespace monte_carlo
} // namespace apex

#endif // APEX_SYSTEM_CORE_MONTE_CARLO_EXPORT_HPP
