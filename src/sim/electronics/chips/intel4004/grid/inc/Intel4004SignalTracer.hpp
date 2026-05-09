#ifndef APEX_INTEL4004SIGNALTRACER_HPP
#define APEX_INTEL4004SIGNALTRACER_HPP
/**
 * @file Intel4004SignalTracer.hpp
 * @brief Signal path tracer for debugging the Intel 4004 circuit.
 *
 * Given a starting net, traces through transistor connections to find all
 * reachable nets. At each simulation step, logs the voltage at every node
 * in the trace path. Helps identify where signal propagation breaks down.
 *
 * Usage:
 * @code
 * Intel4004SignalTracer tracer(grid);
 * auto path = tracer.traceFrom("D0", 5); // 5 hops deep
 * tracer.snapshot(state.nodeVoltages);    // capture current voltages
 * tracer.printReport();                   // show the trace
 * @endcode
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Grid.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace sim::electronics::chips::intel4004 {

/* ----------------------------- TraceNode ----------------------------- */

struct TraceNode {
  std::string name;             ///< Net name.
  algorithms::mna::NetID netId = 0;         ///< Internal net ID.
  int depth = 0;                ///< Hops from starting net.
  std::string connection;       ///< How we got here (e.g., "via M123 gate").
  std::vector<double> voltages; ///< Voltage at each snapshot.
};

/* ----------------------------- Intel4004SignalTracer ----------------------------- */

struct Intel4004SignalTracer {

  explicit Intel4004SignalTracer(const Intel4004Grid& grid) : grid_(grid) {
    // Build reverse map: netId -> name
    for (auto& [name, id] : grid_.netMap_) {
      idToName_[id] = name;
    }
  }

  /**
   * @brief Trace reachable nets from a starting net via transistor connections.
   *
   * Performs a breadth-first search through the transistor network. A net B
   * is reachable from net A if there exists a transistor with A on one
   * terminal (drain/source) and B on another terminal (drain/source/gate).
   *
   * @param startNet Name of the starting net.
   * @param maxDepth Maximum number of hops to trace.
   * @return Number of nodes in the trace.
   */
  std::size_t traceFrom(const std::string& startNet, int maxDepth = 5) {
    trace_.clear();
    visited_.clear();

    auto startId = grid_.findNet(startNet);
    if (startId == 0 && startNet != "GND") {
      std::cerr << "  Net '" << startNet << "' not found\n";
      return 0;
    }

    // BFS through transistor connections
    std::queue<std::pair<algorithms::mna::NetID, int>> bfs;
    bfs.push({startId, 0});
    visited_.insert(startId);

    TraceNode root;
    root.name = startNet;
    root.netId = startId;
    root.depth = 0;
    root.connection = "(start)";
    trace_.push_back(root);

    while (!bfs.empty()) {
      auto [currentNet, depth] = bfs.front();
      bfs.pop();

      if (depth >= maxDepth)
        continue;

      // Find all transistors connected to this net
      for (std::size_t ti = 0; ti < grid_.transistors_.size(); ++ti) {
        const auto& t = grid_.transistors_[ti];
        std::string mName = "M" + std::to_string(ti);

        // Check each terminal
        auto tryAdd = [&](algorithms::mna::NetID neighbor, const std::string& via) {
          if (visited_.count(neighbor))
            return;
          visited_.insert(neighbor);

          TraceNode node;
          node.netId = neighbor;
          node.name =
              idToName_.count(neighbor) ? idToName_[neighbor] : "N" + std::to_string(neighbor);
          node.depth = depth + 1;
          node.connection = via;
          trace_.push_back(node);

          bfs.push({neighbor, depth + 1});
        };

        if (t.drain == currentNet) {
          tryAdd(t.source, mName + " D->S");
          tryAdd(t.gate, mName + " D->G");
        }
        if (t.source == currentNet) {
          tryAdd(t.drain, mName + " S->D");
          tryAdd(t.gate, mName + " S->G");
        }
        if (t.gate == currentNet) {
          tryAdd(t.drain, mName + " G->D");
          tryAdd(t.source, mName + " G->S");
        }
      }
    }

    // Sort by depth then name for readability
    std::sort(trace_.begin(), trace_.end(), [](const TraceNode& a, const TraceNode& b) {
      return a.depth < b.depth || (a.depth == b.depth && a.name < b.name);
    });

    return trace_.size();
  }

  /**
   * @brief Filter trace to only include specific named nets.
   */
  void filterNets(const std::vector<std::string>& names) {
    std::set<std::string> keep(names.begin(), names.end());
    std::vector<TraceNode> filtered;
    for (auto& node : trace_) {
      if (keep.count(node.name)) {
        filtered.push_back(node);
      }
    }
    trace_ = std::move(filtered);
  }

  /**
   * @brief Capture a voltage snapshot for all traced nodes.
   * @param label Description of this snapshot (e.g., "After LDM 5, state M1").
   */
  void snapshot(const std::vector<double>& nodeVoltages, const std::string& label = "") {
    snapshotLabels_.push_back(label);
    for (auto& node : trace_) {
      double v = (node.netId < nodeVoltages.size()) ? nodeVoltages[node.netId] : 0.0;
      node.voltages.push_back(v);
    }
  }

  /**
   * @brief Print a formatted report of all snapshots.
   */
  void printReport() const {
    double threshold = Intel4004Grid::THRESHOLD;

    // Header
    std::printf("  %-15s %3s %-20s", "Net", "Dep", "Connection");
    for (std::size_t i = 0; i < snapshotLabels_.size(); ++i) {
      std::printf(" | %-12s", snapshotLabels_[i].c_str());
    }
    std::printf("\n");

    // Separator
    std::printf("  %-15s %3s %-20s", "---", "---", "---");
    for (std::size_t i = 0; i < snapshotLabels_.size(); ++i) {
      std::printf(" | %-12s", "---");
    }
    std::printf("\n");

    // Nodes
    for (auto& node : trace_) {
      std::printf("  %-15s %3d %-20s", node.name.c_str(), node.depth, node.connection.c_str());
      for (double v : node.voltages) {
        char logic = (v > threshold) ? 'H' : 'L';
        if (v > 4.5)
          logic = '1'; // Near VDD
        if (v < 0.5)
          logic = '0'; // Near GND
        std::printf(" | %7.3fV [%c]", v, logic);
      }
      std::printf("\n");
    }
  }

  /**
   * @brief Get the traced nodes.
   */
  [[nodiscard]] const std::vector<TraceNode>& nodes() const { return trace_; }

  /**
   * @brief Add a named net to the watch list for snapshot capture.
   */
  void addWatch(const std::string& name) {
    auto id = grid_.findNet(name);
    if (id > 0 && !visited_.count(id)) {
      visited_.insert(id);
      TraceNode node;
      node.name = name;
      node.netId = id;
      node.depth = 0;
      node.connection = "watch";
      trace_.push_back(node);
    }
  }

private:
  const Intel4004Grid& grid_;
  std::unordered_map<algorithms::mna::NetID, std::string> idToName_;
  std::vector<TraceNode> trace_;
  std::set<algorithms::mna::NetID> visited_;
  std::vector<std::string> snapshotLabels_;
};

} // namespace sim::electronics::chips::intel4004

#endif // APEX_INTEL4004SIGNALTRACER_HPP
