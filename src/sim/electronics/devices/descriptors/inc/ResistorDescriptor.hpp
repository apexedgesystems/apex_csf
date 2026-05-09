#ifndef APEX_RESISTORDESCRIPTOR_HPP
#define APEX_RESISTORDESCRIPTOR_HPP
/**
 * @file ResistorDescriptor.hpp
 * @brief Resistor topology descriptor (pure topology, no physics).
 *
 * Descriptors are TOPOLOGY ONLY - they specify which nets are connected and
 * device parameters, but contain NO physics code. Physics models use descriptors
 * to know what to simulate.
 *
 * This separation enables:
 * - Netlist parsing independent of physics
 * - Fidelity switching (same topology, different physics models)
 * - Test isolation (verify connectivity without simulation)
 *
 * RT-safety: RT-safe (trivial struct, no allocations).
 * Thread-safety: Safe (stateless data).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::descriptors {

using algorithms::mna::NetID;

/**
 * @brief Resistor topology descriptor.
 *
 * Pure topology - no physics, no stamping, no simulation code.
 * Contains only connectivity (NetIDs) and parameters (resistance value).
 *
 * Usage:
 * @code
 * // Define topology
 * ResistorDescriptor r1{VDD, OUTPUT, 10e3};  // 10k pullup
 *
 * // Physics models use descriptor
 * ResistorModel::stamp(mna, r1.posNet, r1.negNet, r1.resistance);
 * @endcode
 */
struct ResistorDescriptor {
  NetID posNet;      ///< Positive terminal net ID
  NetID negNet;      ///< Negative terminal net ID
  double resistance; ///< Resistance in ohms
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_RESISTORDESCRIPTOR_HPP
