/**
 * @file NonlinearDeviceCuda.cu
 * @brief CUDA kernels for parallel nonlinear device evaluation.
 *
 * Evaluates device I-V characteristics and conductances in parallel on GPU.
 * Each thread evaluates one device independently (embarrassingly parallel).
 *
 * Usage:
 * 1. Copy device parameters to GPU memory
 * 2. Launch evaluateDevicesKernel with one thread per device
 * 3. Copy results (currents, conductances) back to host
 * 4. Use results for Newton-Raphson linearization
 *
 * RT-safety: NOT RT-safe (GPU memory allocation and kernel launch overhead).
 * Performance: ~100x speedup for circuits with 10,000+ devices.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NonlinearDeviceCuda.cuh"

#include <cuda_runtime.h>

namespace sim::electronics::algorithms::nonlinear::cuda {

/* ----------------------------- Device Evaluation Kernels ----------------------------- */

/**
 * @brief CUDA kernel: Evaluate nonlinear devices in parallel.
 *
 * Each thread evaluates one device's current and conductance at the given
 * terminal voltage. This is the core of GPU-accelerated Newton-Raphson.
 *
 * @param deviceParams Device parameters (type, nets, model params).
 * @param nodeVoltages Node voltages (read-only).
 * @param currents Output: device currents.
 * @param conductances Output: device conductances.
 * @param n Number of devices.
 */
__global__ __launch_bounds__(256, 6) void evaluateDevicesKernel(
    const DeviceParams* deviceParams, const double* nodeVoltages, double* currents,
    double* conductances, int n) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= n) {
    return;
  }

  const DeviceParams& dev = deviceParams[idx];

  // Compute terminal voltage
  double vPos = nodeVoltages[dev.posNet];
  double vNeg = nodeVoltages[dev.negNet];
  double vTerminal = vPos - vNeg;

  // Evaluate device based on type
  double i = 0.0;
  double g = 0.0;

  switch (dev.type) {
  case DeviceType::DIODE:
    // Shockley equation: I = Is * (exp(V/Vt) - 1)
    {
      double expArg = fmin(vTerminal / dev.params[1], 40.0); // Limit to prevent overflow
      double expVal = exp(expArg);
      i = dev.params[0] * (expVal - 1.0);           // Is * (exp(V/Vt) - 1)
      g = (dev.params[0] / dev.params[1]) * expVal; // (Is/Vt) * exp(V/Vt)
    }
    break;

  case DeviceType::NONLINEAR_RESISTOR:
    // Cubic: I = G0*V + alpha*V^3
    {
      double G0 = dev.params[0];
      double alpha = dev.params[1];
      double V2 = vTerminal * vTerminal;
      double V3 = V2 * vTerminal;
      i = G0 * vTerminal + alpha * V3; // G0*V + alpha*V^3
      g = G0 + 3.0 * alpha * V2;       // G0 + 3*alpha*V^2
    }
    break;

  case DeviceType::BJT_NPN:
    // Ebers-Moll model (simplified single-junction for now)
    // Full BJT requires multi-terminal support
    {
      double Is = dev.params[0];
      double Vt = dev.params[1];
      double expArg = fmin(vTerminal / Vt, 40.0);
      double expVal = exp(expArg);
      i = Is * (expVal - 1.0);
      g = (Is / Vt) * expVal;
    }
    break;

  case DeviceType::MOSFET_N:
    // Level 1 MOSFET (Shichman-Hodges)
    // Requires Vgs, Vds - multi-terminal support needed
    // Placeholder for now
    i = 0.0;
    g = 1e-12; // Small conductance to avoid singularity
    break;

  default:
    // Unknown device type - zero current, small conductance
    i = 0.0;
    g = 1e-12;
    break;
  }

  // Write results
  currents[idx] = i;
  conductances[idx] = g;
}

