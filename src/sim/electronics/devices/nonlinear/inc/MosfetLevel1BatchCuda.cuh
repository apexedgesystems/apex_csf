#ifndef APEX_MOSFETLEVEL1BATCHCUDA_CUH
#define APEX_MOSFETLEVEL1BATCHCUDA_CUH
/**
 * @file MosfetLevel1BatchCuda.cuh
 * @brief Batch GPU evaluation of MosfetLevel1::stampValues.
 *
 * For large MOSFET counts (e.g., the 2242 transistors of the Intel 4004
 * per NR iteration), evaluating `id`, `gm`, `gds` on the GPU is
 * embarrassingly parallel: each MOSFET is independent. The underlying
 * math is `MosfetLevel1::stampValues`, re-used on device via SIM_HD.
 *
 * Two entry points:
 *   - `evalStampBatchUniform`: all devices share the same
 *     MosfetLevel1Params (typical: single-technology test benches).
 *   - `evalStampBatch`: per-device params array (typical: Intel 4004
 *     where each transistor has its own Kp from the W/L calibration).
 *
 * Inputs and outputs are device pointers. The host is responsible for
 * H2D of biases/params and D2H of {id, gm, gds}, or for keeping the
 * arrays device-resident across many NR iterations.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <cstddef>

namespace sim::electronics::devices::nonlinear::cuda {

/**
 * @brief Per-device MOSFET bias point (input to stamp kernel).
 */
struct MosfetBias {
  double vgs; ///< Gate-source voltage.
  double vds; ///< Drain-source voltage.
};

/**
 * @brief Per-transistor net connectivity for the fused MOSFET stamp.
 *
 * Net IDs index into a flat N-element node-voltage vector and the
 * N x N MNA matrix. Net 0 is ground by convention and is ignored by
 * the scatter (it is the reference node, not a DOF).
 */
struct MosfetNets {
  int drain;
  int gate;
  int source;
};

/**
 * @brief Per-device stamp result (output from stamp kernel).
 */
struct MosfetStamp {
  double id;  ///< Drain current.
  double gm;  ///< dId/dVgs.
  double gds; ///< dId/dVds.
};

/**
 * @brief Availability flag for the GPU batch MOSFET evaluator.
 */
inline constexpr bool available() noexcept {
#if COMPAT_HAVE_CUSOLVER
  return true;
#else
  return false;
#endif
}

/**
 * @brief Launch-config descriptor for profiling / hand-tuning.
 */
struct MosfetBatchConfig {
  std::size_t blockSize = 256;
  std::size_t gridSize = 0;
};

/**
 * @brief Batch-evaluate N MOSFETs, all sharing the same model parameters.
 *
 * Computes `{id, gm, gds}` for each device in parallel. Uses one thread
 * per device, block size 256.
 *
 * @param dBiases  Device array of N MosfetBias.
 * @param params   Model parameters (uniform across the batch, passed by value).
 * @param dStamps  Device array of N MosfetStamp (output).
 * @param count    Number of devices in the batch.
 * @param stream   CUDA stream (nullptr for default stream).
 * @return true on launch success, false otherwise.
 *
 * @note NOT RT-safe: launches a CUDA kernel.
 */
bool evalStampBatchUniform(const MosfetBias* dBiases, const MosfetLevel1Params& params,
                           MosfetStamp* dStamps, std::size_t count,
                           void* stream = nullptr) noexcept;

/**
 * @brief Batch-evaluate N MOSFETs with per-device parameters.
 *
 * Each device has its own `MosfetLevel1Params` (typically for circuits
 * with W/L binning, such as the calibrated Intel 4004 transistors).
 * Params are read once per thread from global memory.
 *
 * @param dBiases  Device array of N MosfetBias.
 * @param dParams  Device array of N MosfetLevel1Params.
 * @param dStamps  Device array of N MosfetStamp (output).
 * @param count    Number of devices in the batch.
 * @param stream   CUDA stream (nullptr for default stream).
 * @return true on launch success, false otherwise.
 *
 * @note NOT RT-safe: launches a CUDA kernel.
 */
bool evalStampBatch(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                    MosfetStamp* dStamps, std::size_t count, void* stream = nullptr) noexcept;

/**
 * @brief Batch-evaluate N MOSFETs, writing outputs in SoA layout.
 *
 * Same math as `evalStampBatch` but writes three aligned parallel
 * arrays (`dId`, `dGm`, `dGds`) instead of the 24-byte `MosfetStamp`
 * struct-of-three. Each store is an 8-byte double at the thread's
 * natural offset; a warp of 32 threads writes one fully-coalesced
 * 256-byte burst per array (3 x 256 B total), versus 32 x 24 B =
 * 768 B in straddled transactions for the AoS variant. Intended for
 * the fused stamp+scatter path and any caller that consumes the
 * three quantities independently (e.g. separate MNA G / I vectors).
 *
 * @note NOT RT-safe: launches a CUDA kernel.
 */
bool evalStampBatchSoA(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                       double* dId, double* dGm, double* dGds, std::size_t count,
                       void* stream = nullptr) noexcept;

