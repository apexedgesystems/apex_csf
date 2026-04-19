#ifndef APEX_SIM_ELECTRONICS_DEVICES_LINEAR_INDUCTORMODEL_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_LINEAR_INDUCTORMODEL_HPP
/**
 * @file InductorModel.hpp
 * @brief Linear inductor physics model (companion model).
 *
 * Inductors are linear reactive elements that require time-domain companion
 * models for transient simulation. This header provides the physics equations
 * and references the companion model implementation.
 *
 * Physical equation: V = L * dI/dt
 * Companion model: Geq = dt/L (backward Euler), Ieq = I_prev
 *
 * For transient simulation, use InductorCompanion from the companions library.
 * For DC analysis, inductors are short circuits (zero impedance).
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/devices/companions/inc/CompanionModels.hpp"

namespace sim::electronics::devices::linear {

using companions::InductorCompanion;
using transient::IntegrationMethod;

/**
 * @brief Inductor model (reactive element).
 *
 * Linear device requiring time-domain integration. Use InductorCompanion
 * from the companions library for simulation.
 *
 * @code
 * // Usage in transient simulation:
 * using namespace sim::electronics::devices::linear;
 *
 * InductorCompanion ind;
 * ind.posNet = 1;
 * ind.negNet = 0;
 * ind.inductance = 1e-3;  // 1 mH
 *
 * // Each time step:
 * ind.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
 * auto result = mna.solve();
 * double voltage = result.voltages[ind.posNet] - result.voltages[ind.negNet];
 * ind.update(voltage, dt);  // Update state for next step
 * @endcode
 */
struct InductorModel {
  /**
   * @brief Calculate reactance at given frequency.
   * @param inductance Inductance in Henries.
   * @param frequency Frequency in Hertz.
   * @return Inductive reactance in ohms (XL = 2*pi*f*L).
   * @note RT-safe.
   */
  [[nodiscard]] static double reactance(double inductance, double frequency) noexcept {
    constexpr double TWO_PI = 6.283185307179586;
    return TWO_PI * frequency * inductance;
  }

  /**
   * @brief Calculate impedance magnitude at given frequency.
   * @param inductance Inductance in Henries.
   * @param frequency Frequency in Hertz.
   * @return Impedance magnitude in ohms.
   * @note RT-safe.
   */
  [[nodiscard]] static double impedance(double inductance, double frequency) noexcept {
    return reactance(inductance, frequency);
  }
};

// Re-export companion model for convenience
using Inductor = InductorCompanion;

} // namespace sim::electronics::devices::linear

#endif // APEX_SIM_ELECTRONICS_DEVICES_LINEAR_INDUCTORMODEL_HPP
