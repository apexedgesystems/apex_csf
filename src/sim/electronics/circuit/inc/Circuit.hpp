#ifndef APEX_SIM_ELECTRONICS_CIRCUIT_CIRCUIT_HPP
#define APEX_SIM_ELECTRONICS_CIRCUIT_CIRCUIT_HPP
/**
 * @file Circuit.hpp
 * @brief Common circuit construction and simulation API.
 *
 * Provides a unified interface for building circuits from net allocation
 * through simulation. Bridges the parts/ (MNA) and transient/ (time-stepping)
 * libraries into a single construction pattern.
 *
 * Usage:
 * 1. Create a Circuit instance
 * 2. Allocate nets with addNet()
 * 3. Register stamps with addStamp() (lambdas that stamp MNA system)
 * 4. Add reactive elements with addCapacitor() / addInductor()
 * 5. Call simulate() with a TransientConfig
 *
 * The same API handles circuits of any complexity -- from a single RC filter
 * to a full transistor-level CPU.
 *
 * @note Construction is NOT RT-safe (allocates).
 * @note Simulation RT-safety depends on TransientSolver configuration.
 *
 * Performance notes:
 * - Stamp functions are stored as std::function (~2-5 ns call overhead).
 *   MNA solve dominates at ~5-10 us per step, so stamp overhead is < 0.1%.
 * - Previous voltages are tracked via a single vector copy per solve step.
 *   For a 150-net circuit this is ~1.2 KB, negligible vs MNA cost.
 * - No heap allocation occurs per time step after build().
 *
 * CUDA expansion opportunity:
 * - The MNA solve (LU decomposition + back-substitution) is the dominant
 *   cost and is a candidate for GPU acceleration via cuSOLVER for large
 *   circuits (1000+ nets). The Circuit class abstracts the solver, so a
 *   GPU-backed MnaSystem could be substituted without changing the API.
 * - Stamp function evaluation is embarrassingly parallel across independent
 *   sub-circuits and could be batched onto GPU for multi-circuit simulation.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/devices/companions/inc/CompanionModels.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sim::electronics::circuit {

using sim::electronics::devices::companions::CompanionSet;
using sim::electronics::mna::MnaSystem;
using sim::electronics::transient::TransientConfig;
using sim::electronics::transient::TransientResult;
using sim::electronics::transient::TransientSolver;
using sim::electronics::transient::TransientState;
using sim::electronics::transient::TransientStatus;

/* ----------------------------- CircuitNet ----------------------------- */

/**
 * @brief Typed wrapper around a NetID returned by Circuit::addNet().
 *
 * Provides a named handle to a circuit net. The underlying id can be
 * passed directly to MNA stamping functions.
 */
struct CircuitNet {
  sim::electronics::mna::NetID id; ///< Underlying net identifier (0 = ground).
};

/* ----------------------------- Circuit ----------------------------- */

/**
 * @brief Circuit construction and simulation interface.
 *
 * Manages net allocation, stamp registration, companion models, and
 * solver lifecycle. Supports both simple DC-like circuits and complex
 * transient simulations with reactive elements.
 */
class Circuit {
public:
  /**
   * @brief Stamp function signature.
   *
   * Receives the MNA system to stamp into, current simulation time,
   * and node voltages from the previous time step. The prevVoltages
   * vector is zero-initialized for the first step and DC operating point.
   *
   * @param mna MNA system to stamp conductances, currents, voltage sources.
   * @param time Current simulation time in seconds.
   * @param prevVoltages Node voltages from the previous time step.
   */
  using StampFn =
      std::function<void(MnaSystem& mna, double time, const std::vector<double>& prevVoltages)>;

  /* ----------------------------- Net Allocation ----------------------------- */

  /**
   * @brief Allocate a new circuit net.
   * @return CircuitNet with a unique NetID (starting at 1; 0 is ground).
   *
   * @note Must be called before build(). Adding nets after build() is
   *       undefined behavior.
   * @note NOT RT-safe: may reallocate name storage.
   */
  CircuitNet addNet() { return CircuitNet{nextNetId_++}; }

