#ifndef APEX_CAPACITORMODEL_HPP
#define APEX_CAPACITORMODEL_HPP
/**
 * @file CapacitorModel.hpp
 * @brief Linear capacitor physics model (companion model).
 *
 * Capacitors are linear reactive elements that require time-domain companion
 * models for transient simulation. This header provides the physics equations
 * and references the companion model implementation.
 *
 * Physical equation: I = C * dV/dt
 * Companion model: Geq = C/dt (backward Euler), Ieq = C*V_prev/dt
 *
 * For transient simulation, use CapacitorCompanion from the companions library.
 * For DC analysis, capacitors are open circuits (infinite impedance).
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"

namespace sim::electronics::devices::linear {

using algorithms::companions::CapacitorCompanion;
using algorithms::transient::IntegrationMethod;

/**
 * @brief Capacitor model (reactive element).
 *
 * Linear device requiring time-domain integration. Use CapacitorCompanion
 * from the companions library for simulation.
 *
 * @code
 * // Usage in transient simulation:
 * using namespace sim::electronics::devices::linear;
 *
 * CapacitorCompanion cap;
 * cap.posNet = 1;
 * cap.negNet = 0;
 * cap.capacitance = 1e-6;  // 1 uF
 *
 * // Each time step:
 * cap.stamp(mna, dt, IntegrationMethod::BACKWARD_EULER);
 * auto result = mna.solve();
 * double voltage = result.voltages[cap.posNet] - result.voltages[cap.negNet];
 * cap.update(voltage, dt);  // Update state for next step
 * @endcode
 */
struct CapacitorModel {
  /**
   * @brief Calculate reactance at given frequency.
   * @param capacitance Capacitance in Farads.
   * @param frequency Frequency in Hertz.
   * @return Capacitive reactance in ohms (Xc = 1/(2*pi*f*C)).
   * @note RT-safe.
   */
  [[nodiscard]] static double reactance(double capacitance, double frequency) noexcept {
    constexpr double TWO_PI = 6.283185307179586;
    return 1.0 / (TWO_PI * frequency * capacitance);
  }

  /**
   * @brief Calculate impedance magnitude at given frequency.
   * @param capacitance Capacitance in Farads.
   * @param frequency Frequency in Hertz.
   * @return Impedance magnitude in ohms.
   * @note RT-safe.
   */
  [[nodiscard]] static double impedance(double capacitance, double frequency) noexcept {
    return reactance(capacitance, frequency);
  }
};

// Re-export companion model for convenience
using Capacitor = CapacitorCompanion;

} // namespace sim::electronics::devices::linear

#endif // APEX_CAPACITORMODEL_HPP
