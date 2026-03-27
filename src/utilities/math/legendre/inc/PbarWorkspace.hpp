#ifndef APEX_UTILITIES_MATH_LEGENDRE_PBAR_WORKSPACE_HPP
#define APEX_UTILITIES_MATH_LEGENDRE_PBAR_WORKSPACE_HPP
/**
 * @file PbarWorkspace.hpp
 * @brief Persistent GPU workspace for batched fully normalized Legendre triangle computation.
 *
 * Provides reusable device buffers to eliminate per-call cudaMalloc/cudaFree overhead.
 * Supports optional pinned host buffers for faster async H2D/D2H transfers.
 *
 * Design goals:
 *  - Reuse device buffers across calls
 *  - Optional pinned host buffers for faster async transfers
 *  - Opaque stream parameter (void*) to avoid including CUDA headers
 */

#include <cstddef>

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"
#include "src/utilities/math/legendre/inc/PbarTriangleCuda.cuh"

namespace apex {
namespace math {
namespace legendre {

/* ----------------------------- PbarWorkspace ----------------------------- */

/**
 * @brief Persistent buffers for batched Legendre Pbar computations.
 *
 * Host members `hXs` and `hOut` exist only when `pinnedHost == true`.
 * Device buffers always exist after successful create.
 */
struct PbarWorkspace {
  // Config
  int n{0};
  int batch{0};
  std::size_t triSize{0}; // (n+1)(n+2)/2
  std::size_t outLen{0};  // triSize * batch
  bool pinnedHost{false};
  void* stream{nullptr}; // cast to cudaStream_t in .cu

  // Device - P values
  double* dXs{nullptr};  // length = batch (sin(phi) or x values)
  double* dOut{nullptr}; // length = outLen (Pbar output)

  // Device - Derivative support (allocated via createPbarWorkspaceWithDerivatives)
  double* dCosPhis{nullptr}; // length = batch (cos(phi) values for derivatives)
  double* dDpOut{nullptr};   // length = outLen (dPbar/dphi output)

  // Precomputed recurrence coefficients (device, length = triSize)
  // A[n,m] and B[n,m] as used in the upward recurrence:
  //   Pbar_{n,m} = A(n,m) * x * Pbar_{n-1,m} - B(n,m) * Pbar_{n-2,m}
  double* dA{nullptr};
  double* dB{nullptr};
  bool coeffReady{false}; // set true once dA/dB are allocated & populated for this n

  // Precomputed beta coefficients for derivatives (device, length = triSize)
  // beta[n,m] = sqrt((n-m)(n+m+1))
  double* dBeta{nullptr};
  bool betaReady{false}; // set true once dBeta is allocated & populated for this n

  // Optional pinned host buffers (allocated when pinnedHost==true)
  double* hXs{nullptr};      // length = batch
  double* hOut{nullptr};     // length = outLen
  double* hCosPhis{nullptr}; // length = batch (for derivatives)
  double* hDpOut{nullptr};   // length = outLen (for derivatives)

