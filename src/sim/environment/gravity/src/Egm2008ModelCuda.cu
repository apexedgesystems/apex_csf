/**
 * @file Egm2008ModelCuda.cu
 * @brief CUDA implementation of EGM2008 spherical harmonic gravity model.
 */

// Standard library headers first (before any CUDA headers)
#include <cmath>
#include <cstdio>
#include <cstring>

#include "src/sim/environment/gravity/inc/earth/Egm2008ModelCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"
#include "src/utilities/math/legendre/inc/PbarWorkspace.hpp"

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace sim {
namespace environment {
namespace gravity {

using apex::math::legendre::pbarTriangleSizeCuda;
using apex::math::legendre::PbarWorkspace;

/* ----------------------------- Host Helpers ----------------------------- */

/// Round up to the next power of 2 (for CUDA reductions).
static inline int nextPowerOf2(int n) noexcept {
  if (n <= 1)
    return 1;
  int p = 1;
  while (p < n)
    p <<= 1;
  return p;
}

/* ----------------------------- Device Helpers ----------------------------- */

#if COMPAT_CUDA_AVAILABLE

__device__ __forceinline__ std::size_t triIdx(int n, int m) {
  return static_cast<std::size_t>(n) * static_cast<std::size_t>(n + 1) / 2 +
         static_cast<std::size_t>(m);
}

/**
 * @brief Warp-level reduction using shuffle instructions.
 *
 * Reduces a value across a warp (32 threads) without synchronization.
 * All threads in the warp must call this function.
 *
 * @param val The value to reduce (each thread's contribution)
 * @return The sum of all values in the warp (valid in thread 0)
 */
__device__ __forceinline__ double warpReduceSum(double val) {
  // Full warp mask
  constexpr unsigned FULL_MASK = 0xffffffff;

  // Shuffle down and add
  val += __shfl_down_sync(FULL_MASK, val, 16);
  val += __shfl_down_sync(FULL_MASK, val, 8);
  val += __shfl_down_sync(FULL_MASK, val, 4);
  val += __shfl_down_sync(FULL_MASK, val, 2);
  val += __shfl_down_sync(FULL_MASK, val, 1);

  return val;
}

/* ----------------------------- Coordinate Conversion Kernel ----------------------------- */

/**
 * @brief Convert ECEF positions to geocentric coordinates.
 *
 * One thread per position. Computes:
 *  - rmag = |r|
 *  - sinPhi = z/rmag
 *  - cosPhi = sqrt(x^2+y^2)/rmag
 *  - cosLam = x/rxy, sinLam = y/rxy
 */
__global__ void coordConvertKernel(const double* __restrict__ posEcef, int count,
                                   double* __restrict__ rmag, double* __restrict__ sinPhi,
                                   double* __restrict__ cosPhi, double* __restrict__ cosLam,
                                   double* __restrict__ sinLam) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= count)
    return;

  const double X = posEcef[IDX * 3 + 0];
  const double Y = posEcef[IDX * 3 + 1];
  const double Z = posEcef[IDX * 3 + 2];

  const double R2 = X * X + Y * Y + Z * Z;
  const double R = sqrt(R2);
  const double RXY = sqrt(X * X + Y * Y);

  rmag[IDX] = R;

  if (R > 0.0) {
    sinPhi[IDX] = Z / R;
    cosPhi[IDX] = (RXY > 0.0) ? (RXY / R) : 1e-12;
  } else {
    sinPhi[IDX] = 0.0;
    cosPhi[IDX] = 1.0;
  }

  if (RXY > 0.0) {
    cosLam[IDX] = X / RXY;
    sinLam[IDX] = Y / RXY;
  } else {
    cosLam[IDX] = 1.0;
    sinLam[IDX] = 0.0;
  }
}

/* ----------------------------- Trig Recurrence Kernel ----------------------------- */

/**
 * @brief Compute cos(m*lambda), sin(m*lambda) via recurrence.
 *
 * One thread per position. Fills cosml[batch * (n+1)] and sinml[batch * (n+1)].
 */
__global__ void trigRecurrenceKernel(const double* __restrict__ cosLam,
                                     const double* __restrict__ sinLam, int count, int n,
                                     double* __restrict__ cosml, double* __restrict__ sinml) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= count)
    return;

  const double CL = cosLam[IDX];
  const double SL = sinLam[IDX];
  const int STRIDE = n + 1;
  double* cm = cosml + IDX * STRIDE;
  double* sm = sinml + IDX * STRIDE;

  cm[0] = 1.0;
  sm[0] = 0.0;

  if (n >= 1) {
    cm[1] = CL;
    sm[1] = SL;
  }

  for (int m = 2; m <= n; ++m) {
    cm[m] = cm[m - 1] * CL - sm[m - 1] * SL;
    sm[m] = sm[m - 1] * CL + cm[m - 1] * SL;
  }
}

