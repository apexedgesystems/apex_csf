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
 *
 * The 8, 16 and 32 kernels are templates over the scalar, instantiated for
 * double and float. The 64 kernels differ by precision: the double matrix
 * stays in global memory, the float matrix fits in shared memory.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_attrs.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <algorithm>

#if COMPAT_HAVE_CUSOLVER
#include <cuda_runtime.h>
#include <cmath>
#include <limits>
#endif

namespace sim::electronics::algorithms::mna::cuda {

/* ----------------------------- Pivot Test ----------------------------- */

#if COMPAT_HAVE_CUSOLVER

/** @brief Magnitude of a value, in its own precision. */
__device__ __forceinline__ double magnitude(double value) { return fabs(value); }

/** @brief Magnitude of a value, in its own precision. */
__device__ __forceinline__ float magnitude(float value) { return fabsf(value); }

/** @brief The larger of two values, in their own precision. */
__device__ __forceinline__ double larger(double a, double b) { return fmax(a, b); }

/** @brief The larger of two values, in their own precision. */
__device__ __forceinline__ float larger(float a, float b) { return fmaxf(a, b); }

/**
 * @brief What a kernel writes to every entry of a system it could not solve.
 *
 * The host reads the first entry of each system to tell which were solved,
 * so no flag travels beside the solutions.
 */
template <typename T> constexpr T NO_SOLUTION = std::numeric_limits<T>::quiet_NaN();

/**
 * @brief Whether the largest candidate for a pivot is no pivot.
 *
 * `columnScale` is the largest magnitude in that column of the matrix as
 * given. A candidate at or below `size * epsilon * columnScale` is what
 * rounding leaves of entries that cancelled. A column of zeros has scale
 * zero and is the limiting case. A candidate that is not a number fails the
 * comparison and is no pivot either.
 *
 * @param candidate Largest magnitude left in the column at this step.
 * @param columnScale Largest magnitude of the column in the matrix as given.
 * @param size Matrix dimension.
 */
template <typename T>
__device__ __forceinline__ bool negligiblePivot(T candidate, T columnScale, int size) {
  return !(candidate > static_cast<T>(size) * std::numeric_limits<T>::epsilon() * columnScale);
}

/* ----------------------------- Kernels ----------------------------- */

/**
 * @brief Kernel: Batch solve for 8x8 systems.
 *
 * Each thread solves one complete 8x8 system using Gaussian elimination
 * with partial pivoting. Matrix stored in registers.
 *
 * @tparam T Scalar: double or float.
 * @param As Input matrices (batch * 64 scalars, row-major).
 * @param bs Input/output RHS vectors (batch * 8 scalars); a system with no
 *           pivot in some column comes back as NO_SOLUTION throughout.
 * @param batch Number of systems.
 */
template <typename T>
__global__ __launch_bounds__(64, 24) void solveBatch8x8Kernel(const T* SIM_RESTRICT As,
                                                              T* SIM_RESTRICT bs, int batch) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 8;

  // Load matrix and RHS into registers
  T A[N][N];
  T b[N];
  T columnScale[N]; // Largest magnitude of each column of the matrix as given

  const T* Ain = As + IDX * N * N;
  T* bout = bs + IDX * N;

