#ifndef APEX_SIM_ELECTRONICS_TRANSIENT_TRANSIENTSOLVER_HPP
#define APEX_SIM_ELECTRONICS_TRANSIENT_TRANSIENTSOLVER_HPP
/**
 * @file TransientSolver.hpp
 * @brief Time-domain circuit simulation engine.
 *
 * Advances circuit state through time using companion models for reactive
 * elements (capacitors, inductors). Supports multiple integration methods
 * and interfaces with the MNA solver for each time step.
 *
 * Usage:
 * 1. Create TransientSolver with net count
 * 2. Add reactive elements via companions()
 * 3. Set up static stamping callback for resistors, sources
 * 4. Call run() with configuration
 *
 * @note Initialization is NOT RT-safe (allocates).
 * @note step() can be made RT-safe with pre-allocated workspace.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/devices/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace sim::electronics::transient {

using devices::companions::CompanionSet;
using mna::MnaFactorizedWorkspace;
using mna::MnaResult;
using mna::MnaSolveWorkspace;
using mna::MnaSystem;
using mna::MnaSystemSparse;
using mna::NetID;

/* ----------------------------- StampCallback ----------------------------- */

/**
 * @brief Callback type for stamping static circuit elements.
 *
 * Called before each solve to stamp resistors, voltage sources, etc.
 * The callback should stamp all non-reactive elements.
 *
 * @param mna MNA system to stamp into.
 * @param time Current simulation time.
 */
using StampCallback = std::function<void(MnaSystem& mna, double time)>;

/**
 * @brief Stateful stamp callback that also receives previous node voltages.
 *
 * Called before each solve to stamp all circuit elements. The prevVoltages
 * vector contains node voltages from the previous time step (or zeros for
 * the first step / DC operating point).
 *
 * @param mna MNA system to stamp into.
 * @param time Current simulation time.
 * @param prevVoltages Node voltages from previous time step.
 */
using StatefulStampCallback =
    std::function<void(MnaSystem& mna, double time, const std::vector<double>& prevVoltages)>;

/**
 * @brief Stateful stamp callback for sparse MNA system.
 *
 * Same as StatefulStampCallback but for sparse matrix representation.
 *
 * @param mna Sparse MNA system to stamp into.
 * @param time Current simulation time.
 * @param prevVoltages Node voltages from previous time step.
 */
using StatefulStampCallbackSparse =
    std::function<void(MnaSystemSparse& mna, double time, const std::vector<double>& prevVoltages)>;

/* ----------------------------- TransientSolver ----------------------------- */

/**
 * @brief Time-domain transient simulation engine.
 *
 * Implements multiple integration methods for circuit simulation using
 * companion models for reactive elements (capacitors, inductors).
 *
 * Supported integration methods:
 * - BACKWARD_EULER: First-order implicit, A-stable, energy dissipative
 * - TRAPEZOIDAL:    Second-order implicit, A-stable, energy conserving
 * - GEAR2 (BDF2):   Second-order implicit, L-stable (history pending)
 *
 * The integration method is set via TransientConfig::method or by calling
 * setIntegrationMethod() before simulation. The method affects how companion
 * models discretize reactive elements (geq() and ieq() calculations).
 *
 * Usage example:
 * ```cpp
 * TransientSolver solver(netCount);
 * solver.companions().addCapacitor(1, 0, 1e-6);
 * solver.setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);
 * TransientResult result = solver.run(config);
 * ```
 *
 * Performance optimizations:
 * - Cached LU factorization for constant-topology circuits
 * - Sparse LU solver (Eigen::SparseLU or KLU) for low-fill matrices
 * - CUDA acceleration via MnaSystemCuda (future: companion batch stamping)
 */
class TransientSolver {
public:
  /**
   * @brief Construct solver for a circuit with given net count.
   * @param netCount Number of nets (including ground at 0).
   * @note NOT RT-safe: allocates internal storage.
   */
  explicit TransientSolver(std::size_t netCount);

  /* ----------------------------- Configuration ----------------------------- */

  /**
   * @brief Access companion models for adding reactive elements.
   * @return Reference to companion set.
   * @note RT-safe: accessor.
   */
  [[nodiscard]] CompanionSet& companions() noexcept { return companions_; }
  [[nodiscard]] const CompanionSet& companions() const noexcept { return companions_; }