/* ----------------------------- Power Array Kernel ----------------------------- */

/**
 * @brief Compute (a/r)^n for n = 0..N.
 *
 * One thread per position.
 */
__global__ void powerArrayKernel(const double* __restrict__ rmag, double a, int count, int n,
                                 double* __restrict__ uPow) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= count)
    return;

  const double R = rmag[IDX];
  const double U = (R > 0.0) ? (a / R) : 0.0;
  const int STRIDE = n + 1;
  double* up = uPow + IDX * STRIDE;

  double power = 1.0;
  for (int i = 0; i <= n; ++i) {
    up[i] = power;
    power *= U;
  }
}

/* ----------------------------- Fused Prep Kernel ----------------------------- */

/**
 * @brief Fused coordinate conversion, trig recurrence, and power array kernel.
 *
 * Combines coordConvertKernel, trigRecurrenceKernel, and powerArrayKernel into
 * a single kernel launch. This eliminates 2 kernel launch overheads and removes
 * the need for intermediate cosLam/sinLam device buffers.
 *
 * One thread per position.
 */
__global__ void fusedPrepKernel(const double* __restrict__ posEcef, int count, int n, double a,
                                double* __restrict__ rmag, double* __restrict__ sinPhi,
                                double* __restrict__ cosPhi, double* __restrict__ cosml,
                                double* __restrict__ sinml, double* __restrict__ uPow) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= count)
    return;

  // --- Coordinate conversion (from coordConvertKernel) ---
  const double X = posEcef[IDX * 3 + 0];
  const double Y = posEcef[IDX * 3 + 1];
  const double Z = posEcef[IDX * 3 + 2];

  const double R2 = X * X + Y * Y + Z * Z;
  const double R = sqrt(R2);
  const double RXY = sqrt(X * X + Y * Y);

  rmag[IDX] = R;

  double sp, cp, cl, sl;
  if (R > 0.0) {
    sp = Z / R;
    cp = (RXY > 0.0) ? (RXY / R) : 1e-12;
  } else {
    sp = 0.0;
    cp = 1.0;
  }
  sinPhi[IDX] = sp;
  cosPhi[IDX] = cp;

  if (RXY > 0.0) {
    cl = X / RXY;
    sl = Y / RXY;
  } else {
    cl = 1.0;
    sl = 0.0;
  }

  // --- Trig recurrence (from trigRecurrenceKernel) ---
  const int STRIDE = n + 1;
  double* cm = cosml + IDX * STRIDE;
  double* sm = sinml + IDX * STRIDE;

  cm[0] = 1.0;
  sm[0] = 0.0;

  if (n >= 1) {
    cm[1] = cl;
    sm[1] = sl;
  }

  for (int m = 2; m <= n; ++m) {
    cm[m] = cm[m - 1] * cl - sm[m - 1] * sl;
    sm[m] = sm[m - 1] * cl + cm[m - 1] * sl;
  }

  // --- Power array (from powerArrayKernel) ---
  const double U = (R > 0.0) ? (a / R) : 0.0;
  double* up = uPow + IDX * STRIDE;

  double power = 1.0;
  for (int i = 0; i <= n; ++i) {
    up[i] = power;
    power *= U;
  }
}

/* ----------------------------- Potential Summation Kernel ----------------------------- */

/**
 * @brief Spherical harmonic summation for potential.
 *
 * One block per position, threads cooperate on degrees.
 * V = (GM/r) * Sum_n (a/r)^n * Sum_m P_nm * [C_nm * cos(m*lam) + S_nm * sin(m*lam)]
 */
