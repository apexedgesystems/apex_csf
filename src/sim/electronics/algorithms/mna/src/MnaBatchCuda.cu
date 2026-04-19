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

namespace sim::electronics::mna::cuda {

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
__global__ void solveBatch8x8Kernel(const double* SIM_RESTRICT As, double* SIM_RESTRICT bs,
                                    int batch, bool* SIM_RESTRICT success) {
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
__global__ void solveBatch16x16Kernel(const double* SIM_RESTRICT As, double* SIM_RESTRICT bs,
                                      int batch, bool* SIM_RESTRICT success) {
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
 * Uses shared memory for better performance with larger matrices.
 * One thread block per system, threads cooperate on elimination.
 *
 * Optimized: Uses 128 threads (4 warps) for better occupancy.
 * Each row update is parallelized across multiple threads.
 */
__global__ void solveBatch32x32Kernel(const double* SIM_RESTRICT As, double* SIM_RESTRICT bs,
                                      int batch, bool* SIM_RESTRICT success) {
  constexpr int N = 32;
  constexpr int THREADS = 128;                 // 4 warps for better occupancy
  constexpr int THREADS_PER_ROW = THREADS / N; // 4 threads per row

  // One block per system
  const int sysIdx = blockIdx.x;
  if (sysIdx >= batch)
    return;

  const int tid = threadIdx.x;
  const int row = tid / THREADS_PER_ROW;  // Which row this thread works on
  const int lane = tid % THREADS_PER_ROW; // Position within row group

  // Shared memory for matrix and RHS
  __shared__ double A[N][N + 1]; // +1 to avoid bank conflicts
  __shared__ double b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (tid == 0)
    singular = false;
  __syncthreads();

  // Load matrix (threads cooperate, 4 threads per row)
  const double* Ain = As + sysIdx * N * N;
  double* bout = bs + sysIdx * N;

  if (row < N) {
    // Each group of 4 threads loads one row, strided
    for (int j = lane; j < N; j += THREADS_PER_ROW) {
      A[row][j] = Ain[row * N + j];
    }
    if (lane == 0) {
      b[row] = bout[row];
    }
  }
  __syncthreads();

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // Thread 0 finds pivot
    if (tid == 0) {
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
      for (int j = tid; j < N; j += THREADS) {
        double tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (tid == 0) {
        double tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    // Eliminate - each thread group handles one row below pivot
    // Parallelize column updates within each row
    if (row > k && row < N) {
      double factor = A[row][k] / A[k][k];
      // Each thread in the row group updates different columns
      for (int j = k + 1 + lane; j < N; j += THREADS_PER_ROW) {
        A[row][j] -= factor * A[k][j];
      }
      if (lane == 0) {
        b[row] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (tid == 0)
      success[sysIdx] = false;
    return;
  }

  // Parallel back substitution with unrolled reduction
  __shared__ double partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    // Each thread computes partial sum for columns it handles
    double mySum = 0.0;
#pragma unroll 4
    for (int j = i + 1 + tid; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[tid] = mySum;
    __syncthreads();

    // Unrolled reduction (128 threads)
    if (tid < 64)
      partialSums[tid] += partialSums[tid + 64];
    __syncthreads();
    if (tid < 32)
      partialSums[tid] += partialSums[tid + 32];
    __syncthreads();
    if (tid < 16)
      partialSums[tid] += partialSums[tid + 16];
    __syncthreads();
    if (tid < 8)
      partialSums[tid] += partialSums[tid + 8];
    __syncthreads();
    if (tid < 4)
      partialSums[tid] += partialSums[tid + 4];
    __syncthreads();
    if (tid < 2)
      partialSums[tid] += partialSums[tid + 2];
    __syncthreads();
    if (tid < 1)
      partialSums[tid] += partialSums[tid + 1];
    __syncthreads();

    // Thread 0 finalizes this row
    if (tid == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (tid == 0) {
    success[sysIdx] = true;
  }

  // Write results (parallel)
  if (row < N && lane == 0) {
    bout[row] = b[row];
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
__global__ void solveBatch64x64Kernel(const double* SIM_RESTRICT As, double* SIM_RESTRICT bs,
                                      int batch, bool* SIM_RESTRICT success) {
  constexpr int N = 64;
  constexpr int THREADS = 256;                 // 8 warps for maximum occupancy
  constexpr int THREADS_PER_ROW = THREADS / N; // 4 threads per row

  const int sysIdx = blockIdx.x;
  if (sysIdx >= batch)
    return;

  const int tid = threadIdx.x;
  const int row = tid / THREADS_PER_ROW;  // Which row this thread works on
  const int lane = tid % THREADS_PER_ROW; // Position within row group

  // Shared memory
  __shared__ double A[N][N + 1]; // +1 avoids bank conflicts
  __shared__ double b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (tid == 0)
    singular = false;
  __syncthreads();

  // Load matrix (4 threads per row, strided access)
  const double* Ain = As + sysIdx * N * N;
  double* bout = bs + sysIdx * N;

  if (row < N) {
    for (int j = lane; j < N; j += THREADS_PER_ROW) {
      A[row][j] = Ain[row * N + j];
    }
    if (lane == 0) {
      b[row] = bout[row];
    }
  }
  __syncthreads();

  // Gaussian elimination
  for (int k = 0; k < N && !singular; ++k) {
    // Find pivot (thread 0)
    if (tid == 0) {
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
      for (int j = tid; j < N; j += THREADS) {
        double tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (tid == 0) {
        double tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    // Eliminate - each thread group handles one row below pivot
    // Parallelize column updates within each row
    if (row > k && row < N) {
      double factor = A[row][k] / A[k][k];
      // Each thread in the row group updates different columns
      for (int j = k + 1 + lane; j < N; j += THREADS_PER_ROW) {
        A[row][j] -= factor * A[k][j];
      }
      if (lane == 0) {
        b[row] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (tid == 0)
      success[sysIdx] = false;
    return;
  }

  // Parallel back substitution with unrolled reduction
  __shared__ double partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    // Each thread computes partial sum for columns it handles
    double mySum = 0.0;
#pragma unroll 4
    for (int j = i + 1 + tid; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[tid] = mySum;
    __syncthreads();

    // Unrolled reduction (256 threads)
    if (tid < 128)
      partialSums[tid] += partialSums[tid + 128];
    __syncthreads();
    if (tid < 64)
      partialSums[tid] += partialSums[tid + 64];
    __syncthreads();
    if (tid < 32)
      partialSums[tid] += partialSums[tid + 32];
    __syncthreads();
    if (tid < 16)
      partialSums[tid] += partialSums[tid + 16];
    __syncthreads();
    if (tid < 8)
      partialSums[tid] += partialSums[tid + 8];
    __syncthreads();
    if (tid < 4)
      partialSums[tid] += partialSums[tid + 4];
    __syncthreads();
    if (tid < 2)
      partialSums[tid] += partialSums[tid + 2];
    __syncthreads();
    if (tid < 1)
      partialSums[tid] += partialSums[tid + 1];
    __syncthreads();

    // Thread 0 finalizes this row
    if (tid == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (tid == 0) {
    success[sysIdx] = true;
  }

  // Write results (parallel)
  if (row < N && lane == 0) {
    bout[row] = b[row];
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
__global__ void solveBatch8x8KernelF32(const float* SIM_RESTRICT As, float* SIM_RESTRICT bs,
                                       int batch, bool* SIM_RESTRICT success) {
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
__global__ void solveBatch16x16KernelF32(const float* SIM_RESTRICT As, float* SIM_RESTRICT bs,
                                         int batch, bool* SIM_RESTRICT success) {
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
__global__ void solveBatch32x32KernelF32(const float* SIM_RESTRICT As, float* SIM_RESTRICT bs,
                                         int batch, bool* SIM_RESTRICT success) {
  constexpr int N = 32;
  constexpr int THREADS = 128;
  constexpr int THREADS_PER_ROW = THREADS / N;

  const int sysIdx = blockIdx.x;
  if (sysIdx >= batch)
    return;

  const int tid = threadIdx.x;
  const int row = tid / THREADS_PER_ROW;
  const int lane = tid % THREADS_PER_ROW;

  __shared__ float A[N][N + 1];
  __shared__ float b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (tid == 0)
    singular = false;
  __syncthreads();

  const float* Ain = As + sysIdx * N * N;
  float* bout = bs + sysIdx * N;

  if (row < N) {
    for (int j = lane; j < N; j += THREADS_PER_ROW) {
      A[row][j] = Ain[row * N + j];
    }
    if (lane == 0) {
      b[row] = bout[row];
    }
  }
  __syncthreads();

  for (int k = 0; k < N && !singular; ++k) {
    if (tid == 0) {
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
      for (int j = tid; j < N; j += THREADS) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (tid == 0) {
        float tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    if (row > k && row < N) {
      float factor = A[row][k] / A[k][k];
      for (int j = k + 1 + lane; j < N; j += THREADS_PER_ROW) {
        A[row][j] -= factor * A[k][j];
      }
      if (lane == 0) {
        b[row] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (tid == 0)
      success[sysIdx] = false;
    return;
  }

  __shared__ float partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    float mySum = 0.0f;
#pragma unroll 4
    for (int j = i + 1 + tid; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[tid] = mySum;
    __syncthreads();

    if (tid < 64)
      partialSums[tid] += partialSums[tid + 64];
    __syncthreads();
    if (tid < 32)
      partialSums[tid] += partialSums[tid + 32];
    __syncthreads();
    if (tid < 16)
      partialSums[tid] += partialSums[tid + 16];
    __syncthreads();
    if (tid < 8)
      partialSums[tid] += partialSums[tid + 8];
    __syncthreads();
    if (tid < 4)
      partialSums[tid] += partialSums[tid + 4];
    __syncthreads();
    if (tid < 2)
      partialSums[tid] += partialSums[tid + 2];
    __syncthreads();
    if (tid < 1)
      partialSums[tid] += partialSums[tid + 1];
    __syncthreads();

    if (tid == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (tid == 0) {
    success[sysIdx] = true;
  }

  if (row < N && lane == 0) {
    bout[row] = b[row];
  }
}

/**
 * @brief Kernel: Batch solve for 64x64 systems (FP32).
 */
__global__ void solveBatch64x64KernelF32(const float* SIM_RESTRICT As, float* SIM_RESTRICT bs,
                                         int batch, bool* SIM_RESTRICT success) {
  constexpr int N = 64;
  constexpr int THREADS = 256;
  constexpr int THREADS_PER_ROW = THREADS / N;

  const int sysIdx = blockIdx.x;
  if (sysIdx >= batch)
    return;

  const int tid = threadIdx.x;
  const int row = tid / THREADS_PER_ROW;
  const int lane = tid % THREADS_PER_ROW;

  __shared__ float A[N][N + 1];
  __shared__ float b[N];
  __shared__ int pivotRow;
  __shared__ bool singular;

  if (tid == 0)
    singular = false;
  __syncthreads();

  const float* Ain = As + sysIdx * N * N;
  float* bout = bs + sysIdx * N;

  if (row < N) {
    for (int j = lane; j < N; j += THREADS_PER_ROW) {
      A[row][j] = Ain[row * N + j];
    }
    if (lane == 0) {
      b[row] = bout[row];
    }
  }
  __syncthreads();

  for (int k = 0; k < N && !singular; ++k) {
    if (tid == 0) {
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
      for (int j = tid; j < N; j += THREADS) {
        float tmp = A[k][j];
        A[k][j] = A[maxRow][j];
        A[maxRow][j] = tmp;
      }
      if (tid == 0) {
        float tmp = b[k];
        b[k] = b[maxRow];
        b[maxRow] = tmp;
      }
    }
    __syncthreads();

    if (row > k && row < N) {
      float factor = A[row][k] / A[k][k];
      for (int j = k + 1 + lane; j < N; j += THREADS_PER_ROW) {
        A[row][j] -= factor * A[k][j];
      }
      if (lane == 0) {
        b[row] -= factor * b[k];
      }
    }
    __syncthreads();
  }

  if (singular) {
    if (tid == 0)
      success[sysIdx] = false;
    return;
  }

  __shared__ float partialSums[THREADS];

#pragma unroll 4
  for (int i = N - 1; i >= 0; --i) {
    float mySum = 0.0f;
#pragma unroll 4
    for (int j = i + 1 + tid; j < N; j += THREADS) {
      mySum += A[i][j] * b[j];
    }
    partialSums[tid] = mySum;
    __syncthreads();

    if (tid < 128)
      partialSums[tid] += partialSums[tid + 128];
    __syncthreads();
    if (tid < 64)
      partialSums[tid] += partialSums[tid + 64];
    __syncthreads();
    if (tid < 32)
      partialSums[tid] += partialSums[tid + 32];
    __syncthreads();
    if (tid < 16)
      partialSums[tid] += partialSums[tid + 16];
    __syncthreads();
    if (tid < 8)
      partialSums[tid] += partialSums[tid + 8];
    __syncthreads();
    if (tid < 4)
      partialSums[tid] += partialSums[tid + 4];
    __syncthreads();
    if (tid < 2)
      partialSums[tid] += partialSums[tid + 2];
    __syncthreads();
    if (tid < 1)
      partialSums[tid] += partialSums[tid + 1];
    __syncthreads();

    if (tid == 0) {
      b[i] = (b[i] - partialSums[0]) / A[i][i];
    }
    __syncthreads();
  }

  if (tid == 0) {
    success[sysIdx] = true;
  }

  if (row < N && lane == 0) {
    bout[row] = b[row];
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
    dim3 grid((batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK);
    dim3 block(THREADS_PER_BLOCK);
    solveBatch8x8Kernel<<<grid, block, 0, s>>>(static_cast<double*>(ws.dA),
                                               static_cast<double*>(ws.db), intBatch,
                                               static_cast<bool*>(ws.dSuccess));
  } else if (dim == 16) {
    dim3 grid((batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK);
    dim3 block(THREADS_PER_BLOCK);
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
    dim3 grid((batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK);
    dim3 block(THREADS_PER_BLOCK);
    solveBatch8x8KernelF32<<<grid, block, 0, s>>>(static_cast<float*>(ws.dA),
                                                  static_cast<float*>(ws.db), intBatch,
                                                  static_cast<bool*>(ws.dSuccess));
  } else if (dim == 16) {
    dim3 grid((batch + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK);
    dim3 block(THREADS_PER_BLOCK);
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

} // namespace sim::electronics::mna::cuda