  // Configuration flag
  bool derivativesEnabled{false}; // true when derivative buffers are allocated
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Create (or re-create) a workspace for given N and batch.
 * @note NOT RT-safe: Performs CUDA allocations.
 */
SIM_NODISCARD bool createPbarWorkspace(PbarWorkspace& ws, int n, int batch, bool pinnedHost = true,
                                       void* stream = nullptr) noexcept;

/**
 * @brief Destroy all buffers owned by @p ws and reset fields to defaults.
 * @note NOT RT-safe: Performs CUDA deallocations.
 */
void destroyPbarWorkspace(PbarWorkspace& ws) noexcept;

/**
 * @brief Create (or re-create) a workspace with derivative support.
 *
 * Allocates additional buffers for cos(phi) inputs and dP/dphi outputs.
 *
 * @note NOT RT-safe: Performs CUDA allocations.
 */
SIM_NODISCARD bool createPbarWorkspaceWithDerivatives(PbarWorkspace& ws, int n, int batch,
                                                      bool pinnedHost = true,
                                                      void* stream = nullptr) noexcept;

/**
 * @brief Ensure device-side coefficient arrays (dA, dB) exist and are populated for ws.n.
 *
 * Allocates and fills ws.dA/ws.dB if not present; sets ws.coeffReady=true on success.
 * No-op (returns true) if already ready for the current ws.n/triSize.
 *
 * @note NOT RT-safe: May perform CUDA allocations and kernel launch.
 */
SIM_NODISCARD bool ensurePbarCoefficients(PbarWorkspace& ws) noexcept;

/**
 * @brief Ensure device-side beta coefficient array exists and is populated for ws.n.
 *
 * Allocates and fills ws.dBeta if not present; sets ws.betaReady=true on success.
 * No-op (returns true) if already ready.
 *
 * @note NOT RT-safe: May perform CUDA allocations and kernel launch.
 */
SIM_NODISCARD bool ensureBetaCoefficients(PbarWorkspace& ws) noexcept;

/**
 * @brief Free device-side coefficient arrays (dA, dB) and clear coeffReady.
 * @note NOT RT-safe: Performs CUDA deallocations.
 */
void freePbarCoefficients(PbarWorkspace& ws) noexcept;

/**
 * @brief Free device-side beta coefficient array and clear betaReady.
 * @note NOT RT-safe: Performs CUDA deallocations.
 */
void freeBetaCoefficients(PbarWorkspace& ws) noexcept;

/**
 * @brief Enqueue a batched compute on @p ws.
 *
 * Copies @p hXs to device (if non-null, else uses ws.hXs), runs kernel into ws.dOut,
 * and optionally copies back to @p hOut (if non-null) or ws.hOut when @p copyBack==true.
 * Uses ws.dA/ws.dB if ws.coeffReady is true; otherwise computes coeffs on the fly.
 *
 * @note RT-safe if workspace is pre-allocated and coefficients are pre-computed.
 */
SIM_NODISCARD bool enqueueCompute(PbarWorkspace& ws, const double* hXs = nullptr,
                                  double* hOut = nullptr, bool copyBack = true) noexcept;

/**
 * @brief Enqueue a batched compute with derivatives on @p ws.
 *
 * Requires workspace created with createPbarWorkspaceWithDerivatives.
 * Copies sin(phi) from @p hSinPhis (or ws.hXs) and cos(phi) from @p hCosPhis (or ws.hCosPhis),
 * runs the derivative kernel, and optionally copies P and dP back.
 * Uses ws.dA/ws.dB/ws.dBeta if respective Ready flags are true.
 *
 * @note RT-safe if workspace is pre-allocated and all coefficients are pre-computed.
 */
SIM_NODISCARD bool enqueueComputeWithDerivatives(PbarWorkspace& ws,
                                                 const double* hSinPhis = nullptr,
                                                 const double* hCosPhis = nullptr,
                                                 double* hPOut = nullptr, double* hDpOut = nullptr,
                                                 bool copyBack = true) noexcept;

/**
 * @brief Synchronize the stream associated with @p ws.
 * @note NOT RT-safe: Blocks on GPU completion.
 */
SIM_NODISCARD bool synchronize(const PbarWorkspace& ws) noexcept;

/**
 * @brief Device pointer accessors (host-only).
 * @note RT-safe: Simple field access.
 */
SIM_FI double* deviceXs(const PbarWorkspace& ws) noexcept { return ws.dXs; }
SIM_FI double* deviceOut(const PbarWorkspace& ws) noexcept { return ws.dOut; }
SIM_FI double* deviceCosPhis(const PbarWorkspace& ws) noexcept { return ws.dCosPhis; }
SIM_FI double* deviceDpOut(const PbarWorkspace& ws) noexcept { return ws.dDpOut; }

} // namespace legendre
} // namespace math
} // namespace apex

#endif // APEX_UTILITIES_MATH_LEGENDRE_PBAR_WORKSPACE_HPP
