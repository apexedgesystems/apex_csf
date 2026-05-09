#ifndef APEX_NEWTONRAPHSON_TPP
#define APEX_NEWTONRAPHSON_TPP
/**
 * @file NewtonRaphson.tpp
 * @brief NewtonRaphsonSolver implementation.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NewtonRaphson.hpp"

namespace sim::electronics::algorithms::nonlinear {

/* ----------------------------- Construction ----------------------------- */

inline NewtonRaphsonSolver::NewtonRaphsonSolver(std::size_t netCount)
    : netCount_(netCount), voltages_(netCount, 0.0) {}

/* ----------------------------- Reset ----------------------------- */

inline void NewtonRaphsonSolver::reset() noexcept {
  std::fill(voltages_.begin(), voltages_.end(), 0.0);
}

/* ----------------------------- Solve ----------------------------- */

inline NonlinearResult NewtonRaphsonSolver::solve(const NonlinearConfig& config) {
  NonlinearResult result;

  // Validate configuration
  if (config.maxIterations == 0) {
    result.status = NonlinearStatus::ERROR_INVALID_CONFIG;
    result.errorMessage = "maxIterations must be > 0";
    return result;
  }

  // Ensure voltages vector is sized
  if (voltages_.size() != netCount_) {
    voltages_.resize(netCount_, 0.0);
  }

  // Newton-Raphson iteration
  std::vector<double> deltaV(netCount_, 0.0);

  for (std::size_t iter = 0; iter < config.maxIterations; ++iter) {
    // Perform single iteration
    bool ok = iterate(config, iter, deltaV);

    if (!ok) {
      result.status = NonlinearStatus::ERROR_SINGULAR_MATRIX;
      result.errorMessage = "Jacobian matrix is singular at iteration " + std::to_string(iter);
      result.iterations = iter + 1;
      return result;
    }

    // Apply damping if enabled
    if (config.enableDamping && iter < config.dampingIterations) {
      applyDamping(deltaV, config.dampingFactor);
    }

    // Update voltages
    for (std::size_t i = 0; i < netCount_; ++i) {
      voltages_[i] += deltaV[i];
    }

    // Compute convergence metrics
    double maxDeltaV = computeMaxDelta(deltaV);
    double maxVoltage = 0.0;
    for (double v : voltages_) {
      maxVoltage = std::max(maxVoltage, std::abs(v));
    }

    // Compute KCL residual (actual current error)
    double maxResidual = computeKclResidual();

    // Check convergence
    if (config.isConverged(maxDeltaV, maxResidual, maxVoltage)) {
      result.status = NonlinearStatus::SUCCESS;
      result.iterations = iter + 1;
      result.finalError = maxDeltaV;
      result.nodeVoltages = voltages_;
      return result;
    }

    // Check for divergence (voltages growing unbounded)
    if (maxVoltage > 1e6) {
      result.status = NonlinearStatus::ERROR_VOLTAGE_DIVERGENCE;
      result.errorMessage = "Voltages diverging (max = " + std::to_string(maxVoltage) + "V)";
      result.iterations = iter + 1;
      result.finalError = maxDeltaV;
      return result;
    }

    result.finalError = maxDeltaV;
  }

  // Failed to converge within iteration limit
  result.status = NonlinearStatus::ERROR_MAX_ITERATIONS;
  result.errorMessage =
      "Failed to converge after " + std::to_string(config.maxIterations) + " iterations";
  result.iterations = config.maxIterations;
  result.nodeVoltages = voltages_;

  return result;
}

/* ----------------------------- Single Iteration ----------------------------- */

inline bool NewtonRaphsonSolver::iterate(const NonlinearConfig& config, std::size_t iteration,
                                         std::vector<double>& deltaV) {
  (void)config;    // Unused in this implementation
  (void)iteration; // Unused in this implementation

  // Build MNA system for current operating point
  MnaSystem mna(netCount_);

  // Stamp linear elements (resistors, voltage sources)
  if (linearStampCallback_) {
    linearStampCallback_(mna);
  }

  // Stamp linearized nonlinear devices
  devices_.stampAllLinearized(mna, voltages_);

  // Solve for voltage update: J * delta(V) = -F
  // MNA solve gives us the voltages directly, so we compute delta as V_new - V_old
  MnaResult result = mna.solve();

  if (!result.success) {
    return false;
  }

  // Compute voltage update
  for (std::size_t i = 0; i < netCount_; ++i) {
    if (i < result.nodeVoltages.size()) {
      deltaV[i] = result.nodeVoltages[i] - voltages_[i];
    } else {
      deltaV[i] = 0.0;
    }
  }

  return true;
}

/* ----------------------------- Helper Methods ----------------------------- */

inline double
NewtonRaphsonSolver::computeMaxDelta(const std::vector<double>& deltaV) const noexcept {
  double maxDelta = 0.0;
  for (double dv : deltaV) {
    maxDelta = std::max(maxDelta, std::abs(dv));
  }
  return maxDelta;
}

inline double NewtonRaphsonSolver::computeMaxResidual(const MnaSystem& mna) const noexcept {
  const auto& current = mna.currentVector();
  double maxResidual = 0.0;
  for (double i : current) {
    maxResidual = std::max(maxResidual, std::abs(i));
  }
  return maxResidual;
}

inline void NewtonRaphsonSolver::applyDamping(std::vector<double>& deltaV,
                                              double dampingFactor) const noexcept {
  for (double& dv : deltaV) {
    dv *= dampingFactor;
  }
}

inline double NewtonRaphsonSolver::computeKclResidual() const noexcept {
  // Compute KCL residual by evaluating device currents at current voltages
  std::vector<double> netCurrents(netCount_, 0.0);

  // Sum device currents at each net
  for (const auto& device : devices_.devices()) {
    NetID pos = device->posNet();
    NetID neg = device->negNet();
    double vTerminal = voltages_[pos] - voltages_[neg];
    double current = device->current(vTerminal);

    // Current flows from pos to neg
    if (pos < netCount_) {
      netCurrents[pos] -= current;
    }
    if (neg < netCount_) {
      netCurrents[neg] += current;
    }
  }

  // Find maximum residual (excluding GND net 0)
  double maxResidual = 0.0;
  for (std::size_t i = 1; i < netCount_; ++i) {
    maxResidual = std::max(maxResidual, std::abs(netCurrents[i]));
  }

  return maxResidual;
}

} // namespace sim::electronics::algorithms::nonlinear

#endif // APEX_NEWTONRAPHSON_TPP
