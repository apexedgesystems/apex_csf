#ifndef APEX_INTEL4004GRIDLEVEL1CUDA_CUH
#define APEX_INTEL4004GRIDLEVEL1CUDA_CUH
/**
 * @file Intel4004GridLevel1Cuda.cuh
 * @brief Host-side scatter-table builder for the GPU Intel 4004 L1 path.
 */

// Project headers
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"

// C++ standard headers
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim::electronics::chips::intel4004::cuda {

/**
 * @brief Availability flag.
 */
inline constexpr bool available() noexcept {
#if COMPAT_HAVE_CUSOLVER
  return true;
#else
  return false;
#endif
}

/**
 * @brief Classification of a transistor for Phase 4 routing.
 *
 * Mirrors the dispatch in `Intel4004GridLevel1::stampTransistorsLevel1`:
 * only `L1_STAMP` transistors go through the GPU stamp kernel; the
 * rest fall back to a host-side CPU stamp path that writes into the
 * same device matrix via a small transfer.
 */
enum class Phase4TransistorClass : std::uint8_t {
  L1_STAMP,       ///< Full Level 1 MOSFET physics (stamp kernel target).
  BINARY_SWITCH,  ///< Clock-gated or latch-core (fixed gds).
  DEPLETION_LOAD, ///< Always-on pull-up (fixed conductance).
};

/**
 * @brief Host-side scatter-table population result.
 */
struct Phase4ScatterTable {
  std::vector<sim::electronics::devices::nonlinear::cuda::MosfetNets>
      nets; ///< Per-L1 transistor (drain, gate, source).
  std::vector<sim::electronics::devices::nonlinear::MosfetLevel1Params>
      params;                                 ///< Per-L1 transistor Kp/Vth/lambda/Vsmooth.
  std::vector<std::size_t> l1Indices;         ///< Index in grid.transistors_.
  std::vector<Phase4TransistorClass> classes; ///< Classification of every transistor.
  std::size_t l1Count = 0;                    ///< Count of L1-eligible transistors.
  std::size_t binarySwitchCount = 0;
  std::size_t depletionLoadCount = 0;
};

/**
 * @brief Walk the grid's transistor list and emit the Phase 4 scatter
 *        table.
 *
 * Selection mirrors `Intel4004GridLevel1::stampTransistorsLevel1`:
 *
 *   - `STANDALONE_LOAD` or `NOR_GATE_MEMBER+isDiodeLoad` -> DEPLETION_LOAD
 *   - `DYNAMIC_STORAGE` with non-NOR-output gate -> BINARY_SWITCH
 *   - gate == clk1Net_ or clk2Net_ -> BINARY_SWITCH
 *   - PASS_GATE / NOR_GATE_MEMBER / DYNAMIC_STORAGE-NOR-output -> L1_STAMP
 *
 * Requires `grid.transistorKp_` and `grid.componentTypes_` to be
 * populated (caller runs one CPU warmup step or calls
 * `grid.computeTransistorKp()` + `classifyComponents` beforehand).
 *
 * @param grid The Intel 4004 grid to classify.
 * @return Populated scatter table. `l1Indices.size() == l1Count`.
 */
Phase4ScatterTable populateScatterTable(const Intel4004GridLevel1& grid);

} // namespace sim::electronics::chips::intel4004::cuda

#endif // APEX_INTEL4004GRIDLEVEL1CUDA_CUH