__global__ void
potentialSummationKernel(int N, int count, std::size_t triSize, const double* __restrict__ P,
                         const double* __restrict__ coeffC, const double* __restrict__ coeffS,
                         const double* __restrict__ cosml, const double* __restrict__ sinml,
                         const double* __restrict__ uPow, const double* __restrict__ rmag,
                         double GM, double* __restrict__ V) {
  extern __shared__ double sdata[];

  const int BATCH_IDX = blockIdx.x;
  if (BATCH_IDX >= count)
    return;

  const int TID = threadIdx.x;
  const int NTHREADS = blockDim.x;

  // Pointers for this batch element
  const double* pP = P + BATCH_IDX * triSize;
  const double* pCosml = cosml + BATCH_IDX * (N + 1);
  const double* pSinml = sinml + BATCH_IDX * (N + 1);
  const double* pUPow = uPow + BATCH_IDX * (N + 1);
  const double R = rmag[BATCH_IDX];

  // Each thread sums a subset of degrees
  double localSum = 0.0;

  for (int n = TID; n <= N; n += NTHREADS) {
    double sn = 0.0;
    const std::size_t ROW_BASE = triIdx(n, 0);

    for (int m = 0; m <= n; ++m) {
      const std::size_t K = ROW_BASE + m;
      const double F = coeffC[K] * pCosml[m] + coeffS[K] * pSinml[m];
      sn += pP[K] * F;
    }

    localSum += pUPow[n] * sn;
  }

  // Reduce within block using hybrid approach:
  // 1. Tree reduction in shared memory down to 32 threads
  // 2. Warp-level reduction using shuffle for final 32 threads
  sdata[TID] = localSum;
  __syncthreads();

  // Tree reduction down to warp size (32)
  for (int s = NTHREADS / 2; s > 32; s >>= 1) {
    if (TID < s) {
      sdata[TID] += sdata[TID + s];
    }
    __syncthreads();
  }

  // Final warp reduction using shuffle (no sync needed within warp)
  if (TID < 32) {
    // Load from shared memory (handles case where NTHREADS >= 64)
    double val = sdata[TID];
    if (NTHREADS >= 64)
      val += sdata[TID + 32];

    // Warp-level reduction
    val = warpReduceSum(val);

    if (TID == 0) {
      V[BATCH_IDX] = (R > 0.0) ? (GM / R) * val : 0.0;
    }
  }
}

/* ----------------------------- Acceleration Summation Kernel ----------------------------- */

/**
 * @brief Spherical harmonic summation for potential and analytic acceleration.
 *
 * One block per position, threads cooperate on degrees.
 */
