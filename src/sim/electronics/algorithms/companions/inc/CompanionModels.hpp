#ifndef APEX_COMPANIONMODELS_HPP
#define APEX_COMPANIONMODELS_HPP
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
 * - GEAR2 (BDF2):   Second-order, L-stable, stiff systems (two steps of history)
 *
 * The companion model stamps:
 * - Conductance Geq between the element terminals
 * - Current source Ieq representing historical state
 *
 * One definition serves the step and its history: update() takes the
 * element's new current from the Geq and Ieq that were stamped, so what a
 * method remembers is what that method computed. GEAR2 weighs its two steps
 * of history by their lengths, so a step of another length (a shortened
 * last step) keeps the method's order.
 *
 * Starting: an element as constructed, after CompanionSet::reset(), after
 * setInitialVoltage() or setInitialCurrent(), or after
 * CompanionSet::initializeFromDC() is at rest with a flat history, which is
 * what GEAR2 then integrates from.
 *
 * RT-safety: All methods are RT-safe (no allocations) after initialization.
 *
 * CUDA: geq(), ieq() and update() are host and device functions; the batch
 * evaluators in CompanionSetCuda call these same definitions.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"
#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"

#include <cstddef>
#include <vector>

namespace sim::electronics::algorithms::companions {

using algorithms::mna::MnaSystem;
using algorithms::mna::MnaSystemSparse;
using algorithms::mna::NetID;
using algorithms::transient::IntegrationMethod;

/* ----------------------------- Gear2Weights ----------------------------- */

/**
 * @brief Gear's second-order formula for the derivative at the end of a step.
 *
 * With y the quantity now, y1 a step of dt earlier and y2 a further step of
 * `before` earlier:
 *
 *   dy/dt = (a0 * y - a1 * y1 + a2 * y2) / dt
 *
 * exact for a quadratic. For equal steps the weights are 3/2, 2 and 1/2. As
 * dt shrinks beside the step before it they tend to 1, 1 and 0, which is
 * backward Euler.
 */
struct Gear2Weights {
  double a0; ///< Weight of the value at the end of this step.
  double a1; ///< Weight of the value at its start.
  double a2; ///< Weight of the value one step further back.
};

/**
 * @brief Weights for a step of dt after a step of `before`.
 * @param dt Length of this step.
 * @param before Length of the step before it; 0 for none recorded, which is
 *        taken as equal steps (a flat history is that of a quantity at rest
 *        whatever the step it is read with).
 * @note RT-safe.
 */
[[nodiscard]] SIM_HD_FI Gear2Weights gear2Weights(double dt, double before) noexcept {
  const double RATIO = (before > 0.0) ? dt / before : 1.0;
  return {(1.0 + 2.0 * RATIO) / (1.0 + RATIO), 1.0 + RATIO, RATIO * RATIO / (1.0 + RATIO)};
}

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

  double prevVoltage = 0.0;  ///< Voltage at the end of the last step.
  double prev2Voltage = 0.0; ///< Voltage at the end of the step before it (GEAR2).
  double current = 0.0;      ///< Current at the end of the last step (TRAPEZOIDAL, output).
  double prevStep = 0.0;     ///< Length of the last step; 0 before any (GEAR2).

