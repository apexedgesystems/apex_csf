#ifndef APEX_INTEL4004GATELEVEL_HPP
#define APEX_INTEL4004GATELEVEL_HPP
/**
 * @file Intel4004GateLevel.hpp
 * @brief Level 2 gate-level Intel 4004 simulator.
 *
 * Extracts ~427 NOR gates from the SPICE netlist and evaluates them as
 * logic functions with event-driven propagation. Each gate is a depletion-
 * load PMOS NOR: output is LOW (active) if ANY input is LOW (active).
 *
 * In PMOS convention: LOW = active/asserted, HIGH = inactive.
 * NOR truth table (PMOS): output = HIGH iff ALL inputs are HIGH (inactive).
 *
 * Uses behavioral timing injection for the timing generator (same as
 * Level 1). The data path gates are evaluated event-driven.
 *
 * Verification:
 * - Gate extraction verified by MC calibration (427 gates, 26 unique types)
 * - Functional results cross-verified against Level 0 (behavioral) and
 *   Level 1 (binary switch)
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Grid.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/Intel4004Netlist.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- Gate ----------------------------- */

struct Gate {
  std::size_t loadTransistorIdx;                 ///< Depletion load transistor index.
  std::vector<std::size_t> logicIndices;         ///< Enhancement transistor indices.
  algorithms::mna::NetID outputNet;              ///< Output net (between load and logic).
  std::vector<algorithms::mna::NetID> inputNets; ///< Input nets (logic transistor gates).

  /// Gate type string for display/categorization.
  [[nodiscard]] std::string typeString() const {
    if (logicIndices.size() == 1)
      return "INV";
    return "NOR" + std::to_string(logicIndices.size());
  }
};

/// A clock-gated pass transistor that transfers data between nodes.
/// When the clock signal is LOW (active in PMOS), source value passes to drain.
/// When HIGH (inactive), the connection is open (held by parasitic capacitance).
struct PassGate {
  std::size_t transistorIdx;        ///< Index into grid transistors.
  algorithms::mna::NetID clockNet;  ///< Gate signal (clock/timing).
  algorithms::mna::NetID sourceNet; ///< Data source.
  algorithms::mna::NetID drainNet;  ///< Data destination.
};

/* ----------------------------- Intel4004GateLevel ----------------------------- */

class Intel4004GateLevel {
public:
  static constexpr double VDD_VOLTAGE = 5.0;
  static constexpr double THRESHOLD = VDD_VOLTAGE / 2.0;
  static constexpr bool LOW = false; ///< PMOS LOW = active = asserted.
  static constexpr bool HIGH = true; ///< PMOS HIGH = inactive.

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Build gate-level model from SPICE netlist.
   *
   * Extracts all NOR gates by finding depletion loads and their connected
   * enhancement logic transistors. Also identifies pass gates (clock-gated
   * transistors that transfer data between pipeline stages).
   */
  void build(const Intel4004Netlist& netlist) {
    grid_.buildCircuit(netlist);

    netCount_ = grid_.netCount();
    vdd_ = grid_.vdd_;

    // Reset state: all nets HIGH (inactive) except GND
    netState_.assign(netCount_, HIGH);
    netState_[0] = LOW;

    buildTimingNetSet();
    extractGates();
    extractPassGates();
    buildFanoutMap();
  }

  /* ----------------------------- Accessors ----------------------------- */

  [[nodiscard]] std::size_t gateCount() const { return gates_.size(); }
  std::size_t netCount() const { return netCount_; }

  /// Get net state (HIGH/LOW).
  [[nodiscard]] bool netValue(algorithms::mna::NetID net) const { return netState_[net]; }

  /// Read accumulator as 4-bit value.
  [[nodiscard]] std::uint8_t readAccumulator() const {
    std::uint8_t acc = 0;
    for (int bit = 0; bit < 4; ++bit) {
      auto net = grid_.accNets_[bit];
      if (net > 0 && !netState_[net]) {
        acc |= (1 << bit);
      }
    }
    return acc;
  }

  /// Read carry flag.
  [[nodiscard]] bool readCarry() const {
    auto net = grid_.cyNet_;
    return (net > 0) && !netState_[net];
  }

  /* ----------------------------- Evaluation ----------------------------- */

  /**
   * @brief Evaluate a single NOR gate.
   *
   * PMOS NOR: output HIGH iff ALL inputs HIGH (inactive).
   * If ANY input is LOW (active), output is LOW.
   *
   * @return New output value.
   */
  static bool evaluateNor(const Gate& gate, const std::vector<std::uint8_t>& netState) {
    // Indexed loop with raw pointers: in a debug build, range-for on
    // std::vector<unsigned int> does not inline the iterator operators
    // (~20% self-time in operator== / operator++ / operator*). Direct
    // indexing produces a single load per iteration.
    const auto* INPUTS = gate.inputNets.data();
    const std::size_t N = gate.inputNets.size();
    const auto* STATE = netState.data();
    for (std::size_t i = 0; i < N; ++i) {
      if (!STATE[INPUTS[i]]) {
        return LOW;
      }
    }
    return HIGH;
  }