__global__ void
accelSummationKernel(int N, int count, std::size_t triSize, const double* __restrict__ P,
                     const double* __restrict__ dP, const double* __restrict__ coeffC,
                     const double* __restrict__ coeffS, const double* __restrict__ cosml,
                     const double* __restrict__ sinml, const double* __restrict__ uPow,
                     const double* __restrict__ posEcef, const double* __restrict__ rmag,
                     const double* __restrict__ cosPhi, double GM, double a, double* __restrict__ V,
                     double* __restrict__ accel) {
  extern __shared__ double sdata[];

  const int BATCH_IDX = blockIdx.x;
  if (BATCH_IDX >= count)
    return;

  const int TID = threadIdx.x;
  const int NTHREADS = blockDim.x;

  // Shared memory layout: [sumN, sumR, sumPhi, sumLam] * NTHREADS
  double* sSumN = sdata;
  double* sSumR = sdata + NTHREADS;
  double* sSumPhi = sdata + 2 * NTHREADS;
  double* sSumLam = sdata + 3 * NTHREADS;

  // Pointers for this batch element
  const double* pP = P + BATCH_IDX * triSize;
  const double* pdP = dP + BATCH_IDX * triSize;
  const double* pCosml = cosml + BATCH_IDX * (N + 1);
  const double* pSinml = sinml + BATCH_IDX * (N + 1);
  const double* pUPow = uPow + BATCH_IDX * (N + 1);

  const double X = posEcef[BATCH_IDX * 3 + 0];
  const double Y = posEcef[BATCH_IDX * 3 + 1];
  const double Z = posEcef[BATCH_IDX * 3 + 2];
  const double R = rmag[BATCH_IDX];
  const double RXY = sqrt(X * X + Y * Y);

  // Local accumulators
  double sumN = 0.0, sumR = 0.0, sumPhi = 0.0, sumLam = 0.0;

  for (int n = TID; n <= N; n += NTHREADS) {
    double sN = 0.0, tPhi = 0.0, tLam = 0.0;
    const std::size_t ROW_BASE = triIdx(n, 0);

    // m=0 case
    {
      const std::size_t K0 = ROW_BASE;
      const double F0 = coeffC[K0]; // cos(0)=1, sin(0)=0
      sN += pP[K0] * F0;
      tPhi += pdP[K0] * F0;
    }

    // m=1..n
    for (int m = 1; m <= n; ++m) {
      const std::size_t K = ROW_BASE + m;
      const double C = coeffC[K];
      const double S = coeffS[K];
      const double CM = pCosml[m];
      const double SM = pSinml[m];

      const double F = C * CM + S * SM;
      const double G = S * CM - C * SM;

      sN += pP[K] * F;
      tPhi += pdP[K] * F;
      tLam += static_cast<double>(m) * pP[K] * G;
    }

    const double UP = pUPow[n];
    sumN += UP * sN;
    sumR += static_cast<double>(n + 1) * UP * sN;
    sumPhi += UP * tPhi;
    sumLam += UP * tLam;
  }

  // Store local sums to shared memory
  sSumN[TID] = sumN;
  sSumR[TID] = sumR;
  sSumPhi[TID] = sumPhi;
  sSumLam[TID] = sumLam;
  __syncthreads();

  // Tree reduction down to warp size (32)
  for (int s = NTHREADS / 2; s > 32; s >>= 1) {
    if (TID < s) {
      sSumN[TID] += sSumN[TID + s];
      sSumR[TID] += sSumR[TID + s];
      sSumPhi[TID] += sSumPhi[TID + s];
      sSumLam[TID] += sSumLam[TID + s];
    }
    __syncthreads();
  }

  // Final warp reduction using shuffle (no sync needed within warp)
  double finalN = 0.0, finalR = 0.0, finalPhi = 0.0, finalLam = 0.0;
  if (TID < 32) {
    // Load from shared memory (handles case where NTHREADS >= 64)
    finalN = sSumN[TID];
    finalR = sSumR[TID];
    finalPhi = sSumPhi[TID];
    finalLam = sSumLam[TID];

    if (NTHREADS >= 64) {
      finalN += sSumN[TID + 32];
      finalR += sSumR[TID + 32];
      finalPhi += sSumPhi[TID + 32];
      finalLam += sSumLam[TID + 32];
    }

    // Warp-level reduction for each sum
    finalN = warpReduceSum(finalN);
    finalR = warpReduceSum(finalR);
    finalPhi = warpReduceSum(finalPhi);
    finalLam = warpReduceSum(finalLam);
  }

  if (TID == 0) {
    if (R <= 0.0) {
      if (V)
        V[BATCH_IDX] = 0.0;
      accel[BATCH_IDX * 3 + 0] = 0.0;
      accel[BATCH_IDX * 3 + 1] = 0.0;
      accel[BATCH_IDX * 3 + 2] = 0.0;
      return;
    }

    // Potential (using warp-reduced values)
    const double VV = (GM / R) * finalN;
    if (V)
      V[BATCH_IDX] = VV;

    // Spherical partials (using warp-reduced values)
    const double DVR = -(GM / (R * R)) * finalR;
    const double DVPHI = (GM / R) * finalPhi;
    const double DVLAM = (GM / R) * finalLam;

    // Convert to Cartesian
    double dVdx = DVR * (X / R);
    double dVdy = DVR * (Y / R);
    double dVdz = DVR * (Z / R);

    constexpr double TINY = 1e-12;
    if (RXY >= TINY) {
      const double INV_R2 = 1.0 / (R * R);
      const double INV_RXY = 1.0 / RXY;
      const double INV_RXY2 = INV_RXY * INV_RXY;

      // phi terms
      dVdx += DVPHI * (-(Z * X) * INV_R2 * INV_RXY);
      dVdy += DVPHI * (-(Z * Y) * INV_R2 * INV_RXY);
      dVdz += DVPHI * (RXY * INV_R2);

      // lambda terms
      dVdx += DVLAM * (-Y * INV_RXY2);
      dVdy += DVLAM * (X * INV_RXY2);
    }

    accel[BATCH_IDX * 3 + 0] = dVdx;
    accel[BATCH_IDX * 3 + 1] = dVdy;
    accel[BATCH_IDX * 3 + 2] = dVdz;
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- Egm2008ModelCuda Implementation ----------------------------- */

Egm2008ModelCuda::~Egm2008ModelCuda() noexcept { destroy(); }

Egm2008ModelCuda::Egm2008ModelCuda(Egm2008ModelCuda&& other) noexcept
    : GM_(other.GM_), a_(other.a_), N_(other.N_), ws_(other.ws_) {
  other.ws_ = Egm2008CudaWorkspace{}; // Reset other's workspace
}

Egm2008ModelCuda& Egm2008ModelCuda::operator=(Egm2008ModelCuda&& other) noexcept {
  if (this != &other) {
    destroy();
    GM_ = other.GM_;
    a_ = other.a_;
    N_ = other.N_;
    ws_ = other.ws_;
    other.ws_ = Egm2008CudaWorkspace{};
  }
  return *this;
}

bool Egm2008ModelCuda::init(const CoeffSource& src, const Egm2008Params& p, int maxBatch,
                            bool pinnedHost, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)src;
  (void)p;
  (void)maxBatch;
  (void)pinnedHost;
  (void)stream;
  return false;
#else
  if (p.GM <= 0.0 || p.a <= 0.0 || p.N < 0 || maxBatch <= 0)
    return false;

  const int16_t N_MAX = src.maxDegree();
  if (N_MAX < 0)
    return false;

  N_ = std::min<int16_t>(p.N, N_MAX);
  GM_ = p.GM;
  a_ = p.a;

  if (!allocateWorkspace(maxBatch, pinnedHost, stream))
    return false;

  if (!uploadCoefficients(src))
    return false;

  // Setup Legendre workspace with derivatives
  using namespace apex::math::legendre;
  if (!createPbarWorkspaceWithDerivatives(ws_.legendreWs, N_, maxBatch, pinnedHost, stream))
    return false;
  if (!ensurePbarCoefficients(ws_.legendreWs))
    return false;
  if (!ensureBetaCoefficients(ws_.legendreWs))
    return false;

  ws_.coeffReady = true;
  return true;
#endif
}

void Egm2008ModelCuda::destroy() noexcept {
#if COMPAT_CUDA_AVAILABLE
  // Free device buffers
  if (ws_.dCoeffC)
    cudaFree(ws_.dCoeffC);
  if (ws_.dCoeffS)
    cudaFree(ws_.dCoeffS);
  if (ws_.dCosml)
    cudaFree(ws_.dCosml);
  if (ws_.dSinml)
    cudaFree(ws_.dSinml);
  if (ws_.dPosEcef)
    cudaFree(ws_.dPosEcef);
  if (ws_.dRmag)
    cudaFree(ws_.dRmag);
  if (ws_.dSinPhi)
    cudaFree(ws_.dSinPhi);
  if (ws_.dCosPhi)
    cudaFree(ws_.dCosPhi);
  if (ws_.dCosLam)
    cudaFree(ws_.dCosLam);
  if (ws_.dSinLam)
    cudaFree(ws_.dSinLam);
  if (ws_.dUPow)
    cudaFree(ws_.dUPow);
  if (ws_.dV)
    cudaFree(ws_.dV);
  if (ws_.dAccel)
    cudaFree(ws_.dAccel);

  // Free pinned host buffers
  if (ws_.hPosEcef)
    cudaFreeHost(ws_.hPosEcef);
  if (ws_.hV)
    cudaFreeHost(ws_.hV);
  if (ws_.hAccel)
    cudaFreeHost(ws_.hAccel);

  // Destroy Legendre workspace
  apex::math::legendre::destroyPbarWorkspace(ws_.legendreWs);
#endif

  ws_ = Egm2008CudaWorkspace{};
  GM_ = 0.0;
  a_ = 1.0;
  N_ = 0;
}

bool Egm2008ModelCuda::allocateWorkspace(int batch, bool pinnedHost, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)batch;
  (void)pinnedHost;
  (void)stream;
  return false;
#else
  ws_.n = N_;
  ws_.batch = batch;
  ws_.pinnedHost = pinnedHost;
  ws_.stream = stream;
  ws_.triSize = pbarTriangleSizeCuda(N_);

  const std::size_t TRIG_SIZE = static_cast<std::size_t>(batch) * (N_ + 1);
  const std::size_t POS_SIZE = static_cast<std::size_t>(batch) * 3;

  // Allocate device buffers
  bool ok = true;

  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dCosml, TRIG_SIZE * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dSinml, TRIG_SIZE * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dPosEcef, POS_SIZE * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dRmag, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dSinPhi, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dCosPhi, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dCosLam, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dSinLam, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dUPow, TRIG_SIZE * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dV, batch * sizeof(double)));
  ok = ok && apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dAccel, POS_SIZE * sizeof(double)));

  if (!ok) {
    destroy();
    return false;
  }

  // Allocate pinned host buffers if requested
  if (pinnedHost) {
    ok = ok &&
         apex::compat::cuda::isSuccess(cudaMallocHost(&ws_.hPosEcef, POS_SIZE * sizeof(double)));
    ok = ok && apex::compat::cuda::isSuccess(cudaMallocHost(&ws_.hV, batch * sizeof(double)));
    ok =
        ok && apex::compat::cuda::isSuccess(cudaMallocHost(&ws_.hAccel, POS_SIZE * sizeof(double)));

    if (!ok) {
      destroy();
      return false;
    }
  }

  return true;
