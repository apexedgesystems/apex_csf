/**
 * @file ConvFilterKernel.cu
 * @brief CUDA kernel implementation for 2D image convolution.
 *
 * Shared memory tiling strategy:
 *   - Each thread block covers a TILE_DIM x TILE_DIM output region
 *   - Shared memory tile is (TILE_DIM + 2*radius) x (TILE_DIM + 2*radius)
 *   - Threads cooperatively load the tile + halo, then compute convolution
 *   - Constant memory stores the kernel weights for fast broadcast reads
 *
 * Boundary handling: clamp to edge (no zero padding).
 */

#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterKernel.cuh"
#include "src/sim/gpu_compute/conv_filter/inc/ConvFilterData.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <cmath>
#include <cstdint>

#if COMPAT_CUDA_AVAILABLE
#include <cuda_runtime.h>
#endif

namespace sim {
namespace gpu_compute {
namespace cuda {

#if COMPAT_CUDA_AVAILABLE

/* ----------------------------- Constants ----------------------------- */

static constexpr int K_TILE_DIM = 16;
static constexpr int K_SEP_TILE_W = 128; ///< Separable row pass: wide tiles for coalescing.
static constexpr int K_SEP_TILE_H = 8;   ///< Separable row pass: thin tiles.
static constexpr int K_MAX_KERNEL_DIAM = 2 * CONV_MAX_KERNEL_RADIUS + 1;
static constexpr int K_MAX_KERNEL_SIZE = K_MAX_KERNEL_DIAM * K_MAX_KERNEL_DIAM;

/// 2D convolution kernel weights in constant memory.
__constant__ float cKernel[K_MAX_KERNEL_SIZE];

/// 1D separable kernel weights in constant memory.
__constant__ float cKernel1D[K_MAX_KERNEL_DIAM];

/* ----------------------------- Conv2D Kernel ----------------------------- */

__global__ void conv2dKernel(const float* SIM_RESTRICT input, int width, int height, int radius,
                             float* SIM_RESTRICT output) {
  // Shared memory tile: tile + 2*radius halo on each side
  // Dynamic shared memory sized at launch
  extern __shared__ float sTile[];

  const int TX = static_cast<int>(threadIdx.x);
  const int TY = static_cast<int>(threadIdx.y);
  const int TILE_W = static_cast<int>(blockDim.x);
  const int TILE_H = static_cast<int>(blockDim.y);
  const int SHARED_W = TILE_W + 2 * radius;

  // Output pixel coordinates
  const int OUT_X = static_cast<int>(blockIdx.x) * TILE_W + TX;
  const int OUT_Y = static_cast<int>(blockIdx.y) * TILE_H + TY;

  // Top-left corner of the shared memory region in image space
  const int SHARED_ORIGIN_X = static_cast<int>(blockIdx.x) * TILE_W - radius;
  const int SHARED_ORIGIN_Y = static_cast<int>(blockIdx.y) * TILE_H - radius;

  // Cooperatively load shared memory tile (including halo)
  // Each thread may need to load multiple elements if shared tile > blockDim
  const int SHARED_H = TILE_H + 2 * radius;
  const int SHARED_SIZE = SHARED_W * SHARED_H;
  const int THREAD_COUNT = TILE_W * TILE_H;
  const int THREAD_ID = TY * TILE_W + TX;

  for (int idx = THREAD_ID; idx < SHARED_SIZE; idx += THREAD_COUNT) {
    const int SY = idx / SHARED_W;
    const int SX = idx % SHARED_W;

    // Image coordinates with clamp-to-edge
    int imgX = SHARED_ORIGIN_X + SX;
    int imgY = SHARED_ORIGIN_Y + SY;
    if (imgX < 0) {
      imgX = 0;
    }
    if (imgX >= width) {
      imgX = width - 1;
    }
    if (imgY < 0) {
      imgY = 0;
    }
    if (imgY >= height) {
      imgY = height - 1;
    }

    sTile[SY * SHARED_W + SX] = input[imgY * width + imgX];
  }

  __syncthreads();

  // Compute convolution for this output pixel
  if (OUT_X < width && OUT_Y < height) {
    float sum = 0.0f;
    const int DIAM = 2 * radius + 1;

    for (int ky = 0; ky < DIAM; ++ky) {
      for (int kx = 0; kx < DIAM; ++kx) {
        const float PIXEL = sTile[(TY + ky) * SHARED_W + (TX + kx)];
        sum += PIXEL * cKernel[ky * DIAM + kx];
      }
    }

    output[OUT_Y * width + OUT_X] = sum;
  }
}

/* ----------------------------- Separable Row Kernel ----------------------------- */

/**
 * Horizontal 1D convolution pass. Each block processes K_SEP_TILE_W x K_SEP_TILE_H
 * output pixels. Shared memory holds the tile plus left/right halo for the row.
 * Threads read coalesced across the row dimension.
 */
__global__ void conv1dRowKernel(const float* SIM_RESTRICT input, int width, int height, int radius,
                                float* SIM_RESTRICT output) {
  extern __shared__ float sRow[];

  const int TX = static_cast<int>(threadIdx.x);
  const int TY = static_cast<int>(threadIdx.y);
  const int TILE_W = static_cast<int>(blockDim.x);
  const int SHARED_W = TILE_W + 2 * radius;

  const int OUT_X = static_cast<int>(blockIdx.x) * TILE_W + TX;
  const int OUT_Y = static_cast<int>(blockIdx.y) * static_cast<int>(blockDim.y) + TY;

  if (OUT_Y >= height) {
    return;
  }

  // Shared memory row for this threadIdx.y
  float* myRow = sRow + TY * SHARED_W;

  // Cooperative load: tile + left/right halo
  const int ROW_ORIGIN_X = static_cast<int>(blockIdx.x) * TILE_W - radius;

  for (int i = TX; i < SHARED_W; i += TILE_W) {
    int imgX = ROW_ORIGIN_X + i;
    if (imgX < 0) {
      imgX = 0;
    }
    if (imgX >= width) {
      imgX = width - 1;
    }
    myRow[i] = input[OUT_Y * width + imgX];
  }

  __syncthreads();

  // Compute 1D horizontal convolution
  if (OUT_X < width) {
    float sum = 0.0f;
    const int DIAM = 2 * radius + 1;
    for (int k = 0; k < DIAM; ++k) {
      sum += myRow[TX + k] * cKernel1D[k];
    }
    output[OUT_Y * width + OUT_X] = sum;
  }
}

/* ----------------------------- Separable Column Kernel ----------------------------- */

/**
 * Vertical 1D convolution pass. Each block processes K_SEP_TILE_H x K_SEP_TILE_W
 * output pixels (transposed layout for coalesced writes). Shared memory holds
 * column strips plus top/bottom halo.
 */
__global__ void conv1dColKernel(const float* SIM_RESTRICT input, int width, int height, int radius,
                                float* SIM_RESTRICT output) {
  extern __shared__ float sCol[];

  const int TX = static_cast<int>(threadIdx.x);
  const int TY = static_cast<int>(threadIdx.y);
  const int TILE_H = static_cast<int>(blockDim.y);
  const int TILE_W = static_cast<int>(blockDim.x);
  const int SHARED_H = TILE_H + 2 * radius;

  const int OUT_X = static_cast<int>(blockIdx.x) * TILE_W + TX;
  const int OUT_Y = static_cast<int>(blockIdx.y) * TILE_H + TY;

  if (OUT_X >= width) {
    return;
  }

  // Shared memory column for this threadIdx.x
  // Layout: sCol[ty * TILE_W + tx] for coalesced access
  const int COL_ORIGIN_Y = static_cast<int>(blockIdx.y) * TILE_H - radius;

  for (int i = TY; i < SHARED_H; i += TILE_H) {
    int imgY = COL_ORIGIN_Y + i;
    if (imgY < 0) {
      imgY = 0;
    }
    if (imgY >= height) {
      imgY = height - 1;
    }
    sCol[i * TILE_W + TX] = input[imgY * width + OUT_X];
  }

  __syncthreads();

  // Compute 1D vertical convolution
  if (OUT_Y < height) {
    float sum = 0.0f;
    const int DIAM = 2 * radius + 1;
    for (int k = 0; k < DIAM; ++k) {
      sum += sCol[(TY + k) * TILE_W + TX] * cKernel1D[k];
    }
    output[OUT_Y * width + OUT_X] = sum;
  }
}

#endif // COMPAT_CUDA_AVAILABLE

/* ----------------------------- API ----------------------------- */

bool convSetKernel(const float* hKernel, std::uint32_t radius) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)hKernel;
  (void)radius;
  return false;
#else
  if (hKernel == nullptr || radius > CONV_MAX_KERNEL_RADIUS) {
    return false;
  }

