#ifndef APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSINVERTER_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSINVERTER_HPP
/**
 * @file CmosInverter.hpp
 * @brief CMOS Inverter (NOT gate) built from PMOS and NMOS transistors.
 *
 * Composite device that combines 2 MOSFETs into a single logical inverter.
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

/* ----------------------------- CmosInverter ----------------------------- */

/**
 * @brief CMOS Inverter (NOT gate).
 *
 * Topology:
 *
 *   VDD
 *    |
 *    +-(PMOS)-+
 *    |        |
 *   IN -------+
 *    |        |
 *    +-(NMOS)-+
 *    |        |
 *   GND      OUT
 *
 * Truth table:
 *   IN | OUT
 *   ---+----
 *    0 |  1
 *    1 |  0
 *
 * Usage:
 * @code
 * CmosInverter inv{VDD, GND, INPUT, OUTPUT, 10e-6, 1e-6};
 * inv.stamp(mna, vdd, vin);  // Stamp into MNA matrix
 * @endcode
 */
struct CmosInverter {
  MosfetDescriptor pmos; ///< PMOS pull-up transistor.
  MosfetDescriptor nmos; ///< NMOS pull-down transistor.

  /**
   * @brief Construct CMOS inverter.
   * @param vddNet VDD supply net.
   * @param gndNet GND supply net.
   * @param inputNet Input net.
   * @param outputNet Output net.
   * @param W Channel width (both transistors).
   * @param L Channel length (both transistors).
   */
  CmosInverter(NetID vddNet, NetID gndNet, NetID inputNet, NetID outputNet, double W,
               double L) noexcept
      : pmos{vddNet, inputNet, outputNet, vddNet, W, L},
        nmos{outputNet, inputNet, gndNet, gndNet, W, L} {}

  /**
   * @brief Stamp inverter into MNA system.
   * @param mna MNA system to stamp into.
   * @param vdd VDD voltage.
   * @param vin Input voltage.
   * @param params MOSFET model parameters.
   */
  static void stamp(MnaSystem& mna, const MosfetDescriptor& pmos_desc,
                    const MosfetDescriptor& nmos_desc, double vdd, double vin,
                    const MosfetLevel1Params& params_nmos, const MosfetLevel1Params& params_pmos) {
    // PMOS: gate-source voltage (Vgs = Vin - VDD, negative for PMOS)
    const double VGS_PMOS = vin - vdd;
    const double VDS_PMOS = 0.0; // Unknown until solved

    // NMOS: gate-source voltage (Vgs = Vin - GND)
    const double VGS_NMOS = vin;
    const double VDS_NMOS = 0.0; // Unknown until solved

    // Stamp PMOS (MosfetLevel1 is 3-terminal, no bulk parameter)
    MosfetLevel1::stamp(mna, pmos_desc.drainNet, pmos_desc.gateNet, pmos_desc.sourceNet, VGS_PMOS,
                        VDS_PMOS, params_pmos);

    // Stamp NMOS (MosfetLevel1 is 3-terminal, no bulk parameter)
    MosfetLevel1::stamp(mna, nmos_desc.drainNet, nmos_desc.gateNet, nmos_desc.sourceNet, VGS_NMOS,
                        VDS_NMOS, params_nmos);
  }

  /**
   * @brief Evaluate truth table (digital logic function).
   * @param input Input logic level (0 or 1).
   * @return Output logic level (0 or 1).
   */
  [[nodiscard]] static constexpr int truthTable(int input) noexcept { return (input == 0) ? 1 : 0; }
};

} // namespace sim::electronics::devices::composite

#endif // APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_CMOSINVERTER_HPP
