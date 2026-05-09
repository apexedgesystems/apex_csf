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
 *  - Falls back to CPU for small circuits
 *
 * Usage:
 *  1. Create MnaCudaWorkspace and prepare for circuit size
 *  2. Call stepCuda() instead of manually solving MNA
 *  3. Function handles GPU solve with automatic CPU fallback
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
 * Performs complete transient step: stamp MNA, solve on GPU, update state.
 * Automatically falls back to CPU for small circuits or on GPU failure.
 *
 * @param cudaWs Prepared CUDA workspace (call prepare() first).
 * @param mna MNA system to stamp into (netCount must match workspace).
 * @param companions Companion models for reactive elements.
 * @param stampCallback Callback to stamp static elements (resistors, sources).
 * @param dt Time step size in seconds.
 * @param time Current simulation time (updated on success).
 * @param prevVoltages Previous node voltages (updated on success).
 * @param workspace CPU workspace for MNA solving.
 * @param state Output state (updated with new voltages, currents, time).
 * @return Status code (SUCCESS or ERROR_STEP_FAILED).
 *
 * Threshold: Uses GPU for circuits with 100+ nets, CPU otherwise.
 *
 * @note NOT RT-SAFE: involves GPU memory transfers and kernel launches.
 */
TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs, algorithms::mna::MnaSystem& mna,
                         CompanionSet& companions, const StampCallback& stampCallback, double dt,
                         double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state);

/**
 * @brief Overload for stateful stamp callback.
 *
 * Same as stepCuda() but uses StatefulStampCallback that receives prevVoltages.
 */
TransientStatus stepCuda(algorithms::mna::cuda::MnaCudaWorkspace& cudaWs, algorithms::mna::MnaSystem& mna,
                         CompanionSet& companions, const StatefulStampCallback& stampCallback,
                         double dt, double& time, std::vector<double>& prevVoltages,
                         algorithms::mna::MnaSolveWorkspace& workspace, TransientState& state);

} // namespace sim::electronics::algorithms::transient::cuda

#endif // APEX_TRANSIENTCUDA_CUH
