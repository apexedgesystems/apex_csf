/**
 * @file Intel4004GridLevel1Cuda.cu
 * @brief Scatter-table populator for the GPU Intel 4004 L1 path.
 */

#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1Cuda.cuh"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004Components.hpp"

namespace sim::electronics::chips::intel4004::cuda {

Phase4ScatterTable populateScatterTable(const Intel4004GridLevel1& grid) {
  using sim::electronics::chips::intel4004::ComponentType;
  namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;

  Phase4ScatterTable out;
  const auto& TRANSISTORS = grid.transistors_;
  out.classes.resize(TRANSISTORS.size(), Phase4TransistorClass::L1_STAMP);

  // Requires the grid to have run `computeTransistorKp()` and the
  // component classification at least once. If the kp vector is
  // empty the grid hasn't primed yet -- classify L1 for everything
  // and use the default Kp. This is the fallback path for tests
  // that exercise the populator without running a full simulation.
  const bool HAVE_KP = !grid.transistorKp_.empty();
  const bool HAVE_TYPES = grid.componentMode_ && !grid.componentTypes_.empty();

  // VTO used by the L1 stamp math in the CPU path (same-VTO mode).
  const double VTH =
      grid.sameVtoMode_ ? Intel4004GridLevel1::VTH_ENH : Intel4004GridLevel1::VTH_ENH;

  for (std::size_t i = 0; i < TRANSISTORS.size(); ++i) {
    const auto& T = TRANSISTORS[i];
    Phase4TransistorClass cls = Phase4TransistorClass::L1_STAMP;

    if (HAVE_TYPES) {
      const auto CTYPE = grid.componentTypes_[i];
      // Mirrors Intel4004GridLevel1::stampTransistorsLevel1 dispatch:
      if (CTYPE == ComponentType::STANDALONE_LOAD ||
          (CTYPE == ComponentType::NOR_GATE_MEMBER && T.isDiodeLoad)) {
        cls = Phase4TransistorClass::DEPLETION_LOAD;
      } else if (CTYPE == ComponentType::DYNAMIC_STORAGE &&
                 grid.norOutputNets_.count(T.gate) == 0) {
        cls = Phase4TransistorClass::BINARY_SWITCH;
      }
    }

    // Clock-gated TRANSISTORS always use the binary-switch CPU path.
    if (grid.clk1Net_ != 0 && (T.gate == grid.clk1Net_ || T.gate == grid.clk2Net_)) {
      cls = Phase4TransistorClass::BINARY_SWITCH;
    }

    out.classes[i] = cls;
    switch (cls) {
    case Phase4TransistorClass::L1_STAMP:
      out.nets.push_back(nl_cuda::MosfetNets{static_cast<int>(T.drain), static_cast<int>(T.gate),
                                             static_cast<int>(T.source)});
      out.params.push_back(sim::electronics::devices::nonlinear::MosfetLevel1Params{
          .Kp = HAVE_KP ? grid.transistorKp_[i] : Intel4004GridLevel1::KP_PROCESS,
          .Vth = VTH,
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

} // namespace sim::electronics::chips::intel4004::cuda