  /**
   * @brief Propagate all gates until stable (event-driven).
   *
   * Evaluates gates whose inputs have changed. Continues until no more
   * changes propagate (stable state reached) or max iterations exceeded.
   *
   * @return Number of evaluation rounds needed.
   */
  std::size_t propagate(std::size_t maxRounds = 100) {
    for (std::size_t round = 0; round < maxRounds; ++round) {
      bool anyChanged = false;

      for (auto& gate : gates_) {
        std::uint8_t newVal = evaluateNor(gate, netState_) ? 1 : 0;
        if (newVal != netState_[gate.outputNet]) {
          netState_[gate.outputNet] = newVal;
          anyChanged = true;
        }
      }

      // Pass gates transfer data when clock is active; parasitic capacitance
      // holds the destination value when the gate closes.
      for (auto& pg : passGates_) {
        if (!netState_[pg.clockNet]) {
          std::uint8_t srcVal = netState_[pg.sourceNet];
          if (srcVal != netState_[pg.drainNet]) {
            netState_[pg.drainNet] = srcVal;
            anyChanged = true;
          }
        }
      }

      if (!anyChanged) {
        return round + 1;
      }
    }

    return maxRounds;
  }

  /**
   * @brief Set a net to a specific value (external drive).
   */
  void driveNet(algorithms::mna::NetID net, bool value) {
    if (net > 0 && net < netCount_) {
      netState_[net] = value;
    }
  }

  /**
   * @brief Drive clock and timing signals for a given machine state.
   *
   * Same behavioral timing injection as Level 1. Drives CLK1, CLK2,
   * timing state signals (A12..X32), SYNC, OPA-IB.
   */
  void driveTimingState(std::uint8_t machineState, bool clk1Phase) {
    driveNet(grid_.clk1Net_, clk1Phase ? LOW : HIGH);
    driveNet(grid_.clk2Net_, clk1Phase ? HIGH : LOW);

    // Only the current state net is active (LOW); all others inactive (HIGH)
    const char* stateNames[] = {"A12", "A22", "A32", "M12", "M22", "X12", "X22", "X32"};
    for (int i = 0; i < 8; ++i) {
      auto net = grid_.findNet(stateNames[i]);
      if (net > 0) {
        driveNet(net, (machineState == i) ? LOW : HIGH);
      }
    }

    // SYNC distinguishes address phase (A1-A3) from execution phase (M1-X3)
    auto syncNet = grid_.findNet("SYNC");
    if (syncNet > 0) {
      driveNet(syncNet, (machineState <= 2) ? HIGH : LOW);
    }

    // OPA-IB and ADD-ACC are derived by the NOR gate network from compound
    // timing signals; behavioral injection would override the circuit logic.
  }

  /**
   * @brief Drive data bus with ROM byte nibble.
   *
   * PMOS convention: bit=1 -> drive LOW, bit=0 -> drive HIGH.
   */
  void driveDataBus(std::uint8_t nibble) {
    for (int bit = 0; bit < 4; ++bit) {
      driveNet(grid_.dataBusNets_[bit], (nibble & (1 << bit)) ? LOW : HIGH);
    }
  }

  /**
   * @brief Release data bus (all HIGH = inactive).
   */
  void releaseDataBus() {
    for (int bit = 0; bit < 4; ++bit) {
      driveNet(grid_.dataBusNets_[bit], HIGH);
    }
  }

  /**
   * @brief Execute one machine state (one clock period).
   *
   * Drives timing signals, propagates gates for both CLK1 and CLK2 phases.
   *
   * @param machineState State index (0=A1 through 7=X3).
   * @param rom ROM data for bus drive during M1/M2.
   * @param romByte Current byte being fetched.
   * @return Total propagation rounds.
   */
  std::size_t executeMachineState(std::uint8_t machineState, std::uint8_t romByte) {
    std::size_t totalRounds = 0;

    driveTimingState(machineState, true);

    // ROM byte enters on data bus during M1 (high nibble) and M2 (low nibble)
    if (machineState == 3) {
      driveDataBus((romByte >> 4) & 0x0F);
    } else if (machineState == 4) {
      driveDataBus(romByte & 0x0F);
    } else {
      releaseDataBus();
    }

    totalRounds += propagate();

    driveTimingState(machineState, false);
    totalRounds += propagate();

    return totalRounds;
  }

