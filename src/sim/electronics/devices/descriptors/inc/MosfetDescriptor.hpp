#ifndef APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_MOSFETDESCRIPTOR_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_MOSFETDESCRIPTOR_HPP
/**
 * @file MosfetDescriptor.hpp
 * @brief MOSFET topology descriptor (connectivity only, no physics).
 *
 * Describes a MOSFET's circuit connectivity (drain, gate, source, bulk nets) and
 * geometric parameters (width, length). Contains NO physics or simulation code.
 *
 * RT-safety: RT-safe (POD struct, no allocations).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::descriptors {

using mna::NetID;

/**
 * @brief MOSFET topology descriptor.
 *
 * Pure topology: connectivity (NetIDs) + geometric parameters.
 * NO physics (I-V curves, threshold voltage, etc.) - that lives in device models.
 *
 * Supports both N-channel and P-channel MOSFETs (type determined by model, not
 * descriptor).
 *
 * Usage:
 * @code
 * MosfetDescriptor m1{VDD, INPUT, OUTPUT, VDD, 10e-6, 1e-6};  // PMOS 10um/1um
 * MosfetDescriptor m2{OUTPUT, INPUT, GND, GND, 10e-6, 1e-6};  // NMOS 10um/1um
 * @endcode
 */
struct MosfetDescriptor {
  NetID drainNet = 0;  ///< Drain terminal net ID.
  NetID gateNet = 0;   ///< Gate terminal net ID.
  NetID sourceNet = 0; ///< Source terminal net ID.
  NetID bulkNet = 0;   ///< Bulk (body) terminal net ID.

  double W = 1e-6; ///< Channel width in meters (default 1um).
  double L = 1e-6; ///< Channel length in meters (default 1um).

  /**
   * @brief Construct MOSFET descriptor.
   * @param drain Drain net ID.
   * @param gate Gate net ID.
   * @param source Source net ID.
   * @param bulk Bulk net ID.
   * @param width Channel width in meters.
   * @param length Channel length in meters.
   */
  constexpr MosfetDescriptor(NetID drain, NetID gate, NetID source, NetID bulk, double width,
                             double length) noexcept
      : drainNet(drain), gateNet(gate), sourceNet(source), bulkNet(bulk), W(width), L(length) {}

  /**
   * @brief Default constructor (all zeros, 1um x 1um).
   */
  constexpr MosfetDescriptor() noexcept = default;
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_SIM_ELECTRONICS_DEVICES_DESCRIPTORS_MOSFETDESCRIPTOR_HPP