  const std::uint32_t DIAM = 2 * radius + 1;
  const std::size_t SIZE = DIAM * DIAM * sizeof(float);
  return ::apex::compat::cuda::isSuccess(
      cudaMemcpyToSymbol(cKernel, hKernel, SIZE, 0, cudaMemcpyHostToDevice));
#endif
}

bool conv2dCuda(const float* dInput, std::uint32_t width, std::uint32_t height,
                std::uint32_t radius, float* dOutput, void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)width;
  (void)height;
  (void)radius;
  (void)dOutput;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dOutput == nullptr || width == 0 || height == 0 ||
      radius > CONV_MAX_KERNEL_RADIUS) {
    return false;
  }

  const dim3 BLOCK(K_TILE_DIM, K_TILE_DIM);
  const dim3 GRID((width + K_TILE_DIM - 1) / K_TILE_DIM, (height + K_TILE_DIM - 1) / K_TILE_DIM);

  const int SHARED_W = K_TILE_DIM + 2 * static_cast<int>(radius);
  const int SHARED_H = K_TILE_DIM + 2 * static_cast<int>(radius);
  const std::size_t SHARED_MEM = static_cast<std::size_t>(SHARED_W) * SHARED_H * sizeof(float);

  auto s = static_cast<cudaStream_t>(stream);
  conv2dKernel<<<GRID, BLOCK, SHARED_MEM, s>>>(
      dInput, static_cast<int>(width), static_cast<int>(height), static_cast<int>(radius), dOutput);
  return ::apex::compat::cuda::isSuccess(cudaGetLastError());
