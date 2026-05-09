#ifndef APEX_CMOSNOR_HPP
#define APEX_CMOSNOR_HPP
/**
 * @file CmosNor.hpp
 * @brief CMOS NOR gate built from 4 MOSFETs (2 PMOS series, 2 NMOS parallel).
 *
 * Composite device that implements 2-input NOR logic gate using CMOS transistors.
 * Provides truth table validation and circuit-level simulation.
 *
 * RT-safety: RT-safe (static functions, no allocations).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/devices/descriptors/inc/MosfetDescriptor.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <array>

namespace sim::electronics::devices::composite {

using descriptors::MosfetDescriptor;
using algorithms::mna::MnaSystem;
using algorithms::mna::NetID;
using devices::nonlinear::MosfetLevel1;
using devices::nonlinear::MosfetLevel1Params;

/* -------------------------------- CmosNor ------------------------------- */

/**
 * @brief CMOS NOR gate (2-input).
 *
 * Topology:
 *
 *       VDD
 *        |
 *       (P1)        PMOS pull-ups (series)
 *        |
 *       (P2)
 *        |
 *       OUT
 *        |
 *    +---+---+
 *    |       |
 *   (N1)    (N2)   NMOS pull-downs (parallel)
 *    |       |
 *    +---+---+
 *        |
 *       GND
 *
 *   A --(P1 gate, N1 gate)
 *   B --(P2 gate, N2 gate)
 *
 * Truth table:
 *   A | B | OUT
 *   --+---+----
 *   0 | 0 |  1
 *   0 | 1 |  0
 *   1 | 0 |  0
 *   1 | 1 |  0
 *
 * Usage:
 * @code
 * CmosNor nor{VDD, GND, INA, INB, OUTPUT, 10e-6, 1e-6};
 * nor.stamp(mna, vdd, vinA, vinB);
 * @endcode
 */
struct CmosNor {
  std::array<MosfetDescriptor, 2> pmos; ///< PMOS pull-up transistors (series).
  std::array<MosfetDescriptor, 2> nmos; ///< NMOS pull-down transistors (parallel).
  NetID internalNode = 0;               ///< Internal node between P1 and P2.

  /**
   * @brief Construct CMOS NOR gate.
   * @param vddNet VDD supply net.
   * @param gndNet GND supply net.
   * @param inputANet Input A net.
   * @param inputBNet Input B net.
   * @param outputNet Output net.
   * @param internalNet Internal node between series PMOS transistors.
   * @param W Channel width (all transistors).
   * @param L Channel length (all transistors).
   */
  CmosNor(NetID vddNet, NetID gndNet, NetID inputANet, NetID inputBNet, NetID outputNet,
          NetID internalNet, double W, double L) noexcept
      : pmos{MosfetDescriptor{internalNet, inputANet, vddNet, vddNet, W, L},
             MosfetDescriptor{outputNet, inputBNet, internalNet, vddNet, W, L}},
        nmos{MosfetDescriptor{outputNet, inputANet, gndNet, gndNet, W, L},
             MosfetDescriptor{outputNet, inputBNet, gndNet, gndNet, W, L}},
        internalNode(internalNet) {}

  /**
   * @brief Evaluate truth table (digital logic function).
   * @param inputA Input A logic level (0 or 1).
   * @param inputB Input B logic level (0 or 1).
   * @return Output logic level (0 or 1).
   */
  [[nodiscard]] static constexpr int truthTable(int inputA, int inputB) noexcept {
    return (inputA == 0 && inputB == 0) ? 1 : 0;
  }
};

} // namespace sim::electronics::devices::composite

#endif // APEX_CMOSNOR_HPP
