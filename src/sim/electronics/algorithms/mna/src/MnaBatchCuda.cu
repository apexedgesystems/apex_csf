/**
 * @file MnaBatchCuda.cu
 * @brief Custom CUDA kernels for batch MNA solving.
 *
 * Optimized for solving many small linear systems in parallel,
 * ideal for Monte Carlo simulations and parameter sweeps.
 *
 * Strategy:
 *  - Each thread handles one complete system (for small N)
 *  - Gaussian elimination with partial pivoting
 *  - Matrices stored in registers/local memory
 *  - Batch-parallel processing
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#if COMPAT_HAVE_CUSOLVER
#include <cuda_runtime.h>
#include <cmath>
#endif

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Constants ----------------------------- */

#if COMPAT_HAVE_CUSOLVER
constexpr int THREADS_PER_BLOCK = 256;
#endif

/* ----------------------------- Small Matrix Kernels ----------------------------- */

#if COMPAT_HAVE_CUSOLVER

/**
 * @brief Kernel: Batch solve for 8x8 systems.
 *
 * Each thread solves one complete 8x8 system using Gaussian elimination
 * with partial pivoting. Matrix stored in registers.
 *
 * @param As Input matrices (batch * 64 doubles, row-major).
 * @param bs Input/output RHS vectors (batch * 8 doubles).
 * @param batch Number of systems.
 * @param success Output success flags (batch bools).
 */
__global__ __launch_bounds__(64, 24) void solveBatch8x8Kernel(const double* SIM_RESTRICT As,
                                                              double* SIM_RESTRICT bs, int batch,
                                                              bool* SIM_RESTRICT success) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 8;

  // Load matrix and RHS into registers
  double A[N][N];
  double b[N];

  const double* Ain = As + IDX * N * N;
  double* bout = bs + IDX * N;

#pragma unroll
  for (int i = 0; i < N; ++i) {
#pragma unroll
    for (int j = 0; j < N; ++j) {
      A[i][j] = Ain[i * N + j];
    }
    b[i] = bout[i];
  }

