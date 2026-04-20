#ifndef APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETLEVEL1_BATCH_CUDA_CUH
#define APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETLEVEL1_BATCH_CUDA_CUH
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

#endif // APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_MOSFETLEVEL1_BATCH_CUDA_CUH
