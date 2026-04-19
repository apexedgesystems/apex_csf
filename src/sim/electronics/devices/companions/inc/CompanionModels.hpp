#ifndef APEX_SIM_ELECTRONICS_DEVICES_COMPANIONS_COMPANIONMODELS_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_COMPANIONS_COMPANIONMODELS_HPP
/**
 * @file CompanionModels.hpp
 * @brief Device-specific companion models for reactive elements (Layer 2).
 *
 * Provides integration wrappers for capacitors and inductors that convert
 * differential equations (I=C*dV/dt, V=L*dI/dt) into equivalent resistor +
 * current source circuits using numerical integration methods from Layer 1.
 *
 * These companions can be stamped into the MNA matrix for time-stepping
 * simulation.
 *
 * Discretization methods:
 * - Backward Euler: First-order, A-stable, energy dissipative
 * - Trapezoidal:    Second-order, A-stable, energy conserving
 * - GEAR2 (BDF2):   Second-order, L-stable, stiff systems (requires history)
 *
 * The companion model stamps:
 * - Conductance Geq between the element terminals
 * - Current source Ieq representing historical state
 *
 * RT-safety: All methods are RT-safe (no allocations) after initialization.
 *
 * CUDA readiness:
 * - Companion models use value semantics (POD-like structs)
 * - geq() and ieq() methods are constexpr-compatible for GPU kernels
 * - stamp() methods can be batched for parallel execution on GPU
 * - Future: CompanionSetCuda with parallel stamp kernels in .cu file
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"

#include <cstddef>
#include <vector>

namespace sim::electronics::devices::companions {

using mna::MnaSystem;
using mna::MnaSystemSparse;
using mna::NetID;
using transient::IntegrationMethod;

/* ----------------------------- CapacitorCompanion ----------------------------- */

/**
 * @brief Companion model for a capacitor.
 *
 * Physical: I = C * dV/dt (current from pos to neg when V increasing)
 *
 * Backward Euler discretization:
 *   I(t) = C * (V(t) - V(t-dt)) / dt
 *        = Geq * V(t) - Geq * V(t-dt)
 *
 * Where:
 *   Geq = C / dt           (equivalent conductance)
 *   Ieq = C * V(t-dt) / dt (equivalent current source flowing into posNet)
 *
 * Stamps as a resistor (Geq) in parallel with a current source (Ieq).
 * The current source injects Ieq into posNet, representing the stored charge.
 */
struct CapacitorCompanion {
  NetID posNet = 0;         ///< Positive terminal net.
  NetID negNet = 0;         ///< Negative terminal net.
  double capacitance = 0.0; ///< Capacitance in Farads.

  double prevVoltage = 0.0;  ///< Voltage at t-dt (previous time step).
  double prev2Voltage = 0.0; ///< Voltage at t-2dt (for GEAR2/BDF2).
  double current = 0.0;      ///< Current through capacitor (for output).

  /**
   * @brief Calculate equivalent conductance for given time step and method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent conductance.
   */
  [[nodiscard]] double
  geq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return capacitance / dt; // C/dt
    case IntegrationMethod::TRAPEZOIDAL:
      return 2.0 * capacitance / dt; // 2C/dt
    case IntegrationMethod::GEAR2:
      return 1.5 * capacitance / dt; // 3C/(2dt)
    }
    return capacitance / dt;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value (injected into posNet).
   */
  [[nodiscard]] double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return capacitance * prevVoltage / dt; // C*V_prev/dt
    case IntegrationMethod::TRAPEZOIDAL:
      return 2.0 * capacitance * prevVoltage / dt + current; // 2C*V_prev/dt + I_prev
    case IntegrationMethod::GEAR2:
      // BDF2: Ieq = (C/dt) * (2*V(t-dt) - 0.5*V(t-2dt))
      return (capacitance / dt) * (2.0 * prevVoltage - 0.5 * prev2Voltage);
    }
    return capacitance * prevVoltage / dt;
  }

  /**
   * @brief Stamp companion model into MNA system.
   * @param mna MNA system to stamp into.
   * @param dt Time step in seconds.
   * @param method Integration method.
   *
   * Stamps:
   * - Conductance Geq between terminals
   * - Current source Ieq from pos to neg
   */
  void stamp(MnaSystem& mna, double dt,
             IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    double g = geq(dt, method);
    double i = ieq(dt, method);

    mna.addConductance(posNet, negNet, g);
    mna.addCurrent(posNet, negNet, i);
  }

  /**
   * @brief Stamp companion model into sparse MNA system.
   * @param mna Sparse MNA system to stamp into.
   * @param dt Time step in seconds.
   * @param method Integration method.
   */
  void stamp(MnaSystemSparse& mna, double dt,
             IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    double g = geq(dt, method);
    double i = ieq(dt, method);

    mna.addConductance(posNet, negNet, g);
    mna.addCurrent(posNet, negNet, i);
  }

  /**
   * @brief Update state after time step.
   * @param newVoltage Voltage across capacitor (Vpos - Vneg).
   * @param dt Time step for current calculation.
   */
  void update(double newVoltage, double dt) noexcept {
    current = capacitance * (newVoltage - prevVoltage) / dt;
    prev2Voltage = prevVoltage; // Shift history for GEAR2
    prevVoltage = newVoltage;
  }
};