/**
 * @brief CUDA kernel: Stamp linearized devices into MNA arrays (parallel).
 *
 * Each thread stamps one device's Norton equivalent (g + ieq) into the
 * conductance matrix and current vector.
 *
 * This kernel performs atomic additions to handle multiple devices connected
 * to the same node.
 *
 * @param deviceParams Device parameters.
 * @param currents Device currents (from evaluateDevicesKernel).
 * @param conductances Device conductances (from evaluateDevicesKernel).
 * @param nodeVoltages Node voltages (for Norton equivalent calculation).
 * @param G_matrix Conductance matrix (modified via atomicAdd).
 * @param I_vector Current vector (modified via atomicAdd).
 * @param netCount Number of nets (matrix dimension).
 * @param n Number of devices.
 */
__global__ __launch_bounds__(256, 6) void stampDevicesKernel(
    const DeviceParams* deviceParams, const double* currents, const double* conductances,
    const double* nodeVoltages, double* G_matrix, double* I_vector, int netCount, int n) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= n) {
    return;
  }

  const DeviceParams& dev = deviceParams[idx];
  double g = conductances[idx];
  double i = currents[idx];

  // Compute terminal voltage and Norton equivalent current
  double vPos = nodeVoltages[dev.posNet];
  double vNeg = nodeVoltages[dev.negNet];
  double vTerminal = vPos - vNeg;
  double iEq = i - g * vTerminal; // Norton equivalent: ieq = I(V) - g*V

  // Stamp conductance into G matrix (G[pos][pos] += g, G[neg][neg] += g, etc.)
  // Using atomicAdd for thread safety when multiple devices share nodes
  if (dev.posNet > 0 && dev.posNet < netCount) {
    atomicAdd(&G_matrix[dev.posNet * netCount + dev.posNet], g);
  }
  if (dev.negNet > 0 && dev.negNet < netCount) {
    atomicAdd(&G_matrix[dev.negNet * netCount + dev.negNet], g);
  }
  if (dev.posNet > 0 && dev.negNet > 0 && dev.posNet < netCount && dev.negNet < netCount) {
    atomicAdd(&G_matrix[dev.posNet * netCount + dev.negNet], -g);
    atomicAdd(&G_matrix[dev.negNet * netCount + dev.posNet], -g);
  }

  // Stamp current into I vector
  if (dev.posNet > 0 && dev.posNet < netCount) {
    atomicAdd(&I_vector[dev.posNet], iEq);
  }
  if (dev.negNet > 0 && dev.negNet < netCount) {
    atomicAdd(&I_vector[dev.negNet], -iEq);
  }
}

/* ----------------------------- Host Interface ----------------------------- */

void evaluateDevicesCuda(const DeviceParams* d_deviceParams, const double* d_nodeVoltages,
                         double* d_currents, double* d_conductances, int deviceCount) {
  // Smaller block size spreads blocks over more SMs for modest device
  // counts (e.g. deviceCount=10k gives grid=79 at TPB=128, ~SM-full).
  constexpr int BLOCK_SIZE = 128;
  int numBlocks = (deviceCount + BLOCK_SIZE - 1) / BLOCK_SIZE;

  evaluateDevicesKernel<<<numBlocks, BLOCK_SIZE>>>(d_deviceParams, d_nodeVoltages, d_currents,
                                                   d_conductances, deviceCount);

  // Note: Caller should check cudaGetLastError() and cudaDeviceSynchronize()
}

void stampDevicesCuda(const DeviceParams* d_deviceParams, const double* d_currents,
                      const double* d_conductances, const double* d_nodeVoltages,
                      double* d_G_matrix, double* d_I_vector, int netCount, int deviceCount) {
  constexpr int BLOCK_SIZE = 128;
  int numBlocks = (deviceCount + BLOCK_SIZE - 1) / BLOCK_SIZE;

  stampDevicesKernel<<<numBlocks, BLOCK_SIZE>>>(d_deviceParams, d_currents, d_conductances,
                                                d_nodeVoltages, d_G_matrix, d_I_vector, netCount,
                                                deviceCount);

  // Note: Caller should check cudaGetLastError() and cudaDeviceSynchronize()
}

} // namespace sim::electronics::algorithms::nonlinear::cuda