// Gaussian elimination with partial pivoting
#pragma unroll
  for (int k = 0; k < N; ++k) {
    // Find pivot
    int maxRow = k;
    double maxVal = fabs(A[k][k]);
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      double val = fabs(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    // Check for singular
    if (maxVal < 1e-15) {
      success[IDX] = false;
      return;
    }

    // Swap rows if needed
    if (maxRow != k) {
#pragma unroll
      for (int j = k; j < N; ++j) {
        double tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      double tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    // Eliminate column k
    double pivot = A[k][k];
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      double factor = A[i][k] / pivot;
#pragma unroll
      for (int j = k + 1; j < N; ++j) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

// Back substitution
#pragma unroll
  for (int i = N - 1; i >= 0; --i) {
    double sum = b[i];
#pragma unroll
    for (int j = i + 1; j < N; ++j) {
      sum -= A[i][j] * b[j];
    }
    b[i] = sum / A[i][i];
  }

// Write results
#pragma unroll
  for (int i = 0; i < N; ++i) {
    bout[i] = b[i];
  }
  success[IDX] = true;
}

/**
 * @brief Kernel: Batch solve for 16x16 systems.
 */
__global__ __launch_bounds__(64, 24) void solveBatch16x16Kernel(const double* SIM_RESTRICT As,
                                                                double* SIM_RESTRICT bs, int batch,
                                                                bool* SIM_RESTRICT success) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 16;

  // Load matrix and RHS into local memory (registers may spill)
  double A[N][N];
  double b[N];

  const double* Ain = As + IDX * N * N;
  double* bout = bs + IDX * N;

  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      A[i][j] = Ain[i * N + j];
    }
    b[i] = bout[i];
  }

  // Gaussian elimination with partial pivoting
  for (int k = 0; k < N; ++k) {
    int maxRow = k;
    double maxVal = fabs(A[k][k]);
    for (int i = k + 1; i < N; ++i) {
      double val = fabs(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    if (maxVal < 1e-15) {
      success[IDX] = false;
      return;
    }

    if (maxRow != k) {
      for (int j = k; j < N; ++j) {
        double tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      double tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    double pivot = A[k][k];
    for (int i = k + 1; i < N; ++i) {
      double factor = A[i][k] / pivot;
      for (int j = k + 1; j < N; ++j) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

  // Back substitution
  for (int i = N - 1; i >= 0; --i) {
    double sum = b[i];
    for (int j = i + 1; j < N; ++j) {
      sum -= A[i][j] * b[j];
    }
    b[i] = sum / A[i][i];
  }

  for (int i = 0; i < N; ++i) {
    bout[i] = b[i];
  }
  success[IDX] = true;
}

/**
 * @brief Kernel: Batch solve for 32x32 systems.
 *
 * Shared-mem A + per-block parallelism across rows. 128 threads per
 * block (4 warps, 4 threads per row). `__launch_bounds__(128, 12)`
 * caps registers at 40 so the register block-limit matches the warp
 * block-limit; shared memory (A[N][N+1] = 8.4 KB) is the remaining
 * binding occupancy constraint.
 */
__global__ __launch_bounds__(128, 12) void solveBatch32x32Kernel(const double* SIM_RESTRICT As,
                                                                 double* SIM_RESTRICT bs, int batch,
                                                                 bool* SIM_RESTRICT success) {
  constexpr int N = 32;
  constexpr int THREADS = 128;                 // 4 warps for better occupancy
  constexpr int THREADS_PER_ROW = THREADS / N; // 4 threads per ROW

  // One block per system
  const int SYS_IDX = blockIdx.x;
  if (SYS_IDX >= batch)
    return;

  const int TID = threadIdx.x;
  const int ROW = TID / THREADS_PER_ROW;  // Which ROW this thread works on
  const int LANE = TID % THREADS_PER_ROW; // Position within ROW group

  // Shared memory for matrix and RHS
  __shared__ double A[N][N + 1]; // +1 to avoid bank conflicts
  __shared__ double b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (TID == 0)
    singular = false;
  __syncthreads();

  // Load matrix (threads cooperate, 4 threads per ROW)
  const double* Ain = As + SYS_IDX * N * N;
  double* bout = bs + SYS_IDX * N;

  if (ROW < N) {
    // Each group of 4 threads loads one ROW, strided
    for (int j = LANE; j < N; j += THREADS_PER_ROW) {
      A[ROW][j] = Ain[ROW * N + j];
    }
    if (LANE == 0) {
      b[ROW] = bout[ROW];
    }
  }
  __syncthreads();

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // Thread 0 finds pivot
    if (TID == 0) {
      int maxRow = k;
      double maxVal = fabs(A[k][k]);
      for (int i = k + 1; i < N; ++i) {
        double val = fabs(A[i][k]);
        if (val > maxVal) {
          maxVal = val;
          maxRow = i;
        }
      }
      if (maxVal < 1e-15) {
        singular = true;
      }
      pivotRow = maxRow;
    }
    __syncthreads();

    if (singular)
      break;

    // Swap rows (parallel across columns)
    int maxRow = pivotRow;
    if (maxRow != k) {
      for (int j = TID; j < N; j += THREADS) {
        double tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (TID == 0) {
        double tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    // Eliminate - each thread group handles one ROW below pivot
    // Parallelize column updates within each ROW
    if (ROW > k && ROW < N) {
      double factor = A[ROW][k] / A[k][k];
      // Each thread in the ROW group updates different columns
      for (int j = k + 1 + LANE; j < N; j += THREADS_PER_ROW) {
        A[ROW][j] -= factor * A[k][j];
      }
      if (LANE == 0) {
        b[ROW] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (TID == 0)
      success[SYS_IDX] = false;
    return;
  }

  // Parallel back substitution with unrolled reduction
  __shared__ double partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    // Each thread computes partial sum for columns it handles
    double mySum = 0.0;
#pragma unroll 4
    for (int j = i + 1 + TID; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[TID] = mySum;
    __syncthreads();

    // Unrolled reduction (128 threads)
    if (TID < 64)
      partialSums[TID] += partialSums[TID + 64];
    __syncthreads();
    if (TID < 32)
      partialSums[TID] += partialSums[TID + 32];
    __syncthreads();
    if (TID < 16)
      partialSums[TID] += partialSums[TID + 16];
    __syncthreads();
    if (TID < 8)
      partialSums[TID] += partialSums[TID + 8];
    __syncthreads();
    if (TID < 4)
      partialSums[TID] += partialSums[TID + 4];
    __syncthreads();
    if (TID < 2)
      partialSums[TID] += partialSums[TID + 2];
    __syncthreads();
    if (TID < 1)
      partialSums[TID] += partialSums[TID + 1];
    __syncthreads();

    // Thread 0 finalizes this ROW
    if (TID == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (TID == 0) {
    success[SYS_IDX] = true;
  }

  // Write results (parallel)
  if (ROW < N && LANE == 0) {
    bout[ROW] = b[ROW];
  }
}

/**
 * @brief Kernel: Batch solve for 64x64 systems.
 *
 * Uses shared memory with multiple threads per row for elimination.
 *
 * Optimized: Uses 256 threads (8 warps) with 4 threads per row for
 * parallelized column updates during elimination.
 */
// __launch_bounds__(256, 6) pins 6 blocks per SM = 48 warps = 100%
// theoretical occupancy (48 warp-slot max / 8 warps per block).
__global__ __launch_bounds__(256, 6) void solveBatch64x64Kernel(const double* SIM_RESTRICT As,
                                                                double* SIM_RESTRICT bs, int batch,
                                                                bool* SIM_RESTRICT success) {
  constexpr int N = 64;
  constexpr int THREADS = 256;
  constexpr int LANES_PER_ROW = THREADS / N;       // 4
  constexpr int COLS_PER_LANE = N / LANES_PER_ROW; // 16

  const int SYS_IDX = blockIdx.x;
  if (SYS_IDX >= batch)
    return;

  const int TID = threadIdx.x;
  const int ROW = TID / LANES_PER_ROW;
  const int LANE = TID % LANES_PER_ROW;

  // A lives in global; only the pivot ROW, b, and a few scalars are in
  // shared memory. Holding A[64][65] in shared (33.3 KB) caps blocks
  // per SM at 2 via the shared-mem block-limit. The global layout keeps
  // static shared at ~1 KB so occupancy is limited by the warp slot
  // count instead.
  __shared__ double pivotR[N + 1]; // +1 keeps bank-conflict avoidance
  __shared__ double bSh[N];        // 0.5 KB
  __shared__ int pivotIdx;
  __shared__ bool singular;

  double* A = const_cast<double*>(As) + SYS_IDX * N * N;
  double* bout = bs + SYS_IDX * N;

  // Load b into shared (one thread per element).
  if (TID < N) {
    bSh[TID] = bout[TID];
  }
  if (TID == 0)
    singular = false;
  __syncthreads();

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // Serial scan for the pivot in thread 0. Parallel warp-shuffle
    // variants add register pressure that outweighs the shorter scan.
    if (TID == 0) {
      int maxRow = k;
      double maxVal = fabs(A[k * N + k]);
      for (int i = k + 1; i < N; ++i) {
        double v = fabs(A[i * N + k]);
        if (v > maxVal) {
          maxVal = v;
          maxRow = i;
        }
      }
      if (maxVal < 1e-15) {
        singular = true;
      }
      pivotIdx = maxRow;
    }
    __syncthreads();

    if (singular)
      break;

    int maxRow = pivotIdx;

    // Swap rows in global if needed. 64 threads, one column each.
    if (maxRow != k && TID < N) {
      double t = A[k * N + TID];
      A[k * N + TID] = A[maxRow * N + TID];
      A[maxRow * N + TID] = t;
    }
    if (maxRow != k && TID == 0) {
      double t = bSh[k];
      bSh[k] = bSh[maxRow];
      bSh[maxRow] = t;
    }
    __syncthreads();

    // Load pivot ROW k into shared. 64 threads, one column each.
    if (TID < N) {
      pivotR[TID] = A[k * N + TID];
    }
    __syncthreads();

    // Eliminate rows below pivot. Each (ROW, LANE) handles 16 consecutive
    // columns of one ROW. Reads A[ROW][k] and A[ROW][cols] from global,
    // writes back A[ROW][cols].
    if (ROW > k && ROW < N) {
      double factor = A[ROW * N + k] / pivotR[k];
      const int C_BEG = LANE * COLS_PER_LANE;
      const int C_END = C_BEG + COLS_PER_LANE;
#pragma unroll
      for (int j = C_BEG; j < C_END; ++j) {
        if (j > k) {
          A[ROW * N + j] -= factor * pivotR[j];
        }
      }
      if (LANE == 0) {
        bSh[ROW] -= factor * bSh[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (TID == 0)
      success[SYS_IDX] = false;
    return;
  }

  // Back substitution. Reads A[i][>i] from global (one ROW per outer
  // step), uses small warp-reduce + cross-warp shared accumulator
  // (8 elements) instead of the old 256-element partialSums.
  __shared__ double warpSum[8];
  for (int i = N - 1; i >= 0; --i) {
    double mySum = 0.0;
    for (int j = i + 1 + TID; j < N; j += THREADS) {
      mySum += A[i * N + j] * bSh[j];
    }
    // Intra-warp reduction.
#pragma unroll
    for (int offset = 16; offset > 0; offset >>= 1) {
      mySum += __shfl_down_sync(0xffffffff, mySum, offset);
    }
    if ((TID & 31) == 0) {
      warpSum[TID >> 5] = mySum;
    }
    __syncthreads();
    if (TID < 32) {
      double v = (TID < 8) ? warpSum[TID] : 0.0;
#pragma unroll
      for (int offset = 4; offset > 0; offset >>= 1) {
        v += __shfl_down_sync(0xffffffff, v, offset);
      }
      if (TID == 0) {
        bSh[i] = (bSh[i] - v) / A[i * N + i];
      }
    }
    __syncthreads();
  }

  if (TID == 0) {
    success[SYS_IDX] = true;
  }

  // Write b back (64 threads, one element each).
  if (TID < N) {
    bout[TID] = bSh[TID];
  }
}

#endif // COMPAT_HAVE_CUSOLVER

/* ----------------------------- FP32 Kernels ----------------------------- */

#if COMPAT_HAVE_CUSOLVER

/**
 * @brief Kernel: Batch solve for 8x8 systems (FP32).
 *
 * Single-precision variant for higher throughput when precision allows.
 */
__global__ __launch_bounds__(64, 24) void solveBatch8x8KernelF32(const float* SIM_RESTRICT As,
                                                                 float* SIM_RESTRICT bs, int batch,
                                                                 bool* SIM_RESTRICT success) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 8;

  float A[N][N];
  float b[N];

  const float* Ain = As + IDX * N * N;
  float* bout = bs + IDX * N;

#pragma unroll
  for (int i = 0; i < N; ++i) {
#pragma unroll
    for (int j = 0; j < N; ++j) {
      A[i][j] = Ain[i * N + j];
    }
    b[i] = bout[i];
  }

#pragma unroll
  for (int k = 0; k < N; ++k) {
    int maxRow = k;
    float maxVal = fabsf(A[k][k]);
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      float val = fabsf(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    if (maxVal < 1e-6f) {
      success[IDX] = false;
      return;
    }

    if (maxRow != k) {
#pragma unroll
      for (int j = k; j < N; ++j) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      float tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    float pivot = A[k][k];
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      float factor = A[i][k] / pivot;
#pragma unroll
      for (int j = k + 1; j < N; ++j) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

#pragma unroll
  for (int i = N - 1; i >= 0; --i) {
    float sum = b[i];
#pragma unroll
    for (int j = i + 1; j < N; ++j) {
      sum -= A[i][j] * b[j];
    }
    b[i] = sum / A[i][i];
  }

#pragma unroll
  for (int i = 0; i < N; ++i) {
    bout[i] = b[i];
  }
  success[IDX] = true;
}

/**
 * @brief Kernel: Batch solve for 16x16 systems (FP32).
 */
__global__ __launch_bounds__(64, 24) void solveBatch16x16KernelF32(const float* SIM_RESTRICT As,
                                                                   float* SIM_RESTRICT bs,
                                                                   int batch,
                                                                   bool* SIM_RESTRICT success) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 16;

  float A[N][N];
  float b[N];

  const float* Ain = As + IDX * N * N;
  float* bout = bs + IDX * N;

  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      A[i][j] = Ain[i * N + j];
    }
    b[i] = bout[i];
  }

  for (int k = 0; k < N; ++k) {
    int maxRow = k;
    float maxVal = fabsf(A[k][k]);
    for (int i = k + 1; i < N; ++i) {
      float val = fabsf(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    if (maxVal < 1e-6f) {
      success[IDX] = false;
      return;
    }

    if (maxRow != k) {
      for (int j = k; j < N; ++j) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      float tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    float pivot = A[k][k];
    for (int i = k + 1; i < N; ++i) {
      float factor = A[i][k] / pivot;
      for (int j = k + 1; j < N; ++j) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

  for (int i = N - 1; i >= 0; --i) {
    float sum = b[i];
    for (int j = i + 1; j < N; ++j) {
      sum -= A[i][j] * b[j];
    }
    b[i] = sum / A[i][i];
  }

  for (int i = 0; i < N; ++i) {
    bout[i] = b[i];
  }
  success[IDX] = true;
}

/**
 * @brief Kernel: Batch solve for 32x32 systems (FP32).
 */
__global__ __launch_bounds__(128, 12) void solveBatch32x32KernelF32(const float* SIM_RESTRICT As,
                                                                    float* SIM_RESTRICT bs,
                                                                    int batch,
                                                                    bool* SIM_RESTRICT success) {
  constexpr int N = 32;
  constexpr int THREADS = 128;
  constexpr int THREADS_PER_ROW = THREADS / N;

  const int SYS_IDX = blockIdx.x;
  if (SYS_IDX >= batch)
    return;

  const int TID = threadIdx.x;
  const int ROW = TID / THREADS_PER_ROW;
  const int LANE = TID % THREADS_PER_ROW;

  __shared__ float A[N][N + 1];
  __shared__ float b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (TID == 0)
    singular = false;
  __syncthreads();

  const float* Ain = As + SYS_IDX * N * N;
  float* bout = bs + SYS_IDX * N;

  if (ROW < N) {
    for (int j = LANE; j < N; j += THREADS_PER_ROW) {
      A[ROW][j] = Ain[ROW * N + j];
    }
    if (LANE == 0) {
      b[ROW] = bout[ROW];
    }
  }
  __syncthreads();

  for (int k = 0; k < N && !singular; ++k) {
    if (TID == 0) {
      int maxRow = k;
      float maxVal = fabsf(A[k][k]);
      for (int i = k + 1; i < N; ++i) {
        float val = fabsf(A[i][k]);
        if (val > maxVal) {
          maxVal = val;
          maxRow = i;
        }
      }
      if (maxVal < 1e-6f) {
        singular = true;
      }
      pivotRow = maxRow;
    }
    __syncthreads();

    if (singular)
      break;

    int maxRow = pivotRow;
    if (maxRow != k) {
      for (int j = TID; j < N; j += THREADS) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (TID == 0) {
        float tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    if (ROW > k && ROW < N) {
      float factor = A[ROW][k] / A[k][k];
      for (int j = k + 1 + LANE; j < N; j += THREADS_PER_ROW) {
        A[ROW][j] -= factor * A[k][j];
      }
      if (LANE == 0) {
        b[ROW] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (TID == 0)
      success[SYS_IDX] = false;
    return;
  }

  __shared__ float partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    float mySum = 0.0f;
#pragma unroll 4
    for (int j = i + 1 + TID; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[TID] = mySum;
    __syncthreads();

    if (TID < 64)
      partialSums[TID] += partialSums[TID + 64];
    __syncthreads();
    if (TID < 32)
      partialSums[TID] += partialSums[TID + 32];
    __syncthreads();
    if (TID < 16)
      partialSums[TID] += partialSums[TID + 16];
    __syncthreads();
    if (TID < 8)
      partialSums[TID] += partialSums[TID + 8];
    __syncthreads();
    if (TID < 4)
      partialSums[TID] += partialSums[TID + 4];
    __syncthreads();
    if (TID < 2)
      partialSums[TID] += partialSums[TID + 2];
    __syncthreads();
    if (TID < 1)
      partialSums[TID] += partialSums[TID + 1];
    __syncthreads();

    if (TID == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (TID == 0) {
    success[SYS_IDX] = true;
  }

  if (ROW < N && LANE == 0) {
    bout[ROW] = b[ROW];
  }
}

/**
 * @brief Kernel: Batch solve for 64x64 systems (FP32).
 */
__global__ __launch_bounds__(256, 6) void solveBatch64x64KernelF32(const float* SIM_RESTRICT As,
                                                                   float* SIM_RESTRICT bs,
                                                                   int batch,
                                                                   bool* SIM_RESTRICT success) {
  constexpr int N = 64;
  constexpr int THREADS = 256;
  constexpr int THREADS_PER_ROW = THREADS / N;

  const int SYS_IDX = blockIdx.x;
  if (SYS_IDX >= batch)
    return;

  const int TID = threadIdx.x;
  const int ROW = TID / THREADS_PER_ROW;
  const int LANE = TID % THREADS_PER_ROW;

  __shared__ float A[N][N + 1];
  __shared__ float b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (TID == 0)
    singular = false;
  __syncthreads();

  const float* Ain = As + SYS_IDX * N * N;
  float* bout = bs + SYS_IDX * N;

  if (ROW < N) {
    for (int j = LANE; j < N; j += THREADS_PER_ROW) {
      A[ROW][j] = Ain[ROW * N + j];
    }
    if (LANE == 0) {
      b[ROW] = bout[ROW];
    }
  }
  __syncthreads();

  for (int k = 0; k < N && !singular; ++k) {
    if (TID == 0) {
      int maxRow = k;
      float maxVal = fabsf(A[k][k]);
      for (int i = k + 1; i < N; ++i) {
        float val = fabsf(A[i][k]);
        if (val > maxVal) {
          maxVal = val;
          maxRow = i;
        }
      }
      if (maxVal < 1e-6f) {
        singular = true;
      }
      pivotRow = maxRow;
    }
    __syncthreads();

    if (singular)
      break;

    int maxRow = pivotRow;
    if (maxRow != k) {
      for (int j = TID; j < N; j += THREADS) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (TID == 0) {
        float tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    if (ROW > k && ROW < N) {
      float factor = A[ROW][k] / A[k][k];
      for (int j = k + 1 + LANE; j < N; j += THREADS_PER_ROW) {
        A[ROW][j] -= factor * A[k][j];
      }
      if (LANE == 0) {
        b[ROW] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (TID == 0)
      success[SYS_IDX] = false;
    return;
  }

  __shared__ float partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    float mySum = 0.0f;
#pragma unroll 4
    for (int j = i + 1 + TID; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[TID] = mySum;
    __syncthreads();

    if (TID < 128)
      partialSums[TID] += partialSums[TID + 128];
    __syncthreads();
    if (TID < 64)
      partialSums[TID] += partialSums[TID + 64];
    __syncthreads();
    if (TID < 32)
      partialSums[TID] += partialSums[TID + 32];
    __syncthreads();
    if (TID < 16)
      partialSums[TID] += partialSums[TID + 16];
    __syncthreads();
    if (TID < 8)
      partialSums[TID] += partialSums[TID + 8];
    __syncthreads();
    if (TID < 4)
      partialSums[TID] += partialSums[TID + 4];
    __syncthreads();
    if (TID < 2)
      partialSums[TID] += partialSums[TID + 2];
    __syncthreads();
    if (TID < 1)
      partialSums[TID] += partialSums[TID + 1];
    __syncthreads();

    if (TID == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (TID == 0) {
    success[SYS_IDX] = true;
  }

  if (ROW < N && LANE == 0) {
    bout[ROW] = b[ROW];
  }
}

#endif // COMPAT_HAVE_CUSOLVER

/* ----------------------------- API Functions ----------------------------- */

MnaBatchWorkspace::~MnaBatchWorkspace() { release(); }

bool MnaBatchWorkspace::prepare(std::size_t dim, std::size_t maxBatch) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (initialized && dim <= maxDim && maxBatch <= maxBatchSize) {
    return true;
  }

  release();

  if (cudaSetDevice(0) != cudaSuccess) {
    return false;
  }

  std::size_t matrixBytes = maxBatch * dim * dim * sizeof(double);
  std::size_t vectorBytes = maxBatch * dim * sizeof(double);
  std::size_t flagBytes = maxBatch * sizeof(bool);

  if (cudaMalloc(&dA, matrixBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&db, vectorBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&dSuccess, flagBytes) != cudaSuccess) {
    release();
    return false;
  }

  maxDim = dim;
  maxBatchSize = maxBatch;
  initialized = true;
  return true;
#else
  (void)dim;
  (void)maxBatch;
  return false;
#endif
}

void MnaBatchWorkspace::release() noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (dA) {
    cudaFree(dA);
    dA = nullptr;
  }
  if (db) {
    cudaFree(db);
    db = nullptr;
  }
  if (dSuccess) {
    cudaFree(dSuccess);
    dSuccess = nullptr;
  }
  maxDim = 0;
  maxBatchSize = 0;
  initialized = false;
#endif
}

bool solveBatchCustom(MnaBatchWorkspace& ws, const double* As, double* bs, std::size_t dim,
                      std::size_t batch, void* stream) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!ws.canHandle(dim, batch)) {
    return false;
  }

  cudaStream_t s = stream ? static_cast<cudaStream_t>(stream) : nullptr;

  std::size_t matrixBytes = batch * dim * dim * sizeof(double);
  std::size_t vectorBytes = batch * dim * sizeof(double);

  // Copy to device
  if (cudaMemcpyAsync(ws.dA, As, matrixBytes, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpyAsync(ws.db, bs, vectorBytes, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }

  // Select kernel based on dimension
  int intBatch = static_cast<int>(batch);
  if (dim == 8) {
    // TPB = 64 keeps the grid large enough to span many SMs even on
    // modest batches (batch / 256 would give only 4 blocks at 1024).
    constexpr int TPB_8 = 64;
    dim3 grid((batch + TPB_8 - 1) / TPB_8);
    dim3 block(TPB_8);
    solveBatch8x8Kernel<<<grid, block, 0, s>>>(static_cast<double*>(ws.dA),
                                               static_cast<double*>(ws.db), intBatch,
                                               static_cast<bool*>(ws.dSuccess));
  } else if (dim == 16) {
    // TPB = 64 -- same rationale as dim=8. Each thread owns one full
    // 16x16 system in local memory; smaller blocks keep enough grid
    // parallelism for typical batch sizes.
    constexpr int TPB_16 = 64;
    dim3 grid((batch + TPB_16 - 1) / TPB_16);
    dim3 block(TPB_16);
    solveBatch16x16Kernel<<<grid, block, 0, s>>>(static_cast<double*>(ws.dA),
                                                 static_cast<double*>(ws.db), intBatch,
                                                 static_cast<bool*>(ws.dSuccess));
  } else if (dim == 32) {
    dim3 grid(batch);
    dim3 block(128); // 4 warps for better occupancy
    solveBatch32x32Kernel<<<grid, block, 0, s>>>(static_cast<double*>(ws.dA),
                                                 static_cast<double*>(ws.db), intBatch,
                                                 static_cast<bool*>(ws.dSuccess));
  } else if (dim == 64) {
    dim3 grid(batch);
    dim3 block(256); // 8 warps for maximum occupancy
    solveBatch64x64Kernel<<<grid, block, 0, s>>>(static_cast<double*>(ws.dA),
                                                 static_cast<double*>(ws.db), intBatch,
                                                 static_cast<bool*>(ws.dSuccess));
  } else {
    // Unsupported dimension
    return false;
  }

  // Check kernel launch
  if (cudaGetLastError() != cudaSuccess) {
    return false;
  }

  // Copy results back
  if (cudaMemcpyAsync(bs, ws.db, vectorBytes, cudaMemcpyDeviceToHost, s) != cudaSuccess) {
    return false;
  }

  // Sync
  if (cudaStreamSynchronize(s) != cudaSuccess) {
    return false;
  }

  return true;
#else
  (void)ws;
  (void)As;
  (void)bs;
  (void)dim;
  (void)batch;
  (void)stream;
  return false;
#endif
}

LaunchConfig getLaunchConfig(std::size_t dim, std::size_t batch) noexcept {
  LaunchConfig cfg{};
#if COMPAT_HAVE_CUSOLVER
  if (dim == 8 || dim == 16) {
    cfg.gridX = (batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
    cfg.blockX = THREADS_PER_BLOCK;
    cfg.threadsPerSystem = 1;
  } else if (dim == 32) {
    cfg.gridX = batch;
    cfg.blockX = 128; // 4 warps for better occupancy
    cfg.threadsPerSystem = 128;
    // Shared: A[32][33] + b[32] + pivotRow + singular + partialSums[128]
    cfg.sharedMemBytes = 32 * 33 * sizeof(double) + 32 * sizeof(double) + sizeof(int) +
                         sizeof(bool) + 128 * sizeof(double);
  } else if (dim == 64) {
    cfg.gridX = batch;
    cfg.blockX = 256; // 8 warps for maximum occupancy
    cfg.threadsPerSystem = 256;
    // Shared: A[64][65] + b[64] + pivotRow + singular + partialSums[256]
    cfg.sharedMemBytes = 64 * 65 * sizeof(double) + 64 * sizeof(double) + sizeof(int) +
                         sizeof(bool) + 256 * sizeof(double);
  }
#else
  (void)dim;
  (void)batch;
#endif
  return cfg;
}

/* ----------------------------- FP32 API Functions ----------------------------- */

MnaBatchWorkspaceF32::~MnaBatchWorkspaceF32() { release(); }

bool MnaBatchWorkspaceF32::prepare(std::size_t dim, std::size_t maxBatch) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (initialized && dim <= maxDim && maxBatch <= maxBatchSize) {
    return true;
  }

  release();

  if (cudaSetDevice(0) != cudaSuccess) {
    return false;
  }

  std::size_t matrixBytes = maxBatch * dim * dim * sizeof(float);
  std::size_t vectorBytes = maxBatch * dim * sizeof(float);
  std::size_t flagBytes = maxBatch * sizeof(bool);

  if (cudaMalloc(&dA, matrixBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&db, vectorBytes) != cudaSuccess) {
    release();
    return false;
  }
  if (cudaMalloc(&dSuccess, flagBytes) != cudaSuccess) {
    release();
    return false;
  }

  maxDim = dim;
  maxBatchSize = maxBatch;
  initialized = true;
  return true;
#else
  (void)dim;
  (void)maxBatch;
  return false;
#endif
}

void MnaBatchWorkspaceF32::release() noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (dA) {
    cudaFree(dA);
    dA = nullptr;
  }
  if (db) {
    cudaFree(db);
    db = nullptr;
  }
  if (dSuccess) {
    cudaFree(dSuccess);
    dSuccess = nullptr;
  }
  maxDim = 0;
  maxBatchSize = 0;
  initialized = false;
#endif
}

bool solveBatchCustomF32(MnaBatchWorkspaceF32& ws, const float* As, float* bs, std::size_t dim,
                         std::size_t batch, void* stream) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!ws.canHandle(dim, batch)) {
    return false;
  }

  cudaStream_t s = stream ? static_cast<cudaStream_t>(stream) : nullptr;

  std::size_t matrixBytes = batch * dim * dim * sizeof(float);
  std::size_t vectorBytes = batch * dim * sizeof(float);

  // Copy to device
  if (cudaMemcpyAsync(ws.dA, As, matrixBytes, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpyAsync(ws.db, bs, vectorBytes, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }

  // Select kernel based on dimension
  int intBatch = static_cast<int>(batch);
  if (dim == 8) {
    // TPB = 64 keeps the grid wide enough to span many SMs; TPB=256
    // would land only 4 blocks for batch=1024 (5% SM utilisation).
    constexpr int TPB = 64;
    dim3 grid((batch + TPB - 1) / TPB);
    dim3 block(TPB);
    solveBatch8x8KernelF32<<<grid, block, 0, s>>>(static_cast<float*>(ws.dA),
                                                  static_cast<float*>(ws.db), intBatch,
                                                  static_cast<bool*>(ws.dSuccess));
  } else if (dim == 16) {
    constexpr int TPB = 64;
    dim3 grid((batch + TPB - 1) / TPB);
    dim3 block(TPB);
    solveBatch16x16KernelF32<<<grid, block, 0, s>>>(static_cast<float*>(ws.dA),
                                                    static_cast<float*>(ws.db), intBatch,
                                                    static_cast<bool*>(ws.dSuccess));
  } else if (dim == 32) {
    dim3 grid(batch);
    dim3 block(128);
    solveBatch32x32KernelF32<<<grid, block, 0, s>>>(static_cast<float*>(ws.dA),
                                                    static_cast<float*>(ws.db), intBatch,
                                                    static_cast<bool*>(ws.dSuccess));
  } else if (dim == 64) {
    dim3 grid(batch);
    dim3 block(256);
    solveBatch64x64KernelF32<<<grid, block, 0, s>>>(static_cast<float*>(ws.dA),
                                                    static_cast<float*>(ws.db), intBatch,
                                                    static_cast<bool*>(ws.dSuccess));
  } else {
    return false;
  }

  // Check kernel launch
  if (cudaGetLastError() != cudaSuccess) {
    return false;
  }

  // Copy results back
  if (cudaMemcpyAsync(bs, ws.db, vectorBytes, cudaMemcpyDeviceToHost, s) != cudaSuccess) {
    return false;
  }

  // Sync
  if (cudaStreamSynchronize(s) != cudaSuccess) {
    return false;
  }

  return true;
#else
  (void)ws;
  (void)As;
  (void)bs;
  (void)dim;
  (void)batch;
  (void)stream;
  return false;
#endif
}

LaunchConfig getLaunchConfigF32(std::size_t dim, std::size_t batch) noexcept {
  LaunchConfig cfg{};
#if COMPAT_HAVE_CUSOLVER
  if (dim == 8 || dim == 16) {
    cfg.gridX = (batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
    cfg.blockX = THREADS_PER_BLOCK;
    cfg.threadsPerSystem = 1;
  } else if (dim == 32) {
    cfg.gridX = batch;
    cfg.blockX = 128;
    cfg.threadsPerSystem = 128;
    // Shared: A[32][33] + b[32] + pivotRow + singular + partialSums[128] (all float)
    cfg.sharedMemBytes = 32 * 33 * sizeof(float) + 32 * sizeof(float) + sizeof(int) + sizeof(bool) +
                         128 * sizeof(float);
  } else if (dim == 64) {
    cfg.gridX = batch;
    cfg.blockX = 256;
    cfg.threadsPerSystem = 256;
    // Shared: A[64][65] + b[64] + pivotRow + singular + partialSums[256] (all float)
    cfg.sharedMemBytes = 64 * 65 * sizeof(float) + 64 * sizeof(float) + sizeof(int) + sizeof(bool) +
                         256 * sizeof(float);
  }
#else
  (void)dim;
  (void)batch;
#endif
  return cfg;
}

} // namespace sim::electronics::algorithms::mna::cuda
