/**
 * @file Intel4004Netlist_pTest.cpp
 * @brief Performance tests for Intel 4004 SPICE netlist parsing.
 *
 * Measures throughput of parsing the full lajos-4004.spice netlist (2,242
 * transistors, ~1,081 unique nets). The parser reads string content and
 * builds transistor/net data structures via std::istringstream.
 *
 * Usage:
 *   ./Intel4004Netlist_PTEST --csv results.csv
 *   ./Intel4004Netlist_PTEST --quick
 */

#include "src/bench/inc/Perf.hpp"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace ub = vernier::bench;
using sim::electronics::chips::intel4004::parseSpiceNetlist;

/* ----------------------------- Helpers ----------------------------- */

namespace {

/// Load the full netlist file into a string (done once, outside measurement).
std::string loadNetlistContent() {
  const std::string PATH = std::string(INTEL4004_DATA_DIR) + "/lajos-4004.spice";
  std::ifstream file(PATH);
  if (!file.is_open()) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

/* ----------------------------- Netlist Parse ----------------------------- */

/**
 * @brief Parse lajos-4004.spice (2,242 transistors) throughput.
 *
 * Measures the cost of parsing a pre-loaded string into transistor entries
 * and unique net lists. This is the per-invocation cost when the file
 * content is already in memory.
 */
PERF_TEST(NetlistPerf, NetlistParse) {
  UB_PERF_GUARD(perf);

  const std::string CONTENT = loadNetlistContent();
  ASSERT_FALSE(CONTENT.empty()) << "Failed to load lajos-4004.spice";

  volatile std::size_t sink = 0;

  std::printf("\n=== Netlist Parse (2242 transistors, ~1081 nets) ===\n");

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      auto result = parseSpiceNetlist(CONTENT);
      sink = result.transistorCount();
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        for (int i = 0; i < perf.cycles(); ++i) {
          auto netlist = parseSpiceNetlist(CONTENT);
          sink = netlist.transistorCount();
        }
      },
      "netlist_parse_2242tx");

  const double PER_PARSE_US = result.stats.median / perf.cycles();
  std::printf("  2242 transistors: %8.0f parses/s  (%.0f us/parse)\n",
              result.callsPerSecond * perf.cycles(), PER_PARSE_US);
}

PERF_MAIN()