  /**
   * @brief Allocate a named circuit net (for debugging / diagnostics).
   * @param name Human-readable net name.
   * @return CircuitNet with a unique NetID.
   *
   * Named nets can be queried via netName() for diagnostics output.
   * The name does not affect simulation behavior.
   *
   * @note NOT RT-safe: allocates string storage.
   */
  CircuitNet addNet(std::string_view name) {
    sim::electronics::mna::NetID id = nextNetId_++;
    if (id >= netNames_.size()) {
      netNames_.resize(id + 1);
    }
    netNames_[id] = std::string(name);
    return CircuitNet{id};
  }

  /**
   * @brief Get the ground net.
   * @return NetID 0 (always ground).
   */
  [[nodiscard]] static constexpr sim::electronics::mna::NetID ground() noexcept { return 0; }

  /**
   * @brief Get the current net count (including ground).
   * @return Number of nets allocated so far.
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return nextNetId_; }

  /**
   * @brief Get the name of a net (empty if unnamed).
   * @param id Net ID to query.
   * @return Net name, or empty string if unnamed or out of range.
   */
  [[nodiscard]] std::string_view netName(sim::electronics::mna::NetID id) const noexcept {
    if (id < netNames_.size()) {
      return netNames_[id];
    }
    return {};
  }

  /* ----------------------------- Stamp Registration ----------------------------- */

  /**
   * @brief Register a stamp function.
   * @param fn Function that stamps circuit elements into MNA system.
   *
   * Stamp functions are called in registration order at each time step.
   * They should stamp conductances, current sources, and voltage sources
   * for all non-reactive elements they manage.
   *
   * The lambda typically captures component instances and net IDs by value,
   * then calls the component's stamp() method with appropriate arguments
   * read from prevVoltages.
   *
   * @note Must be called before build(). Adding stamps after build() is
   *       undefined behavior.
   * @note NOT RT-safe: may reallocate stamp vector.
   */
  void addStamp(StampFn fn) { stamps_.push_back(std::move(fn)); }

  /**
   * @brief Get the number of registered stamp functions.
   * @return Stamp count.
   */
  [[nodiscard]] std::size_t stampCount() const noexcept { return stamps_.size(); }

  /* ----------------------------- Companion Management ----------------------------- */

  /**
   * @brief Add a capacitor companion model.
   * @param pos Positive terminal net.
   * @param neg Negative terminal net.
   * @param farads Capacitance in Farads.
   * @return Index of the added capacitor (for later access).
   *
   * @note NOT RT-safe: may reallocate companion storage.
   */
  std::size_t addCapacitor(sim::electronics::mna::NetID pos, sim::electronics::mna::NetID neg,
                           double farads) {
    return companions_.addCapacitor(pos, neg, farads);
  }

  /**
   * @brief Add an inductor companion model.
   * @param pos Positive terminal net.
   * @param neg Negative terminal net.
   * @param henries Inductance in Henries.
   * @return Index of the added inductor (for later access).
   *
   * @note NOT RT-safe: may reallocate companion storage.
   */
  std::size_t addInductor(sim::electronics::mna::NetID pos, sim::electronics::mna::NetID neg,
                          double henries) {
    return companions_.addInductor(pos, neg, henries);
  }

  /**
   * @brief Access companion models.
   * @return Mutable reference to the internal CompanionSet.
   *
   * Use this to set initial conditions on capacitors/inductors,
   * or to read companion state after simulation.
   */
  [[nodiscard]] CompanionSet& companions() noexcept { return companions_; }

  /**
   * @brief Access companion models (const).
   * @return Read-only reference to the internal CompanionSet.
   */
  [[nodiscard]] const CompanionSet& companions() const noexcept { return companions_; }

  /* ----------------------------- Build ----------------------------- */