#endif
}

bool Egm2008ModelCuda::uploadCoefficients(const CoeffSource& src) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)src;
  return false;
#else
  const std::size_t TRI = ws_.triSize;

  // Allocate device coefficient arrays
  if (!apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dCoeffC, TRI * sizeof(double))))
    return false;
  if (!apex::compat::cuda::isSuccess(cudaMalloc(&ws_.dCoeffS, TRI * sizeof(double)))) {
    cudaFree(ws_.dCoeffC);
    ws_.dCoeffC = nullptr;
    return false;
  }

  // Build host coefficient arrays and upload
  std::vector<double> hostC(TRI), hostS(TRI);
  const int N0 = static_cast<int>(src.minDegree());

  for (int n = 0; n <= N_; ++n) {
    for (int m = 0; m <= n; ++m) {
      double c = 0.0, s = 0.0;

      if (n < N0) {
        // Standard values for degrees below source minimum
        if (n == 0 && m == 0)
          c = 1.0;
      } else {
        if (!src.get(static_cast<int16_t>(n), static_cast<int16_t>(m), c, s))
          return false;
      }

      const std::size_t K = static_cast<std::size_t>(n) * (n + 1) / 2 + m;
      hostC[K] = c;
      hostS[K] = s;
    }
  }

  auto s = static_cast<cudaStream_t>(ws_.stream);
  if (!apex::compat::cuda::isSuccess(cudaMemcpyAsync(
          ws_.dCoeffC, hostC.data(), TRI * sizeof(double), cudaMemcpyHostToDevice, s)))
    return false;
  if (!apex::compat::cuda::isSuccess(cudaMemcpyAsync(
          ws_.dCoeffS, hostS.data(), TRI * sizeof(double), cudaMemcpyHostToDevice, s)))
    return false;

  // Synchronize to ensure upload is complete
  return apex::compat::cuda::isSuccess(cudaStreamSynchronize(s));
