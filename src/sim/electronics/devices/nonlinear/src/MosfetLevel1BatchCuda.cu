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
__global__ __launch_bounds__(256, 6) void kStampBatchUniform(const MosfetBias* __restrict__ biases,
                                                             MosfetLevel1Params params,
                                                             MosfetStamp* __restrict__ stamps,
                                                             int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }
  const MosfetBias B = biases[i];
  const auto SV = MosfetLevel1::stampValues(B.vgs, B.vds, params);
  MosfetStamp out;
  out.id = SV.id;
  out.gm = SV.gm;
  out.gds = SV.gds;
  stamps[i] = out;
}

/* ----------------------------- Per-Device-Params Kernel ----------------------------- */

/**
 * @brief One thread per device. Params read from global memory per thread.
 */
__global__
__launch_bounds__(256, 6) void kStampBatchPerDevice(const MosfetBias* __restrict__ biases,
                                                    const MosfetLevel1Params* __restrict__ params,
                                                    MosfetStamp* __restrict__ stamps, int count) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }
  const MosfetBias B = biases[i];
  const MosfetLevel1Params P = params[i]; // coalesced 32B / thread
  const auto SV = MosfetLevel1::stampValues(B.vgs, B.vds, P);
  MosfetStamp out;
  out.id = SV.id;
  out.gm = SV.gm;
  out.gds = SV.gds;
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
  const MosfetBias B = biases[i];
  const MosfetLevel1Params P = params[i];
  const auto SV = MosfetLevel1::stampValues(B.vgs, B.vds, P);
  idOut[i] = SV.id;
  gmOut[i] = SV.gm;
  gdsOut[i] = SV.gds;
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
    const MosfetNets* __restrict__ nets, double* __restrict__ G, double* __restrict__ I, int count,
    int netCount, double gmin) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) {
    return;
  }

  const MosfetBias B = biases[i];
  const MosfetLevel1Params P = params[i];
  const MosfetNets n = nets[i];

  // SPICE mode selection by VDS sign. VSG / VSD are already the
  // NMOS-mirror-convention inputs the CPU path passes in (bias is
  // computed by the host before the launch).
  const double VSG = B.vgs; // Here: vgs == VSG in the PMOS mirror.
  const double VSD = B.vds; // And: vds == VSD.

  int xnrm, xrev;
  double evalVgs, evalVds;
  if (VSD >= 0.0) {
    xnrm = 1;
    xrev = 0;
    evalVgs = VSG;
    evalVds = VSD;
  } else {
    xnrm = 0;
    xrev = 1;
    evalVgs = VSG - VSD; // VD - VG = VSG - VSD (in the mirror frame).
    evalVds = -VSD;
  }

  const double VGS_EVAL = fmax(evalVgs, 0.0);
  const double VDS_EVAL = fmax(evalVds, 0.0);
  const auto SV = MosfetLevel1::stampValues(VGS_EVAL, VDS_EVAL, P);
  const double ID = SV.id;
  const double GM = SV.gm;
  const double GDS_DEV = SV.gds;
  const double GDS_STAMP = fmax(GDS_DEV, gmin);

  // Compensation current: CPU uses cdreq = -(ID - gds*VSD - GM*VSG) for
  // xnrm==1, else cdreq = (ID - gds*(-VSD) - GM*(VD-VG)). Match exactly.
  double cdreq;
  if (xnrm == 1) {
    cdreq = -(ID - GDS_DEV * VSD - GM * VSG);
  } else {
    cdreq = (ID - GDS_DEV * (-VSD) - GM * (VSG - VSD));
  }

  const int D = n.drain;
  const int GATE = n.gate;
  const int S = n.source;

  // Symmetric gds (matches addConductance(D, S, GDS_STAMP)).
  atomicAddIfNode(G, D, D, netCount, GDS_STAMP);
  atomicAddIfNode(G, S, S, netCount, GDS_STAMP);
  atomicAddIfNode(G, D, S, netCount, -GDS_STAMP);
  atomicAddIfNode(G, S, D, netCount, -GDS_STAMP);

  // GM coupling, xnrm/xrev distribution.
  const double XREV_GM = xrev * GM;
  const double XNRM_GM = xnrm * GM;
  const double X_DELTA = (xnrm - xrev) * GM;
  atomicAddIfNode(G, D, D, netCount, XREV_GM);
  atomicAddIfNode(G, S, S, netCount, XNRM_GM);
  atomicAddIfNode(G, D, GATE, netCount, X_DELTA);
  atomicAddIfNode(G, D, S, netCount, -XNRM_GM);
  atomicAddIfNode(G, S, GATE, netCount, -X_DELTA);
  atomicAddIfNode(G, S, D, netCount, -XREV_GM);

  // RHS: I[drain] -= cdreq ; I[source] += cdreq (matches addCurrent(D, S, -cdreq)).
  atomicAddIfRow(I, D, netCount, -cdreq);
  atomicAddIfRow(I, S, netCount, cdreq);
}

/* ----------------------------- NR update + convergence reduction ----------------------------- */

/**
 * @brief Block-level reduction of max(|newV[i] - prevV[i]|), with
 *        grid-level aggregation via `atomicMax` on double bits.
 *
 * The output `dMaxDelta` must be initialised to 0.0 before launch.
 */
__device__ __forceinline__ void atomicMaxDouble(double* addr, double value) {
  unsigned long long* asUll = reinterpret_cast<unsigned long long*>(addr);
  unsigned long long oldBits = *asUll;
  unsigned long long expected;
  do {
    expected = oldBits;
    double current = __longlong_as_double(static_cast<long long>(expected));
    if (current >= value)
      return;
    unsigned long long desired = static_cast<unsigned long long>(__double_as_longlong(value));
    oldBits = atomicCAS(asUll, expected, desired);
  } while (oldBits != expected);
}