  /**
   * @brief Set callback for stamping static elements.
   * @param cb Callback function.
   *
   * This callback is invoked at each time step before solving.
   * It should stamp all resistors, voltage sources, current sources, etc.
   * @note NOT RT-safe: moves std::function (may allocate).
   */
  void setStampCallback(StampCallback cb) { stampCallback_ = std::move(cb); }

  /**
   * @brief Set stateful callback for stamping circuit elements.
   * @param cb Callback function receiving MNA system, time, and previous voltages.
   *
   * Stateful callbacks receive previous node voltages, allowing components
   * to read their terminal voltages from the prior time step. This is
   * required by components whose stamp depends on prior state (e.g.,
   * digital registers, full adders).
   *
   * @note Mutually exclusive with setStampCallback(). If both are set,
   *       the stateful callback takes priority.
   * @note NOT RT-safe: moves std::function (may allocate).
   */
  void setStatefulStampCallback(StatefulStampCallback cb) {
    statefulStampCallback_ = std::move(cb);
  }

  /**
   * @brief Set stateful callback for sparse stamping.
   * @param cb Callback function for sparse MNA system.
   *
   * Used when sparse solver is enabled. If not set, sparse mode will fail.
   * @note NOT RT-safe: moves std::function (may allocate).
   */
  void setStatefulStampCallbackSparse(StatefulStampCallbackSparse cb) {
    statefulStampCallbackSparse_ = std::move(cb);
  }

  /* ----------------------------- Simulation ----------------------------- */

  /**
   * @brief Run transient simulation with given configuration.
   * @param config Simulation parameters.
   * @param recordHistory If true, save all time points to result.history.
   * @return Simulation result with final state and optional history.
   *
   * @note NOT RT-safe: may allocate for history storage.
   */
  TransientResult run(const TransientConfig& config, bool recordHistory = false);

  /**
   * @brief Execute a single time step.
   * @param dt Time step size in seconds.
   * @param state Current state (updated in place).
   * @return Status code.
   *
   * @note Can be RT-safe if workspace is pre-allocated.
   */
  TransientStatus step(double dt, TransientState& state);

  /**
   * @brief Compute DC operating point.
   * @param state Output state for DC solution.
   * @return Status code.
   *
   * Solves the circuit with capacitors open and inductors shorted.
   * @note NOT RT-safe: allocates MNA workspace.
   */
  TransientStatus computeDC(TransientState& state);

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get current simulation time.
   * @return Time in seconds.
   * @note RT-safe: accessor.
   */
  [[nodiscard]] double time() const noexcept { return time_; }

  /**
   * @brief Get net count.
   * @return Number of nets.
   * @note RT-safe: accessor.
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return netCount_; }

  /**
   * @brief Reset solver state.
   *
   * Clears time, resets companions, prepares for new simulation.
   * @note RT-safe: resets scalar fields only.
   */
  void reset() noexcept;

  /**
   * @brief Set integration method for companion models.
   * @param method Integration method to use.
   *
   * This affects the discretization of reactive elements (capacitors, inductors).
   * Default is Backward Euler. Must be set before run() or step() calls.
   * @note RT-safe: sets scalar field.
   */
  void setIntegrationMethod(IntegrationMethod method) noexcept { method_ = method; }

  /**
   * @brief Get current integration method.
   * @return Integration method being used.
   * @note RT-safe: accessor.
   */
  [[nodiscard]] IntegrationMethod integrationMethod() const noexcept { return method_; }

private:
  std::size_t netCount_;
  double time_ = 0.0;
  IntegrationMethod method_ = IntegrationMethod::BACKWARD_EULER;
  CompanionSet companions_;
  StampCallback stampCallback_;
  StatefulStampCallback statefulStampCallback_;
  StatefulStampCallbackSparse statefulStampCallbackSparse_;
  std::vector<double> prevVoltages_;
  MnaSolveWorkspace workspace_;

  // Cached LU optimization (disabled by default - enable for constant-topology circuits)
  MnaFactorizedWorkspace factorizedWs_;
  std::unique_ptr<MnaSystem> cachedMna_;
  double cachedDt_ = 0.0;
  bool useCachedLU_ = false; // Disabled by default (enable for constant topology)