#endif
}

bool Egm2008ModelCuda::evaluateBatchECEF(const double* posEcef, int count, double* V,
                                         double* accel) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)posEcef;
  (void)count;
  (void)V;
  (void)accel;
  return false;
#else
  if (!ws_.coeffReady || count <= 0 || count > ws_.batch)
    return false;
  if (!posEcef || !accel)
    return false;

  auto s = static_cast<cudaStream_t>(ws_.stream);
  const int N = N_;

  // 1. Upload positions to device
  const std::size_t POS_BYTES = count * 3 * sizeof(double);
  if (ws_.pinnedHost) {
    std::memcpy(ws_.hPosEcef, posEcef, POS_BYTES);
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.dPosEcef, ws_.hPosEcef, POS_BYTES, cudaMemcpyHostToDevice, s)))
      return false;
  } else {
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.dPosEcef, posEcef, POS_BYTES, cudaMemcpyHostToDevice, s)))
      return false;
  }

  // 2. Fused prep kernel: coord conversion + trig recurrence + power array
  // (Reduces 3 kernel launches to 1)
  const int THREADS = 256;
  const int BLOCKS = (count + THREADS - 1) / THREADS;
  fusedPrepKernel<<<BLOCKS, THREADS, 0, s>>>(ws_.dPosEcef, count, N, a_, ws_.dRmag, ws_.dSinPhi,
                                             ws_.dCosPhi, ws_.dCosml, ws_.dSinml, ws_.dUPow);

  // 5. Compute Legendre P and dP using workspace
  // Copy sin(phi) to Legendre workspace
  if (!apex::compat::cuda::isSuccess(cudaMemcpyAsync(
          ws_.legendreWs.dXs, ws_.dSinPhi, count * sizeof(double), cudaMemcpyDeviceToDevice, s)))
    return false;
  if (!apex::compat::cuda::isSuccess(cudaMemcpyAsync(ws_.legendreWs.dCosPhis, ws_.dCosPhi,
                                                     count * sizeof(double),
                                                     cudaMemcpyDeviceToDevice, s)))
    return false;

  // Run Legendre kernel with derivatives
  if (!apex::math::legendre::computeNormalizedPbarTriangleWithDerivativesBatchCuda(
          N, ws_.legendreWs.dXs, ws_.legendreWs.dCosPhis, count, ws_.legendreWs.dOut,
          ws_.legendreWs.dDpOut, ws_.legendreWs.outLen, s, ws_.legendreWs.dA, ws_.legendreWs.dB,
          ws_.legendreWs.dBeta))
    return false;

  // 6. Spherical harmonic summation with acceleration
  // Use power-of-2 thread count for tree reduction correctness
  const int SUM_THREADS = std::min(nextPowerOf2(N + 1), 256);
  const std::size_t SHARED_BYTES = 4 * SUM_THREADS * sizeof(double);
  accelSummationKernel<<<count, SUM_THREADS, SHARED_BYTES, s>>>(
      N, count, ws_.triSize, ws_.legendreWs.dOut, ws_.legendreWs.dDpOut, ws_.dCoeffC, ws_.dCoeffS,
      ws_.dCosml, ws_.dSinml, ws_.dUPow, ws_.dPosEcef, ws_.dRmag, ws_.dCosPhi, GM_, a_, ws_.dV,
      ws_.dAccel);

  // 7. Download results
  const std::size_t V_BYTES = count * sizeof(double);
  const std::size_t ACCEL_BYTES = count * 3 * sizeof(double);

  if (ws_.pinnedHost) {
    if (V) {
      if (!apex::compat::cuda::isSuccess(
              cudaMemcpyAsync(ws_.hV, ws_.dV, V_BYTES, cudaMemcpyDeviceToHost, s)))
        return false;
    }
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.hAccel, ws_.dAccel, ACCEL_BYTES, cudaMemcpyDeviceToHost, s)))
      return false;

    if (!apex::compat::cuda::isSuccess(cudaStreamSynchronize(s)))
      return false;

    if (V)
      std::memcpy(V, ws_.hV, V_BYTES);
    std::memcpy(accel, ws_.hAccel, ACCEL_BYTES);
  } else {
    if (V) {
      if (!apex::compat::cuda::isSuccess(cudaMemcpy(V, ws_.dV, V_BYTES, cudaMemcpyDeviceToHost)))
        return false;
    }
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpy(accel, ws_.dAccel, ACCEL_BYTES, cudaMemcpyDeviceToHost)))
      return false;
  }

  return true;
