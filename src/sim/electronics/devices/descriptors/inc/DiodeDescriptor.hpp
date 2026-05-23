#ifndef APEX_DIODEDESCRIPTOR_HPP
#define APEX_DIODEDESCRIPTOR_HPP
/**
 * @file DiodeDescriptor.hpp
 * @brief Diode topology descriptor (connectivity only, no physics).
 *
 * Describes a diode's circuit connectivity (anode, cathode nets) and geometric
 * parameters (area scaling). Contains NO physics or simulation code.
 *
 * RT-safety: RT-safe (POD struct, no allocations).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

namespace sim::electronics::devices::descriptors {

using algorithms::mna::NetID;

/**
 * @brief Diode topology descriptor.
 *
 * Pure topology: connectivity (NetIDs) + geometric parameters.
 * NO physics (I-V curves, stamping, etc.) - that lives in device models.
 *
 * Usage:
 * @code
 * DiodeDescriptor d1{VDD, OUTPUT, 1.0};  // Standard diode
 * DiodeDescriptor d2{INPUT, GND, 2.5};   // 2.5x area (higher current)
 * @endcode
 */
struct DiodeDescriptor {
  NetID anodeNet = 0;   ///< Anode terminal net ID.
  NetID cathodeNet = 0; ///< Cathode terminal net ID.
  double area = 1.0;    ///< Area scaling factor (unitless, 1.0 = nominal).

  /**
   * @brief Construct diode descriptor.
   * @param anode Anode net ID.
   * @param cathode Cathode net ID.
   * @param areaScale Area scaling factor (default 1.0).
   */
  constexpr DiodeDescriptor(NetID anode, NetID cathode, double areaScale = 1.0) noexcept
      : anodeNet(anode), cathodeNet(cathode), area(areaScale) {}

  /**
   * @brief Default constructor (all zeros).
   */
  constexpr DiodeDescriptor() noexcept = default;
};

} // namespace sim::electronics::devices::descriptors

#endif // APEX_DIODEDESCRIPTOR_HPP
