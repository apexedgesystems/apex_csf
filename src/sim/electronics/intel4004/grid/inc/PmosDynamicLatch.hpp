#ifndef APEX_SIM_ELECTRONICS_INTEL4004_PMOS_DYNAMIC_LATCH_HPP
#define APEX_SIM_ELECTRONICS_INTEL4004_PMOS_DYNAMIC_LATCH_HPP
/**
 * @file PmosDynamicLatch.hpp
 * @brief Charge-transfer model for Intel 4004 PMOS dynamic storage nodes.
 *
 * The Intel 4004 uses depletion-load PMOS dynamic logic where data is stored
 * as charge on parasitic node capacitances. The standard MNA+NR approach
 * finds the DC steady-state (VOL ~= VTH), but the real circuit operates in
 * a charge-transfer regime where nodes discharge well below VTH during the
 * evaluate clock phase.
 *
 * This model computes node voltages by integrating MOSFET drain current
 * over the evaluate window:
 *
 *   V_final = V_precharge - integral(I_ds, 0, t_eval) / C_node
 *
 * The result depends on:
 *   - Precharge voltage (VDD, from depletion load during CLK1)
 *   - Pull-down current (from Level 1 Shichman-Hodges equations)
 *   - Evaluate time (clock half-period)
 *   - Node capacitance (parasitic)
 *
 * For the 4004's calibrated parameters, the pull-down fully discharges
 * the node in ~4ps (I_ds ~= 100mA, C ~= 100fF), giving V_final near 0V
 * and strong overdrive (>1V) for cascaded latch feedback.
 *
 * This is Intel 4004-specific physics for the ~338 cross-coupled latch
 * feedback transistors. All other transistors (NOR gates, pass gates,
 * depletion loads) continue to use standard Level 1 MNA stamps.
 *
 * @note NOT RT-safe (allocates during initialization).
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace sim::electronics::intel4004 {

using devices::nonlinear::MosfetLevel1;
using devices::nonlinear::MosfetLevel1Params;

/**
 * @brief Charge-transfer latch node model.
 *
 * Each dynamic storage node tracks:
 *   - Its parasitic capacitance
 *   - The transistors connected to it (pull-ups and pull-downs)
 *   - Its current charge state (voltage)
 *
 * During each simulation sub-step, the model:
 *   1. Computes net current into the node from all connected transistors
 *   2. Integrates: V_new = V_old + (I_net * dt) / C
 *   3. Stamps the resulting voltage as a voltage source in the MNA
 */
struct DynamicLatchNode {
  mna::NetID net = 0;
  double voltage = 5.0;       ///< Current node voltage (initialized to VDD = precharged)
  double capacitance = 100e-15; ///< Node parasitic capacitance (F)

  struct ConnectedTransistor {
    mna::NetID otherTerminal; ///< The other drain/source terminal
    mna::NetID gate;          ///< Gate terminal
    MosfetLevel1Params params;
    bool isDeplLoad = false;  ///< Depletion load (always-ON pull toward VDD)
  };
  std::vector<ConnectedTransistor> transistors;
};

/**
 * @brief Manager for all dynamic latch nodes in the Intel 4004 grid.
 *
 * Replaces the behavioral sample-and-hold with charge-based physics.
 * Each sub-step:
 *   1. For each latch node, compute net PMOS current from connected transistors
 *   2. Integrate charge: dV = I_net * dt / C
 *   3. Update stored voltage
 *   4. Stamp as voltage source in MNA (prevents NR from finding DC metastable)
 */
struct PmosDynamicLatchManager {

  /// Evaluate time for one clock phase (used for current integration).
  double evalTime = 25e-9;  // 25ns default (quarter of 1us machine state)

