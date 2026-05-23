#ifndef APEX_CAPACITORDESCRIPTOR_HPP
#define APEX_CAPACITORDESCRIPTOR_HPP
/**
 * @file CapacitorDescriptor.hpp
 * @brief Capacitor topology descriptor (pure topology, no physics).
 *
 * Descriptors are TOPOLOGY ONLY - they specify which nets are connected and
 * device parameters, but contain NO physics code. Physics models use descriptors
 * to know what to simulate.
 *
 * RT-safety: RT-safe (trivial struct, no allocations).
 * Thread-safety: Safe (stateless data).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::descriptors {

using algorithms::mna::NetID;

/**
 * @brief Capacitor topology descriptor.
 *
 * Pure topology - no physics, no stamping, no simulation code.
 * Contains only connectivity (NetIDs) and parameters (capacitance value).
 *
 * Usage:
 * @code
 * // Define topology
 * CapacitorDescriptor c1{OUTPUT, GND, 100e-12};  // 100pF decoupling cap
 *
 * // Physics models use descriptor
 * CapacitorCompanion cap;
 * cap.posNet = c1.posNet;
 * cap.negNet = c1.negNet;
 * cap.capacitance = c1.capacitance;
 * @endcode
 */
struct CapacitorDescriptor {
  NetID posNet;       ///< Positive terminal net ID
  NetID negNet;       ///< Negative terminal net ID
  double capacitance; ///< Capacitance in farads
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_CAPACITORDESCRIPTOR_HPP
