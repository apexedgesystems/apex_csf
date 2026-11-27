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
 * CompanionSet::initializeFromDC() has one value behind it and no step
 * taken. One value supports a first-order formula, so GEAR2 takes its first
 * step as backward Euler and is Gear's formula from the second step on.
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
 * backward Euler; with no step before it they are 1, 1 and 0.
 */
struct Gear2Weights {
  double a0; ///< Weight of the value at the end of this step.
  double a1; ///< Weight of the value at its start.
  double a2; ///< Weight of the value one step further back.
};

/**
 * @brief Weights for a step of dt after a step of `before`.
 * @param dt Length of this step.
 * @param before Length of the step before it; 0 for none recorded. A first
 *        step has one value behind it, not two, and takes backward Euler's
 *        weights: with the value before that one invented as equal to it,
 *        the step would be of first order with a larger error.
 * @note RT-safe.
 */
[[nodiscard]] SIM_HD_FI Gear2Weights gear2Weights(double dt, double before) noexcept {
  if (!(before > 0.0)) {
    return {1.0, 1.0, 0.0};
  }
  const double RATIO = dt / before;
  return {(1.0 + 2.0 * RATIO) / (1.0 + RATIO), 1.0 + RATIO, RATIO * RATIO / (1.0 + RATIO)};
}

/**
 * @brief What a companion puts into the system for one step: a conductance
 *        between its terminals and a source beside it.
 */
