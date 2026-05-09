#ifndef APEX_INTEL4004NETLIST_HPP
#define APEX_INTEL4004NETLIST_HPP
/**
 * @file Intel4004Netlist.hpp
 * @brief Parsed Intel 4004 transistor-level SPICE netlist data.
 *
 * Holds the result of parsing the lajos-4004.spice file: 2,242 enhancement-mode
 * PMOS transistors with drain/gate/source net names. All transistors share the
 * GND bulk terminal and "efet" model type.
 *
 * @note NOT RT-safe: std::string and std::vector allocations.
 */

#include <cstddef>
#include <string>
#include <vector>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- TransistorEntry ----------------------------- */

/**
 * @struct TransistorEntry
 * @brief A single PMOS transistor from the 4004 SPICE netlist.
 *
 * Each entry stores the three signal nets (drain, gate, source).
 * The bulk terminal (GND) and model type (efet) are common to all
 * transistors and not stored per-entry.
 */
struct TransistorEntry {
  std::string drain;  ///< Drain net name.
  std::string gate;   ///< Gate net name.
  std::string source; ///< Source net name.
};

/* ----------------------------- Intel4004Netlist ----------------------------- */

/**
 * @struct Intel4004Netlist
 * @brief Complete parsed Intel 4004 SPICE netlist.
 *
 * Expected values from lajos-4004.spice:
 * - 2,242 transistors (M0-M2241)
 * - ~1,081 unique nets (including GND and VDD)
 */
struct Intel4004Netlist {
  std::vector<TransistorEntry> transistors; ///< All transistors in netlist order.
  std::vector<std::string> uniqueNets;      ///< Sorted unique net names.

  /// Number of transistors in the netlist.
  /// @note RT-safe: Bounded O(1) accessor.
  std::size_t transistorCount() const noexcept { return transistors.size(); }

  /// Number of unique nets in the netlist.
  /// @note RT-safe: Bounded O(1) accessor.
  std::size_t netCount() const noexcept { return uniqueNets.size(); }

  /**
   * @brief Check if a named net exists in the netlist.
   * @param name Net name to search for.
   * @return true if found.
   * @note NOT RT-safe: Linear search over unique nets.
   */
  bool hasNet(const std::string& name) const {
    for (const auto& net : uniqueNets) {
      if (net == name) {
        return true;
      }
    }
    return false;
  }
};

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004NETLIST_HPP
