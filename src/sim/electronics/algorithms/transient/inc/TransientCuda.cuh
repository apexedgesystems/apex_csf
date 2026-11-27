#ifndef APEX_TRANSIENTCUDA_CUH
#define APEX_TRANSIENTCUDA_CUH
/**
 * @file TransientCuda.cuh
 * @brief CUDA-accelerated transient simulation utilities.
 *
 * Provides GPU-accelerated solving for transient circuit simulation.
 * These are free functions that complement the CPU-based TransientSolver.
 *
 * Design:
 *  - No CUDA headers in signature (workspace as opaque type)
 *  - Explicit parameter passing (no hidden state)
 *  - A step the GPU does not take is reported, not taken on the CPU
 *
 * Usage:
 *  1. Create MnaCudaWorkspace and prepare for circuit size
 *  2. Call stepCuda() instead of manually solving MNA
 *  3. On ERROR_STEP_FAILED nothing was stepped: the caller chooses what to do
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"

#include <cstddef>
#include <vector>

namespace sim::electronics::algorithms::transient::cuda {

/* ----------------------------- CUDA Availability ----------------------------- */

/**
 * @brief Check if CUDA transient solving is available.
 * @return true if cuSOLVER is available, false otherwise.
 * @note RT-SAFE: pure constexpr.
 */
inline constexpr bool available() noexcept { return algorithms::mna::cuda::available(); }

/* ----------------------------- CUDA Transient Step ----------------------------- */

/**
 * @brief Execute one transient time step using GPU acceleration.
 *
 * Clears the system, stamps it, solves on the GPU and updates the state.
 * A step the GPU does not take (no CUDA, fewer than 100 nets, a workspace
 * too small, a failed solve) returns ERROR_STEP_FAILED with time, companions
 * and state as they were, for the caller to take it on the CPU.
 *
 * @param cudaWs Prepared CUDA workspace (call prepare() first).
 * @param mna MNA system to stamp into (netCount must match workspace). It is
 *            cleared first, so one system serves every step.
 * @param companions Companion models for reactive elements.
 * @param stampCallback Callback to stamp static elements (resistors, sources).
 * @param dt Time step size in seconds.
 * @param method Integration method of the step: the one the companions are
 *               stamped and updated with. The companions, previous voltages
 *               and time are the caller's; a TransientSolver holds its own,
 *               and passing its method here does not bring the two
 *               histories together.
 * @param time Current simulation time (updated on success).
 * @param prevVoltages Previous node voltages (updated on success).
 * @param workspace CPU workspace for MNA solving.
 * @param state Output state (updated with new voltages, currents, time).
 * @return Status code (SUCCESS or ERROR_STEP_FAILED).
 *
 * Threshold: circuits of fewer than 100 nets are not taken (ERROR_STEP_FAILED).
 *
 * @note NOT RT-SAFE: involves GPU memory transfers and kernel launches.
 */
TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs,
                         algorithms::mna::MnaSystem& mna, CompanionSet& companions,
                         const StampCallback& stampCallback, double dt, IntegrationMethod method,
                         double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state);

/**
 * @brief Overload for stateful stamp callback.
 *
 * Same as stepCuda() but uses StatefulStampCallback that receives prevVoltages.
 */
TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs,
                         algorithms::mna::MnaSystem& mna, CompanionSet& companions,
                         const StatefulStampCallback& stampCallback, double dt,
                         IntegrationMethod method, double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state);

} // namespace sim::electronics::algorithms::transient::cuda

#endif // APEX_TRANSIENTCUDA_CUH
