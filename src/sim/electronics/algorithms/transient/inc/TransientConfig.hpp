#ifndef APEX_TRANSIENTCONFIG_HPP
#define APEX_TRANSIENTCONFIG_HPP
/**
 * @file TransientConfig.hpp
 * @brief Configuration structures for transient circuit simulation.
 *
 * Defines time-stepping parameters, integration method selection,
 * and convergence tolerances for transient analysis.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sim::electronics::algorithms::transient {

/* ----------------------------- IntegrationMethod ----------------------------- */

/**
 * @brief Available integration methods for transient simulation.
 *
 * Each method trades off accuracy, stability, and computational cost.
 * Backward Euler is recommended for most digital circuits due to its
 * unconditional stability (A-stable).
 */
enum class IntegrationMethod : std::uint8_t {
  BACKWARD_EULER, ///< First-order, A-stable, strong damping. Best for digital.
  TRAPEZOIDAL,    ///< Second-order, A-stable, less damping. Good for analog.
  GEAR2           ///< Second-order BDF, L-stable. Best for stiff circuits.
};

/* ----------------------------- TransientConfig ----------------------------- */

/**
 * @brief Configuration for transient simulation.
 *
 * Controls time span, step size, and numerical behavior.
 */
struct TransientConfig {
  double tStart = 0.0; ///< Simulation start time (seconds).
  double tEnd = 1e-6;  ///< Simulation end time (seconds).
  double tStep = 1e-9; ///< Fixed time step (seconds).

  double maxStep = 1e-8;  ///< Maximum adaptive step (if adaptive enabled).
  double minStep = 1e-12; ///< Minimum adaptive step.

  double relTol = 1e-3; ///< Relative tolerance for adaptive stepping.
  double absTol = 1e-6; ///< Absolute tolerance for adaptive stepping.

  IntegrationMethod method = IntegrationMethod::BACKWARD_EULER;

  bool adaptiveStep = false; ///< Enable adaptive time stepping.
  bool dcOpPoint = true;     ///< Compute DC operating point before transient.

  std::size_t maxNonlinearIter = 20; ///< Max Newton iterations per step.
  double nonlinearTol = 1e-9;        ///< Nonlinear convergence tolerance.

  /// A span within this fraction of a step of a whole number of steps is
  /// that number of steps: (tEnd - tStart) / tStep is rarely whole in binary
  /// when the two are decimal.
  static constexpr double WHOLE_STEP_TOLERANCE = 1e-9;

  /// Step counts are kept below 2^53, where a double still counts by ones.
  static constexpr double STEP_COUNT_LIMIT = 9007199254740992.0;

  /**
   * @brief The number of steps of tStep that fit the span.
   * @return 0 when tStep is not above zero, the span is not above zero, or
   *         the count is not below STEP_COUNT_LIMIT (not-a-number included).
   * @note RT-safe: pure arithmetic.
   */
  [[nodiscard]] constexpr std::size_t wholeSteps() const noexcept {
    if (!(tStep > 0.0) || !(tEnd > tStart)) {
      return 0;
    }
    const double RATIO = (tEnd - tStart) / tStep;
    if (!(RATIO < STEP_COUNT_LIMIT)) {
      return 0;
    }
    return static_cast<std::size_t>(RATIO + WHOLE_STEP_TOLERANCE);
  }

  /**
   * @brief Length of the shorter step that ends a span which is not a whole
   *        number of steps.
   * @return 0 when the span is whole, or has no countable number of steps.
   * @note RT-safe: pure arithmetic.
   */
  [[nodiscard]] constexpr double lastStep() const noexcept {
    if (!(tStep > 0.0) || !(tEnd > tStart) || !((tEnd - tStart) / tStep < STEP_COUNT_LIMIT)) {
      return 0.0;
    }
    const double REST = (tEnd - tStart) - static_cast<double>(wholeSteps()) * tStep;
    return REST > WHOLE_STEP_TOLERANCE * tStep ? REST : 0.0;
  }

  /**
   * @brief The number of steps run() takes: the whole steps, and one more
   *        when a shorter step ends the span.
   * @return 0 for a configuration run() refuses.
   * @note RT-safe: pure arithmetic.
   */
  [[nodiscard]] constexpr std::size_t stepCount() const noexcept {
    return wholeSteps() + (lastStep() > 0.0 ? 1U : 0U);
  }

  /**
   * @brief Get integration order for current method.
   * @return Order of the integration method (1 or 2).
   * @note RT-safe: pure arithmetic.
   */
  [[nodiscard]] constexpr std::uint8_t order() const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return 1;
    case IntegrationMethod::TRAPEZOIDAL:
    case IntegrationMethod::GEAR2:
      return 2;
    }
    return 1;
  }
};

/* ----------------------------- TransientState ----------------------------- */

/**
 * @brief State snapshot at a single time point.
 *
 * Stores node voltages and branch currents for reactive element
 * history and output recording.
 */
struct TransientState {
  double time = 0.0;                  ///< Current simulation time.
  std::vector<double> nodeVoltages;   ///< Voltage at each net.
  std::vector<double> branchCurrents; ///< Current through voltage sources.

  /**
   * @brief Allocate storage for given circuit size.
   * @param netCount Number of nets.
   * @param vsrcCount Number of voltage sources.
   * @note NOT RT-safe: allocates memory.
   */
  void resize(std::size_t netCount, std::size_t vsrcCount) {
    nodeVoltages.resize(netCount, 0.0);
    branchCurrents.resize(vsrcCount, 0.0);
  }

  /**
   * @brief Get voltage at a net.
   * @param net Net ID.
   * @return Voltage at net, or 0 if out of bounds.
   * @note RT-safe: bounds-checked read.
   */
  [[nodiscard]] double voltage(std::size_t net) const noexcept {
    return (net < nodeVoltages.size()) ? nodeVoltages[net] : 0.0;
  }
};

/* ----------------------------- TransientResult ----------------------------- */

/**
 * @brief Result of transient simulation.
 *
 * Contains final state, success status, and optional time history.
 */
struct TransientResult {
  bool success = false;     ///< True if simulation completed.
  std::string errorMessage; ///< Error description if failed.

  std::size_t stepsTaken = 0;     ///< Number of time steps executed.
  std::size_t nonlinearIters = 0; ///< Total nonlinear iterations.
  double finalTime = 0.0;         ///< Simulation end time reached.

  TransientState finalState;           ///< State at final time.
  std::vector<TransientState> history; ///< Optional full time history.
};

/* ----------------------------- Status ----------------------------- */

/**
 * @brief Status codes for transient operations.
 */
enum class TransientStatus : std::uint8_t {
  SUCCESS = 0,
  ERROR_INVALID_CONFIG = 1,     ///< Invalid configuration parameters.
  ERROR_DC_FAILED = 2,          ///< DC operating point solve failed.
  ERROR_STEP_FAILED = 3,        ///< Time step solve failed.
  ERROR_CONVERGENCE = 4,        ///< Nonlinear iteration did not converge.
  ERROR_TIMESTEP_TOO_SMALL = 5, ///< Adaptive step went below minimum.
  ERROR_MATRIX_SINGULAR = 6     ///< MNA matrix is singular.
};

} // namespace sim::electronics::algorithms::transient

#endif // APEX_TRANSIENTCONFIG_HPP