#endif
}

bool convSetKernel1D(const float* hKernel1D, std::uint32_t radius) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)hKernel1D;
  (void)radius;
  return false;
#else
  if (hKernel1D == nullptr || radius > CONV_MAX_KERNEL_RADIUS) {
    return false;
  }

  const std::uint32_t DIAM = 2 * radius + 1;
  const std::size_t SIZE = DIAM * sizeof(float);
  return ::apex::compat::cuda::isSuccess(
      cudaMemcpyToSymbol(cKernel1D, hKernel1D, SIZE, 0, cudaMemcpyHostToDevice));
#endif
}

bool conv2dSeparableCuda(const float* dInput, std::uint32_t width, std::uint32_t height,
                         std::uint32_t radius, float* dTemp, float* dOutput,
                         void* stream) noexcept {
#if !COMPAT_CUDA_AVAILABLE
  (void)dInput;
  (void)width;
  (void)height;
  (void)radius;
  (void)dTemp;
  (void)dOutput;
  (void)stream;
  return false;
#else
  if (dInput == nullptr || dTemp == nullptr || dOutput == nullptr || width == 0 || height == 0 ||
      radius > CONV_MAX_KERNEL_RADIUS) {
    return false;
  }

  auto s = static_cast<cudaStream_t>(stream);
  const int R = static_cast<int>(radius);

  // Pass 1: Horizontal convolution (input -> temp)
  {
    const dim3 BLOCK(K_SEP_TILE_W, K_SEP_TILE_H);
    const dim3 GRID((static_cast<int>(width) + K_SEP_TILE_W - 1) / K_SEP_TILE_W,
                    (static_cast<int>(height) + K_SEP_TILE_H - 1) / K_SEP_TILE_H);
    const int SHARED_W = K_SEP_TILE_W + 2 * R;
    const std::size_t SHARED_MEM =
        static_cast<std::size_t>(K_SEP_TILE_H) * SHARED_W * sizeof(float);

    conv1dRowKernel<<<GRID, BLOCK, SHARED_MEM, s>>>(dInput, static_cast<int>(width),
                                                    static_cast<int>(height), R, dTemp);
    if (!::apex::compat::cuda::isSuccess(cudaGetLastError())) {
      return false;
    }
  }

  // Pass 2: Vertical convolution (temp -> output)
  {
    const dim3 BLOCK(K_SEP_TILE_W, K_SEP_TILE_H);
    const dim3 GRID((static_cast<int>(width) + K_SEP_TILE_W - 1) / K_SEP_TILE_W,
                    (static_cast<int>(height) + K_SEP_TILE_H - 1) / K_SEP_TILE_H);
    const int SHARED_H = K_SEP_TILE_H + 2 * R;
    const std::size_t SHARED_MEM =
        static_cast<std::size_t>(SHARED_H) * K_SEP_TILE_W * sizeof(float);

    conv1dColKernel<<<GRID, BLOCK, SHARED_MEM, s>>>(dTemp, static_cast<int>(width),
                                                    static_cast<int>(height), R, dOutput);
    if (!::apex::compat::cuda::isSuccess(cudaGetLastError())) {
      return false;
    }
  }

  return true;
#endif
}

