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
 *  - Returns bool: true when every system of the batch was solved
 *
 * A system is solved unless, at some elimination step, no entry left in the
 * pivot column is above dim * epsilon * (the largest magnitude of that
 * column in the matrix as given), epsilon being the precision's machine
 * epsilon. A column of zeros is the limiting case. A system that is not
 * solved comes back as not-a-number in every entry, and the others of its
 * batch are solved all the same. The entries given are taken to be finite.
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches and memory transfers.
 */

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <cstddef>

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Availability ----------------------------- */

/**
 * @brief Check if the custom batch MNA solver was built.
 * @return true if this build has the kernels, false otherwise.
 *
 * A compile-time fact (COMPAT_HAVE_CUSOLVER, set by CMake when cuSOLVER and
 * with it the CUDA toolkit is found). It does not say that a device is
 * present: without one, prepare() returns false.
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
 * Owns device memory for batch matrices and vectors. Create once at setup,
 * reuse for multiple batch solves. Not copyable: the memory has one owner.
 *
 * @tparam Scalar double (MnaBatchWorkspace) or float (MnaBatchWorkspaceF32).
 */
template <typename Scalar> struct BatchWorkspace {
  void* dA = nullptr;           ///< Device matrices (batch * dim * dim).
  void* db = nullptr;           ///< Device RHS/solution vectors (batch * dim).
  std::size_t maxDim = 0;       ///< Maximum dimension supported.
  std::size_t maxBatchSize = 0; ///< Maximum batch size supported.
  bool initialized = false;

  BatchWorkspace() = default;
  ~BatchWorkspace();

  BatchWorkspace(const BatchWorkspace&) = delete;
  BatchWorkspace& operator=(const BatchWorkspace&) = delete;
  BatchWorkspace(BatchWorkspace&&) = delete;
  BatchWorkspace& operator=(BatchWorkspace&&) = delete;

  /**
   * @brief Prepare workspace for given dimension and batch size.
   * @param dim Matrix dimension (must be 8, 16, 32, or 64).
   * @param maxBatch Maximum batch size (at least 1).
   * @return true on success; false for another dimension, an empty batch,
   *         no device, or an allocation failure.
   */
  bool prepare(std::size_t dim, std::size_t maxBatch) noexcept;

  /**
   * @brief Release all device memory.
   */
  void release() noexcept;

  /**
   * @brief Check if workspace can handle dimension and batch.
   */
  [[nodiscard]] bool canHandle(std::size_t dim, std::size_t batch) const noexcept {
    return initialized && dim <= maxDim && batch <= maxBatchSize;
  }
};

extern template struct BatchWorkspace<double>;
extern template struct BatchWorkspace<float>;

/// Workspace for double-precision batches.
using MnaBatchWorkspace = BatchWorkspace<double>;

/**
 * @brief Workspace for single-precision batches.
 *
 * 32-bit floats take half the memory bandwidth, where about 7 digits of
 * precision are enough.
 */
using MnaBatchWorkspaceF32 = BatchWorkspace<float>;

/* ----------------------------- Launch Configuration ----------------------------- */

/**
 * @brief The launch a batch solve performs, for profiling.
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
 * @brief Get the launch solveBatchCustom performs for a dimension and batch.
 *
 * Grid and block come from the table the launch itself reads; the shared
 * memory is what the compiled kernel declares, asked of the CUDA runtime
 * (zero when there is no device to ask).
 *
 * @param dim Matrix dimension.
 * @param batch Batch size.
 * @return The launch configuration; all zeros for an unsupported dimension.
 */
LaunchConfig getLaunchConfig(std::size_t dim, std::size_t batch) noexcept;

/* ----------------------------- Batch Solve API ----------------------------- */

/**
 * @brief Solve batch of linear systems using custom CUDA kernels.
 *
 * Solves batch independent systems Ax=b using Gaussian elimination with
 * partial pivoting. Optimized for small matrices (8-64).
 *
 * @param ws Prepared workspace.
 * @param As Host matrices (batch * dim * dim elements, row-major).
 * @param bs Host RHS vectors (batch * dim elements), receives solutions. A
 *           system that is not solved receives not-a-number in every entry.
 * @param dim Matrix dimension (must be 8, 16, 32, or 64).
 * @param batch Number of systems to solve.
 * @param stream CUDA stream (nullptr for default stream).
 * @param solved Optional: batch flags, true for each system solved. Written
 *               only when the batch ran.
 * @return true when every system was solved (an empty batch included).
 *         False when at least one was not, or when the batch did not run:
 *         unsupported dimension, workspace too small, or a CUDA error.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveBatchCustom(MnaBatchWorkspace& ws, const double* As, double* bs, std::size_t dim,
                      std::size_t batch, void* stream = nullptr, bool* solved = nullptr) noexcept;

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
 * @param bs Host RHS vectors (batch * dim floats), receives solutions. A
 *           system that is not solved receives not-a-number in every entry.
 * @param dim Matrix dimension (must be 8, 16, 32, or 64).
 * @param batch Number of systems to solve.
 * @param stream CUDA stream (nullptr for default stream).
 * @param solved Optional: batch flags, true for each system solved. Written
 *               only when the batch ran.
 * @return As solveBatchCustom.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveBatchCustomF32(MnaBatchWorkspaceF32& ws, const float* As, float* bs, std::size_t dim,
                         std::size_t batch, void* stream = nullptr,
                         bool* solved = nullptr) noexcept;

/**
 * @brief Get the launch solveBatchCustomF32 performs.
 * @param dim Matrix dimension.
 * @param batch Batch size.
 * @return As getLaunchConfig, for the single-precision kernels.
 */
LaunchConfig getLaunchConfigF32(std::size_t dim, std::size_t batch) noexcept;

} // namespace sim::electronics::algorithms::mna::cuda

#endif // APEX_MNABATCHCUDA_CUH