/* ----------------------------- InductorCompanion ----------------------------- */

/**
 * @brief Companion model for an inductor.
 *
 * Physical: V = L * dI/dt
 *
 * Backward Euler discretization:
 *   V(t) = L * (I(t) - I(t-dt)) / dt
 *   I(t) = (dt/L) * V(t) + I(t-dt)
 *        = Geq * V(t) + Ieq
 *
 * Where:
 *   Geq = dt / L           (equivalent conductance)
 *   Ieq = I(t-dt)          (equivalent current source)
 *
 * Stamps as a resistor (Geq) in parallel with a current source (Ieq).
 */
struct InductorCompanion {
  NetID posNet = 0;        ///< Positive terminal net.
  NetID negNet = 0;        ///< Negative terminal net.
  double inductance = 0.0; ///< Inductance in Henries.

  double prevCurrent = 0.0;  ///< Current at t-dt (previous time step).
  double prev2Current = 0.0; ///< Current at t-2dt (for GEAR2/BDF2).
  double voltage = 0.0;      ///< Voltage across inductor (for output).

  /**
   * @brief Calculate equivalent conductance for given time step and method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent conductance.
   */
  [[nodiscard]] double
  geq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return dt / inductance; // dt/L
    case IntegrationMethod::TRAPEZOIDAL:
      return dt / (2.0 * inductance); // dt/(2L)
    case IntegrationMethod::GEAR2:
      return 1.5 * dt / inductance; // 3dt/(2L) -- BDF2 uses different discretization
    }
    return dt / inductance;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value.
   */
  [[nodiscard]] double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return prevCurrent; // I_prev
    case IntegrationMethod::TRAPEZOIDAL:
      return prevCurrent + (dt / (2.0 * inductance)) * voltage; // I_prev + (dt/(2L))*V_prev
    case IntegrationMethod::GEAR2:
      // BDF2: Ieq = 2*I(t-dt) - 0.5*I(t-2dt)
      return 2.0 * prevCurrent - 0.5 * prev2Current;
    }
    return prevCurrent;
  }

  /**
   * @brief Stamp companion model into MNA system.
   * @param mna MNA system to stamp into.
   * @param dt Time step in seconds.
   * @param method Integration method.
   *
   * The current source Ieq represents previous current flowing from pos to neg.
   * addCurrent(negNet, posNet, Ieq) injects Ieq into negNet, representing
   * current arriving at negNet from posNet.
   */
  void stamp(MnaSystem& mna, double dt,
             IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    double g = geq(dt, method);
    double i = ieq(dt, method);

    mna.addConductance(posNet, negNet, g);
    // Current source: Ieq flows from posNet to negNet
    // addCurrent(a, b, i) injects into a, removes from b
    // So inject into negNet (current arrives there)
    mna.addCurrent(negNet, posNet, i);
  }

  /**
   * @brief Stamp companion model into sparse MNA system.
   * @param mna Sparse MNA system to stamp into.
   * @param dt Time step in seconds.
   * @param method Integration method.
   */
  void stamp(MnaSystemSparse& mna, double dt,
             IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    double g = geq(dt, method);
    double i = ieq(dt, method);

    mna.addConductance(posNet, negNet, g);
    mna.addCurrent(negNet, posNet, i);
  }

  /**
   * @brief Update state after time step.
   * @param newVoltage Voltage across inductor (Vpos - Vneg).
   * @param dt Time step for current calculation.
   */
  void update(double newVoltage, double dt) noexcept {
    voltage = newVoltage;
    // I(t) = I(t-dt) + (dt/L) * V(t)
    double newCurrent = prevCurrent + (dt / inductance) * newVoltage;
    prev2Current = prevCurrent; // Shift history for GEAR2
    prevCurrent = newCurrent;
  }
};