  /**
   * @brief Finalize circuit and create the underlying TransientSolver.
   *
   * Wires all registered stamps into a single stateful stamp callback
   * on the solver and transfers companion models. After build(), the
   * solver is ready for stepping or simulation.
   *
   * Calling build() multiple times rebuilds the solver from scratch.
   *
   * @note NOT RT-safe: allocates solver internals.
   */
  void build() {
    solver_ = std::make_unique<TransientSolver>(nextNetId_);

    // Transfer companions to solver
    solver_->companions() = companions_;

    // Wire all registered stamps into a single stateful callback
    // Capture stamps_ by reference -- Circuit must outlive solver
    solver_->setStatefulStampCallback(
        [this](MnaSystem& mna, double time, const std::vector<double>& prevVoltages) {
          for (const auto& fn : stamps_) {
            fn(mna, time, prevVoltages);
          }
        });

    built_ = true;
  }

  /**
   * @brief Check if the circuit has been built.
   * @return True if build() has been called.
   */
  [[nodiscard]] bool isBuilt() const noexcept { return built_; }

  /* ----------------------------- Simulation ----------------------------- */

  /**
   * @brief Run transient simulation.
   * @param config Simulation parameters (time span, step size, method).
   * @param recordHistory If true, save all time points to result.history.
   * @return TransientResult with final state and optional history.
   *
   * Calls build() automatically if not already done. After simulation,
   * companion states in the solver reflect the final time step.
   *
   * @note NOT RT-safe: may allocate for history storage.
   */
  TransientResult simulate(const TransientConfig& config, bool recordHistory = false) {
    if (!built_) {
      build();
    }
    return solver_->run(config, recordHistory);
  }

  /**
   * @brief Execute a single time step.
   * @param dt Time step size in seconds.
   * @param state Current state (updated in place).
   * @return Status code.
   *
   * Calls build() automatically if not already done. Useful for
   * manual time-stepping when simulation control logic is external.
   */
  TransientStatus step(double dt, TransientState& state) {
    if (!built_) {
      build();
    }
    return solver_->step(dt, state);
  }

  /**
   * @brief Compute DC operating point.
   * @param state Output state for DC solution.
   * @return Status code.
   *
   * Calls build() automatically if not already done.
   */
  TransientStatus computeDC(TransientState& state) {
    if (!built_) {
      build();
    }
    return solver_->computeDC(state);
  }

  /* ----------------------------- Solver Access ----------------------------- */

  /**
   * @brief Access the underlying TransientSolver.
   * @return Reference to the solver.
   *
   * Calls build() automatically if not already done. Use this for
   * advanced configuration (cached LU, dual-LU, etc.).
   *
   * @note The solver's companion set is a copy of the Circuit's companions
   *       at build time. Modifications to the solver's companions after
   *       build() do not propagate back to the Circuit.
   */
  TransientSolver& solver() {
    if (!built_) {
      build();
    }
    return *solver_;
  }

  /* ----------------------------- Reset ----------------------------- */

  /**
   * @brief Reset circuit to pre-build state.
   *
   * Destroys the solver and clears the built flag. Nets, stamps, and
   * companions are preserved. Call build() or simulate() to re-create
   * the solver.
   *
   * Use this to re-simulate with different initial conditions by
   * modifying companions between reset() and simulate().
   */
  void resetSolver() noexcept {
    solver_.reset();
    built_ = false;
  }

private:
  sim::electronics::mna::NetID nextNetId_ = 1; ///< Next available net ID (0 is ground).
  std::vector<std::string> netNames_;          ///< Optional net names for diagnostics.
  std::vector<StampFn> stamps_;                ///< Registered stamp functions.
  CompanionSet companions_;                    ///< Reactive element companions.
  std::unique_ptr<TransientSolver> solver_;    ///< Underlying solver (created by build()).
  bool built_ = false;                         ///< True after build() called.
};

} // namespace sim::electronics::circuit

#endif // APEX_SIM_ELECTRONICS_CIRCUIT_CIRCUIT_HPP
