#ifndef APEX_RESISTORMODEL_HPP
#define APEX_RESISTORMODEL_HPP
/**
 * @file ResistorModel.hpp
 * @brief Linear resistor physics model (Ohm's law).
 *
 * Provides static stamping functions for resistors. Resistors are exact linear
 * devices with no fidelity levels (one model covers all cases).
 *
 * Physical equation: V = I * R (Ohm's law)
 * Conductance: G = 1/R
 * Current: I = V/R = G * V
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

namespace sim::electronics::devices::linear {

using algorithms::mna::MnaSystem;
using algorithms::mna::MnaSystemSparse;
using algorithms::mna::NetID;

/**
 * @brief Resistor model (Ohm's law).
 *
 * Linear device with exact stamping (no iterations, no approximations).
 */
struct ResistorModel {
  /**
   * @brief Calculate conductance from resistance.
   * @param resistance Resistance in ohms.
   * @return Conductance in siemens (1/R).
   * @note RT-safe.
   */
  [[nodiscard]] static constexpr double conductance(double resistance) noexcept {
    return 1.0 / resistance;
  }

  /**
   * @brief Calculate current from voltage and resistance.
   * @param voltage Voltage across resistor (V = Vpos - Vneg).
   * @param resistance Resistance in ohms.
   * @return Current in amperes (I = V/R).
   * @note RT-safe.
   */
  [[nodiscard]] static constexpr double current(double voltage, double resistance) noexcept {
    return voltage / resistance;
  }

  /**
   * @brief Stamp resistor into dense MNA system.
   * @param mna MNA system to stamp into.
   * @param posNet Positive terminal net.
   * @param negNet Negative terminal net.
   * @param resistance Resistance in ohms.
   *
   * Stamps conductance G = 1/R between terminals:
   *   G[pos][pos] += G
   *   G[pos][neg] -= G
   *   G[neg][pos] -= G
   *   G[neg][neg] += G
   *
   * @note RT-safe.
   */
  static void stamp(MnaSystem& mna, NetID posNet, NetID negNet, double resistance) {
    double g = conductance(resistance);
    mna.addConductance(posNet, negNet, g);
  }

  /**
   * @brief Stamp resistor into sparse MNA system.
   * @param mna Sparse MNA system to stamp into.
   * @param posNet Positive terminal net.
   * @param negNet Negative terminal net.
   * @param resistance Resistance in ohms.
   * @note RT-safe.
   */
  static void stamp(MnaSystemSparse& mna, NetID posNet, NetID negNet, double resistance) {
    double g = conductance(resistance);
    mna.addConductance(posNet, negNet, g);
  }
};

} // namespace sim::electronics::devices::linear

#endif // APEX_RESISTORMODEL_HPP