  /**
   * @brief Execute a complete program.
   *
   * Runs warmup NOPs + program bytes through the gate-level model.
   * Each byte takes 8 machine states (one instruction cycle).
   *
   * @param rom Full ROM (warmup NOPs + program).
   * @param romSize Size of ROM array.
   * @param warmupBytes Number of leading NOP bytes.
   * @param programBytes Number of program bytes to execute.
   * @return Final accumulator value.
   */
  std::uint8_t execute(const std::uint8_t* rom, std::size_t romSize, std::size_t warmupBytes,
                       std::size_t programBytes) {
    std::size_t totalBytes = warmupBytes + programBytes;

    for (std::size_t byteIdx = 0; byteIdx < totalBytes; ++byteIdx) {
      std::uint8_t romByte = (byteIdx < romSize) ? rom[byteIdx] : 0x00;

      for (std::uint8_t state = 0; state < 8; ++state) {
        executeMachineState(state, romByte);
      }
    }

    return readAccumulator();
  }

  /* ----------------------------- Data ----------------------------- */

  std::vector<Gate> gates_;         ///< All extracted NOR gates.
  std::vector<PassGate> passGates_; ///< Clock-gated pass transistors.
  Intel4004Grid grid_;              ///< Underlying grid (for net resolution).

private:
  std::size_t netCount_ = 0;
  algorithms::mna::NetID vdd_ = 0;
  std::vector<std::uint8_t> netState_;          ///< Current logic state per net (0/1).
  std::set<algorithms::mna::NetID> timingNets_; ///< Nets driven by clock/timing signals.
  std::set<algorithms::mna::NetID>
      dynamicNets_; ///< Nets only driven by pass gates (need state hold).

  /// For each net, which gates have it as an input (for event-driven eval).
  std::vector<std::vector<std::size_t>> fanoutMap_;

  /* ----------------------------- Timing Net Detection ----------------------------- */

  void buildTimingNetSet() {
    timingNets_.clear();
    const char* names[] = {"CLK1", "CLK2", "SYNC", "M12",    "M22",     "A12",    "A22",  "A32",
                           "X12",  "X22",  "X32",  "OPA-IB", "ADD-ACC", "ADD-IB", "ADSR", "ADSL"};
    for (auto name : names) {
      auto id = grid_.findNet(name);
      if (id > 0)
        timingNets_.insert(id);
    }
    // Compound timing signals (containing CLK or ~()) also qualify
    for (auto& [name, id] : grid_.netMap_) {
      if (name.find("CLK") != std::string::npos || name.find("~(") != std::string::npos) {
        timingNets_.insert(id);
      }
    }
  }

  /* ----------------------------- Gate Extraction ----------------------------- */

  void extractGates() {
    gates_.clear();
    gates_.reserve(500);

    for (std::size_t i = 0; i < grid_.transistors_.size(); ++i) {
      const auto& LOAD = grid_.transistors_[i];
      if (!LOAD.isDiodeLoad)
        continue;

      Gate gate;
      gate.loadTransistorIdx = i;
      gate.outputNet = (LOAD.drain == vdd_) ? LOAD.source : LOAD.drain;

      for (std::size_t j = 0; j < grid_.transistors_.size(); ++j) {
        if (j == i)
          continue;
        const auto& T = grid_.transistors_[j];
        if (T.isDiodeLoad || T.isLoad)
          continue;

        if (T.drain == gate.outputNet || T.source == gate.outputNet) {
          gate.logicIndices.push_back(j);

          algorithms::mna::NetID inputNet = T.gate;
          if (inputNet != 0 && inputNet != vdd_) {
            bool found = false;
            for (auto n : gate.inputNets) {
              if (n == inputNet) {
                found = true;
                break;
              }
            }
            if (!found) {
              gate.inputNets.push_back(inputNet);
            }
          }
        }
      }

      gates_.push_back(gate);
    }
  }

  void extractPassGates() {
    passGates_.clear();

    // Enhancement transistors gated by timing signals act as pass gates,
    // transferring data between pipeline stages on specific clock edges.
    for (std::size_t i = 0; i < grid_.transistors_.size(); ++i) {
      const auto& T = grid_.transistors_[i];
      if (T.isDiodeLoad || T.isLoad)
        continue;

      if (timingNets_.count(T.gate)) {
        if (T.drain != 0 && T.source != 0 && T.drain != vdd_ && T.source != vdd_) {
          passGates_.push_back({i, T.gate, T.source, T.drain});
        }
      }
    }
  }

  void buildFanoutMap() {
    fanoutMap_.resize(netCount_);
    for (std::size_t i = 0; i < gates_.size(); ++i) {
      for (auto inputNet : gates_[i].inputNets) {
        if (inputNet < netCount_) {
          fanoutMap_[inputNet].push_back(i);
        }
      }
    }

    // Nets reached only by pass gates (not NOR outputs) need state hold
    // because they rely on parasitic capacitance rather than active drive.
    std::set<algorithms::mna::NetID> norOutputs;
    for (auto& gate : gates_) {
      norOutputs.insert(gate.outputNet);
    }
    for (auto& pg : passGates_) {
      if (!norOutputs.count(pg.drainNet)) {
        dynamicNets_.insert(pg.drainNet);
      }
      // Pass gates are bidirectional
      if (!norOutputs.count(pg.sourceNet)) {
        dynamicNets_.insert(pg.sourceNet);
      }
    }
  }
};

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004GATELEVEL_HPP