#endif
}

bool Egm2008ModelCuda::potentialBatchECEF(const double* posEcef, int count, double* V) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)posEcef;
  (void)count;
  (void)V;
  return false;
#else
  if (!ws_.coeffReady || count <= 0 || count > ws_.batch)
    return false;
  if (!posEcef || !V)
    return false;

  auto s = static_cast<cudaStream_t>(ws_.stream);
  const int N = N_;

  // 1. Upload positions
  const std::size_t POS_BYTES = count * 3 * sizeof(double);
  if (ws_.pinnedHost) {
    std::memcpy(ws_.hPosEcef, posEcef, POS_BYTES);
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.dPosEcef, ws_.hPosEcef, POS_BYTES, cudaMemcpyHostToDevice, s)))
      return false;
  } else {
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.dPosEcef, posEcef, POS_BYTES, cudaMemcpyHostToDevice, s)))
      return false;
  }

  // 2. Fused prep kernel: coord conversion + trig recurrence + power array
  // (Reduces 3 kernel launches to 1)
  const int THREADS = 256;
  const int BLOCKS = (count + THREADS - 1) / THREADS;
  fusedPrepKernel<<<BLOCKS, THREADS, 0, s>>>(ws_.dPosEcef, count, N, a_, ws_.dRmag, ws_.dSinPhi,
                                             ws_.dCosPhi, ws_.dCosml, ws_.dSinml, ws_.dUPow);

  // 5. Legendre P only (no derivatives needed for potential-only)
  if (!apex::compat::cuda::isSuccess(cudaMemcpyAsync(
          ws_.legendreWs.dXs, ws_.dSinPhi, count * sizeof(double), cudaMemcpyDeviceToDevice, s)))
    return false;

  if (!apex::math::legendre::computeNormalizedPbarTriangleBatchCuda(
          N, ws_.legendreWs.dXs, count, ws_.legendreWs.dOut, ws_.legendreWs.outLen, s,
          ws_.legendreWs.dA, ws_.legendreWs.dB))
    return false;

  // 6. Potential summation only
  // Use power-of-2 thread count for tree reduction correctness
  const int SUM_THREADS = std::min(nextPowerOf2(N + 1), 256);
  const std::size_t SHARED_BYTES = SUM_THREADS * sizeof(double);
  potentialSummationKernel<<<count, SUM_THREADS, SHARED_BYTES, s>>>(
      N, count, ws_.triSize, ws_.legendreWs.dOut, ws_.dCoeffC, ws_.dCoeffS, ws_.dCosml, ws_.dSinml,
      ws_.dUPow, ws_.dRmag, GM_, ws_.dV);

  // 7. Download results
  const std::size_t V_BYTES = count * sizeof(double);
  if (ws_.pinnedHost) {
    if (!apex::compat::cuda::isSuccess(
            cudaMemcpyAsync(ws_.hV, ws_.dV, V_BYTES, cudaMemcpyDeviceToHost, s)))
      return false;
    if (!apex::compat::cuda::isSuccess(cudaStreamSynchronize(s)))
      return false;
    std::memcpy(V, ws_.hV, V_BYTES);
  } else {
    if (!apex::compat::cuda::isSuccess(cudaMemcpy(V, ws_.dV, V_BYTES, cudaMemcpyDeviceToHost)))
      return false;
  }

  return true;
#endif
}

bool Egm2008ModelCuda::evaluateECEF(const double r[3], double& V, double a[3]) noexcept {
  double pos[3] = {r[0], r[1], r[2]};
  double accel[3] = {0.0, 0.0, 0.0};
  double potential = 0.0;

  if (!evaluateBatchECEF(pos, 1, &potential, accel))
    return false;

  V = potential;
  a[0] = accel[0];
  a[1] = accel[1];
  a[2] = accel[2];
  return true;
}

bool Egm2008ModelCuda::potentialECEF(const double r[3], double& V) noexcept {
  double pos[3] = {r[0], r[1], r[2]};
  double potential = 0.0;

  if (!potentialBatchECEF(pos, 1, &potential))
    return false;

  V = potential;
  return true;
}

} // namespace gravity
} // namespace environment
} // namespace sim
