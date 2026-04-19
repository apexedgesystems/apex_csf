#ifndef APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSNAND_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSNAND_HPP
/**
 * @file CmosNand.hpp
 * @brief CMOS NAND gate built from 4 MOSFETs (2 PMOS parallel, 2 NMOS series).
 *
 * Composite device that implements 2-input NAND logic gate using CMOS transistors.
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
using mna::MnaSystem;
using mna::NetID;
using nonlinear::MosfetLevel1;
using nonlinear::MosfetLevel1Params;

/* ------------------------------- CmosNand ------------------------------- */

/**
 * @brief CMOS NAND gate (2-input).
 *
 * Topology:
 *
 *       VDD
 *        |
 *    +---+---+
 *    |       |
 *   (P1)    (P2)   PMOS pull-ups (parallel)
 *    |       |
 *    +---+---+
 *        |
 *       OUT
 *        |
 *       (N1)        NMOS pull-downs (series)
 *        |
 *       (N2)
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
 *   0 | 1 |  1
 *   1 | 0 |  1
 *   1 | 1 |  0
 *
 * Usage:
 * @code
 * CmosNand nand{VDD, GND, INA, INB, OUTPUT, 10e-6, 1e-6};
 * nand.stamp(mna, vdd, vinA, vinB);
 * @endcode
 */
struct CmosNand {
  std::array<MosfetDescriptor, 2> pmos; ///< PMOS pull-up transistors (parallel).
  std::array<MosfetDescriptor, 2> nmos; ///< NMOS pull-down transistors (series).
  NetID internalNode = 0;               ///< Internal node between N1 and N2.

  /**
   * @brief Construct CMOS NAND gate.
   * @param vddNet VDD supply net.
   * @param gndNet GND supply net.
   * @param inputANet Input A net.
   * @param inputBNet Input B net.
   * @param outputNet Output net.
   * @param internalNet Internal node between series NMOS transistors.
   * @param W Channel width (all transistors).
   * @param L Channel length (all transistors).
   */
  CmosNand(NetID vddNet, NetID gndNet, NetID inputANet, NetID inputBNet, NetID outputNet,
           NetID internalNet, double W, double L) noexcept
      : pmos{MosfetDescriptor{vddNet, inputANet, outputNet, vddNet, W, L},
             MosfetDescriptor{vddNet, inputBNet, outputNet, vddNet, W, L}},
        nmos{MosfetDescriptor{outputNet, inputANet, internalNet, gndNet, W, L},
             MosfetDescriptor{internalNet, inputBNet, gndNet, gndNet, W, L}},
        internalNode(internalNet) {}

  /**
   * @brief Evaluate truth table (digital logic function).
   * @param inputA Input A logic level (0 or 1).
   * @param inputB Input B logic level (0 or 1).
   * @return Output logic level (0 or 1).
   */
  [[nodiscard]] static constexpr int truthTable(int inputA, int inputB) noexcept {
    return (inputA == 1 && inputB == 1) ? 0 : 1;
  }
};

} // namespace sim::electronics::devices::composite

#endif // APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSNAND_HPP
