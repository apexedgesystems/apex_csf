/**
 * @file MosfetLevel1BatchCuda.cu
 * @brief Batch MOSFET Level 1 stamp evaluation on the GPU.
 *
 * Implements the kernels declared in MosfetLevel1BatchCuda.cuh. The
 * per-device math (`MosfetLevel1::stampValues`) is re-used verbatim from
 * the CPU path via SIM_HD -- single source of truth for the Shichman-
 * Hodges three-region MOSFET model.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"

#include <cuda_runtime.h>

namespace sim::electronics::devices::nonlinear::cuda {

namespace {

constexpr int BLOCK_SIZE = 256;

/* ----------------------------- Uniform-Params Kernel ----------------------------- */

/**
 * @brief One thread per device. Params passed by value in a register.
 */
__global__ __launch_bounds__(256, 6) void kStampBatchUniform(
    const MosfetBias* __restrict__ biases, MosfetLevel1Params params,
    MosfetStamp* __restrict__ stamps, int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }
  const MosfetBias b = biases[i];
  const auto sv = MosfetLevel1::stampValues(b.vgs, b.vds, params);
  MosfetStamp out;
  out.id = sv.id;
  out.gm = sv.gm;
  out.gds = sv.gds;
  stamps[i] = out;
}

/* ----------------------------- Per-Device-Params Kernel ----------------------------- */

/**
 * @brief One thread per device. Params read from global memory per thread.
 */
__global__ __launch_bounds__(256, 6) void kStampBatchPerDevice(
    const MosfetBias* __restrict__ biases, const MosfetLevel1Params* __restrict__ params,
    MosfetStamp* __restrict__ stamps, int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }
  const MosfetBias b = biases[i];
  const MosfetLevel1Params p = params[i]; // coalesced 32B / thread
  const auto sv = MosfetLevel1::stampValues(b.vgs, b.vds, p);
  MosfetStamp out;
  out.id = sv.id;
  out.gm = sv.gm;
  out.gds = sv.gds;
  stamps[i] = out;
}

} // namespace

/* ----------------------------- Host-Side Launch Wrappers ----------------------------- */

bool evalStampBatchUniform(const MosfetBias* dBiases, const MosfetLevel1Params& params,
                           MosfetStamp* dStamps, std::size_t count, void* stream) noexcept {
  if (dBiases == nullptr || dStamps == nullptr || count == 0) {
    return false;
  }
  const int threads = BLOCK_SIZE;
  const int blocks = static_cast<int>((count + threads - 1) / threads);
  const cudaStream_t s = static_cast<cudaStream_t>(stream);
  kStampBatchUniform<<<blocks, threads, 0, s>>>(dBiases, params, dStamps,
                                                static_cast<int>(count));
  return cudaPeekAtLastError() == cudaSuccess;
}

bool evalStampBatch(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                    MosfetStamp* dStamps, std::size_t count, void* stream) noexcept {
  if (dBiases == nullptr || dParams == nullptr || dStamps == nullptr || count == 0) {
    return false;
  }
  const int threads = BLOCK_SIZE;
  const int blocks = static_cast<int>((count + threads - 1) / threads);
  const cudaStream_t s = static_cast<cudaStream_t>(stream);
  kStampBatchPerDevice<<<blocks, threads, 0, s>>>(dBiases, dParams, dStamps,
                                                  static_cast<int>(count));
  return cudaPeekAtLastError() == cudaSuccess;
}

MosfetBatchConfig getLaunchConfig(std::size_t count) noexcept {
  MosfetBatchConfig c;
  c.blockSize = BLOCK_SIZE;
  c.gridSize = (count + BLOCK_SIZE - 1) / BLOCK_SIZE;
  return c;
}

/* ----------------------------- MosfetStampDriver ----------------------------- */

MosfetStampDriver::MosfetStampDriver(std::size_t maxCount) noexcept : maxCount_(maxCount) {
  if (maxCount == 0) {
    return;
  }
  if (cudaMalloc(&dBiases_, maxCount * sizeof(MosfetBias)) != cudaSuccess) {
    dBiases_ = nullptr;
    return;
  }
  if (cudaMalloc(&dParams_, maxCount * sizeof(MosfetLevel1Params)) != cudaSuccess) {
    cudaFree(dBiases_);
    dBiases_ = nullptr;
    dParams_ = nullptr;
    return;
  }
  if (cudaMalloc(&dStamps_, maxCount * sizeof(MosfetStamp)) != cudaSuccess) {
    cudaFree(dBiases_);
    cudaFree(dParams_);
    dBiases_ = nullptr;
    dParams_ = nullptr;
    dStamps_ = nullptr;
    return;
  }
}

MosfetStampDriver::~MosfetStampDriver() noexcept {
  if (dBiases_ != nullptr) cudaFree(dBiases_);
  if (dParams_ != nullptr) cudaFree(dParams_);
  if (dStamps_ != nullptr) cudaFree(dStamps_);
}

bool MosfetStampDriver::ready() const noexcept {
  return dBiases_ != nullptr && dParams_ != nullptr && dStamps_ != nullptr;
}

bool MosfetStampDriver::setParams(const MosfetLevel1Params* hostParams,
                                  std::size_t count) noexcept {
  if (!ready() || count == 0 || count > maxCount_ || hostParams == nullptr) {
    return false;
  }
  if (cudaMemcpy(dParams_, hostParams, count * sizeof(MosfetLevel1Params),
                 cudaMemcpyHostToDevice) != cudaSuccess) {
    return false;
  }
  paramsCount_ = count;
  return true;
}

bool MosfetStampDriver::evalBatch(const MosfetBias* hostBiases, MosfetStamp* hostStamps,
                                  std::size_t count) noexcept {
  if (!ready() || count == 0 || count > maxCount_ || count != paramsCount_) {
    return false;
  }
  if (hostBiases == nullptr || hostStamps == nullptr) {
    return false;
  }
  if (cudaMemcpy(dBiases_, hostBiases, count * sizeof(MosfetBias), cudaMemcpyHostToDevice) !=
      cudaSuccess) {
    return false;
  }
  if (!evalStampBatch(static_cast<const MosfetBias*>(dBiases_),
                      static_cast<const MosfetLevel1Params*>(dParams_),
                      static_cast<MosfetStamp*>(dStamps_), count)) {
    return false;
  }
  if (cudaDeviceSynchronize() != cudaSuccess) {
    return false;
  }
  if (cudaMemcpy(hostStamps, dStamps_, count * sizeof(MosfetStamp), cudaMemcpyDeviceToHost) !=
      cudaSuccess) {
    return false;
  }
  return true;
}

} // namespace sim::electronics::devices::nonlinear::cuda
