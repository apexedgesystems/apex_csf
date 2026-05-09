#ifndef APEX_INDUCTORDESCRIPTOR_HPP
#define APEX_INDUCTORDESCRIPTOR_HPP
/**
 * @file InductorDescriptor.hpp
 * @brief Inductor topology descriptor (pure topology, no physics).
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
 * @brief Inductor topology descriptor.
 *
 * Pure topology - no physics, no stamping, no simulation code.
 * Contains only connectivity (NetIDs) and parameters (inductance value).
 *
 * Usage:
 * @code
 * // Define topology
 * InductorDescriptor l1{VDD, OUTPUT, 10e-6};  // 10uH filter inductor
 *
 * // Physics models use descriptor
 * InductorCompanion ind;
 * ind.posNet = l1.posNet;
 * ind.negNet = l1.negNet;
 * ind.inductance = l1.inductance;
 * @endcode
 */
struct InductorDescriptor {
  NetID posNet;      ///< Positive terminal net ID
  NetID negNet;      ///< Negative terminal net ID
  double inductance; ///< Inductance in henries
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_INDUCTORDESCRIPTOR_HPP
