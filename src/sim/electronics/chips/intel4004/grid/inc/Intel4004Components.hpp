#ifndef APEX_INTEL4004COMPONENTS_HPP
#define APEX_INTEL4004COMPONENTS_HPP
/**
 * @file Intel4004Components.hpp
 * @brief Component-level classification of Intel 4004 transistors.
 *
 * Classifies each of the 2242 transistors into one of four component types:
 *   1. NOR gate member: part of a depletion-load NOR gate (proven physics)
 *   2. Pass gate: clock/timing-gated data transfer transistor
 *   3. Dynamic storage: no active driver, held by parasitic capacitance
 *   4. Standalone load: depletion load with no NOR pull-downs
 *
 * Each type gets a different stamp method and verification strategy.
 * This prevents global parameters (GMIN, damping) from cascading across
 * component types.
 *
 * Data sources:
 *   - GateExtractor (427 NOR gates, from calibration app)
 *   - TransistorClassifier (7 W/L bins, from calibration app)
 *   - Intel4004GateLevel (133 pass gates, 66 standalone loads)
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Grid.hpp"

#include <cstddef>
#include <unordered_set>
#include <vector>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- Component Type ----------------------------- */

enum class ComponentType : std::uint8_t {
  NOR_GATE_MEMBER = 0, ///< Part of a depletion-load NOR gate (proven).
  PASS_GATE,           ///< Clock/timing-gated data transfer.
  DYNAMIC_STORAGE,     ///< No active driver, held by capacitance.
  STANDALONE_LOAD,     ///< Depletion load with no NOR pull-downs.
  UNKNOWN              ///< Unclassified.
};

inline const char* componentTypeName(ComponentType t) {
  switch (t) {
  case ComponentType::NOR_GATE_MEMBER:
    return "NOR_GATE";
  case ComponentType::PASS_GATE:
    return "PASS_GATE";
  case ComponentType::DYNAMIC_STORAGE:
    return "DYNAMIC";
  case ComponentType::STANDALONE_LOAD:
    return "LOAD";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- Classification Result ----------------------------- */

struct ComponentClassification {
  std::vector<ComponentType> types; ///< Per-transistor component type.
  std::size_t counts[5] = {};       ///< Count per type.

  std::size_t norGateCount() const { return counts[0]; }
  std::size_t passGateCount() const { return counts[1]; }
  std::size_t dynamicCount() const { return counts[2]; }
  std::size_t loadCount() const { return counts[3]; }
  std::size_t unknownCount() const { return counts[4]; }
};

/* ----------------------------- Classification ----------------------------- */

/**
 * @brief Classify all transistors by component type.
 *
 * Uses the grid's transistor list and net map to determine each transistor's
 * role in the circuit. The classification is based on connectivity:
 *
 * 1. Depletion loads (gate=VDD, drain/source=VDD) -> STANDALONE_LOAD
 * 2. Enhancement transistors with drain/source at a depletion-loaded net
 *    -> NOR_GATE_MEMBER
 * 3. Enhancement transistors with a timing signal on gate -> PASS_GATE
 * 4. Everything else -> DYNAMIC_STORAGE
 */
inline ComponentClassification classifyComponents(const Intel4004Grid& grid) {
  ComponentClassification result;
  result.types.resize(grid.transistors_.size(), ComponentType::UNKNOWN);

  auto vdd = grid.vdd_;

  // Step 1: Find all depletion-loaded nets (NOR gate output nets)
  std::unordered_set<algorithms::mna::NetID> norOutputNets;
  for (std::size_t i = 0; i < grid.transistors_.size(); ++i) {
    const auto& t = grid.transistors_[i];
    if (t.isDiodeLoad) {
      algorithms::mna::NetID outNet = (t.drain == vdd) ? t.source : t.drain;
      if (outNet != 0)
        norOutputNets.insert(outNet);
      result.types[i] = ComponentType::STANDALONE_LOAD;
      result.counts[static_cast<int>(ComponentType::STANDALONE_LOAD)]++;
    }
  }

  // Step 2: Build timing net set for pass gate identification
  std::unordered_set<algorithms::mna::NetID> timingNets;
  for (auto& [name, id] : grid.netMap_) {
    if (name == "CLK1" || name == "CLK2" || name == "SYNC" || name == "M12" || name == "M22" ||
        name == "A12" || name == "A22" || name == "A32" || name == "X12" || name == "X22" ||
        name == "X32" || name.find("CLK") != std::string::npos ||
        name.find("&") != std::string::npos) { // Compound timing signals
      timingNets.insert(id);
    }
  }

  // Step 3: Classify non-load transistors
  for (std::size_t i = 0; i < grid.transistors_.size(); ++i) {
    if (result.types[i] != ComponentType::UNKNOWN)
      continue; // Already classified

    const auto& t = grid.transistors_[i];

    // Check if drain or source connects to a NOR output net
    bool touchesNorOutput = norOutputNets.count(t.drain) || norOutputNets.count(t.source);

    // Check if gate is a timing signal
    bool gatedByTiming = timingNets.count(t.gate);

    if (touchesNorOutput && !gatedByTiming) {
      // Enhancement pull-down in a NOR gate
      result.types[i] = ComponentType::NOR_GATE_MEMBER;
      result.counts[static_cast<int>(ComponentType::NOR_GATE_MEMBER)]++;
    } else if (gatedByTiming) {
      // Pass gate (timing-gated)
      result.types[i] = ComponentType::PASS_GATE;
      result.counts[static_cast<int>(ComponentType::PASS_GATE)]++;
    } else {
      // No NOR output connection, no timing gate -> dynamic storage
      result.types[i] = ComponentType::DYNAMIC_STORAGE;
      result.counts[static_cast<int>(ComponentType::DYNAMIC_STORAGE)]++;
    }
  }

  // Reclassify standalone loads that have NOR gate members -> they're NOR loads
  for (std::size_t i = 0; i < grid.transistors_.size(); ++i) {
    if (result.types[i] == ComponentType::STANDALONE_LOAD) {
      const auto& t = grid.transistors_[i];
      algorithms::mna::NetID outNet = (t.drain == vdd) ? t.source : t.drain;
      // Check if any NOR_GATE_MEMBER connects to this output
      bool hasMembers = false;
      for (std::size_t j = 0; j < grid.transistors_.size(); ++j) {
        if (result.types[j] == ComponentType::NOR_GATE_MEMBER) {
          const auto& m = grid.transistors_[j];
          if (m.drain == outNet || m.source == outNet) {
            hasMembers = true;
            break;
          }
        }
      }
      if (hasMembers) {
        // Exclude from standalone count: part of NOR gate
        result.types[i] = ComponentType::NOR_GATE_MEMBER;
        result.counts[static_cast<int>(ComponentType::STANDALONE_LOAD)]--;
        result.counts[static_cast<int>(ComponentType::NOR_GATE_MEMBER)]++;
      }
    }
  }

  return result;
}

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004COMPONENTS_HPP
