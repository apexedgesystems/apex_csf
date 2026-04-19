#ifndef APEX_SIM_ELECTRONICS_DEVICES_LINEAR_LINEARDEVICES_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_LINEAR_LINEARDEVICES_HPP
/**
 * @file LinearDevices.hpp
 * @brief Registry of all linear device models.
 *
 * Single import point for all linear device models (resistor, capacitor, inductor).
 * Linear devices are exact (no approximations, no iterations) and have one fidelity
 * level that covers all simulation needs.
 *
 * Usage:
 * @code
 * #include "src/sim/electronics/devices/linear/inc/LinearDevices.hpp"
 *
 * using namespace sim::electronics::devices::linear;
 *
 * // Resistor (DC/AC/Transient)
 * ResistorModel::stamp(mna, VDD, OUTPUT, 10e3);  // 10k ohm resistor
 *
 * // Capacitor (Transient)
 * CapacitorCompanion cap{OUTPUT, GND, 1e-6};  // 1uF capacitor
 * cap.stamp(mna, dt);
 * cap.update(voltage, dt);
 *
 * // Inductor (Transient)
 * InductorCompanion ind{VDD, OUTPUT, 1e-3};  // 1mH inductor
 * ind.stamp(mna, dt);
 * ind.update(voltage, dt);
 * @endcode
 */

#include "src/sim/electronics/devices/linear/inc/ResistorModel.hpp"
#include "src/sim/electronics/devices/linear/inc/CapacitorModel.hpp"
#include "src/sim/electronics/devices/linear/inc/InductorModel.hpp"

#endif // APEX_SIM_ELECTRONICS_DEVICES_LINEAR_LINEARDEVICES_HPP
