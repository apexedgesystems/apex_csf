#ifndef APEX_MATH_INTEGRATION_ACCUMULATOR_HPP
#define APEX_MATH_INTEGRATION_ACCUMULATOR_HPP
/**
 * @file Accumulator.hpp
 * @brief RT-safe accumulator for direct integration of sensor readings.
 *
 * Design goals:
 *  - Multi-rate sensor fusion support
 *  - Forward Euler and trapezoidal integration
 *  - Zero-allocation, O(1) operations
 */

#include <cstddef>
#include <cstdint>

namespace apex {
namespace math {
namespace integration {

/* ------------------------------- Status ---------------------------------- */

/**
 * @brief Accumulator status codes.
 */
enum class AccumulatorStatus : uint8_t { SUCCESS = 0, ERROR_INVALID_DT = 1, ERROR_OVERFLOW = 2 };

/* ------------------------------ Accumulator ------------------------------ */

/**
 * @brief RT-safe accumulator for direct integration.
 *
 * Simple first-order integration of sensor readings or computed values.
 * Designed for multi-rate systems where readings arrive at different
 * frequencies.
 *
 * Use cases:
 *   - Integrate accelerometer readings to velocity
 *   - Integrate velocity to position
 *   - Dead-reckoning navigation
 *   - Simple thermal accumulation
 *
 * @tparam State State type (must support +, -, * scalar operations).
 *
 * Example:
 * @code
 *   Accumulator<StateVector<3>> velocityAccum;
 *   velocityAccum.reset(State3{0, 0, 0});
 *
 *   // IMU at 400 Hz
 *   State3 accel = readAccelerometer();
 *   velocityAccum.accumulate(accel, 1.0 / 400.0);
 *
 *   State3 velocity = velocityAccum.state();
 * @endcode
 *
 * @note RT-safe: All operations are O(1), no allocations.
 */
template <typename State> class Accumulator {
public:
  /* -------------------------- Construction ------------------------------ */

  /** @brief Default constructor (zero-initialized state). */
  constexpr Accumulator() noexcept = default;

  /** @brief Construct with initial state. */
  constexpr explicit Accumulator(const State& initial) noexcept
      : state_(initial), t_(0.0), sampleCount_(0) {}

  /* -------------------------- Core Operations --------------------------- */

  /**
   * @brief Reset accumulator to initial state.
   * @param initial Initial state value.
   * @note RT-safe: Direct assignment.
   */
  constexpr void reset(const State& initial) noexcept {
    state_ = initial;
    t_ = 0.0;
    sampleCount_ = 0;
  }

  /**
   * @brief Accumulate a derivative value over a time step.
   *
   * Uses forward Euler: state += derivative * dt
   *
   * @param derivative Rate of change (e.g., acceleration, velocity).
   * @param dt Time step in seconds (must be positive).
   * @return Status code.
   * @note RT-safe: O(1) arithmetic operations.
   */
  constexpr uint8_t accumulate(const State& derivative, double dt) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(AccumulatorStatus::ERROR_INVALID_DT);
    }
    state_ = state_ + derivative * dt;
    t_ += dt;
    ++sampleCount_;
    return static_cast<uint8_t>(AccumulatorStatus::SUCCESS);
  }

  /**
   * @brief Accumulate using trapezoidal rule for better accuracy.
   *
   * Uses average of previous and current derivative:
   * state += 0.5 * (prevDerivative + derivative) * dt
   *
   * @param prevDerivative Previous derivative value.
   * @param derivative Current derivative value.
   * @param dt Time step in seconds.
   * @return Status code.
   * @note RT-safe: O(1) arithmetic operations.
   */
  constexpr uint8_t accumulateTrapezoidal(const State& prevDerivative, const State& derivative,
                                          double dt) noexcept {
    if (dt <= 0.0) {
      return static_cast<uint8_t>(AccumulatorStatus::ERROR_INVALID_DT);
    }
    state_ = state_ + (prevDerivative + derivative) * (0.5 * dt);
    t_ += dt;
    ++sampleCount_;
    return static_cast<uint8_t>(AccumulatorStatus::SUCCESS);
  }

  /**
   * @brief Direct state update (for corrections, resets).
   * @param newState New state value.
   * @note RT-safe: Direct assignment.
   */
  constexpr void setState(const State& newState) noexcept { state_ = newState; }