  // Dual-LU caching for alternating topology (clock HIGH/LOW)
  MnaFactorizedWorkspace dualFactorizedWs_[2]; // [0]=state0, [1]=state1
  std::unique_ptr<MnaSystem> dualCachedMna_[2];
  double dualCachedDt_ = 0.0;
  bool useDualLU_ = false;

  // Sparse LU solver (KLU for low-fill circuits)
  std::unique_ptr<MnaSystemSparse> sparseMna_;
  bool useSparse_ = false;
  bool sparseCacheValid_ = false;   ///< Sparse LU cache state (invalidated per half-clock).
  std::size_t sparseCachedDim_ = 0; ///< Augmented dimension of cached sparse factorization.
  bool alwaysReanalyze_ = false;    ///< Force full symbolic+numeric factorize every step.

  // Newton-Raphson iteration for nonlinear transient (used with alwaysReanalyze_)
  std::size_t nrMaxIterations_ = 10; ///< Max NR iterations per timestep.
  double nrVoltageTolerance_ = 1e-6; ///< Convergence: ngspice CKTvoltTol (absolute component).
  std::vector<double> nrPrevIterV_;  ///< Previous NR iteration voltages (workspace).

  /// Post-solve device limiting callback. Called after each NR matrix solve
  /// to apply per-device voltage limiting (fetlim). The callback receives
  /// the new node voltages and the previous iteration voltages, and should
  /// modify the new voltages in-place to apply device-specific limits.
  /// This is how ngspice integrates DEVfetlim into the NR loop.
  using NrLimitCallback =
      std::function<void(std::vector<double>& newV, const std::vector<double>& prevV)>;
  NrLimitCallback nrLimitCallback_;

  /// Called before each NR iteration batch (once per timestep/sub-step).
  /// Receives the actual sub-step dt (may differ from original dt due to
  /// timestep control). Used by the stamp callback to update parasitic cap
  /// conductance and reset per-timestep state.
  std::function<void(double subDt)> nrPreBatchCallback_;

  /**
   * @brief Invoke the appropriate stamp callback.
   * @param mna MNA system to stamp into.
   * @param time Current simulation time.
   *
   * Prefers statefulStampCallback_ (with prevVoltages) if set,
   * falls back to stampCallback_ (without prevVoltages).
   */
  void invokeStampCallback(MnaSystem& mna, double time);

  /**
   * @brief Build and solve MNA system for current time step.
   * @param dt Time step.
   * @param state Output state.
   * @return Status code.
   */
  TransientStatus solveStep(double dt, TransientState& state);

  /**
   * @brief Build matrix and factorize for cached LU solve.
   * @param dt Time step.
   * @return true if factorization succeeded.
   */
  bool buildAndFactorize(double dt);

  /**
   * @brief Capture LU factors from workspace after dgesv.
   * @param dt Time step (stored for cache validity check).
   * @param dim Augmented matrix dimension (n + m).
   *
   * Copies the LU factors and pivot indices from workspace_ (stride ld)
   * into factorizedWs_ (stride dim) after a dgesv call. This avoids
   * the O(n^3) cost of buildAndFactorize by reusing the factorization
   * that dgesv already computed. Cost: O(n^2) memcpy vs O(n^3) dgetrf.
   */
  void captureLU(double dt, std::size_t dim);

  /**
   * @brief Solve using cached LU factorization.
   * @param dt Time step (must match cachedDt_).
   * @param state Output state.
   * @return Status code.
   */
  TransientStatus solveCachedStep(double dt, TransientState& state);

  /**
   * @brief Solve sparse path using cached SparseLU factorization.
   *
   * Stamps into MnaSystemSparse, computes A*x from triplets (O(nnz)),
   * builds residual, and solves with cached SparseLU factors.
   * Avoids the expensive buildAugmentedMatrix + refactorize per cached step.
   *
   * @param dt Time step.
   * @param state Output state.
   * @return Status code.
   */
  TransientStatus solveCachedStepSparse(double dt, TransientState& state);

public:
  /// Mutable access to previous voltages (for clock forcing, Entry 14).
  /// @note RT-safe: accessor.
  std::vector<double>& prevVoltages() { return prevVoltages_; }
  /**
   * @brief Enable or disable cached LU optimization.
   * @param enable True to enable (default), false to use full solve each step.
   *
   * Cached LU provides ~6-8x speedup when circuit topology is constant.
   * Disable if topology changes dynamically during simulation.
   * @note RT-safe: sets boolean flag.
   */
  void setCachedLU(bool enable) noexcept { useCachedLU_ = enable; }

