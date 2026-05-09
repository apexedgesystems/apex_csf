/**
 * @file Intel4004GridLevel1Cuda.cu
 * @brief Scaffold for the fully GPU-resident Intel 4004 L1 NR loop.
 *
 * The constructor and destructor manage the persistent device state
 * container; the `simulateByte` entry point returns false for now.
 * Follow-up work will fill in, in order:
 *
 *   1. Scatter-table population from the host circuit topology
 *      (which transistor's (Gm, Gds) goes to which (row, col) slot).
 *   2. A device-resident stamp + scatter kernel that reads the SoA
 *      MOSFET outputs and updates `dA` / `db` directly (no D2H).
 *   3. cuSOLVER getrf/getrs on `dA` / `db` for the NR solve.
 *   4. A device-resident NR convergence check + voltage update.
 *   5. The outer time-stepping loop kept on host; only the inner NR
 *      iterations stay on device.
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1Cuda.cuh"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Components.hpp"

#include <cuda_runtime.h>

namespace sim::electronics::chips::intel4004::cuda {

Phase4ScatterTable populateScatterTable(const Intel4004GridLevel1& grid) {
  using sim::electronics::chips::intel4004::ComponentType;
  namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;

  Phase4ScatterTable out;
  const auto& transistors = grid.transistors_;
  out.classes.resize(transistors.size(), Phase4TransistorClass::L1_STAMP);

  // Requires the grid to have run `computeTransistorKp()` and the
  // component classification at least once. If the kp vector is
  // empty the grid hasn't primed yet -- classify L1 for everything
  // and use the default Kp. This is the fallback path for tests
  // that exercise the populator without running a full simulation.
  const bool haveKp = !grid.transistorKp_.empty();
  const bool haveTypes = grid.componentMode_ && !grid.componentTypes_.empty();

  // VTO used by the L1 stamp math in the CPU path (same-VTO mode).
  const double vth =
      grid.sameVtoMode_ ? Intel4004GridLevel1::VTH_ENH : Intel4004GridLevel1::VTH_ENH;

  for (std::size_t i = 0; i < transistors.size(); ++i) {
    const auto& t = transistors[i];
    Phase4TransistorClass cls = Phase4TransistorClass::L1_STAMP;

    if (haveTypes) {
      const auto ctype = grid.componentTypes_[i];
      // Mirrors Intel4004GridLevel1::stampTransistorsLevel1 dispatch:
      if (ctype == ComponentType::STANDALONE_LOAD ||
          (ctype == ComponentType::NOR_GATE_MEMBER && t.isDiodeLoad)) {
        cls = Phase4TransistorClass::DEPLETION_LOAD;
      } else if (ctype == ComponentType::DYNAMIC_STORAGE &&
                 grid.norOutputNets_.count(t.gate) == 0) {
        cls = Phase4TransistorClass::BINARY_SWITCH;
      }
    }

    // Clock-gated transistors always use the binary-switch CPU path.
    if (grid.clk1Net_ != 0 && (t.gate == grid.clk1Net_ || t.gate == grid.clk2Net_)) {
      cls = Phase4TransistorClass::BINARY_SWITCH;
    }

    out.classes[i] = cls;
    switch (cls) {
      case Phase4TransistorClass::L1_STAMP:
        out.nets.push_back(nl_cuda::MosfetNets{static_cast<int>(t.drain),
                                               static_cast<int>(t.gate),
                                               static_cast<int>(t.source)});
        out.params.push_back(sim::electronics::devices::nonlinear::MosfetLevel1Params{
            .Kp = haveKp ? grid.transistorKp_[i] : Intel4004GridLevel1::KP_PROCESS,
            .Vth = vth,
            .lambda = Intel4004GridLevel1::LAMBDA,
            .Vsmooth = 0.1});
        out.l1Indices.push_back(i);
        out.l1Count++;
        break;
      case Phase4TransistorClass::BINARY_SWITCH:
        out.binarySwitchCount++;
        break;
      case Phase4TransistorClass::DEPLETION_LOAD:
        out.depletionLoadCount++;
        break;
    }
  }

  return out;
}

Intel4004GridLevel1Cuda::Intel4004GridLevel1Cuda(Intel4004GridLevel1& /*grid*/) noexcept {
  // Scaffold: no allocations yet. Follow-up passes will size the
  // device buffers from the grid's circuit (netCount, transistor
  // list) and mark `state_.ready = true` once everything is live.
  state_.ready = false;
}

Intel4004GridLevel1Cuda::~Intel4004GridLevel1Cuda() noexcept {
  if (state_.dA != nullptr) cudaFree(state_.dA);
  if (state_.db != nullptr) cudaFree(state_.db);
  if (state_.dX != nullptr) cudaFree(state_.dX);
  if (state_.dPrevV != nullptr) cudaFree(state_.dPrevV);
  if (state_.dBiases != nullptr) cudaFree(state_.dBiases);
  if (state_.dParams != nullptr) cudaFree(state_.dParams);
  if (state_.dId != nullptr) cudaFree(state_.dId);
  if (state_.dGm != nullptr) cudaFree(state_.dGm);
  if (state_.dGds != nullptr) cudaFree(state_.dGds);
  if (state_.dScatterGRows != nullptr) cudaFree(state_.dScatterGRows);
  if (state_.dScatterGCols != nullptr) cudaFree(state_.dScatterGCols);
  if (state_.dScatterIRows != nullptr) cudaFree(state_.dScatterIRows);
  if (state_.dSolverWork != nullptr) cudaFree(state_.dSolverWork);
  if (state_.dPivot != nullptr) cudaFree(state_.dPivot);
  if (state_.dInfo != nullptr) cudaFree(state_.dInfo);
}

bool Intel4004GridLevel1Cuda::ready() const noexcept {
  return state_.ready;
}

bool Intel4004GridLevel1Cuda::simulateByte(
    const std::uint8_t* /*rom*/, std::size_t /*romSize*/, std::size_t /*warmupInstructions*/,
    std::size_t /*programInstructions*/,
    sim::electronics::algorithms::transient::TransientState& /*outState*/) noexcept {
  // Not yet implemented. Callers should fall back to the CPU
  // `Intel4004GridLevel1::simulateLevel1` path.
  return false;
}

} // namespace sim::electronics::chips::intel4004::cuda