  /**
   * @brief Initialize from the grid's transistor list and component classification.
   *
   * Creates a DynamicLatchNode for each net touched by DYNAMIC_STORAGE
   * transistors whose gate is NOT a NOR output (the latch feedback core).
   */
  void initialize(const auto& grid, const auto& classification, const auto& componentTypes,
                  const auto& norOutputNets) {
    nodes_.clear();
    nodeMap_.clear();

    auto vdd = grid.vdd_;

    // Find latch core nets: dynamic storage transistors whose gate is NOT a NOR output
    for (std::size_t i = 0; i < grid.transistors_.size(); ++i) {
      if (componentTypes[i] != ComponentType::DYNAMIC_STORAGE) continue;
      if (norOutputNets.count(grid.transistors_[i].gate)) continue; // NOR-gated = Level 1

      const auto& t = grid.transistors_[i];

      // Determine effective Kp from W/L bin
      double kp = 5e-3 * 3.23; // Default enhancement
      double vth = 1.17;
      if (t.isDiodeLoad || (t.gate == vdd && (t.drain == vdd || t.source == vdd))) {
        kp = 5e-3 * 0.10;
        vth = -0.17;
      }
      MosfetLevel1Params params{.Kp = kp, .Vth = vth, .lambda = 0.03};

      // Register both terminals as latch nodes
      auto registerNode = [&](mna::NetID nodeNet, mna::NetID otherNet) {
        if (nodeNet == 0 || nodeNet == vdd) return;
        if (nodeMap_.count(nodeNet) == 0) {
          nodeMap_[nodeNet] = nodes_.size();
          DynamicLatchNode node;
          node.net = nodeNet;
          node.voltage = 5.0; // Precharged to VDD
          nodes_.push_back(node);
        }
        auto& node = nodes_[nodeMap_[nodeNet]];
        node.transistors.push_back({otherNet, t.gate, params,
                                    t.isDiodeLoad || (t.gate == vdd)});
      };
      registerNode(t.drain, t.source);
      registerNode(t.source, t.drain);
    }
  }

  /**
   * @brief Update latch node voltages by integrating PMOS currents.
   *
   * For each latch node:
   *   1. Compute net current from all connected transistors using Level 1 I-V
   *   2. Integrate: V_new = V_old + I_net * dt / C
   *   3. Clamp to [0, VDD]
   *
   * @param prevV Current node voltages (for gate/other terminal lookups)
   * @param dt Timestep for integration
   * @param vdd Supply voltage
   */
  void updateCharges(const std::vector<double>& prevV, double dt, double vdd) {
    for (auto& node : nodes_) {
      double iNet = 0.0;

      for (const auto& tr : node.transistors) {
        double vNode = node.voltage;
        double vOther = (tr.otherTerminal > 0 && tr.otherTerminal < prevV.size())
                            ? prevV[tr.otherTerminal]
                            : (tr.otherTerminal == 0 ? 0.0 : vdd);
        double vGate = (tr.gate > 0 && tr.gate < prevV.size())
                            ? prevV[tr.gate]
                            : (tr.gate == 0 ? 0.0 : vdd);

        // PMOS: current flows from higher to lower potential terminal
        double vHigh = std::max(vNode, vOther);
        double vLow = std::min(vNode, vOther);
        double vsg = vHigh - vGate;
        double vsd = vHigh - vLow;

        if (vsg <= 0.0) continue; // Gate above source = OFF

        double vsgC = std::max(vsg, 0.0);
        double vsdC = std::max(vsd, 0.0);
        double id = MosfetLevel1::current(vsgC, vsdC, tr.params);

        // Current direction: flows from high to low.
        // If node is the high terminal, current leaves (negative).
        // If node is the low terminal, current enters (positive).
        if (vNode >= vOther) {
          iNet -= id; // Current leaves this node
        } else {
          iNet += id; // Current enters this node
        }
      }

      // Integrate charge: dV = I * dt / C
      double dv = iNet * dt / node.capacitance;

      // Limit voltage change per step for stability
      if (dv > 0.5) dv = 0.5;
      if (dv < -0.5) dv = -0.5;

      node.voltage += dv;

      // Clamp to rail
      if (node.voltage < 0.0) node.voltage = 0.0;
      if (node.voltage > vdd) node.voltage = vdd;
    }
  }

  /**
   * @brief Stamp latch node voltages as voltage sources in MNA.
   *
   * Forces each latch node to its charge-computed voltage. This prevents
   * the NR solver from finding the DC metastable mid-rail point.
   */
  template <typename MnaSystemT>
  void stamp(MnaSystemT& mna) const {
    for (const auto& node : nodes_) {
      mna.addVoltageSource(node.net, 0, node.voltage);
    }
  }

  /**
   * @brief Force a specific node to a voltage (for state seeding).
   */
  void forceNodeVoltage(mna::NetID net, double voltage) {
    if (nodeMap_.count(net)) {
      nodes_[nodeMap_[net]].voltage = voltage;
    }
  }

  [[nodiscard]] std::size_t nodeCount() const { return nodes_.size(); }

private:
  std::vector<DynamicLatchNode> nodes_;
  std::unordered_map<mna::NetID, std::size_t> nodeMap_;
};

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_INTEL4004_PMOS_DYNAMIC_LATCH_HPP
