#ifndef APEX_NONLINEARDEVICECUDA_CUH
#define APEX_NONLINEARDEVICECUDA_CUH
/**
 * @file NonlinearDeviceCuda.cuh
 * @brief CUDA interface for parallel nonlinear device evaluation.
 *
 * Provides GPU-accelerated device evaluation for Newton-Raphson iteration.
 * Achieves ~100x speedup for circuits with 10,000+ nonlinear devices.
 *
 * Usage workflow:
 * 1. Convert host-side NonlinearDevice objects to DeviceParams structs
 * 2. Copy DeviceParams array to GPU (cudaMemcpy)
 * 3. Copy node voltages to GPU
 * 4. Call evaluateDevicesCuda() to compute I(V) and g(V) in parallel
 * 5. Call stampDevicesCuda() to build MNA system on GPU
 * 6. Copy MNA system back to host for solving
 *
 * RT-safety: NOT RT-safe (GPU operations involve kernel launches).
 * Thread-safety: Safe (each device evaluated independently).
 */

#include <cstddef>
#include <cstdint>

namespace sim::electronics::algorithms::nonlinear::cuda {

/* ----------------------------- Device Types ----------------------------- */

/**
 * @brief Enum of supported device types for GPU evaluation.
 */
enum class DeviceType : std::uint8_t {
  DIODE = 0,              ///< Diode (Shockley equation).
  NONLINEAR_RESISTOR = 1, ///< Nonlinear resistor (polynomial I-V).
  BJT_NPN = 2,            ///< NPN BJT (Ebers-Moll simplified).
  BJT_PNP = 3,            ///< PNP BJT (Ebers-Moll simplified).
  MOSFET_N = 4,           ///< N-channel MOSFET (Level 1).
  MOSFET_P = 5,           ///< P-channel MOSFET (Level 1).
  TUNNEL_DIODE = 6,       ///< Tunnel diode (Esaki model).
};

/* ----------------------------- Device Parameters ----------------------------- */

/**
 * @brief Device parameters for GPU evaluation.
 *
 * Compact representation of a nonlinear device for GPU transfer.
 * Contains device type, terminal nets, and model parameters.
 *
 * Maximum 8 parameters per device (enough for most models):
 * - Diode: Is, Vt
 * - Nonlinear resistor: G0, alpha (cubic model)
 * - BJT: Is, Vt, Bf, Br, Ise, Isc (Ebers-Moll)
 * - MOSFET: Vth, Kp, lambda, W, L (Level 1)
 */
struct DeviceParams {
  DeviceType type;       ///< Device type (determines evaluation kernel).
  std::uint16_t posNet;  ///< Positive terminal net ID.
  std::uint16_t negNet;  ///< Negative terminal net ID.
  std::uint16_t auxNet1; ///< Auxiliary net 1 (for 3+ terminal devices).
  std::uint16_t auxNet2; ///< Auxiliary net 2 (for 4-terminal MOSFETs).
  double params[8];      ///< Model parameters (device-specific).
};

/* ----------------------------- CUDA Kernels (Host Interface) ----------------------------- */

/**
 * @brief Evaluate all nonlinear devices in parallel on GPU.
 *
 * Computes I(V) and g(V) for each device at the given node voltages.
 * Each device is evaluated independently by one CUDA thread.
 *
 * @param d_deviceParams Device parameters (GPU memory).
 * @param d_nodeVoltages Node voltages (GPU memory, size = netCount).
 * @param d_currents Output: device currents (GPU memory, size = deviceCount).
 * @param d_conductances Output: device conductances (GPU memory, size = deviceCount).
 * @param deviceCount Number of devices.
 *
 * @note Caller must check cudaGetLastError() and call cudaDeviceSynchronize().
 * @note NOT RT-safe: kernel launch overhead (~1-10 us).
 */
void evaluateDevicesCuda(const DeviceParams* d_deviceParams, const double* d_nodeVoltages,
                         double* d_currents, double* d_conductances, int deviceCount);

/**
 * @brief Stamp linearized devices into MNA system on GPU.
 *
 * Builds conductance matrix G and current vector I from device evaluations.
 * Uses atomic operations for thread-safe stamping when multiple devices
 * connect to the same node.
 *
 * @param d_deviceParams Device parameters (GPU memory).
 * @param d_currents Device currents (from evaluateDevicesCuda).
 * @param d_conductances Device conductances (from evaluateDevicesCuda).
 * @param d_nodeVoltages Node voltages (GPU memory).
 * @param d_G_matrix Output: conductance matrix (GPU memory, size = netCount*netCount).
 * @param d_I_vector Output: current vector (GPU memory, size = netCount).
 * @param netCount Number of nets (matrix dimension).
 * @param deviceCount Number of devices.
 *
 * @note Caller must zero d_G_matrix and d_I_vector before calling.
 * @note Caller must check cudaGetLastError() and call cudaDeviceSynchronize().
 * @note NOT RT-safe: kernel launch overhead.
 */
void stampDevicesCuda(const DeviceParams* d_deviceParams, const double* d_currents,
                      const double* d_conductances, const double* d_nodeVoltages,
                      double* d_G_matrix, double* d_I_vector, int netCount, int deviceCount);

} // namespace sim::electronics::algorithms::nonlinear::cuda

#endif // APEX_NONLINEARDEVICECUDA_CUH