  /**
   * @brief Calculate equivalent conductance for given time step and method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent conductance.
   */
  [[nodiscard]] SIM_HD_FI double
  geq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return capacitance / dt; // C/dt
    case IntegrationMethod::TRAPEZOIDAL:
      return 2.0 * capacitance / dt; // 2C/dt
    case IntegrationMethod::GEAR2:
      return gear2Weights(dt, prevStep).a0 * capacitance / dt; // 3C/(2dt) for equal steps
    }
    return capacitance / dt;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value (injected into posNet).
   */
  [[nodiscard]] SIM_HD_FI double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return capacitance * prevVoltage / dt; // C*V_prev/dt
    case IntegrationMethod::TRAPEZOIDAL:
      return 2.0 * capacitance * prevVoltage / dt + current; // 2C*V_prev/dt + I_prev
    case IntegrationMethod::GEAR2: {
      // (C/dt) * (2*V_prev - 0.5*V_prev2) for equal steps
      const Gear2Weights W = gear2Weights(dt, prevStep);
      return (capacitance / dt) * (W.a1 * prevVoltage - W.a2 * prev2Voltage);
    }
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
   *
   * The current is the one the stamped companion carries at the solved
   * voltage, I = Geq * V - Ieq, so the history a method reads on the next
   * step is the one that method produced on this one.
   *
   * @param newVoltage Voltage across capacitor (Vpos - Vneg).
   * @param dt Time step that was taken.
   * @param method Integration method the step was stamped with.
   */
  SIM_HD_FI void update(double newVoltage, double dt, IntegrationMethod method) noexcept {
    current = geq(dt, method) * newVoltage - ieq(dt, method);
    prev2Voltage = prevVoltage;
    prevVoltage = newVoltage;
    prevStep = dt;
  }

  /**
   * @brief Put the capacitor at rest at a voltage: no current, a flat history.
   * @param volts Voltage across the capacitor (Vpos - Vneg).
   * @note RT-safe.
   */
  SIM_HD_FI void setInitialVoltage(double volts) noexcept {
    prevVoltage = volts;
    prev2Voltage = volts;
    current = 0.0;
    prevStep = 0.0;
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

  double prevCurrent = 0.0;  ///< Current at the end of the last step.
  double prev2Current = 0.0; ///< Current at the end of the step before it (GEAR2).
  double voltage = 0.0;      ///< Voltage at the end of the last step (TRAPEZOIDAL, output).
  double prevStep = 0.0;     ///< Length of the last step; 0 before any (GEAR2).

  /**
   * @brief Calculate equivalent conductance for given time step and method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent conductance.
   */
  [[nodiscard]] SIM_HD_FI double
  geq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return dt / inductance; // dt/L
    case IntegrationMethod::TRAPEZOIDAL:
      return dt / (2.0 * inductance); // dt/(2L)
    case IntegrationMethod::GEAR2:
      return dt / (gear2Weights(dt, prevStep).a0 * inductance); // 2dt/(3L) for equal steps
    }
    return dt / inductance;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value.
   */
  [[nodiscard]] SIM_HD_FI double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return prevCurrent; // I_prev
    case IntegrationMethod::TRAPEZOIDAL:
      return prevCurrent + (dt / (2.0 * inductance)) * voltage; // I_prev + (dt/(2L))*V_prev
    case IntegrationMethod::GEAR2: {
      // (4*I_prev - I_prev2) / 3 for equal steps
      const Gear2Weights W = gear2Weights(dt, prevStep);
      return (W.a1 * prevCurrent - W.a2 * prev2Current) / W.a0;
    }
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
   *
   * The current is the one the stamped companion carries at the solved
   * voltage, I = Ieq + Geq * V, so the history a method reads on the next
   * step is the one that method produced on this one.
   *
   * @param newVoltage Voltage across inductor (Vpos - Vneg).
   * @param dt Time step that was taken.
   * @param method Integration method the step was stamped with.
   */
  SIM_HD_FI void update(double newVoltage, double dt, IntegrationMethod method) noexcept {
    const double NEW_CURRENT = ieq(dt, method) + geq(dt, method) * newVoltage;
    voltage = newVoltage;
    prev2Current = prevCurrent;
    prevCurrent = NEW_CURRENT;
    prevStep = dt;
  }

  /**
   * @brief Put the inductor at rest with a current: no voltage, a flat history.
   * @param amperes Current from posNet to negNet through the inductor.
   * @note RT-safe.
   */
  SIM_HD_FI void setInitialCurrent(double amperes) noexcept {
    prevCurrent = amperes;
    prev2Current = amperes;
    voltage = 0.0;
    prevStep = 0.0;
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
 * - GEAR2: Best for stiff systems
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
   * @brief Update all companion states after time step.
   * @param nodeVoltages Solved node voltages.
   * @param dt Time step that was taken.
   * @param method Integration method the step was stamped with.
   */
  void updateAll(const std::vector<double>& nodeVoltages, double dt, IntegrationMethod method) {
    const auto AT = [&nodeVoltages](NetID net) {
      return net < nodeVoltages.size() ? nodeVoltages[net] : 0.0;
    };
    for (auto& cap : capacitors_) {
      cap.update(AT(cap.posNet) - AT(cap.negNet), dt, method);
    }
    for (auto& ind : inductors_) {
      ind.update(AT(ind.posNet) - AT(ind.negNet), dt, method);
    }
  }

  /**
   * @brief The factor a method puts on C/dt for a step of dt taken now (and
   *        divides dt/L by): 1 for backward Euler, 2 for the trapezoidal
   *        rule, and for GEAR2 3/2 after a step of the same length and
   *        another value after a step of another length.
   *
   * Steps of equal length, method and factor stamp equal conductances: what
   * a caller that holds factors of the matrix has to know. The elements of a
   * set are stepped together, so the first one answers for all.
   * @note RT-safe.
   */
  [[nodiscard]] double conductanceFactor(double dt, IntegrationMethod method) const noexcept {
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      return 1.0;
    case IntegrationMethod::TRAPEZOIDAL:
      return 2.0;
    case IntegrationMethod::GEAR2:
      break;
    }
    double before = 0.0;
    if (!capacitors_.empty()) {
      before = capacitors_.front().prevStep;
    } else if (!inductors_.empty()) {
      before = inductors_.front().prevStep;
    }
    return gear2Weights(dt, before).a0;
  }

  /**
   * @brief Put every element at rest at zero: no voltage, no current, no step taken.
   * @note RT-safe.
   */
  void reset() noexcept {
    for (auto& cap : capacitors_) {
      cap.setInitialVoltage(0.0);
    }
    for (auto& ind : inductors_) {
      ind.setInitialCurrent(0.0);
    }
  }

  /**
   * @brief Take every capacitor's voltage and every inductor's current from a resting state.
   *
   * At rest a capacitor carries no current and has the voltage between its
   * nets. An inductor has no voltage across it and carries a current the
   * node voltages do not give by themselves: it is what the solve that
   * found them passed through the conductance it stood in for the inductor
   * with, @p inductorSiemens times the voltage that solve left across it.
   * With 0 the inductors start without current.
   *
   * Each element's history is flat at its resting value, which is the
   * history GEAR2 starts from.
   * @param nodeVoltages The resting state's voltages.
   * @param inductorSiemens The conductance each inductor was solved as.
   */
  void initializeFromDC(const std::vector<double>& nodeVoltages, double inductorSiemens = 0.0) {
    const auto AT = [&nodeVoltages](NetID net) {
      return net < nodeVoltages.size() ? nodeVoltages[net] : 0.0;
    };
    for (auto& cap : capacitors_) {
      cap.setInitialVoltage(AT(cap.posNet) - AT(cap.negNet));
    }
    for (auto& ind : inductors_) {
      ind.setInitialCurrent(inductorSiemens * (AT(ind.posNet) - AT(ind.negNet)));
    }
  }

  /* ----------------------------- Accessors ----------------------------- */

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

} // namespace sim::electronics::algorithms::companions

#endif // APEX_COMPANIONMODELS_HPP