/**
 * @brief Fused stamp + scatter for the MNA Jacobian of N MOSFETs.
 *
 * Evaluates `MosfetLevel1::stampValues(vgs, vds, params)` for each
 * transistor and, using SPICE mode selection (xnrm/xrev from the sign
 * of vds), atomically adds the linearised contribution into the MNA
 * matrix `dG` (row-major, netCount x netCount) and RHS vector `dI`.
 * This is the core building block of the fully GPU-resident Intel 4004
 * L1 NR loop.
 *
 * The caller must have cleared the MOSFET contribution slice of `dG`
 * and `dI` before calling this kernel (MNA accumulates; any pre-
 * existing stamps from other component types will be preserved).
 *
 * Net 0 is treated as ground and skipped (no writes).
 *
 * @param dBiases   Device array of N MosfetBias (vgs, vds).
 * @param dParams   Device array of N MosfetLevel1Params.
 * @param dNets     Device array of N MosfetNets (drain, gate, source).
 * @param dG        Device N_net x N_net matrix, row-major (modified).
 * @param dI        Device N_net RHS vector (modified).
 * @param count     Number of MOSFETs.
 * @param netCount  N_net (row/column extent of dG; length of dI).
 * @param gmin      Conductance floor (added to gds for numerical
 *                  stability; matches the CPU stampTransistorsLevel1
 *                  convention).
 * @param stream    CUDA stream.
 * @return true on launch success.
 *
 * @note NOT RT-safe: launches a CUDA kernel.
 */
bool stampMosfetL1Batch(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                        const MosfetNets* dNets, double* dG, double* dI, std::size_t count,
                        std::size_t netCount, double gmin = 1e-12,
                        void* stream = nullptr) noexcept;

/**
 * @brief Apply NR limiting and update node voltages on device.
 *
 * Given the freshly-solved voltages `dNewV` and the previous NR
 * iteration's voltages `dPrevV`, this kernel (in two launches):
 *
 *   1. Computes `delta[i] = dNewV[i] - dPrevV[i]` and reduces to
 *      `maxAbsDelta = max_i |delta[i]|`.
 *   2. If `maxAbsDelta > limit`, scales all deltas uniformly so the
 *      largest change is exactly `limit` volts; otherwise leaves
 *      them alone. Writes the result back into `dPrevV` for the
 *      next NR iteration.
 *
 * `dMaxDelta` is a single device-resident double. The caller reads
 * it after the kernel returns to decide convergence (compare to the
 * NR threshold). `dMaxDelta` is the UNLIMITED max-delta, which is
 * what the CPU path compares to for convergence.
 *
 * Matches `Intel4004GridLevel1::simulateLevel1` NR limiter: 5.0 V
 * per-iteration cap on max voltage change.
 *
 * @param dNewV     Device array of N doubles (freshly solved x).
 * @param dPrevV    Device array of N doubles (prev iter; updated in place).
 * @param dMaxDelta Device scalar (1 double); output: L_inf delta.
 * @param n         N (length of dNewV / dPrevV).
 * @param limit     Max |delta| per iter (default 5.0 V, matches CPU).
 * @param stream    CUDA stream.
 * @return true on launch success.
 */
bool nrUpdateAndLimit(const double* dNewV, double* dPrevV, double* dMaxDelta, std::size_t n,
                      double limit = 5.0, void* stream = nullptr) noexcept;

/**
 * @brief Get the launch configuration used for a given batch size.
 * @note Useful for nsys / ncu profiling annotations.
 */
MosfetBatchConfig getLaunchConfig(std::size_t count) noexcept;

/**
 * @brief Driver with persistent device buffers for repeated batch stamps.
 *
 * Use case: a Newton-Raphson loop that needs to evaluate stampValues for
 * the same set of MOSFETs many times in succession with new bias points
 * each iteration. Allocating + uploading params once at setup avoids
 * paying the per-iteration cost of `cudaMalloc` / `cudaMemcpy` for
 * the params array. The bias array and stamp output array are still
 * transferred per call (since the bias changes each NR iter), but the
 * device-resident params eliminate one large upload per iteration.
 *
 * This is the architectural prerequisite for any fully GPU-resident
 * L1 path: without persistent device state, per-call transfer overhead
 * dominates and the GPU loses to a CPU `stampValues` loop.
 *
 * @note NOT RT-safe (allocates device memory in constructor / setup).
 */
class MosfetStampDriver {
public:
  /**
   * @brief Construct with a maximum supported MOSFET count.
   * @param maxCount Maximum count of devices for any single evalBatch call.
   *                 Allocates device buffers for this size.
   */
  explicit MosfetStampDriver(std::size_t maxCount) noexcept;

  ~MosfetStampDriver() noexcept;

  MosfetStampDriver(const MosfetStampDriver&) = delete;
  MosfetStampDriver& operator=(const MosfetStampDriver&) = delete;
  MosfetStampDriver(MosfetStampDriver&&) = delete;
  MosfetStampDriver& operator=(MosfetStampDriver&&) = delete;

  /**
   * @brief Upload per-device MosfetLevel1Params once for the driver lifetime.
   * @param hostParams Host array of N MosfetLevel1Params.
   * @param count      N (must be <= maxCount given at construction).
   * @return true on success, false on size violation or transfer error.
   */
  bool setParams(const MosfetLevel1Params* hostParams, std::size_t count) noexcept;

  /**
   * @brief Per-iteration call: upload bias, run kernel, download stamps.
   * @param hostBiases Host MosfetBias array of N.
   * @param hostStamps Host MosfetStamp output array of N.
   * @param count      N (must match the count used in the most recent
   *                   `setParams` call).
   * @return true on success.
   */
  bool evalBatch(const MosfetBias* hostBiases, MosfetStamp* hostStamps,
                 std::size_t count) noexcept;

  /**
   * @brief True if the driver is initialised and CUDA is available.
   */
  [[nodiscard]] bool ready() const noexcept;

  /**
   * @brief Maximum count supported (set at construction).
   */
  [[nodiscard]] std::size_t maxCount() const noexcept { return maxCount_; }

private:
  std::size_t maxCount_ = 0;
  std::size_t paramsCount_ = 0;
  void* dBiases_ = nullptr;
  void* dParams_ = nullptr;
  void* dStamps_ = nullptr;
};

} // namespace sim::electronics::devices::nonlinear::cuda

#endif // APEX_MOSFETLEVEL1BATCHCUDA_CUH