  // Column by column, so that each column's scale is gathered in a register
#pragma unroll
  for (int j = 0; j < N; ++j) {
    T scale = T(0);
#pragma unroll
    for (int i = 0; i < N; ++i) {
      const T ENTRY = Ain[i * N + j];
      A[i][j] = ENTRY;
      scale = larger(scale, magnitude(ENTRY));
    }
    columnScale[j] = scale;
    b[j] = bout[j];
  }

// Gaussian elimination with partial pivoting
#pragma unroll
  for (int k = 0; k < N; ++k) {
    // Find pivot
    int maxRow = k;
    T maxVal = magnitude(A[k][k]);
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      T val = magnitude(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    // No pivot in this column: the system has no solution to give
    if (negligiblePivot(maxVal, columnScale[k], N)) {
#pragma unroll
      for (int i = 0; i < N; ++i) {
        bout[i] = NO_SOLUTION<T>;
      }
      return;
    }

    // Swap rows if needed
    if (maxRow != k) {
#pragma unroll
      for (int j = k; j < N; ++j) {
        T tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      T tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    // Eliminate column k
    T pivot = A[k][k];
#pragma unroll
    for (int i = k + 1; i < N; ++i) {
      T factor = A[i][k] / pivot;
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
    T sum = b[i];
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
}

/**
 * @brief Kernel: Batch solve for 16x16 systems.
 *
 * One thread per system, as the 8x8 kernel, with the matrix in local memory.
 */
template <typename T>
__global__ __launch_bounds__(64, 24) void solveBatch16x16Kernel(const T* SIM_RESTRICT As,
                                                                T* SIM_RESTRICT bs, int batch) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;
  if (IDX >= batch)
    return;

  constexpr int N = 16;

  // Load matrix and RHS into local memory (registers may spill)
  T A[N][N];
  T b[N];
  T columnScale[N]; // Largest magnitude of each column of the matrix as given

  const T* Ain = As + IDX * N * N;
  T* bout = bs + IDX * N;

  // Column by column, so that each column's scale is gathered in a register
  for (int j = 0; j < N; ++j) {
    T scale = T(0);
    for (int i = 0; i < N; ++i) {
      const T ENTRY = Ain[i * N + j];
      A[i][j] = ENTRY;
      scale = larger(scale, magnitude(ENTRY));
    }
    columnScale[j] = scale;
    b[j] = bout[j];
  }

  // Gaussian elimination with partial pivoting
  for (int k = 0; k < N; ++k) {
    int maxRow = k;
    T maxVal = magnitude(A[k][k]);
    for (int i = k + 1; i < N; ++i) {
      T val = magnitude(A[i][k]);
      if (val > maxVal) {
        maxVal = val;
        maxRow = i;
      }
    }

    if (negligiblePivot(maxVal, columnScale[k], N)) {
      for (int i = 0; i < N; ++i) {
        bout[i] = NO_SOLUTION<T>;
      }
      return;
    }

    if (maxRow != k) {
      for (int j = k; j < N; ++j) {
        T tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      T tmp = b[k];
      b[k] = b[maxRow];
      b[maxRow] = tmp;
    }

    T pivot = A[k][k];
    for (int i = k + 1; i < N; ++i) {
      T factor = A[i][k] / pivot;
      for (int j = k + 1; j < N; ++j) {
        A[i][j] -= factor * A[k][j];
      }
      b[i] -= factor * b[k];
    }
  }

  // Back substitution
  for (int i = N - 1; i >= 0; --i) {
    T sum = b[i];
    for (int j = i + 1; j < N; ++j) {
      sum -= A[i][j] * b[j];
    }
    b[i] = sum / A[i][i];
  }

  for (int i = 0; i < N; ++i) {
    bout[i] = b[i];
  }
}

/**
 * @brief Kernel: Batch solve for 32x32 systems.
 *
 * Shared-mem A + per-block parallelism across rows. 128 threads per
 * block (4 warps, 4 threads per row). `__launch_bounds__(128, 12)`
 * caps registers at 40 so the register block-limit matches the warp
 * block-limit; shared memory (A[N][N+1], 8.4 KB in double precision)
 * is the remaining binding occupancy constraint.
 */
template <typename T>
__global__ __launch_bounds__(128, 12) void solveBatch32x32Kernel(const T* SIM_RESTRICT As,
                                                                 T* SIM_RESTRICT bs, int batch) {
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
  __shared__ T A[N][N + 1]; // +1 to avoid bank conflicts
  __shared__ T b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (TID == 0)
    singular = false;
  __syncthreads();

  // Load matrix (threads cooperate, 4 threads per ROW)
  const T* Ain = As + SYS_IDX * N * N;
  T* bout = bs + SYS_IDX * N;

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

  // The thread of column TID keeps the largest magnitude of that column of
  // the matrix as given, and finds the column's pivot when its turn comes.
  T columnScale = T(0);
  if (TID < N) {
    for (int i = 0; i < N; ++i) {
      const T ENTRY = magnitude(A[i][TID]);
      if (ENTRY > columnScale) {
        columnScale = ENTRY;
      }
    }
  }

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // The column's thread finds its pivot
    if (TID == k) {
      int maxRow = k;
      T maxVal = magnitude(A[k][k]);
      for (int i = k + 1; i < N; ++i) {
        T val = magnitude(A[i][k]);
        if (val > maxVal) {
          maxVal = val;
          maxRow = i;
        }
      }
      if (negligiblePivot(maxVal, columnScale, N)) {
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
        T tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (TID == 0) {
        T tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    // Eliminate - each thread group handles one ROW below pivot
    // Parallelize column updates within each ROW
    if (ROW > k && ROW < N) {
      T factor = A[ROW][k] / A[k][k];
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
    if (ROW < N && LANE == 0) {
      bout[ROW] = NO_SOLUTION<T>;
    }
    return;
  }

  // Parallel back substitution with unrolled reduction
  __shared__ T partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    // Each thread computes partial sum for columns it handles
    T mySum = T(0);
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

  // Write results (parallel)
  if (ROW < N && LANE == 0) {
    bout[ROW] = b[ROW];
  }
}

/**
 * @brief Kernel: Batch solve for 64x64 systems.
 *
 * Double precision. The matrix stays in global memory; 256 threads (8
 * warps), 4 per row, update the columns in parallel during elimination.
 */
// __launch_bounds__(256, 6) pins 6 blocks per SM = 48 warps = 100%
// theoretical occupancy (48 warp-slot max / 8 warps per block).
__global__ __launch_bounds__(256, 6) void solveBatch64x64Kernel(const double* SIM_RESTRICT As,
                                                                double* SIM_RESTRICT bs,
                                                                int batch) {
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

  // The thread of column TID keeps the largest magnitude of that column of
  // the matrix as given, and finds the column's pivot when its turn comes.
  double columnScale = 0.0;
  if (TID < N) {
    for (int i = 0; i < N; ++i) {
      const double ENTRY = magnitude(A[i * N + TID]);
      if (ENTRY > columnScale) {
        columnScale = ENTRY;
      }
    }
  }

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // Serial scan for the pivot in the column's thread. Parallel
    // warp-shuffle variants add register pressure that outweighs the
    // shorter scan.
    if (TID == k) {
      int maxRow = k;
      double maxVal = magnitude(A[k * N + k]);
      for (int i = k + 1; i < N; ++i) {
        double v = magnitude(A[i * N + k]);
        if (v > maxVal) {
          maxVal = v;
          maxRow = i;
        }
      }
      if (negligiblePivot(maxVal, columnScale, N)) {
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
    if (TID < N) {
      bout[TID] = NO_SOLUTION<double>;
    }
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

  // Write b back (64 threads, one element each).
  if (TID < N) {
    bout[TID] = bSh[TID];
  }
}

/**
 * @brief Kernel: Batch solve for 64x64 systems.
 *
 * Single precision. The matrix fits in shared memory (16.6 KB), so this is
 * the 32x32 scheme with 256 threads.
 */
__global__ __launch_bounds__(256, 6) void solveBatch64x64Kernel(const float* SIM_RESTRICT As,
                                                                float* SIM_RESTRICT bs, int batch) {
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

  // The thread of column TID keeps the largest magnitude of that column of
  // the matrix as given, and finds the column's pivot when its turn comes.
  float columnScale = 0.0f;
  if (TID < N) {
    for (int i = 0; i < N; ++i) {
      const float ENTRY = magnitude(A[i][TID]);
      if (ENTRY > columnScale) {
        columnScale = ENTRY;
      }
    }
  }

  for (int k = 0; k < N && !singular; ++k) {
    if (TID == k) {
      int maxRow = k;
      float maxVal = magnitude(A[k][k]);
      for (int i = k + 1; i < N; ++i) {
        float val = magnitude(A[i][k]);
        if (val > maxVal) {
          maxVal = val;
          maxRow = i;
        }
      }
      if (negligiblePivot(maxVal, columnScale, N)) {
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
    if (ROW < N && LANE == 0) {
      bout[ROW] = NO_SOLUTION<float>;
    }
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

  if (ROW < N && LANE == 0) {
    bout[ROW] = b[ROW];
  }
}

#endif // COMPAT_HAVE_CUSOLVER

/* ----------------------------- Launch Shape ----------------------------- */

#if COMPAT_HAVE_CUSOLVER

namespace {

/**
 * @brief How a size's kernel is launched.
 *
 * The launch and getLaunchConfig both read it here.
 */
struct KernelShape {
  unsigned threadsPerBlock;
  unsigned systemsPerBlock;
};

/**
 * @brief Launch shape for a supported dimension, the same in both precisions.
 *
 * 8 and 16: one thread per system, 64 systems to a block, so that a modest
 * batch still spans many multiprocessors (256 to a block gives 4 blocks at
 * a batch of 1024). 32 and 64: one block per system, of 128 and 256 threads.
 */
constexpr KernelShape shapeOf(std::size_t dim) noexcept {
  return dim == 32 ? KernelShape{128, 1} : dim == 64 ? KernelShape{256, 1} : KernelShape{64, 64};
}

template <typename T> using Kernel = void (*)(const T*, T*, int);

/** @brief The kernel for a supported dimension and scalar. */
template <typename T> Kernel<T> kernelOf(std::size_t dim) noexcept {
  switch (dim) {
  case 8:
    return solveBatch8x8Kernel<T>;
  case 16:
    return solveBatch16x16Kernel<T>;
  case 32:
    return solveBatch32x32Kernel<T>;
  default:
    return static_cast<Kernel<T>>(solveBatch64x64Kernel); // Overloaded on the scalar
  }
}

template <typename T> LaunchConfig launchConfigOf(std::size_t dim, std::size_t batch) noexcept {
  LaunchConfig cfg{};
  if (!isSupportedDim(dim)) {
    return cfg;
  }

  const KernelShape SHAPE = shapeOf(dim);
  cfg.gridX = (batch + SHAPE.systemsPerBlock - 1) / SHAPE.systemsPerBlock;
  cfg.blockX = SHAPE.threadsPerBlock;
  cfg.threadsPerSystem = SHAPE.threadsPerBlock / SHAPE.systemsPerBlock;

  // The compiled kernel says how much shared memory it declares.
  cudaFuncAttributes attributes{};
  if (cudaFuncGetAttributes(&attributes, reinterpret_cast<const void*>(kernelOf<T>(dim))) ==
      cudaSuccess) {
    cfg.sharedMemBytes = attributes.sharedSizeBytes;
  }
  return cfg;
}

template <typename T>
bool solveBatch(BatchWorkspace<T>& ws, const T* As, T* bs, std::size_t dim, std::size_t batch,
                void* stream, bool* solved) noexcept {
  // Whatever keeps the batch from running to its end, no system of it is
  // reported solved: the flags are true only where the last loop says so.
  if (solved != nullptr) {
    std::fill_n(solved, batch, false);
  }
  if (!isSupportedDim(dim) || !ws.canHandle(dim, batch)) {
    return false;
  }
  if (batch == 0) {
    return true;
  }

  auto s = static_cast<cudaStream_t>(stream);

  const std::size_t MATRIX_BYTES = batch * dim * dim * sizeof(T);
  const std::size_t VECTOR_BYTES = batch * dim * sizeof(T);

  // Copy to device
  if (cudaMemcpyAsync(ws.dA, As, MATRIX_BYTES, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpyAsync(ws.db, bs, VECTOR_BYTES, cudaMemcpyHostToDevice, s) != cudaSuccess) {
    return false;
  }

  const KernelShape SHAPE = shapeOf(dim);
  const dim3 GRID(
      static_cast<unsigned>((batch + SHAPE.systemsPerBlock - 1) / SHAPE.systemsPerBlock));
  const dim3 BLOCK(SHAPE.threadsPerBlock);
  const Kernel<T> KERNEL = kernelOf<T>(dim);
  KERNEL<<<GRID, BLOCK, 0, s>>>(static_cast<T*>(ws.dA), static_cast<T*>(ws.db),
                                static_cast<int>(batch));

  // Check kernel launch
  if (cudaGetLastError() != cudaSuccess) {
    return false;
  }

  // Copy results back
  if (cudaMemcpyAsync(bs, ws.db, VECTOR_BYTES, cudaMemcpyDeviceToHost, s) != cudaSuccess) {
    return false;
  }

  // Sync
  if (cudaStreamSynchronize(s) != cudaSuccess) {
    return false;
  }

  // A system that was not solved came back as NO_SOLUTION throughout.
  bool all = true;
  for (std::size_t i = 0; i < batch; ++i) {
    const bool SOLVED = !std::isnan(bs[i * dim]);
    all = all && SOLVED;
    if (solved != nullptr) {
      solved[i] = SOLVED;
    }
  }
  return all;
}

} // namespace

#endif // COMPAT_HAVE_CUSOLVER

/* ----------------------------- Workspace ----------------------------- */

template <typename Scalar> BatchWorkspace<Scalar>::~BatchWorkspace() { release(); }

template <typename Scalar>
bool BatchWorkspace<Scalar>::prepare(std::size_t dim, std::size_t maxBatch) noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (!isSupportedDim(dim) || maxBatch == 0) {
    return false;
  }
  if (initialized && dim <= maxDim && maxBatch <= maxBatchSize) {
    return true;
  }

  release();

  if (cudaSetDevice(0) != cudaSuccess) {
    return false;
  }

  const std::size_t MATRIX_BYTES = maxBatch * dim * dim * sizeof(Scalar);
  const std::size_t VECTOR_BYTES = maxBatch * dim * sizeof(Scalar);

  if (cudaMalloc(&dA, MATRIX_BYTES) != cudaSuccess ||
      cudaMalloc(&db, VECTOR_BYTES) != cudaSuccess) {
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

template <typename Scalar> void BatchWorkspace<Scalar>::release() noexcept {
#if COMPAT_HAVE_CUSOLVER
  if (dA) {
    cudaFree(dA);
    dA = nullptr;
  }
  if (db) {
    cudaFree(db);
    db = nullptr;
  }
  maxDim = 0;
  maxBatchSize = 0;
  initialized = false;
#endif
}

template struct BatchWorkspace<double>;
template struct BatchWorkspace<float>;

/* ----------------------------- API Functions ----------------------------- */

bool solveBatchCustom(MnaBatchWorkspace& ws, const double* As, double* bs, std::size_t dim,
                      std::size_t batch, void* stream, bool* solved) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return solveBatch(ws, As, bs, dim, batch, stream, solved);
#else
  (void)ws;
  (void)As;
  (void)bs;
  (void)dim;
  (void)stream;
  if (solved != nullptr) {
    std::fill_n(solved, batch, false);
  }
  return false;
#endif
}

bool solveBatchCustomF32(MnaBatchWorkspaceF32& ws, const float* As, float* bs, std::size_t dim,
                         std::size_t batch, void* stream, bool* solved) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return solveBatch(ws, As, bs, dim, batch, stream, solved);
#else
  (void)ws;
  (void)As;
  (void)bs;
  (void)dim;
  (void)stream;
  if (solved != nullptr) {
    std::fill_n(solved, batch, false);
  }
  return false;
#endif
}

LaunchConfig getLaunchConfig(std::size_t dim, std::size_t batch) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return launchConfigOf<double>(dim, batch);
#else
  (void)dim;
  (void)batch;
  return {};
#endif
}

LaunchConfig getLaunchConfigF32(std::size_t dim, std::size_t batch) noexcept {
#if COMPAT_HAVE_CUSOLVER
  return launchConfigOf<float>(dim, batch);
#else
  (void)dim;
  (void)batch;
  return {};
#endif
}

} // namespace sim::electronics::algorithms::mna::cuda
