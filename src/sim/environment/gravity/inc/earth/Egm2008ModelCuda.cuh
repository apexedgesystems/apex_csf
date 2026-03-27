#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_CUDA_CUH
#define APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_CUDA_CUH
/**
 * @file Egm2008ModelCuda.cuh
 * @brief CUDA-accelerated EGM2008 spherical harmonic gravity model for batch evaluation.
 *
 * Provides GPU-accelerated computation of gravitational potential and acceleration
 * using the EGM2008 spherical harmonic model. Designed for batch evaluation where
 * multiple positions are evaluated together for maximum throughput.
 *
 * Architecture:
 *  - Uses Legendre library PbarWorkspace for P/dP computation on GPU
 *  - Device-resident C/S coefficients (uploaded once at init)
 *  - Custom CUDA kernels for spherical harmonic summation
 *  - Optional pinned host buffers for faster async transfers
 *
 * Performance targets (N=2190, batch=100):
 *  - CPU: ~100ms per position
 *  - GPU: ~1ms per position (100x speedup)
 *
 * @note CUDA headers required; compile with nvcc or clang with CUDA support.
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

#include <cstdint>
#include <vector>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- Egm2008CudaWorkspace ----------------------------- */

/**
 * @brief Workspace for batched CUDA gravity evaluation.
 *
 * Contains device buffers for positions, intermediate values, and results.
 * Manages Legendre computation via PbarWorkspace.
 */
struct Egm2008CudaWorkspace {
  // Configuration
  int16_t n{0}; ///< Max degree
  int batch{0}; ///< Batch size
  bool pinnedHost{false};
  void* stream{nullptr};

  // Legendre workspace (owns its own buffers)
  apex::math::legendre::PbarWorkspace legendreWs{};

  // Device coefficient storage (length = triSize each)
  double* dCoeffC{nullptr};
  double* dCoeffS{nullptr};
  std::size_t triSize{0}; ///< (n+1)(n+2)/2

  // Device trig buffers for cos(m*lam), sin(m*lam) (length = batch * (n+1))
  double* dCosml{nullptr};
  double* dSinml{nullptr};

  // Device position inputs (length = batch * 3)
  double* dPosEcef{nullptr};

  // Device intermediate values (length = batch each)
  double* dRmag{nullptr};   ///< |r|
  double* dSinPhi{nullptr}; ///< sin(phi) = z/|r|
  double* dCosPhi{nullptr}; ///< cos(phi)
  double* dCosLam{nullptr}; ///< cos(lam) = x/rxy
  double* dSinLam{nullptr}; ///< sin(lam) = y/rxy
  double* dUPow{nullptr};   ///< (a/r)^n power array, length = batch * (n+1)

  // Device outputs (length = batch for V, batch * 3 for acceleration)
  double* dV{nullptr};
  double* dAccel{nullptr};

  // Pinned host buffers (allocated when pinnedHost==true)
  double* hPosEcef{nullptr}; ///< length = batch * 3
  double* hV{nullptr};       ///< length = batch
  double* hAccel{nullptr};   ///< length = batch * 3

  // Status flags
  bool coeffReady{false}; ///< C/S coefficients uploaded to device
};

/* ----------------------------- Egm2008ModelCuda ----------------------------- */

/**
 * @brief CUDA-accelerated EGM2008 spherical harmonics gravity model.
 *
 * Optimized for batch evaluation of multiple positions. Single-position
 * evaluation is supported but has transfer overhead.
 *
 * Usage:
 * @code
 * Egm2008ModelCuda model;
 * model.init(coeffSource, params, batchSize);
 *
 * // Batch evaluation (fastest)
 * std::vector<double> positions(batchSize * 3);  // [x0,y0,z0, x1,y1,z1, ...]
 * std::vector<double> V(batchSize);
 * std::vector<double> accel(batchSize * 3);
 * model.evaluateBatchECEF(positions.data(), batchSize, V.data(), accel.data());
 * @endcode
 *
 * @note RT-safety: init() allocates; evaluateBatchECEF() is RT-safe after init.
 */
class Egm2008ModelCuda {
public:
  Egm2008ModelCuda() noexcept = default;
  ~Egm2008ModelCuda() noexcept;

  // Non-copyable (owns device resources)
  Egm2008ModelCuda(const Egm2008ModelCuda&) = delete;
  Egm2008ModelCuda& operator=(const Egm2008ModelCuda&) = delete;

  // Movable
  Egm2008ModelCuda(Egm2008ModelCuda&& other) noexcept;
  Egm2008ModelCuda& operator=(Egm2008ModelCuda&& other) noexcept;

  /**
   * @brief Initialize model with coefficients and allocate device buffers.
   * @param src Coefficient source (data is copied to device).
   * @param p Model parameters (GM, a, N).
   * @param maxBatch Maximum batch size for evaluateBatchECEF().
   * @param pinnedHost Use pinned host memory for faster transfers.
   * @param stream CUDA stream (nullptr for default stream).
   * @return true on success.
   * @note NOT RT-safe: Allocates device and optionally host memory.
   */
  bool init(const CoeffSource& src, const Egm2008Params& p, int maxBatch = 100,
            bool pinnedHost = true, void* stream = nullptr) noexcept;

  /**
   * @brief Release all device and host resources.
   * @note NOT RT-safe: Deallocates memory.
   */
  void destroy() noexcept;

  /**
   * @brief Check if model is initialized and ready for evaluation.
   */
  bool isReady() const noexcept { return ws_.coeffReady; }

  /**
   * @brief Maximum batch size supported after init().
   */
  int maxBatchSize() const noexcept { return ws_.batch; }

  /**
   * @brief Maximum degree used by this model.
   */
  int16_t maxDegree() const noexcept { return N_; }

  /**
   * @brief Evaluate potential and acceleration for a batch of ECEF positions.
   *
   * @param posEcef Input positions [x0,y0,z0, x1,y1,z1, ...], length = count * 3.
   * @param count Number of positions (must be <= maxBatchSize()).
   * @param V Output potentials, length = count. May be nullptr if not needed.
   * @param accel Output accelerations [ax0,ay0,az0, ...], length = count * 3.
   *              May be nullptr if not needed.
   * @return true on success.
   * @note RT-safe after init(): No allocation in hot path.
   */
  bool evaluateBatchECEF(const double* posEcef, int count, double* V, double* accel) noexcept;

  /**
   * @brief Evaluate potential only for a batch of ECEF positions.
   *
   * Faster than evaluateBatchECEF when acceleration is not needed.
   *
   * @param posEcef Input positions, length = count * 3.
   * @param count Number of positions.
   * @param V Output potentials, length = count.
   * @return true on success.
   * @note RT-safe after init().
   */
  bool potentialBatchECEF(const double* posEcef, int count, double* V) noexcept;

  /**
   * @brief Convenience: evaluate single position (has transfer overhead).
   * @note RT-safe after init(), but slower than batch for multiple calls.
   */
  bool evaluateECEF(const double r[3], double& V, double a[3]) noexcept;

  /**
   * @brief Convenience: potential only for single position.
   * @note RT-safe after init().
   */
  bool potentialECEF(const double r[3], double& V) noexcept;

private:
  bool uploadCoefficients(const CoeffSource& src) noexcept;
  bool allocateWorkspace(int batch, bool pinnedHost, void* stream) noexcept;

private:
  double GM_{0.0};
  double a_{1.0};
  int16_t N_{0};

  Egm2008CudaWorkspace ws_{};
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_EARTH_EGM2008_MODEL_CUDA_CUH
