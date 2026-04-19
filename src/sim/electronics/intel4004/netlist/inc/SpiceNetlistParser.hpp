#ifndef APEX_SIM_ELECTRONICS_CPU_INTEL4004_SPICE_NETLIST_PARSER_HPP
#define APEX_SIM_ELECTRONICS_CPU_INTEL4004_SPICE_NETLIST_PARSER_HPP
/**
 * @file SpiceNetlistParser.hpp
 * @brief Parser for SPICE3-format transistor netlists.
 *
 * Reads the lajos-4004.spice netlist format:
 *   M<id> <drain> <gate> <source> <bulk> <model>
 *
 * Example lines:
 *   M0 N0385 N0770 GND GND efet
 *   M26 VDD SC(A22+M22)CLK2 N0866 GND efet
 *
 * Signal names may contain letters, digits, dots, parentheses, tildes,
 * and plus signs. They are stored as opaque string identifiers.
 *
 * @note NOT RT-safe: File I/O and memory allocation.
 */

#include "src/sim/electronics/intel4004/netlist/inc/Intel4004Netlist.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace sim::electronics::intel4004 {

/* ----------------------------- API ----------------------------- */

/**
 * @brief Parse a SPICE3 netlist from a string.
 *
 * Extracts all M-lines (transistor definitions) and builds a sorted
 * list of unique net names from drain/gate/source fields.
 *
 * @param content Full text content of the SPICE file.
 * @return Parsed netlist with transistors and unique nets.
 * @note NOT RT-safe: String parsing, memory allocation.
 */
inline Intel4004Netlist parseSpiceNetlist(const std::string& content) {
  Intel4004Netlist result;
  std::set<std::string> netSet;

  std::istringstream stream(content);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '*') {
      continue;
    }

    if (line[0] != 'M') {
      continue;
    }

    std::istringstream tokens(line);
    std::string id;
    std::string drain;
    std::string gate;
    std::string source;

    tokens >> id >> drain >> gate >> source;

    if (drain.empty() || gate.empty() || source.empty()) {
      continue;
    }

    result.transistors.push_back({drain, gate, source});

    netSet.insert(drain);
    netSet.insert(gate);
    netSet.insert(source);
  }

  result.uniqueNets.assign(netSet.begin(), netSet.end());

  return result;
}

/**
 * @brief Load and parse a SPICE3 netlist from a file.
 * @param filePath Path to the SPICE file.
 * @return Parsed netlist with transistors and unique nets.
 * @throws std::runtime_error if the file cannot be opened.
 * @note NOT RT-safe: File I/O, memory allocation.
 */
inline Intel4004Netlist loadSpiceNetlist(const std::string& filePath) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open SPICE netlist: " + filePath);
  }

  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  return parseSpiceNetlist(content);
}

} // namespace sim::electronics::intel4004

#endif // APEX_SIM_ELECTRONICS_CPU_INTEL4004_SPICE_NETLIST_PARSER_HPP
