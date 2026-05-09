#ifndef APEX_MNABATCHCUDA_CUH
#define APEX_MNABATCHCUDA_CUH
/**
 * @file MnaBatchCuda.cuh
 * @brief Custom CUDA kernels for batch MNA solving.
 *
 * Provides GPU-accelerated batch linear system solving optimized for
 * small to medium matrices (8-64). Ideal for Monte Carlo simulations
 * and parameter sweeps where many independent systems must be solved.
 *
 * Key advantages over cuSOLVER for small matrices:
 *  - Lower kernel launch overhead
 *  - No library initialization cost
 *  - Better occupancy for small systems
 *  - Custom kernels optimized per size
 *
 * Supported dimensions: 8, 16, 32, 64
 *
 * API Design:
 *  - Host-device transfers are explicit
 *  - Stream parameter for async operation
 *  - Returns bool: true on success, false on failure
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches and memory transfers.
 */

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <cstddef>

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Availability ----------------------------- */

/**
 * @brief Check if custom batch MNA solver is available.
 * @return true if CUDA runtime is available, false otherwise.
 *
 * @note Uses COMPAT_HAVE_CUSOLVER which is a CMake-detected flag
 *       set when cuSOLVER (and thus CUDA runtime) is found.
 */
inline constexpr bool batchAvailable() noexcept {
#if COMPAT_HAVE_CUSOLVER
  return true;
#else
  return false;
#endif
}

/* ----------------------------- Supported Dimensions ----------------------------- */

/**
 * @brief Check if dimension is supported by custom kernels.
 * @param dim Matrix dimension.
 * @return true if supported (8, 16, 32, or 64).
 */
inline constexpr bool isSupportedDim(std::size_t dim) noexcept {
  return dim == 8 || dim == 16 || dim == 32 || dim == 64;
}

/* ----------------------------- Workspace ----------------------------- */

/**
 * @brief GPU workspace for batch MNA solver.
 *
 * Manages device memory for batch matrices, vectors, and status flags.
 * Create once at setup, reuse for multiple batch solves.
 */
struct MnaBatchWorkspace {
  void* dA = nullptr;           ///< Device matrices (batch * dim * dim).
  void* db = nullptr;           ///< Device RHS/solution vectors (batch * dim).
  void* dSuccess = nullptr;     ///< Device success flags (batch bools).
  std::size_t maxDim = 0;       ///< Maximum dimension supported.
  std::size_t maxBatchSize = 0; ///< Maximum batch size supported.
  bool initialized = false;

  ~MnaBatchWorkspace();

  /**
   * @brief Prepare workspace for given dimension and batch size.
   * @param dim Matrix dimension (must be 8, 16, 32, or 64).
   * @param maxBatch Maximum batch size.
   * @return true on success, false on allocation failure.
   */
  bool prepare(std::size_t dim, std::size_t maxBatch) noexcept;

  /**
   * @brief Release all device memory.
   */
  void release() noexcept;

  /**
   * @brief Check if workspace can handle dimension and batch.
   */
  bool canHandle(std::size_t dim, std::size_t batch) const noexcept {
    return initialized && dim <= maxDim && batch <= maxBatchSize;
  }
};

/* ----------------------------- Launch Configuration ----------------------------- */

/**
 * @brief Kernel launch configuration for profiling.
 */
struct LaunchConfig {
  std::size_t gridX = 0;
  std::size_t gridY = 1;
  std::size_t gridZ = 1;
  std::size_t blockX = 0;
  std::size_t blockY = 1;
  std::size_t blockZ = 1;
  std::size_t sharedMemBytes = 0;
  std::size_t threadsPerSystem = 1;
};

/**
 * @brief Get launch configuration for given dimension and batch.
 * @param dim Matrix dimension.
 * @param batch Batch size.
 * @return Launch configuration for profiling.
 */
LaunchConfig getLaunchConfig(std::size_t dim, std::size_t batch) noexcept;

/* ----------------------------- FP32 Workspace ----------------------------- */

/**
 * @brief GPU workspace for batch MNA solver (single precision).
 *
 * Uses 32-bit floats for 2x memory bandwidth and potentially higher throughput.
 * Suitable for applications where ~7 digits of precision is sufficient.
 */
struct MnaBatchWorkspaceF32 {
  void* dA = nullptr;           ///< Device matrices (batch * dim * dim floats).
  void* db = nullptr;           ///< Device RHS/solution vectors (batch * dim floats).
  void* dSuccess = nullptr;     ///< Device success flags (batch bools).
  std::size_t maxDim = 0;       ///< Maximum dimension supported.
  std::size_t maxBatchSize = 0; ///< Maximum batch size supported.
  bool initialized = false;

  ~MnaBatchWorkspaceF32();

  /**
   * @brief Prepare workspace for given dimension and batch size.
   * @param dim Matrix dimension (must be 8, 16, 32, or 64).
   * @param maxBatch Maximum batch size.
   * @return true on success, false on allocation failure.
   */
  bool prepare(std::size_t dim, std::size_t maxBatch) noexcept;

  /**
   * @brief Release all device memory.
   */
  void release() noexcept;

  /**
   * @brief Check if workspace can handle dimension and batch.
   */
  bool canHandle(std::size_t dim, std::size_t batch) const noexcept {
    return initialized && dim <= maxDim && batch <= maxBatchSize;
  }
};

/* ----------------------------- Batch Solve API ----------------------------- */

/**
 * @brief Solve batch of linear systems using custom CUDA kernels.
 *
 * Solves batch independent systems Ax=b using Gaussian elimination with
 * partial pivoting. Optimized for small matrices (8-64).
 *
 * @param ws Prepared workspace.
 * @param As Host matrices (batch * dim * dim elements, row-major).
 * @param bs Host RHS vectors (batch * dim elements), receives solutions.
 * @param dim Matrix dimension (must be 8, 16, 32, or 64).
 * @param batch Number of systems to solve.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveBatchCustom(MnaBatchWorkspace& ws, const double* As, double* bs, std::size_t dim,
                      std::size_t batch, void* stream = nullptr) noexcept;

/**
 * @brief Solve batch of linear systems using single precision (FP32).
 *
 * Same algorithm as solveBatchCustom but uses 32-bit floats for:
 *  - 2x memory bandwidth (half the data size)
 *  - Potentially 2x throughput on memory-bound kernels
 *  - ~7 digits of precision (vs ~16 for FP64)
 *
 * Use when precision requirements allow and throughput is critical.
 *
 * @param ws Prepared FP32 workspace.
 * @param As Host matrices (batch * dim * dim floats, row-major).
 * @param bs Host RHS vectors (batch * dim floats), receives solutions.
 * @param dim Matrix dimension (must be 8, 16, 32, or 64).
 * @param batch Number of systems to solve.
 * @param stream CUDA stream (nullptr for default stream).
 * @return true on success, false on failure.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveBatchCustomF32(MnaBatchWorkspaceF32& ws, const float* As, float* bs, std::size_t dim,
                         std::size_t batch, void* stream = nullptr) noexcept;

/**
 * @brief Get launch configuration for FP32 kernels.
 * @param dim Matrix dimension.
 * @param batch Batch size.
 * @return Launch configuration for profiling.
 */
LaunchConfig getLaunchConfigF32(std::size_t dim, std::size_t batch) noexcept;

} // namespace sim::electronics::algorithms::mna::cuda

#endif // APEX_MNABATCHCUDA_CUH