/* ----------------------------- CompanionSet ----------------------------- */

/**
 * @brief Collection of companion models for a circuit.
 *
 * Manages all reactive element companions and provides bulk stamping
 * and update operations.
 *
 * Usage in circuit simulation:
 * ```cpp
 * // Circuit setup:
 * TransientSolver solver(netCount);
 * solver.companions().addCapacitor(nodeA, nodeB, capacitance);
 * solver.companions().addInductor(nodeC, nodeD, inductance);
 *
 * // Set integration method (default: backward Euler):
 * solver.setIntegrationMethod(IntegrationMethod::TRAPEZOIDAL);
 *
 * // Run simulation:
 * TransientConfig config;
 * config.method = IntegrationMethod::TRAPEZOIDAL;
 * config.tStep = 1e-9;
 * config.tEnd = 1e-6;
 * TransientResult result = solver.run(config);
 * ```
 *
 * Integration method selection guidelines:
 * - BACKWARD_EULER: Fast, stable, but energy dissipative (use for digital circuits)
 * - TRAPEZOIDAL: Slower, energy conserving (use for analog oscillators, filters)
 * - GEAR2: Best for stiff systems (future: requires history implementation)
 */
class CompanionSet {
public:
  /**
   * @brief Add a capacitor companion.
   * @param posNet Positive terminal.
   * @param negNet Negative terminal.
   * @param capacitance Value in Farads.
   * @return Index of added capacitor.
   * @note NOT RT-safe: resizes internal vector.
   */
  std::size_t addCapacitor(NetID posNet, NetID negNet, double capacitance) {
    capacitors_.push_back({posNet, negNet, capacitance, 0.0, 0.0});
    return capacitors_.size() - 1;
  }

  /**
   * @brief Add an inductor companion.
   * @param posNet Positive terminal.
   * @param negNet Negative terminal.
   * @param inductance Value in Henries.
   * @return Index of added inductor.
   * @note NOT RT-safe: resizes internal vector.
   */
  std::size_t addInductor(NetID posNet, NetID negNet, double inductance) {
    inductors_.push_back({posNet, negNet, inductance, 0.0, 0.0});
    return inductors_.size() - 1;
  }

  /**
   * @brief Stamp all companion models into MNA system.
   * @param mna MNA system.
   * @param dt Time step.
   * @param method Integration method.
   * @note RT-safe: no allocations, iterates pre-sized vectors.
   */
  void stampAll(MnaSystem& mna, double dt,
                IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    for (const auto& cap : capacitors_) {
      cap.stamp(mna, dt, method);
    }
    for (const auto& ind : inductors_) {
      ind.stamp(mna, dt, method);
    }
  }

  /**
   * @brief Stamp all companion models into sparse MNA system.
   * @param mna Sparse MNA system.
   * @param dt Time step.
   * @param method Integration method.
   * @note RT-safe: no allocations, iterates pre-sized vectors.
   */
  void stampAll(MnaSystemSparse& mna, double dt,
                IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    for (const auto& cap : capacitors_) {
      cap.stamp(mna, dt, method);
    }
    for (const auto& ind : inductors_) {
      ind.stamp(mna, dt, method);
    }
  }

  /**
   * @brief Stamp only conductances (for matrix building).
   *
   * Used for cached LU optimization: stamp conductances once for matrix
   * factorization, then use stampCurrentAll() for RHS updates.
   *
   * @param mna MNA system.
   * @param dt Time step (determines Geq based on integration method).
   * @param method Integration method.
   * @note RT-safe: no allocations.
   */
  void stampConductanceAll(MnaSystem& mna, double dt,
                           IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    for (const auto& cap : capacitors_) {
      mna.addConductance(cap.posNet, cap.negNet, cap.geq(dt, method));
    }
    for (const auto& ind : inductors_) {
      mna.addConductance(ind.posNet, ind.negNet, ind.geq(dt, method));
    }
  }

