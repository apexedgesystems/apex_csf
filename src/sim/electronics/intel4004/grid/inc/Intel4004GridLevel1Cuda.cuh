#ifndef APEX_SIM_ELECTRONICS_INTEL4004_GRID_LEVEL1_CUDA_CUH
#define APEX_SIM_ELECTRONICS_INTEL4004_GRID_LEVEL1_CUDA_CUH
/**
 * @file Intel4004GridLevel1Cuda.cuh
 * @brief Fully GPU-resident Intel 4004 Level 1 NR loop (scaffold).
 *
 * This sidecar holds the state and entry points for running the
 * Intel 4004 L1 Newton-Raphson loop on the device end-to-end: stamp,
 * solve, and voltage update all without leaving GPU memory. One host
 * synchronisation per NR convergence, not per iteration.
 *
 * The CPU-driven hybrid pattern was measured to lose (0.66x vs CPU at
 * single-4004 scale) because per-iter `cudaDeviceSynchronize` + H2D/
 * D2H transfers of the bias and stamp arrays dominate the tiny
 * 2242-device kernel body. Only full GPU residence recovers the math
 * time advantage. See
 * `docs/optimization/0419/sim_electronics_devices_nonlinear_cuda/phase4a_driver/analysis.md`.
 *
 * Scaffold status: declaration only. Implementation will iterate
 * across follow-up sessions, adding the device-resident NR loop,
 * MOSFET stamp->scatter, and the GPU solve path incrementally.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/sim/electronics/algorithms/transient/inc/TransientConfig.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"
#include "src/sim/electronics/intel4004/grid/inc/Intel4004GridLevel1.hpp"

namespace sim::electronics::intel4004::cuda {

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
 * @brief Fully device-resident state for one L1 simulation.
 *
 * Owned by `Intel4004GridLevel1Cuda`. All buffers are allocated once
 * at construction from the circuit topology; the NR loop never
 * touches the host side except for the initial ROM upload and the
 * final node-voltage readback per byte.
 *
 * Memory footprint for a 2242-transistor / 1121-net Intel 4004 in
 * FP64: ~10 MB for the dense MNA matrix (dominant), plus small
 * vectors (< 100 KB total).
 */
struct Intel4004Level1CudaState {
  std::size_t netCount = 0;        ///< N in Ax=b (4004: 1121).
  std::size_t transistorCount = 0; ///< 2242 for full 4004.

  // MNA system (dense for the first cut; sparse via cuSolverSP is a later pass).
  void* dA = nullptr; ///< N x N dense matrix, column-major.
  void* db = nullptr; ///< N RHS vector.
  void* dX = nullptr; ///< N solution vector (node voltages).

  // NR state.
  void* dPrevV = nullptr; ///< Previous iteration's node voltages (for convergence / limiting).

  // MOSFET state (per-transistor).
  void* dBiases = nullptr; ///< sim::electronics::devices::nonlinear::cuda::MosfetBias[T].
  void* dParams = nullptr; ///< sim::electronics::devices::nonlinear::MosfetLevel1Params[T].
  void* dId = nullptr;     ///< T doubles -- stamp SoA output.
  void* dGm = nullptr;     ///< T doubles.
  void* dGds = nullptr;    ///< T doubles.

  // Scatter pattern: for each transistor, the (G row, G col, I row) offsets
  // into the MNA matrix / RHS. Populated once from the circuit topology.
  void* dScatterGRows = nullptr; ///< int[T * 4] -- 4 G stamps per transistor.
  void* dScatterGCols = nullptr; ///< int[T * 4].
  void* dScatterIRows = nullptr; ///< int[T * 2] -- 2 I stamps per transistor.

  // cuSOLVER workspace.
  void* dSolverWork = nullptr;
  int solverWorkSize = 0;
  int* dPivot = nullptr;
  int* dInfo = nullptr;
  void* solverHandle = nullptr;

  bool ready = false; ///< All allocations succeeded.
};

/**
 * @brief Driver for the fully GPU-resident L1 NR loop.
 *
 * Constructs and owns `Intel4004Level1CudaState`. Each `simulateByte`
 * call executes one ROM byte's worth of NR iterations end-to-end on
 * the device.
 *
 * @note NOT RT-safe (allocates device memory in constructor).
 */
class Intel4004GridLevel1Cuda {
public:
  /**
   * @brief Construct from a Level 1 grid that has already been built
   *        (i.e. has a netlist / circuit / per-transistor params).
   *
   * Allocates all device buffers and builds the scatter tables from
   * the host-side circuit topology.
   */
  explicit Intel4004GridLevel1Cuda(Intel4004GridLevel1& grid) noexcept;

  ~Intel4004GridLevel1Cuda() noexcept;

  Intel4004GridLevel1Cuda(const Intel4004GridLevel1Cuda&) = delete;
  Intel4004GridLevel1Cuda& operator=(const Intel4004GridLevel1Cuda&) = delete;
  Intel4004GridLevel1Cuda(Intel4004GridLevel1Cuda&&) = delete;
  Intel4004GridLevel1Cuda& operator=(Intel4004GridLevel1Cuda&&) = delete;

  /**
   * @brief Run one ROM byte's NR iterations on the device.
   *
   * @param rom                 ROM bytes (host pointer; uploaded each call).
   * @param romSize             Number of bytes available at `rom`.
   * @param warmupInstructions  Number of binary-switch warm-up bytes.
   * @param programInstructions Number of L1 analog bytes to execute.
   * @param outState            Host-side result state; node voltages are
   *                            the only field populated in this scaffold.
   * @return true on success.
   */
  [[nodiscard]] bool simulateByte(const std::uint8_t* rom, std::size_t romSize,
                                  std::size_t warmupInstructions,
                                  std::size_t programInstructions,
                                  sim::electronics::transient::TransientState& outState) noexcept;

  [[nodiscard]] bool ready() const noexcept;

private:
  Intel4004Level1CudaState state_;
};

} // namespace sim::electronics::intel4004::cuda

#endif // APEX_SIM_ELECTRONICS_INTEL4004_GRID_LEVEL1_CUDA_CUH