struct CompanionTerms {
  double geq; ///< Equivalent conductance.
  double ieq; ///< Equivalent current source.
};

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
    return terms(dt, method).geq;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value (injected into posNet).
   */
  [[nodiscard]] SIM_HD_FI double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    return terms(dt, method).ieq;
  }

  /**
   * @brief Both terms of a step. C/dt is divided once and shared by the two.
   * @param dt Time step in seconds.
   * @param method Integration method.
   * @return Conductance and source: C/dt and (C/dt) V_prev for backward
   *         Euler; 2C/dt and (2C/dt) V_prev + I_prev for the trapezoidal
   *         rule; a0 C/dt and (C/dt)(a1 V_prev - a2 V_prev2) for GEAR2.
   * @note RT-safe.
   */
  [[nodiscard]] SIM_HD_FI CompanionTerms terms(double dt, IntegrationMethod method) const noexcept {
    return terms(dt, method,
                 method == IntegrationMethod::GEAR2 ? gear2Weights(dt, prevStep)
                                                    : Gear2Weights{1.0, 1.0, 0.0});
  }

  /**
   * @brief Both terms of a step, with Gear's weights given.
   *
   * For a caller that steps many elements whose last step was the same: the
   * weights depend on the two step lengths alone and are worked out once.
   *
   * @param gear gear2Weights(dt, prevStep) of this element. Read under GEAR2
   *        only.
   * @note RT-safe.
   */
  [[nodiscard]] SIM_HD_FI CompanionTerms terms(double dt, IntegrationMethod method,
                                               const Gear2Weights& gear) const noexcept {
    const double RATE = capacitance / dt;
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      break;
    case IntegrationMethod::TRAPEZOIDAL:
      return {2.0 * RATE, 2.0 * RATE * prevVoltage + current};
    case IntegrationMethod::GEAR2:
      return {gear.a0 * RATE, RATE * (gear.a1 * prevVoltage - gear.a2 * prev2Voltage)};
    }
    return {RATE, RATE * prevVoltage};
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
    const CompanionTerms STAMPED = terms(dt, method);
    const double g = STAMPED.geq;
    const double i = STAMPED.ieq;

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
    const CompanionTerms STAMPED = terms(dt, method);
    const double g = STAMPED.geq;
    const double i = STAMPED.ieq;

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
    update(newVoltage, dt, terms(dt, method));
  }

  /**
   * @brief Update state after time step from the terms that were stamped.
   * @param newVoltage Voltage across capacitor (Vpos - Vneg).
   * @param dt Time step that was taken.
   * @param stamped terms() of this element for that step, taken before this
   *        call.
   * @note RT-safe.
   */
  SIM_HD_FI void update(double newVoltage, double dt, const CompanionTerms& stamped) noexcept {
    current = stamped.geq * newVoltage - stamped.ieq;
    prev2Voltage = prevVoltage;
    prevVoltage = newVoltage;
    prevStep = dt;
  }

  /**
   * @brief Put the capacitor at a voltage with no current and no step taken.
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
    return terms(dt, method).geq;
  }

  /**
   * @brief Calculate equivalent current source for given method.
   * @param dt Time step in seconds.
   * @param method Integration method (default: Backward Euler).
   * @return Equivalent current source value.
   */
  [[nodiscard]] SIM_HD_FI double
  ieq(double dt, IntegrationMethod method = IntegrationMethod::BACKWARD_EULER) const noexcept {
    return terms(dt, method).ieq;
  }

  /**
   * @brief Both terms of a step. dt/L is divided once and shared by the two.
   * @param dt Time step in seconds.
   * @param method Integration method.
   * @return Conductance and source: dt/L and I_prev for backward Euler;
   *         dt/(2L) and I_prev + (dt/(2L)) V_prev for the trapezoidal rule;
   *         dt/(a0 L) and (a1 I_prev - a2 I_prev2)/a0 for GEAR2.
   * @note RT-safe.
   */
  [[nodiscard]] SIM_HD_FI CompanionTerms terms(double dt, IntegrationMethod method) const noexcept {
    return terms(dt, method,
                 method == IntegrationMethod::GEAR2 ? gear2Weights(dt, prevStep)
                                                    : Gear2Weights{1.0, 1.0, 0.0});
  }

  /**
   * @brief Both terms of a step, with Gear's weights given.
   * @param gear gear2Weights(dt, prevStep) of this element. Read under GEAR2
   *        only.
   * @note RT-safe.
   */
  [[nodiscard]] SIM_HD_FI CompanionTerms terms(double dt, IntegrationMethod method,
                                               const Gear2Weights& gear) const noexcept {
    const double RATE = dt / inductance;
    switch (method) {
    case IntegrationMethod::BACKWARD_EULER:
      break;
    case IntegrationMethod::TRAPEZOIDAL:
      return {0.5 * RATE, prevCurrent + 0.5 * RATE * voltage};
    case IntegrationMethod::GEAR2: {
      const double SHARE = 1.0 / gear.a0;
      return {RATE * SHARE, (gear.a1 * prevCurrent - gear.a2 * prev2Current) * SHARE};
    }
    }
    return {RATE, prevCurrent};
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
    const CompanionTerms STAMPED = terms(dt, method);
    const double g = STAMPED.geq;
    const double i = STAMPED.ieq;

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
    const CompanionTerms STAMPED = terms(dt, method);
    const double g = STAMPED.geq;
    const double i = STAMPED.ieq;

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
    update(newVoltage, dt, terms(dt, method));
  }

  /**
   * @brief Update state after time step from the terms that were stamped.
   * @param newVoltage Voltage across inductor (Vpos - Vneg).
   * @param dt Time step that was taken.
   * @param stamped terms() of this element for that step, taken before this
   *        call.
   * @note RT-safe.
   */
  SIM_HD_FI void update(double newVoltage, double dt, const CompanionTerms& stamped) noexcept {
    const double NEW_CURRENT = stamped.ieq + stamped.geq * newVoltage;
    voltage = newVoltage;
    prev2Current = prevCurrent;
    prevCurrent = NEW_CURRENT;
    prevStep = dt;
  }

  /**
   * @brief Put the inductor at a current with no voltage and no step taken.
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
    stampAllInto(mna, dt, method);
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
    stampAllInto(mna, dt, method);
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
    GearWeightsOfPass gear(dt);
    for (const auto& cap : capacitors_) {
      mna.addConductance(cap.posNet, cap.negNet, cap.terms(dt, method, gear.of(cap.prevStep)).geq);
    }
    for (const auto& ind : inductors_) {
      mna.addConductance(ind.posNet, ind.negNet, ind.terms(dt, method, gear.of(ind.prevStep)).geq);
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
    GearWeightsOfPass gear(dt);
    for (const auto& cap : capacitors_) {
      mna.addCurrent(cap.posNet, cap.negNet, cap.terms(dt, method, gear.of(cap.prevStep)).ieq);
    }
    for (const auto& ind : inductors_) {
      // Inductor current flows from pos to neg
      mna.addCurrent(ind.negNet, ind.posNet, ind.terms(dt, method, gear.of(ind.prevStep)).ieq);
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
    GearWeightsOfPass gear(dt);
    for (auto& cap : capacitors_) {
      cap.update(AT(cap.posNet) - AT(cap.negNet), dt, cap.terms(dt, method, gear.of(cap.prevStep)));
    }
    for (auto& ind : inductors_) {
      ind.update(AT(ind.posNet) - AT(ind.negNet), dt, ind.terms(dt, method, gear.of(ind.prevStep)));
    }
  }

  /**
   * @brief The factor a method puts on C/dt for a step of dt taken now (and
   *        divides dt/L by): 1 for backward Euler, 2 for the trapezoidal
   *        rule, and for GEAR2 1 on a first step, 3/2 after a step of the
   *        same length and another value after a step of another length.
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
   * Each element is left with that one value behind it and no step taken,
   * so GEAR2 takes its first step from it as backward Euler.
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
  /**
   * @brief Gear's weights for the elements of one pass over the set.
   *
   * The weights depend on the step being taken and on the element's last
   * step. Elements stepped together share their last step, so the weights
   * are worked out for the first and again only for an element whose last
   * step differs from the one before it.
   */
  class GearWeightsOfPass {
  public:
    explicit GearWeightsOfPass(double dt) noexcept : dt_(dt) {}

    /** @brief gear2Weights(dt, before), reusing the last result for the same `before`. */
    [[nodiscard]] const Gear2Weights& of(double before) noexcept {
      if (before != before_) {
        weights_ = gear2Weights(dt_, before);
        before_ = before;
      }
      return weights_;
    }

  private:
    double dt_;
    double before_ = -1.0; ///< No step length is negative: the first element always computes.
    Gear2Weights weights_{1.0, 1.0, 0.0};
  };

  /** @brief Stamp every element's conductance and source into a dense or a sparse system. */
  template <typename Mna> void stampAllInto(Mna& mna, double dt, IntegrationMethod method) const {
    GearWeightsOfPass gear(dt);
    for (const auto& cap : capacitors_) {
      const CompanionTerms TERMS = cap.terms(dt, method, gear.of(cap.prevStep));
      mna.addConductance(cap.posNet, cap.negNet, TERMS.geq);
      mna.addCurrent(cap.posNet, cap.negNet, TERMS.ieq);
    }
    for (const auto& ind : inductors_) {
      const CompanionTerms TERMS = ind.terms(dt, method, gear.of(ind.prevStep));
      mna.addConductance(ind.posNet, ind.negNet, TERMS.geq);
      // The source carries Ieq from posNet to negNet: it arrives at negNet.
      mna.addCurrent(ind.negNet, ind.posNet, TERMS.ieq);
    }
  }

  std::vector<CapacitorCompanion> capacitors_;
  std::vector<InductorCompanion> inductors_;
};

} // namespace sim::electronics::algorithms::companions

#endif // APEX_COMPANIONMODELS_HPP
