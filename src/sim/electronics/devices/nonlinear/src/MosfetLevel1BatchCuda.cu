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

/* ----------------------------- SoA Output Kernel ----------------------------- */

/**
 * @brief Per-device params, SoA output. Each thread writes one double
 *        into each of three aligned parallel arrays.
 */
__global__ __launch_bounds__(256, 6) void kStampBatchPerDeviceSoA(
    const MosfetBias* __restrict__ biases, const MosfetLevel1Params* __restrict__ params,
    double* __restrict__ idOut, double* __restrict__ gmOut, double* __restrict__ gdsOut,
    int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }
  const MosfetBias b = biases[i];
  const MosfetLevel1Params p = params[i];
  const auto sv = MosfetLevel1::stampValues(b.vgs, b.vds, p);
  idOut[i] = sv.id;
  gmOut[i] = sv.gm;
  gdsOut[i] = sv.gds;
}

/* ----------------------------- Fused Stamp + Scatter ----------------------------- */

/**
 * @brief Per-thread: evaluate stampValues and atomically scatter 10
 *        matrix contributions + 2 RHS contributions.
 *
 * Mirrors the CPU `Intel4004GridLevel1::stampTransistorsLevel1` Level 1
 * stamp pattern (PMOS in NMOS-mirror convention): mode selection by
 * sign of VDS, gds symmetric, gm coupling distributed by xnrm/xrev,
 * Norton-equivalent current `cdreq` into drain/source.
 */
__device__ __forceinline__ void atomicAddIfNode(double* A, int row, int col, int netCount,
                                                double value) {
  if (row <= 0 || col <= 0 || row >= netCount || col >= netCount) {
    return; // Skip ground and out-of-range (ground is net 0).
  }
  atomicAdd(&A[row * netCount + col], value);
}

__device__ __forceinline__ void atomicAddIfRow(double* I, int row, int netCount, double value) {
  if (row <= 0 || row >= netCount) {
    return;
  }
  atomicAdd(&I[row], value);
}

__global__ __launch_bounds__(256, 6) void kStampMosfetL1Batch(
    const MosfetBias* __restrict__ biases, const MosfetLevel1Params* __restrict__ params,
    const MosfetNets* __restrict__ nets, double* __restrict__ G, double* __restrict__ I,
    int count, int netCount, double gmin) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }

  const MosfetBias b = biases[i];
  const MosfetLevel1Params p = params[i];
  const MosfetNets n = nets[i];

  // SPICE mode selection by VDS sign. VSG / VSD are already the
  // NMOS-mirror-convention inputs the CPU path passes in (bias is
  // computed by the host before the launch).
  const double vsg = b.vgs; // Here: vgs == VSG in the PMOS mirror.
  const double vsd = b.vds; // And: vds == VSD.

  int xnrm, xrev;
  double evalVgs, evalVds;
  if (vsd >= 0.0) {
    xnrm = 1;
    xrev = 0;
    evalVgs = vsg;
    evalVds = vsd;
  } else {
    xnrm = 0;
    xrev = 1;
    evalVgs = vsg - vsd; // VD - VG = VSG - VSD (in the mirror frame).
    evalVds = -vsd;
  }

  const double vgsEval = fmax(evalVgs, 0.0);
  const double vdsEval = fmax(evalVds, 0.0);
  const auto sv = MosfetLevel1::stampValues(vgsEval, vdsEval, p);
  const double id = sv.id;
  const double gm = sv.gm;
  const double gdsDev = sv.gds;
  const double gdsStamp = fmax(gdsDev, gmin);

  // Compensation current: CPU uses cdreq = -(id - gds*VSD - gm*VSG) for
  // xnrm==1, else cdreq = (id - gds*(-VSD) - gm*(VD-VG)). Match exactly.
  double cdreq;
  if (xnrm == 1) {
    cdreq = -(id - gdsDev * vsd - gm * vsg);
  } else {
    cdreq = (id - gdsDev * (-vsd) - gm * (vsg - vsd));
  }

  const int d = n.drain;
  const int g = n.gate;
  const int s = n.source;

  // Symmetric gds (matches addConductance(d, s, gdsStamp)).
  atomicAddIfNode(G, d, d, netCount, gdsStamp);
  atomicAddIfNode(G, s, s, netCount, gdsStamp);
  atomicAddIfNode(G, d, s, netCount, -gdsStamp);
  atomicAddIfNode(G, s, d, netCount, -gdsStamp);

  // gm coupling, xnrm/xrev distribution.
  const double xrevGm = xrev * gm;
  const double xnrmGm = xnrm * gm;
  const double xDelta = (xnrm - xrev) * gm;
  atomicAddIfNode(G, d, d, netCount, xrevGm);
  atomicAddIfNode(G, s, s, netCount, xnrmGm);
  atomicAddIfNode(G, d, g, netCount, xDelta);
  atomicAddIfNode(G, d, s, netCount, -xnrmGm);
  atomicAddIfNode(G, s, g, netCount, -xDelta);
  atomicAddIfNode(G, s, d, netCount, -xrevGm);

  // RHS: I[drain] -= cdreq ; I[source] += cdreq (matches addCurrent(d, s, -cdreq)).
  atomicAddIfRow(I, d, netCount, -cdreq);
  atomicAddIfRow(I, s, netCount, cdreq);
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

bool evalStampBatchSoA(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                       double* dId, double* dGm, double* dGds, std::size_t count,
                       void* stream) noexcept {
  if (dBiases == nullptr || dParams == nullptr || dId == nullptr || dGm == nullptr ||
      dGds == nullptr || count == 0) {
    return false;
  }
  const int threads = BLOCK_SIZE;
  const int blocks = static_cast<int>((count + threads - 1) / threads);
  const cudaStream_t s = static_cast<cudaStream_t>(stream);
  kStampBatchPerDeviceSoA<<<blocks, threads, 0, s>>>(dBiases, dParams, dId, dGm, dGds,
                                                     static_cast<int>(count));
  return cudaPeekAtLastError() == cudaSuccess;
}

bool stampMosfetL1Batch(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                        const MosfetNets* dNets, double* dG, double* dI, std::size_t count,
                        std::size_t netCount, double gmin, void* stream) noexcept {
  if (dBiases == nullptr || dParams == nullptr || dNets == nullptr || dG == nullptr ||
      dI == nullptr || count == 0 || netCount == 0) {
    return false;
  }
  const int threads = BLOCK_SIZE;
  const int blocks = static_cast<int>((count + threads - 1) / threads);
  const cudaStream_t s = static_cast<cudaStream_t>(stream);
  kStampMosfetL1Batch<<<blocks, threads, 0, s>>>(dBiases, dParams, dNets, dG, dI,
                                                 static_cast<int>(count),
                                                 static_cast<int>(netCount), gmin);
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