  /**
   * @brief Stamp only current sources (for RHS update).
   *
   * Used for cached LU optimization: after matrix is factorized,
   * only the current sources need to be updated each step.
   *
   * @param mna MNA system.
   * @param dt Time step.
   * @param method Integration method.
   * @note RT-safe: no allocations.
   */
  void stampCurrentAll(MnaSystem& mna, double dt,
                       IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const {
    for (const auto& cap : capacitors_) {
      mna.addCurrent(cap.posNet, cap.negNet, cap.ieq(dt, method));
    }
    for (const auto& ind : inductors_) {
      // Inductor current flows from pos to neg
      mna.addCurrent(ind.negNet, ind.posNet, ind.ieq(dt, method));
    }
  }

  /**
   * @brief Update all companion states after solving.
   * @param nodeVoltages Solved node voltages.
   * @param dt Time step.
   * @note RT-safe: no allocations, modifies pre-sized vectors.
   */
  void updateAll(const std::vector<double>& nodeVoltages, double dt) {
    for (auto& cap : capacitors_) {
      double vPos = (cap.posNet < nodeVoltages.size()) ? nodeVoltages[cap.posNet] : 0.0;
      double vNeg = (cap.negNet < nodeVoltages.size()) ? nodeVoltages[cap.negNet] : 0.0;
      cap.update(vPos - vNeg, dt);
    }
    for (auto& ind : inductors_) {
      double vPos = (ind.posNet < nodeVoltages.size()) ? nodeVoltages[ind.posNet] : 0.0;
      double vNeg = (ind.negNet < nodeVoltages.size()) ? nodeVoltages[ind.negNet] : 0.0;
      ind.update(vPos - vNeg, dt);
    }
  }

  /**
   * @brief Reset all companion states to zero.
   * @note RT-safe: no allocations.
   */
  void reset() noexcept {
    for (auto& cap : capacitors_) {
      cap.prevVoltage = 0.0;
      cap.prev2Voltage = 0.0;
      cap.current = 0.0;
    }
    for (auto& ind : inductors_) {
      ind.prevCurrent = 0.0;
      ind.prev2Current = 0.0;
      ind.voltage = 0.0;
    }
  }

  /**
   * @brief Initialize capacitor voltages from DC solution.
   * @param nodeVoltages DC operating point voltages.
   * @note RT-safe: no allocations.
   */
  void initializeFromDC(const std::vector<double>& nodeVoltages) {
    for (auto& cap : capacitors_) {
      double vPos = (cap.posNet < nodeVoltages.size()) ? nodeVoltages[cap.posNet] : 0.0;
      double vNeg = (cap.negNet < nodeVoltages.size()) ? nodeVoltages[cap.negNet] : 0.0;
      cap.prevVoltage = vPos - vNeg;
      cap.current = 0.0; // No current in steady state
    }
    // Inductors: steady-state current would require additional info
    // For now, assume zero initial current
  }

  /* ----------------------- Accessors ----------------------- */

  [[nodiscard]] std::size_t capacitorCount() const noexcept { return capacitors_.size(); }

  [[nodiscard]] std::size_t inductorCount() const noexcept { return inductors_.size(); }

  [[nodiscard]] const CapacitorCompanion& capacitor(std::size_t idx) const {
    return capacitors_.at(idx);
  }

  [[nodiscard]] const InductorCompanion& inductor(std::size_t idx) const {
    return inductors_.at(idx);
  }

  [[nodiscard]] CapacitorCompanion& capacitor(std::size_t idx) { return capacitors_.at(idx); }

  [[nodiscard]] InductorCompanion& inductor(std::size_t idx) { return inductors_.at(idx); }

private:
  std::vector<CapacitorCompanion> capacitors_;
  std::vector<InductorCompanion> inductors_;
};

} // namespace sim::electronics::devices::companions

#endif // APEX_SIM_ELECTRONICS_DEVICES_COMPANIONS_COMPANIONMODELS_HPP
