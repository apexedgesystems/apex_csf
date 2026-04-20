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

#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel1Cuda.cuh"

#include <cuda_runtime.h>

namespace sim::electronics::intel4004::cuda {

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
    sim::electronics::transient::TransientState& /*outState*/) noexcept {
  // Not yet implemented. Callers should fall back to the CPU
  // `Intel4004GridLevel1::simulateLevel1` path.
  return false;
}

} // namespace sim::electronics::intel4004::cuda