void generateGaussianKernel1D(float* hKernel1D, std::uint32_t radius, float sigma) noexcept {
  const int DIAM = static_cast<int>(2 * radius + 1);
  const int R = static_cast<int>(radius);
  float sum = 0.0f;

  for (int x = -R; x <= R; ++x) {
    const float VAL = std::exp(-(static_cast<float>(x * x)) / (2.0f * sigma * sigma));
    hKernel1D[x + R] = VAL;
    sum += VAL;
  }

  if (sum > 0.0f) {
    const float INV_SUM = 1.0f / sum;
    for (int i = 0; i < DIAM; ++i) {
      hKernel1D[i] *= INV_SUM;
    }
  }
}

void generateGaussianKernel(float* hKernel, std::uint32_t radius, float sigma) noexcept {
  const int DIAM = static_cast<int>(2 * radius + 1);
  const int R = static_cast<int>(radius);
  float sum = 0.0f;

  for (int y = -R; y <= R; ++y) {
    for (int x = -R; x <= R; ++x) {
      const float VAL = std::exp(-(static_cast<float>(x * x + y * y)) / (2.0f * sigma * sigma));
      hKernel[(y + R) * DIAM + (x + R)] = VAL;
      sum += VAL;
    }
  }

  // Normalize
  if (sum > 0.0f) {
    const float INV_SUM = 1.0f / sum;
    for (int i = 0; i < DIAM * DIAM; ++i) {
      hKernel[i] *= INV_SUM;
    }
  }
}

void generateBoxKernel(float* hKernel, std::uint32_t radius) noexcept {
  const std::uint32_t DIAM = 2 * radius + 1;
  const float VAL = 1.0f / static_cast<float>(DIAM * DIAM);
  for (std::uint32_t i = 0; i < DIAM * DIAM; ++i) {
    hKernel[i] = VAL;
  }
}

} // namespace cuda
} // namespace gpu_compute
} // namespace sim
