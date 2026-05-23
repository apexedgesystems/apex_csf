#ifndef APEX_BJTDESCRIPTOR_HPP
#define APEX_BJTDESCRIPTOR_HPP
/**
 * @file BjtDescriptor.hpp
 * @brief BJT topology descriptor (connectivity only, no physics).
 *
 * Describes a bipolar junction transistor's circuit connectivity (collector, base,
 * emitter nets) and geometric parameters (area scaling). Contains NO physics or
 * simulation code.
 *
 * RT-safety: RT-safe (POD struct, no allocations).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::descriptors {

using algorithms::mna::NetID;

/**
 * @brief BJT topology descriptor.
 *
 * Pure topology: connectivity (NetIDs) + geometric parameters.
 * NO physics (I-V curves, beta, Early voltage, etc.) - that lives in device models.
 *
 * Supports both NPN and PNP transistors (type determined by model, not descriptor).
 *
 * Usage:
 * @code
 * BjtDescriptor q1{VDD, INPUT, OUTPUT, 1.0};  // PNP, standard area
 * BjtDescriptor q2{OUTPUT, INPUT, GND, 2.5};  // NPN, 2.5x area (higher current)
 * @endcode
 */
struct BjtDescriptor {
  NetID collectorNet = 0; ///< Collector terminal net ID.
  NetID baseNet = 0;      ///< Base terminal net ID.
  NetID emitterNet = 0;   ///< Emitter terminal net ID.
  double area = 1.0;      ///< Area scaling factor (unitless, 1.0 = nominal).

  /**
   * @brief Construct BJT descriptor.
   * @param collector Collector net ID.
   * @param base Base net ID.
   * @param emitter Emitter net ID.
   * @param areaScale Area scaling factor (default 1.0).
   */
  constexpr BjtDescriptor(NetID collector, NetID base, NetID emitter,
                          double areaScale = 1.0) noexcept
      : collectorNet(collector), baseNet(base), emitterNet(emitter), area(areaScale) {}

  /**
   * @brief Default constructor (all zeros).
   */
  constexpr BjtDescriptor() noexcept = default;
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_BJTDESCRIPTOR_HPP
