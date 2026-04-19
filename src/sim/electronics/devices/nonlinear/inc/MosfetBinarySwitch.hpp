#ifndef APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBINARYSWITCH_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBINARYSWITCH_HPP
/**
 * @file MosfetBinarySwitch.hpp
 * @brief Binary switch MOSFET model (digital circuits, fast simulation).
 *
 * Simplest MOSFET model: treats transistor as ideal switch controlled by gate
 * voltage. No analog I-V curves, no subthreshold region - just ON or OFF.
 *
 * - ON (Vgs > Vth): Rds = Ron (typically 100-1000 ohm)
 * - OFF (Vgs < Vth): Rds = Roff (typically 10M-1G ohm)
 *
 * This model is FAST and suitable for digital circuit verification (e.g.,
 * Intel 4004 grid simulation) where analog accuracy is not required.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 * Thread-safety: Safe (stateless, pure functions).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::nonlinear {

using mna::MnaSystem;
using mna::NetID;

/**
 * @brief Binary switch MOSFET parameters.
 *
 * Defines threshold voltage and on/off resistances.
 */
struct MosfetBinarySwitchParams {
  double Vth = 0.7;   ///< Threshold voltage (V), typical 0.5-1.5V
  double Ron = 500.0; ///< On-resistance (ohm), typical 100-1000 ohm
  double Roff = 1e9;  ///< Off-resistance (ohm), typical 10M-1G ohm
};

/**
 * @brief Binary switch MOSFET model.
 *
 * Treats MOSFET as voltage-controlled resistor with two states.
 *
 * Usage:
 * @code
 * MosfetBinarySwitchParams params{.Vth = 0.7, .Ron = 500.0, .Roff = 1e9};
 *
 * // Compute drain-source resistance
 * double vgs = 1.5;  // Gate voltage relative to source
 * double rds = MosfetBinarySwitch::resistance(vgs, params);
 *
 * // Stamp into MNA system
 * MosfetBinarySwitch::stamp(mna, drain, source, vgs, params);
 * @endcode
 */
struct MosfetBinarySwitch {
  /**
   * @brief Compute drain-source resistance based on gate voltage.
   *
   * R = Ron if Vgs > Vth (ON), Roff if Vgs < Vth (OFF).
   *
   * @param vgs Gate-source voltage in volts.
   * @param params MOSFET switch parameters.
   * @return Drain-source resistance in ohms.
   *
   * @note RT-safe (no allocations, pure math).
   */
  [[nodiscard]] static double resistance(double vgs,
                                         const MosfetBinarySwitchParams& params) noexcept {
    return (vgs > params.Vth) ? params.Ron : params.Roff;
  }

  /**
   * @brief Check if MOSFET is conducting (ON state).
   *
   * @param vgs Gate-source voltage in volts.
   * @param params MOSFET switch parameters.
   * @return True if Vgs > Vth (ON), false otherwise (OFF).
   *
   * @note RT-safe (no allocations, pure comparison).
   */
  [[nodiscard]] static bool isOn(double vgs, const MosfetBinarySwitchParams& params) noexcept {
    return vgs > params.Vth;
  }

  /**
   * @brief Stamp MOSFET into MNA system as resistor.
   *
   * Stamps drain-source conductance: G = 1/R where R = Ron or Roff.
   *
   * @param mna MNA system to stamp into.
   * @param drain Drain net ID.
   * @param source Source net ID.
   * @param vgs Gate-source voltage (from previous iteration).
   * @param params MOSFET switch parameters.
   *
   * @note RT-safe (stamps into pre-allocated matrix).
   */
  static void stamp(MnaSystem& mna, NetID drain, NetID source, double vgs,
                    const MosfetBinarySwitchParams& params) {
    const double R = resistance(vgs, params);
    const double G = 1.0 / R;
    mna.addConductance(drain, source, G);
  }
};

} // namespace sim::electronics::devices::nonlinear

#endif // APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETBINARYSWITCH_HPP
