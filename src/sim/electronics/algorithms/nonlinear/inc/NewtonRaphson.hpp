#ifndef APEX_NEWTONRAPHSON_HPP
#define APEX_NEWTONRAPHSON_HPP
/**
 * @file NewtonRaphson.hpp
 * @brief Newton-Raphson nonlinear solver for circuit simulation.
 *
 * Iteratively solves nonlinear circuits by linearizing devices around the
 * current operating point, solving the linearized MNA system, updating
 * voltages, and repeating until convergence.
 *
 * Algorithm:
 * 1. Initialize: V^0 = initial guess (DC voltages or previous timestep)
 * 2. Iterate:
 *    a. Linearize all nonlinear devices around V^k
 *    b. Build MNA system with linearized stamps
 *    c. Solve: J * delta(V) = -F(V^k)  where J is Jacobian, F is residual
 *    d. Update: V^{k+1} = V^k + delta(V) (with optional damping)
 *    e. Check convergence: |delta(V)| < tolerance
 * 3. Return: Converged solution V
 *
 * RT-safety: Can be made RT-safe with pre-allocated workspace.
 * CUDA readiness: Device evaluation and Jacobian assembly are parallel.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/nonlinear/inc/NonlinearConfig.hpp"
#include "src/sim/electronics/algorithms/nonlinear/inc/NonlinearDevice.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace sim::electronics::algorithms::nonlinear {

using algorithms::mna::MnaResult;
using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;

/* ----------------------------- StampCallback ----------------------------- */

/**
 * @brief Callback for stamping linear elements during Newton-Raphson.
 *
 * Called before each iteration to stamp resistors, voltage sources, etc.
 * These elements don't change during Newton-Raphson iteration.
 *
 * @param mna MNA system to stamp into.
 */
using LinearStampCallback = std::function<void(MnaSystem& mna)>;

/* ----------------------------- NewtonRaphsonSolver ----------------------------- */

/**
 * @brief Newton-Raphson nonlinear circuit solver.
 *
 * Solves circuits containing nonlinear devices (diodes, transistors, etc.)
 * by iteratively linearizing and solving until convergence.
 *
 * Usage:
 * ```cpp
 * NewtonRaphsonSolver solver(netCount);
 *
 * // Add nonlinear devices
 * solver.devices().addDevice(std::make_unique<DiodeModel>(1, 0, 1e-12, 0.026));
 *
 * // Set linear element stamp callback
 * solver.setLinearStampCallback([](MnaSystem& mna) {
 *   mna.addVoltageSource(1, 0, 5.0);  // 5V source
 *   mna.addConductance(1, 2, 1e3);     // 1k resistor
 * });
 *
 * // Solve
 * NonlinearConfig config;
 * NonlinearResult result = solver.solve(config);
 * ```
 */
class NewtonRaphsonSolver {
public:
  /**
   * @brief Construct solver for circuit with given net count.
   *
   * @param netCount Number of nets (including ground at 0).
   * @note NOT RT-safe: allocates internal storage.
   */
  explicit NewtonRaphsonSolver(std::size_t netCount);

  /* ----------------------------- Configuration ----------------------------- */

  /**
   * @brief Access nonlinear device set.
   * @return Reference to device set.
   */
  [[nodiscard]] NonlinearDeviceSet& devices() noexcept { return devices_; }
  [[nodiscard]] const NonlinearDeviceSet& devices() const noexcept { return devices_; }

  /**
   * @brief Set callback for stamping linear elements.
   *
   * @param cb Callback function.
   */
  void setLinearStampCallback(LinearStampCallback cb) { linearStampCallback_ = std::move(cb); }

  /**
   * @brief Set initial guess for node voltages.
   *
   * Good initial guess improves convergence. Use DC operating point from
   * previous solve or previous timestep in transient analysis.
   *
   * @param voltages Initial voltage guess for each node.
   * @note RT-safe: copies voltages to internal buffer.
   */
  void setInitialGuess(const std::vector<double>& voltages) { voltages_ = voltages; }

  /* ----------------------------- Solving ----------------------------- */

  /**
   * @brief Solve nonlinear circuit using Newton-Raphson.
   *
   * @param config Solver configuration (tolerances, iteration limits).
   * @return Result containing converged solution or error.
   * @note Can be RT-safe if workspace is pre-allocated.
   */
  NonlinearResult solve(const NonlinearConfig& config);

  /**
   * @brief Reset solver state.
   *
   * Clears voltages and prepares for new solve.
   * @note RT-safe: no allocations.
   */
  void reset() noexcept;

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get net count.
   * @return Number of nets.
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return netCount_; }

  /**
   * @brief Get current node voltages.
   * @return Voltage vector.
   */
  [[nodiscard]] const std::vector<double>& voltages() const noexcept { return voltages_; }

private:
  std::size_t netCount_;
  NonlinearDeviceSet devices_;
  LinearStampCallback linearStampCallback_;
  std::vector<double> voltages_; ///< Current voltage estimate (Newton-Raphson state).

  /**
   * @brief Perform single Newton-Raphson iteration.
   *
   * Linearizes devices, builds Jacobian, solves for voltage update.
   *
   * @param config Solver configuration.
   * @param iteration Current iteration number (for damping).
   * @param deltaV Output: voltage update for this iteration.
   * @return True if solve succeeded.
   */
  bool iterate(const NonlinearConfig& config, std::size_t iteration, std::vector<double>& deltaV);

  /**
   * @brief Compute maximum voltage change magnitude.
   *
   * @param deltaV Voltage update vector.
   * @return max(|delta(V)|) across all nodes.
   */
  [[nodiscard]] double computeMaxDelta(const std::vector<double>& deltaV) const noexcept;

  /**
   * @brief Compute maximum residual current.
   *
   * @param mna MNA system after linearization.
   * @return max(|I_residual|) across all nodes.
   */
  [[nodiscard]] double computeMaxResidual(const MnaSystem& mna) const noexcept;

  /**
   * @brief Apply damping to voltage update.
   *
   * Reduces update magnitude to improve convergence for oscillatory systems.
   *
   * @param deltaV Voltage update vector (modified in place).
   * @param dampingFactor Damping factor (0.5 = half-step).
   */
  void applyDamping(std::vector<double>& deltaV, double dampingFactor) const noexcept;

  /**
   * @brief Compute KCL residual error.
   *
   * Evaluates all devices at current voltages and computes net current at each node.
   * Returns maximum residual current magnitude (excluding ground).
   *
   * @return max(|I_residual|) across all non-ground nodes.
   */
  [[nodiscard]] double computeKclResidual() const noexcept;
};

} // namespace sim::electronics::algorithms::nonlinear

#include "src/sim/electronics/algorithms/nonlinear/src/NewtonRaphson.tpp"

#endif // APEX_NEWTONRAPHSON_HPP
