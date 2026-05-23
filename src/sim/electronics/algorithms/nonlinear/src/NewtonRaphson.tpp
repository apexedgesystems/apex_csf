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

  if (config.maxIterations == 0) {
    result.status = NonlinearStatus::ERROR_INVALID_CONFIG;
    result.errorMessage = "maxIterations must be > 0";
    return result;
  }

  if (voltages_.size() != netCount_) {
    voltages_.resize(netCount_, 0.0);
  }

  std::vector<double> deltaV(netCount_, 0.0);

  for (std::size_t iter = 0; iter < config.maxIterations; ++iter) {
    bool ok = iterate(config, iter, deltaV);

    if (!ok) {
      result.status = NonlinearStatus::ERROR_SINGULAR_MATRIX;
      result.errorMessage = "Jacobian matrix is singular at iteration " + std::to_string(iter);
      result.iterations = iter + 1;
      return result;
    }

    if (config.enableDamping && iter < config.dampingIterations) {
      applyDamping(deltaV, config.dampingFactor);
    }

    for (std::size_t i = 0; i < netCount_; ++i) {
      voltages_[i] += deltaV[i];
    }

    double maxDeltaV = computeMaxDelta(deltaV);
    double maxVoltage = 0.0;
    for (double v : voltages_) {
      maxVoltage = std::max(maxVoltage, std::abs(v));
    }

    double maxResidual = computeKclResidual();

    if (config.isConverged(maxDeltaV, maxResidual, maxVoltage)) {
      result.status = NonlinearStatus::SUCCESS;
      result.iterations = iter + 1;
      result.finalError = maxDeltaV;
      result.nodeVoltages = voltages_;
      return result;
    }

    // Voltages above 1e6 V are treated as divergence; the MNA matrix is
    // ill-conditioned long before any physical circuit reaches this range.
    if (maxVoltage > 1e6) {
      result.status = NonlinearStatus::ERROR_VOLTAGE_DIVERGENCE;
      result.errorMessage = "Voltages diverging (max = " + std::to_string(maxVoltage) + "V)";
      result.iterations = iter + 1;
      result.finalError = maxDeltaV;
      return result;
    }

    result.finalError = maxDeltaV;
  }

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
  (void)config;
  (void)iteration;

  MnaSystem mna(netCount_);

  if (linearStampCallback_) {
    linearStampCallback_(mna);
  }

  devices_.stampAllLinearized(mna, voltages_);

  // J * delta(V) = -F. The MNA solve returns absolute voltages, so the
  // Newton update is the difference against the current iterate.
  MnaResult result = mna.solve();

  if (!result.success) {
    return false;
  }

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
  const auto& CURRENT = mna.currentVector();
  double maxResidual = 0.0;
  for (double i : CURRENT) {
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
  std::vector<double> netCurrents(netCount_, 0.0);

  for (const auto& device : devices_.devices()) {
    NetID pos = device->posNet();
    NetID neg = device->negNet();
    double vTerminal = voltages_[pos] - voltages_[neg];
    double current = device->current(vTerminal);

    if (pos < netCount_) {
      netCurrents[pos] -= current;
    }
    if (neg < netCount_) {
      netCurrents[neg] += current;
    }
  }

  // Skip net 0 (ground) so the residual does not include the constrained node.
  double maxResidual = 0.0;
  for (std::size_t i = 1; i < netCount_; ++i) {
    maxResidual = std::max(maxResidual, std::abs(netCurrents[i]));
  }

  return maxResidual;
}

} // namespace sim::electronics::algorithms::nonlinear

#endif // APEX_NEWTONRAPHSON_TPP