  /**
   * @brief Apply additive correction to state.
   * @param correction Correction value to add.
   * @note RT-safe: Single addition.
   */
  constexpr void correct(const State& correction) noexcept { state_ = state_ + correction; }

  /* -------------------------- Accessors --------------------------------- */

  /** @brief Current accumulated state. */
  constexpr const State& state() const noexcept { return state_; }

  /** @brief Mutable reference to state. */
  constexpr State& state() noexcept { return state_; }

  /** @brief Total accumulated time. */
  constexpr double time() const noexcept { return t_; }

  /** @brief Number of samples accumulated. */
  constexpr std::size_t sampleCount() const noexcept { return sampleCount_; }

  /** @brief Average sample period (or 0 if no samples). */
  constexpr double averageDt() const noexcept {
    return sampleCount_ > 0 ? t_ / static_cast<double>(sampleCount_) : 0.0;
  }

private:
  State state_{};
  double t_{0.0};
  std::size_t sampleCount_{0};
};

/* -------------------------- MultiRateAccumulator ------------------------- */

/**
 * @brief Accumulator with support for multiple input rates.
 *
 * Tracks multiple input sources with different sample rates and
 * provides synchronized state estimation.
 *
 * @tparam State State type.
 * @tparam N Maximum number of input sources.
 *
 * Example:
 * @code
 *   MultiRateAccumulator<State6, 2> nav;
 *   nav.reset(State6{});
 *
 *   // Source 0: IMU at 400 Hz
 *   nav.accumulate(0, imuReading, 1.0 / 400.0);
 *
 *   // Source 1: GPS correction at 10 Hz
 *   nav.accumulate(1, gpsCorrection, 1.0 / 10.0);
 * @endcode
 *
 * @note RT-safe: Fixed-size arrays, O(1) operations.
 */
template <typename State, std::size_t N> class MultiRateAccumulator {
public:
  /** @brief Per-source statistics. */
  struct SourceStats {
    double totalTime{0.0};
    std::size_t sampleCount{0};
  };

  /* -------------------------- Construction ------------------------------ */

  constexpr MultiRateAccumulator() noexcept = default;

  constexpr explicit MultiRateAccumulator(const State& initial) noexcept : state_(initial) {}

  /* -------------------------- Core Operations --------------------------- */

  /**
   * @brief Reset all accumulators.
   * @param initial Initial state.
   */
  constexpr void reset(const State& initial) noexcept {
    state_ = initial;
    for (std::size_t i = 0; i < N; ++i) {
      sourceStats_[i] = SourceStats{};
    }
  }

  /**
   * @brief Accumulate from a specific source.
   * @param sourceIdx Source index (0 to N-1).
   * @param derivative Derivative from this source.
   * @param dt Time step for this source.
   * @return Status code.
   */
  constexpr uint8_t accumulate(std::size_t sourceIdx, const State& derivative, double dt) noexcept {
    if (sourceIdx >= N) {
      return static_cast<uint8_t>(AccumulatorStatus::ERROR_OVERFLOW);
    }
    if (dt <= 0.0) {
      return static_cast<uint8_t>(AccumulatorStatus::ERROR_INVALID_DT);
    }
    state_ = state_ + derivative * dt;
    sourceStats_[sourceIdx].totalTime += dt;
    ++sourceStats_[sourceIdx].sampleCount;
    return static_cast<uint8_t>(AccumulatorStatus::SUCCESS);
  }

  /**
   * @brief Apply correction (e.g., from GPS or external sensor fusion).
   * @param correction Correction to apply.
   */
  constexpr void correct(const State& correction) noexcept { state_ = state_ + correction; }

  /* -------------------------- Accessors --------------------------------- */

  constexpr const State& state() const noexcept { return state_; }
  constexpr State& state() noexcept { return state_; }

  constexpr const SourceStats& sourceStats(std::size_t idx) const noexcept {
    return sourceStats_[idx < N ? idx : 0];
  }

  static constexpr std::size_t maxSources() noexcept { return N; }

private:
  State state_{};
  SourceStats sourceStats_[N]{};
};

} // namespace integration
} // namespace math
} // namespace apex

#endif // APEX_MATH_INTEGRATION_ACCUMULATOR_HPP
