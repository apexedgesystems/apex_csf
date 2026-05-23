#ifndef APEX_MNASYSTEMCUDA_CUH
#define APEX_MNASYSTEMCUDA_CUH
/**
 * @file MnaSystemCuda.cuh
 * @brief CUDA-accelerated MNA solver using cuSOLVER.
 *
 * Provides GPU-accelerated LU factorization and solve for large circuits.
 * Particularly beneficial for circuits with 100+ nets where GPU parallelism
 * outweighs data transfer overhead.
 *
 * API Design:
 *  - Device memory is managed internally.
 *  - Host-device transfers are explicit (prepare, sync).
 *  - Returns bool: true on success, false on failure or CUDA unavailable.
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches and memory transfers.
 */

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <cstddef>
#include <cstdint>

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Availability ----------------------------- */

/**
 * @brief Check if CUDA MNA solver is available.
 * @return true if cuSOLVER is available, false otherwise.
 * @note RT-SAFE: pure constexpr.
 */
inline constexpr bool available() noexcept {
#if COMPAT_HAVE_CUSOLVER
  return true;
#else
  return false;
#endif
}

/* ----------------------------- GPU Workspace ----------------------------- */

/**
 * @brief GPU workspace for MNA solver.
 *
 * Manages device memory for matrix, vectors, and cuSOLVER workspace.
 * Create once at setup, reuse for multiple solves.
 */
struct MnaCudaWorkspace {
  void* dA = nullptr;           ///< Device matrix (dim x dim).
  void* db = nullptr;           ///< Device RHS/solution vector.
  void* dInfo = nullptr;        ///< Device info for cuSOLVER.
  void* dWork = nullptr;        ///< cuSOLVER workspace.
  int* dIpiv = nullptr;         ///< Device pivot indices.
  std::size_t maxDim = 0;       ///< Maximum dimension supported.
  std::size_t workSize = 0;     ///< cuSOLVER workspace size.
  void* solverHandle = nullptr; ///< cuSOLVER handle.
  bool initialized = false;

  /**
   * @brief Prepare workspace for given maximum dimension.
   * @param dim Maximum matrix dimension.
   * @return true on success, false on allocation failure.
   * @note NOT RT-SAFE: allocates device memory.
   */
  bool prepare(std::size_t dim) noexcept;

  /**
   * @brief Release all device memory.
   * @note NOT RT-SAFE: deallocates device memory.
   */
  void release() noexcept;

  /**
   * @brief Check if workspace can handle dimension.
   * @note RT-SAFE.
   */
  [[nodiscard]] bool canHandle(std::size_t dim) const noexcept {
    return initialized && dim <= maxDim;
  }
};

/* ----------------------------- GPU Solve API ----------------------------- */

/**
 * @brief Solve MNA system on GPU.
 *
 * Copies matrix A and vector b to device, solves Ax=b using cuSOLVER,
 * and copies result back to host.
 *
 * @param ws Prepared workspace.
 * @param A Host matrix (row-major, dim x dim).
 * @param b Host RHS vector (dim elements), receives solution.
 * @param dim Matrix dimension.
 * @return true on success, false on failure.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveCuda(MnaCudaWorkspace& ws, const double* A, double* b, std::size_t dim) noexcept;

/**
 * @brief Solve Ax = b for device-resident A and b (no H2D / D2H).
 *
 * Assumes the caller has already populated `dA` (dim x dim row-major)
 * and `dB` (dim) on the device; the solution is written back into
 * `dB` in place. `dA` is destroyed by the LU factorization. The
 * workspace must be prepared for a dim >= the requested dim.
 *
 * This is the Phase 4 inner-NR-iter solve: the MNA matrix is
 * populated on device by `stampMosfetL1Batch`, so re-uploading it to
 * the device after every iteration would be wasted bandwidth.
 *
 * @param ws   Prepared workspace (`dA`/`dB` members are unused; any
 *             device pointers can be passed explicitly).
 * @param dA   Device matrix (dim x dim, row-major).
 * @param dB   Device RHS (dim); receives solution.
 * @param dim  Matrix dimension.
 * @return true on success, false on singular matrix or CUDA error.
 *
 * @note NOT RT-SAFE: kernel launches.
 */
bool solveCudaDeviceResident(MnaCudaWorkspace& ws, double* dA, double* dB,
                             std::size_t dim) noexcept;

/**
 * @brief Batch solve multiple MNA systems on GPU.
 *
 * Solves multiple independent systems in parallel. Each system must have
 * the same dimension. Useful for Monte Carlo simulations or parameter sweeps.
 *
 * @param ws Prepared workspace.
 * @param As Host matrices (batch * dim * dim elements).
 * @param bs Host RHS vectors (batch * dim elements), receives solutions.
 * @param dim Matrix dimension.
 * @param batchSize Number of systems to solve.
 * @return true on success, false on failure.
 *
 * @note NOT RT-SAFE: involves memory transfers and kernel launches.
 */
bool solveBatchCuda(MnaCudaWorkspace& ws, const double* As, double* bs, std::size_t dim,
                    std::size_t batchSize) noexcept;

} // namespace sim::electronics::algorithms::mna::cuda

#endif // APEX_MNASYSTEMCUDA_CUH
