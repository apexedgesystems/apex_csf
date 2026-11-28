/**
 * @file TransientCuda.cu
 * @brief CUDA implementation of GPU-accelerated transient utilities.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientCuda.cuh"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace sim::electronics::algorithms::transient::cuda {

/* ----------------------------- CUDA Transient Step ----------------------------- */

namespace {

/**
 * @brief One GPU step. `stamp` puts the static elements into the system; the
 *        two public forms differ in what their callback is given.
 */
template <typename Stamp>
TransientStatus stepWith(const Stamp& stamp, algorithms::mna::cuda::MnaCudaWorkspace& cudaWs,
                         algorithms::mna::MnaSystem& mna, CompanionSet& companions, double dt,
                         IntegrationMethod method, double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state) {
  // Check CUDA availability
  if (!available()) {
    return TransientStatus::ERROR_STEP_FAILED;
  }

  // Small circuits: GPU overhead not worth it
  if (mna.netCount() < 100) {
    return TransientStatus::ERROR_STEP_FAILED; // Signal caller to use CPU
  }

  // The system holds this step's stamps alone, whatever it held before.
  mna.clear();

  // Stamp static elements (resistors, sources)
  stamp(mna);

  // Stamp companion models for reactive elements
  companions.stampAll(mna, dt, method);

  // Check if GPU can handle this dimension
  const std::size_t DIM = mna.augmentedDim();
  if (!cudaWs.canHandle(DIM)) {
    return TransientStatus::ERROR_STEP_FAILED; // Workspace too small, use CPU
  }

  // Build augmented matrix for GPU solve
  const std::size_t N = mna.netCount();
  const std::size_t M = mna.voltageSourceCount();

  workspace.prepare(DIM);
  std::fill(workspace.A.begin(), workspace.A.begin() + static_cast<std::ptrdiff_t>(DIM * DIM), 0.0);
  std::fill(workspace.b.begin(), workspace.b.begin() + static_cast<std::ptrdiff_t>(DIM), 0.0);
  mna.buildAugmentedMatrix(workspace.A.data(), workspace.b.data());

  // Solve on GPU
  if (!algorithms::mna::cuda::solveCuda(cudaWs, workspace.A.data(), workspace.b.data(), DIM)) {
    return TransientStatus::ERROR_STEP_FAILED; // GPU solve failed, use CPU
  }

  // Extract results from solution vector
  state.time = time + dt;
  state.nodeVoltages.resize(N);
  for (std::size_t i = 0; i < N; ++i) {
    state.nodeVoltages[i] = workspace.b[i];
  }
  state.branchCurrents.resize(M);
  for (std::size_t i = 0; i < M; ++i) {
    state.branchCurrents[i] = workspace.b[N + i];
  }

  // Update state
  prevVoltages = state.nodeVoltages;
  companions.updateAll(state.nodeVoltages, dt, method);
  time += dt;

  return TransientStatus::SUCCESS;
}

} // namespace

TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs,
                         algorithms::mna::MnaSystem& mna, CompanionSet& companions,
                         const StampCallback& stampCallback, double dt, IntegrationMethod method,
                         double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state) {
  const double STAMP_TIME = time + dt;
  return stepWith(
      [&stampCallback, STAMP_TIME](algorithms::mna::MnaSystem& system) {
        if (stampCallback) {
          stampCallback(system, STAMP_TIME);
        }
      },
      cudaWs, mna, companions, dt, method, time, prevVoltages, workspace, state);
}

TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs,
                         algorithms::mna::MnaSystem& mna, CompanionSet& companions,
                         const StatefulStampCallback& stampCallback, double dt,
                         IntegrationMethod method, double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state) {
  const double STAMP_TIME = time + dt;
  return stepWith(
      [&stampCallback, &prevVoltages, STAMP_TIME](algorithms::mna::MnaSystem& system) {
        if (stampCallback) {
          stampCallback(system, STAMP_TIME, prevVoltages);
        }
      },
      cudaWs, mna, companions, dt, method, time, prevVoltages, workspace, state);
}

} // namespace sim::electronics::algorithms::transient::cuda
