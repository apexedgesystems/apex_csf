/**
 * @file TransientCuda.cu
 * @brief CUDA implementation of GPU-accelerated transient utilities.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientCuda.cuh"

#include <algorithm>

namespace sim::electronics::transient::cuda {

/* ----------------------------- CUDA Transient Step ----------------------------- */

TransientStatus stepCuda(mna::cuda::MnaCudaWorkspace& cudaWs, mna::MnaSystem& mna,
                         CompanionSet& companions, const StampCallback& stampCallback, double dt,
                         double& time, std::vector<double>& prevVoltages,
                         mna::MnaSolveWorkspace& workspace, TransientState& state) {
  // Check CUDA availability
  if (!available()) {
    return TransientStatus::ERROR_STEP_FAILED;
  }

  // Small circuits: GPU overhead not worth it
  std::size_t netCount = mna.netCount();
  if (netCount < 100) {
    return TransientStatus::ERROR_STEP_FAILED; // Signal caller to use CPU
  }

  // Stamp static elements (resistors, sources)
  if (stampCallback) {
    stampCallback(mna, time + dt);
  }

  // Stamp companion models for reactive elements
  companions.stampAll(mna, dt);

  // Check if GPU can handle this dimension
  std::size_t dim = mna.augmentedDim();
  if (!cudaWs.canHandle(dim)) {
    return TransientStatus::ERROR_STEP_FAILED; // Workspace too small, use CPU
  }

  // Build augmented matrix for GPU solve
  std::size_t n = mna.netCount();
  std::size_t m = mna.voltageSourceCount();

  workspace.prepare(dim);
  std::fill(workspace.A.begin(), workspace.A.begin() + dim * dim, 0.0);
  std::fill(workspace.b.begin(), workspace.b.begin() + dim, 0.0);
  mna.buildAugmentedMatrix(workspace.A.data(), workspace.b.data());

  // Solve on GPU
  if (!mna::cuda::solveCuda(cudaWs, workspace.A.data(), workspace.b.data(), dim)) {
    return TransientStatus::ERROR_STEP_FAILED; // GPU solve failed, use CPU
  }

  // Extract results from solution vector
  state.time = time + dt;
  state.nodeVoltages.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    state.nodeVoltages[i] = workspace.b[i];
  }
  state.branchCurrents.resize(m);
  for (std::size_t i = 0; i < m; ++i) {
    state.branchCurrents[i] = workspace.b[n + i];
  }

  // Update state
  prevVoltages = state.nodeVoltages;
  companions.updateAll(state.nodeVoltages, dt);
  time += dt;

  return TransientStatus::SUCCESS;
}

TransientStatus stepCuda(mna::cuda::MnaCudaWorkspace& cudaWs, mna::MnaSystem& mna,
                         CompanionSet& companions, const StatefulStampCallback& stampCallback,
                         double dt, double& time, std::vector<double>& prevVoltages,
                         mna::MnaSolveWorkspace& workspace, TransientState& state) {
  // Check CUDA availability
  if (!available()) {
    return TransientStatus::ERROR_STEP_FAILED;
  }

  // Small circuits: GPU overhead not worth it
  std::size_t netCount = mna.netCount();
  if (netCount < 100) {
    return TransientStatus::ERROR_STEP_FAILED; // Signal caller to use CPU
  }

  // Stamp static elements with previous voltages
  if (stampCallback) {
    stampCallback(mna, time + dt, prevVoltages);
  }

  // Stamp companion models for reactive elements
  companions.stampAll(mna, dt);

  // Check if GPU can handle this dimension
  std::size_t dim = mna.augmentedDim();
  if (!cudaWs.canHandle(dim)) {
    return TransientStatus::ERROR_STEP_FAILED; // Workspace too small, use CPU
  }

  // Build augmented matrix for GPU solve
  std::size_t n = mna.netCount();
  std::size_t m = mna.voltageSourceCount();

  workspace.prepare(dim);
  std::fill(workspace.A.begin(), workspace.A.begin() + dim * dim, 0.0);
  std::fill(workspace.b.begin(), workspace.b.begin() + dim, 0.0);
  mna.buildAugmentedMatrix(workspace.A.data(), workspace.b.data());

  // Solve on GPU
  if (!mna::cuda::solveCuda(cudaWs, workspace.A.data(), workspace.b.data(), dim)) {
    return TransientStatus::ERROR_STEP_FAILED; // GPU solve failed, use CPU
  }

  // Extract results from solution vector
  state.time = time + dt;
  state.nodeVoltages.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    state.nodeVoltages[i] = workspace.b[i];
  }
  state.branchCurrents.resize(m);
  for (std::size_t i = 0; i < m; ++i) {
    state.branchCurrents[i] = workspace.b[n + i];
  }

  // Update state
  prevVoltages = state.nodeVoltages;
  companions.updateAll(state.nodeVoltages, dt);
  time += dt;

  return TransientStatus::SUCCESS;
}

} // namespace sim::electronics::transient::cuda
