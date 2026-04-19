#ifndef APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_DESCRIPTORS_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_DESCRIPTORS_HPP
/**
 * @file Descriptors.hpp
 * @brief Registry of all device topology descriptors.
 *
 * Single import point for all descriptors. Descriptors define circuit TOPOLOGY
 * (connectivity + parameters) but contain NO physics or simulation code.
 *
 * Usage:
 * @code
 * #include "src/sim/electronics/devices/descriptors/inc/Descriptors.hpp"
 *
 * using namespace sim::electronics::devices::descriptors;
 *
 * // Linear devices
 * ResistorDescriptor r1{VDD, OUTPUT, 10e3};
 * CapacitorDescriptor c1{OUTPUT, GND, 100e-12};
 * InductorDescriptor l1{VDD, OUTPUT, 10e-6};
 *
 * // Nonlinear devices
 * DiodeDescriptor d1{VDD, OUTPUT, 1.0};
 * MosfetDescriptor m1{VDD, INPUT, OUTPUT, VDD, 10e-6, 1e-6};
 * BjtDescriptor q1{VDD, INPUT, OUTPUT, 1.0};
 * @endcode
 */

// Linear device descriptors
#include "src/sim/electronics/devices/descriptors/inc/CapacitorDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/InductorDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/ResistorDescriptor.hpp"

// Nonlinear device descriptors
#include "src/sim/electronics/devices/descriptors/inc/BjtDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/DiodeDescriptor.hpp"
#include "src/sim/electronics/devices/descriptors/inc/MosfetDescriptor.hpp"

#endif // APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_DESCRIPTORS_HPP
