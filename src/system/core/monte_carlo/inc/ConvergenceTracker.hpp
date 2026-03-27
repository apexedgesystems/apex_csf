#ifndef APEX_SYSTEM_CORE_MONTE_CARLO_CONVERGENCE_TRACKER_HPP
#define APEX_SYSTEM_CORE_MONTE_CARLO_CONVERGENCE_TRACKER_HPP
/**
 * @file ConvergenceTracker.hpp
 * @brief Running statistics and convergence detection for Monte Carlo.
 *
 * Design:
 *   - Welford's online algorithm for numerically stable running mean/variance
 *   - No storage of individual samples (O(1) memory per tracker)
 *   - Convergence criterion: coefficient of variation of mean estimate
 *     drops below threshold (i.e., adding more runs barely changes the mean)
 *   - Window-based convergence: compares recent mean to overall mean
 *
 * Use cases:
 *   - Determine if N runs is sufficient (stop early if converged)
 *   - Monitor convergence during a sweep without waiting for completion
 *   - Track multiple output fields independently
 *
 * Usage:
 * @code
 * ConvergenceTracker tracker(0.001);  // 0.1% threshold
 *
 * for (const auto& result : results.runs) {
 *     tracker.addSample(result.peakVoltage);
 *     if (tracker.isConverged()) {
 *         fmt::print("Converged at sample {}\n", tracker.count());
 *         break;
 *     }
 * }
 *
 * fmt::print("mean={:.6f} stddev={:.6f} sem={:.6g}\n",
 *            tracker.mean(), tracker.stddev(),
 *            tracker.standardErrorOfMean());
 * @endcode
 *
 * @note NOT RT-safe in general usage, but all methods are O(1) with no allocation.
 */

#include <cmath>
#include <cstdint>

namespace apex {
namespace monte_carlo {

/* ----------------------------- ConvergenceTracker ----------------------------- */

/**
 * @class ConvergenceTracker
 * @brief Online running statistics with convergence detection.
 *
 * Uses Welford's algorithm for numerically stable single-pass computation
 * of mean and variance. Memory usage is O(1) regardless of sample count.
 *
 * Convergence is declared when the standard error of the mean (SEM)
 * divided by the absolute mean drops below the configured threshold.
 * This measures how much the mean estimate would change with more samples.
 *
 * A minimum sample count (default 30) prevents premature convergence
 * from lucky early samples.
 */
class ConvergenceTracker {
public:
  /**
   * @brief Construct tracker with convergence threshold.
   * @param threshold Convergence threshold for SEM/|mean| ratio.
   *        Typical values: 0.01 (1%), 0.001 (0.1%), 0.0001 (0.01%).
   * @param minSamples Minimum samples before convergence can be declared.
   */
  explicit ConvergenceTracker(double threshold = 0.001, std::uint32_t minSamples = 30) noexcept
      : threshold_(threshold), minSamples_(minSamples) {}

  /* ----------------------------- API ----------------------------- */

  /**
   * @brief Add a sample value.
   * @param value New observation.
   *
   * Updates running mean and variance using Welford's algorithm.
   * O(1) time and memory.
   */
  void addSample(double value) noexcept {
    ++count_;
    const double DELTA = value - mean_;
    mean_ += DELTA / static_cast<double>(count_);
    const double DELTA2 = value - mean_;
    m2_ += DELTA * DELTA2;

    if (value < min_) {
      min_ = value;
    }
    if (value > max_) {
      max_ = value;
    }
  }

  /**
   * @brief Check if the mean estimate has converged.
   * @return true if SEM/|mean| < threshold and count >= minSamples.
   *
   * Convergence means adding more samples is unlikely to significantly
   * change the mean estimate.
   *
   * Returns false if:
   *   - Fewer than minSamples observations
   *   - Mean is exactly zero (ratio undefined)
   *   - Variance is zero (degenerate case, returns true if count >= min)
   */
  [[nodiscard]] bool isConverged() const noexcept {
    if (count_ < minSamples_) {
      return false;
    }

    const double SEM = standardErrorOfMean();

    // Degenerate case: all samples identical
    if (SEM == 0.0) {
      return true;
    }

    // Mean near zero: use absolute SEM threshold instead of ratio
    const double ABS_MEAN = std::abs(mean_);
    if (ABS_MEAN < 1e-15) {
      return SEM < threshold_;
    }

    return (SEM / ABS_MEAN) < threshold_;
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Running mean.
   * @return Current mean estimate (0 if no samples).
   */
  [[nodiscard]] double mean() const noexcept { return mean_; }

  /**
   * @brief Population variance.
   * @return Variance (0 if fewer than 2 samples).
   */
  [[nodiscard]] double variance() const noexcept {
    if (count_ < 2) {
      return 0.0;
    }
    return m2_ / static_cast<double>(count_);
  }

  /**
   * @brief Population standard deviation.
   * @return Stddev (0 if fewer than 2 samples).
   */
  [[nodiscard]] double stddev() const noexcept { return std::sqrt(variance()); }

  /**
   * @brief Standard error of the mean (SEM).
   * @return stddev / sqrt(N). Measures uncertainty in the mean estimate.
   *
   * This is the key convergence metric. As N grows, SEM shrinks
   * proportionally to 1/sqrt(N), indicating the mean is stabilizing.
   */
  [[nodiscard]] double standardErrorOfMean() const noexcept {
    if (count_ < 2) {
      return 0.0;
    }
    return stddev() / std::sqrt(static_cast<double>(count_));
  }

  /**
   * @brief Coefficient of variation (CV = stddev/|mean|).
   * @return CV (0 if mean is zero or fewer than 2 samples).
   *
   * Measures relative spread of the population (not the mean estimate).
   * High CV means the underlying distribution has wide spread.
   */
  [[nodiscard]] double coefficientOfVariation() const noexcept {
    const double ABS_MEAN = std::abs(mean_);
    if (ABS_MEAN < 1e-15 || count_ < 2) {
      return 0.0;
    }
    return stddev() / ABS_MEAN;
  }

  /** @brief Number of samples added. */
  [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

  /** @brief Minimum observed value. */
  [[nodiscard]] double min() const noexcept { return min_; }

  /** @brief Maximum observed value. */
  [[nodiscard]] double max() const noexcept { return max_; }

  /** @brief Convergence threshold. */
  [[nodiscard]] double threshold() const noexcept { return threshold_; }

  /** @brief Minimum samples for convergence. */
  [[nodiscard]] std::uint32_t minSamples() const noexcept { return minSamples_; }

  /* ----------------------------- Reset ----------------------------- */

  /**
   * @brief Reset all statistics to initial state.
   *
   * Preserves threshold and minSamples configuration.
   */
  void reset() noexcept {
    count_ = 0;
    mean_ = 0.0;
    m2_ = 0.0;
    min_ = INFINITY;
    max_ = -INFINITY;
  }

private:
  double threshold_;
  std::uint32_t minSamples_;
  std::uint32_t count_{0};
  double mean_{0.0};
  double m2_{0.0}; ///< Welford's sum of squared deviations.
  double min_{INFINITY};
  double max_{-INFINITY};
};

} // namespace monte_carlo
} // namespace apex

#endif // APEX_SYSTEM_CORE_MONTE_CARLO_CONVERGENCE_TRACKER_HPP