__global__ __launch_bounds__(256, 6) void kNrMaxDelta(const double* __restrict__ newV,
                                                      const double* __restrict__ prevV,
                                                      double* maxDelta, int n) {
  extern __shared__ double sdata[];
  const int TID = threadIdx.x;
  const int i = blockIdx.x * blockDim.x + TID;

  double my = 0.0;
  if (i < n) {
    my = fabs(newV[i] - prevV[i]);
  }
  sdata[TID] = my;
  __syncthreads();

  for (int offset = blockDim.x / 2; offset > 0; offset >>= 1) {
    if (TID < offset) {
      double other = sdata[TID + offset];
      if (other > sdata[TID])
        sdata[TID] = other;
    }
    __syncthreads();
  }
  if (TID == 0) {
    atomicMaxDouble(maxDelta, sdata[0]);
  }
}

/**
 * @brief Apply NR limiter and write the updated voltages into prevV.
 *
 * Reads `maxDelta` (populated by `kNrMaxDelta`) to decide the scale
 * factor: if the unlimited max change exceeds `limit`, all deltas are
 * shrunk uniformly so the largest is exactly `limit`; otherwise no
 * scaling is applied.
 */
__global__ void kNrApplyLimit(const double* __restrict__ newV, double* prevV,
                              const double* maxDelta, int n, double limit) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n)
    return;
  const double M = *maxDelta;
  const double SCALE = (M > limit) ? (limit / M) : 1.0;
  const double DELTA = newV[i] - prevV[i];
  prevV[i] += SCALE * DELTA;
}

} // namespace

/* ----------------------------- Host-Side Launch Wrappers ----------------------------- */

bool evalStampBatchUniform(const MosfetBias* dBiases, const MosfetLevel1Params& params,
                           MosfetStamp* dStamps, std::size_t count, void* stream) noexcept {
  if (dBiases == nullptr || dStamps == nullptr || count == 0) {
    return false;
  }
  const int THREADS = BLOCK_SIZE;
  const int BLOCKS = static_cast<int>((count + THREADS - 1) / THREADS);
  const cudaStream_t S = static_cast<cudaStream_t>(stream);
  kStampBatchUniform<<<BLOCKS, THREADS, 0, S>>>(dBiases, params, dStamps, static_cast<int>(count));
  return cudaPeekAtLastError() == cudaSuccess;
}

bool evalStampBatch(const MosfetBias* dBiases, const MosfetLevel1Params* dParams,
                    MosfetStamp* dStamps, std::size_t count, void* stream) noexcept {
  if (dBiases == nullptr || dParams == nullptr || dStamps == nullptr || count == 0) {
    return false;
  }
  const int THREADS = BLOCK_SIZE;
  const int BLOCKS = static_cast<int>((count + THREADS - 1) / THREADS);
  const cudaStream_t S = static_cast<cudaStream_t>(stream);
  kStampBatchPerDevice<<<BLOCKS, THREADS, 0, S>>>(dBiases, dParams, dStamps,
                                                  static_cast<int>(count));
  return cudaPeekAtLastError() == cudaSuccess;
}

bool evalStampBatchSoA(const MosfetBias* dBiases, const MosfetLevel1Params* dParams, double* dId,
                       double* dGm, double* dGds, std::size_t count, void* stream) noexcept {
  if (dBiases == nullptr || dParams == nullptr || dId == nullptr || dGm == nullptr ||
      dGds == nullptr || count == 0) {
    return false;
  }
  const int THREADS = BLOCK_SIZE;
  const int BLOCKS = static_cast<int>((count + THREADS - 1) / THREADS);
  const cudaStream_t S = static_cast<cudaStream_t>(stream);
  kStampBatchPerDeviceSoA<<<BLOCKS, THREADS, 0, S>>>(dBiases, dParams, dId, dGm, dGds,
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
  const int THREADS = BLOCK_SIZE;
  const int BLOCKS = static_cast<int>((count + THREADS - 1) / THREADS);
  const cudaStream_t S = static_cast<cudaStream_t>(stream);
  kStampMosfetL1Batch<<<BLOCKS, THREADS, 0, S>>>(
      dBiases, dParams, dNets, dG, dI, static_cast<int>(count), static_cast<int>(netCount), gmin);
  return cudaPeekAtLastError() == cudaSuccess;
}

bool nrUpdateAndLimit(const double* dNewV, double* dPrevV, double* dMaxDelta, std::size_t n,
                      double limit, void* stream) noexcept {
  if (dNewV == nullptr || dPrevV == nullptr || dMaxDelta == nullptr || n == 0) {
    return false;
  }
  const int THREADS = BLOCK_SIZE;
  const int BLOCKS = static_cast<int>((n + THREADS - 1) / THREADS);
  const cudaStream_t S = static_cast<cudaStream_t>(stream);

  // Reset max-delta accumulator, then reduce.
  if (cudaMemsetAsync(dMaxDelta, 0, sizeof(double), S) != cudaSuccess)
    return false;
  kNrMaxDelta<<<BLOCKS, THREADS, THREADS * sizeof(double), S>>>(dNewV, dPrevV, dMaxDelta,
                                                                static_cast<int>(n));
  if (cudaPeekAtLastError() != cudaSuccess)
    return false;

  kNrApplyLimit<<<BLOCKS, THREADS, 0, S>>>(dNewV, dPrevV, dMaxDelta, static_cast<int>(n), limit);
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
  if (dBiases_ != nullptr)
    cudaFree(dBiases_);
  if (dParams_ != nullptr)
    cudaFree(dParams_);
  if (dStamps_ != nullptr)
    cudaFree(dStamps_);
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