  /**
   * @brief Invalidate cached LU factorization.
   *
   * Call this if circuit topology changes (e.g., switch opens/closes).
   * The next step will re-factorize the matrix.
   * @note RT-safe: resets scalar fields only.
   */
  void invalidateCache() noexcept {
    factorizedWs_.invalidate();
    cachedDt_ = 0.0;
    sparseCacheValid_ = false;
  }

  /**
   * @brief Enable dual-LU caching for alternating topology circuits.
   * @param enable True to enable.
   *
   * Use this for circuits that alternate between two states (e.g., clock HIGH/LOW).
   * Both states will be factorized on first use, then O(n^2) back-substitution.
   *
   * @note Mutually exclusive with setCachedLU(). Enabling this disables single cache.
   * @note RT-safe: sets boolean flags.
   */
  void setDualLU(bool enable) noexcept {
    useDualLU_ = enable;
    if (enable)
      useCachedLU_ = false;
  }

  /**
   * @brief Enable sparse LU solver via Eigen::SparseLU.
   * @param enable True to enable sparse solve path.
   *
   * For circuits with low fill ratio (< 5%), sparse LU provides 10-50x
   * speedup over dense LAPACK by only processing non-zero matrix entries.
   * The 150-net Apex4Grid has ~2% fill (450 non-zeros in 22,500 entries).
   * @note RT-safe: sets boolean flag (allocation deferred to first solve).
   */
  void setSparse(bool enable) noexcept { useSparse_ = enable; }

  /**
   * @brief Force full symbolic reanalysis on every sparse solve step.
   * @param enable True to always reanalyze.
   *
   * Required when device stamps produce voltage-dependent sparsity patterns
   * (e.g., Level 1 MOSFET symmetric drain/source swap). Without this, the
   * 3-level cache may reuse a stale symbolic analysis when nnz coincidentally
   * matches but the CSC pattern differs, causing KLU to segfault.
   * @note RT-safe: sets boolean flag.
   */
  void setAlwaysReanalyze(bool enable) noexcept { alwaysReanalyze_ = enable; }

  /**
   * @brief Set per-device NR voltage limiting callback.
   *
   * Called after each NR matrix solve. The callback applies device-specific
   * voltage limiting (e.g., fetlim for MOSFETs) by modifying the new node
   * voltages in-place. This matches ngspice's per-device limiting approach.
   * @note NOT RT-safe: moves std::function (may allocate).
   */
  void setNrLimitCallback(NrLimitCallback cb) { nrLimitCallback_ = std::move(cb); }

  /// Set pre-batch callback (called once before each NR iteration batch).
  /// @note NOT RT-safe: moves std::function (may allocate).
  void setNrPreBatchCallback(std::function<void(double)> cb) {
    nrPreBatchCallback_ = std::move(cb);
  }

  /**
   * @brief Step with dual-LU cache, selecting state based on index.
   * @param dt Time step.
   * @param stateIdx Which cached state to use (0 or 1).
   * @param state Output state.
   * @return Status code.
   *
   * On first call for each stateIdx, builds and factorizes.
   * Subsequent calls use O(n^2) back-substitution.
   * @note NOT RT-safe: first call per stateIdx allocates and factorizes.
   */
  TransientStatus stepDual(double dt, int stateIdx, TransientState& state);

private:
  /**
   * @brief Build and factorize for dual-LU cache.
   * @param dt Time step.
   * @param stateIdx Which state to build (0 or 1).
   * @return true if successful.
   */
  bool buildAndFactorizeDual(double dt, int stateIdx);

  /**
   * @brief Solve using dual-LU cache.
   * @param dt Time step.
   * @param stateIdx Which cache to use.
   * @param state Output state.
   * @return Status code.
   */
  TransientStatus solveDualCachedStep(double dt, int stateIdx, TransientState& state);
};

} // namespace sim::electronics::transient

#include "src/sim/electronics/algorithms/transient/src/TransientSolver.tpp"

#endif // APEX_SIM_ELECTRONICS_TRANSIENT_TRANSIENTSOLVER_HPP
