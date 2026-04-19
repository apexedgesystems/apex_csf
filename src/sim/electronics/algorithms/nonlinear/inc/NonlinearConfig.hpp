#ifndef APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARCONFIG_HPP
#define APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARCONFIG_HPP
/**
 * @file NonlinearConfig.hpp
 * @brief Configuration and status codes for Newton-Raphson nonlinear solver.
 *
 * Defines convergence tolerances, iteration limits, and status codes for
 * nonlinear circuit simulation.
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace sim::electronics::nonlinear {

/* ----------------------------- NonlinearConfig ----------------------------- */

/**
 * @brief Configuration for Newton-Raphson nonlinear solver.
 *
 * Controls iteration limits and convergence criteria.
 */
struct NonlinearConfig {
  std::size_t maxIterations = 20;  ///< Maximum Newton-Raphson iterations.
  double voltageTolerance = 1e-6;  ///< Voltage convergence (Volts).
  double currentTolerance = 1e-9;  ///< Current convergence (Amps).
  double relativeTolerance = 1e-3; ///< Relative error tolerance.

  bool enableDamping = true;         ///< Enable damping for oscillatory convergence.
  double dampingFactor = 0.5;        ///< Damping factor (0.5 = half-step update).
  std::size_t dampingIterations = 5; ///< Apply damping for first N iterations.

  /**
   * @brief Check if convergence criteria are met.
   *
   * @param maxDeltaV Maximum voltage change across all nodes.
   * @param maxCurrent Maximum residual current at any node.
   * @param maxVoltage Maximum voltage magnitude (for relative error).
   * @return True if converged.
   * @note RT-safe.
   */
  [[nodiscard]] bool isConverged(double maxDeltaV, double maxCurrent,
                                 double maxVoltage) const noexcept {
    // Absolute voltage tolerance
    if (maxDeltaV < voltageTolerance) {
      return true;
    }

    // Absolute current tolerance (residual error)
    if (maxCurrent < currentTolerance) {
      return true;
    }

    // Relative voltage tolerance
    if (maxVoltage > 0.0 && maxDeltaV / maxVoltage < relativeTolerance) {
      return true;
    }

    return false;
  }
};

/* ----------------------------- NonlinearStatus ----------------------------- */

/**
 * @brief Status codes for nonlinear solver operations.
 */
enum class NonlinearStatus : std::uint8_t {
  SUCCESS = 0,                  ///< Converged successfully.
  ERROR_MAX_ITERATIONS = 1,     ///< Failed to converge within iteration limit.
  ERROR_SINGULAR_MATRIX = 2,    ///< Jacobian matrix is singular (no solution).
  ERROR_VOLTAGE_DIVERGENCE = 3, ///< Voltages diverging (unstable circuit).
  ERROR_INVALID_CONFIG = 4      ///< Invalid configuration parameters.
};

/**
 * @brief Convert status to human-readable string.
 * @param status Status code.
 * @return Status description.
 */
inline const char* toString(NonlinearStatus status) noexcept {
  switch (status) {
  case NonlinearStatus::SUCCESS:
    return "SUCCESS";
  case NonlinearStatus::ERROR_MAX_ITERATIONS:
    return "ERROR_MAX_ITERATIONS";
  case NonlinearStatus::ERROR_SINGULAR_MATRIX:
    return "ERROR_SINGULAR_MATRIX";
  case NonlinearStatus::ERROR_VOLTAGE_DIVERGENCE:
    return "ERROR_VOLTAGE_DIVERGENCE";
  case NonlinearStatus::ERROR_INVALID_CONFIG:
    return "ERROR_INVALID_CONFIG";
  }
  return "UNKNOWN";
}

/* ----------------------------- NonlinearResult ----------------------------- */

/**
 * @brief Result of Newton-Raphson iteration.
 *
 * Contains converged solution, iteration count, and diagnostics.
 */
struct NonlinearResult {
  NonlinearStatus status = NonlinearStatus::ERROR_MAX_ITERATIONS;
  std::string errorMessage; ///< Error description if failed.

  std::size_t iterations = 0; ///< Number of Newton-Raphson iterations.
  double finalError = 0.0;    ///< Final residual error (max |delta(V)|).

  std::vector<double> nodeVoltages;   ///< Converged node voltages.
  std::vector<double> branchCurrents; ///< Converged branch currents (voltage sources).

  /**
   * @brief Check if solution is valid.
   * @return True if status is SUCCESS.
   */
  [[nodiscard]] bool success() const noexcept { return status == NonlinearStatus::SUCCESS; }
};

} // namespace sim::electronics::nonlinear

#endif // APEX_SIM_ELECTRONICS_NONLINEAR_NONLINEARCONFIG_HPP
