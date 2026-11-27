#ifndef APEX_COMPANIONSETCUDA_HPP
#define APEX_COMPANIONSETCUDA_HPP
/**
 * @file CompanionSetCuda.hpp
 * @brief CUDA-accelerated companion model evaluation.
 *
 * Provides GPU kernels for parallel evaluation of companion model parameters
 * (geq, ieq) for circuits with thousands of reactive elements.
 *
 * Usage:
 * ```cpp
 * std::vector<CapacitorCompanion> capacitors = ...;
 * std::vector<double> geq(capacitors.size());
 * std::vector<double> ieq(capacitors.size());
 *
 * if (!cuda::evaluateCapacitorsCuda(capacitors.data(), static_cast<int>(capacitors.size()),
 *                                   dt, IntegrationMethod::TRAPEZOIDAL,
 *                                   geq.data(), ieq.data())) {
 *   // No device, or the device refused: geq and ieq are not to be read.
 * }
 * ```
 *
 * The values are those CapacitorCompanion::geq() and ieq() return on the
 * host: the kernels call the same functions.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"

namespace sim::electronics::algorithms::companions::cuda {

/**
 * @brief Evaluate all capacitor companions in parallel on GPU.
 *
 * Computes geq and ieq values for all capacitors using CUDA parallelization.
 * Each capacitor is processed by one GPU thread.
 *
 * @param capacitors Host array of capacitor companions.
 * @param n Number of capacitors.
 * @param dt Time step in seconds.
 * @param method Integration method.
 * @param geqOut Host output array for geq values (pre-allocated, size n).
 * @param ieqOut Host output array for ieq values (pre-allocated, size n).
 * @return true when both outputs were written (and for n of 0). False when a
 *         pointer is null, n is negative, there is no device, or a CUDA call
 *         failed; the outputs are then not to be read.
 *
 * @note NOT RT-safe: performs device memory allocation and data transfers.
 * @note Requires CUDA-capable GPU and CUDA toolkit.
 */
[[nodiscard]] bool evaluateCapacitorsCuda(const CapacitorCompanion* capacitors, int n, double dt,
                                          IntegrationMethod method, double* geqOut, double* ieqOut);

/**
 * @brief Evaluate all inductor companions in parallel on GPU.
 *
 * Computes geq and ieq values for all inductors using CUDA parallelization.
 * Each inductor is processed by one GPU thread.
 *
 * @param inductors Host array of inductor companions.
 * @param n Number of inductors.
 * @param dt Time step in seconds.
 * @param method Integration method.
 * @param geqOut Host output array for geq values (pre-allocated, size n).
 * @param ieqOut Host output array for ieq values (pre-allocated, size n).
 * @return As evaluateCapacitorsCuda.
 *
 * @note NOT RT-safe: performs device memory allocation and data transfers.
 * @note Requires CUDA-capable GPU and CUDA toolkit.
 */
[[nodiscard]] bool evaluateInductorsCuda(const InductorCompanion* inductors, int n, double dt,
                                         IntegrationMethod method, double* geqOut, double* ieqOut);

} // namespace sim::electronics::algorithms::companions::cuda

#endif // APEX_COMPANIONSETCUDA_HPP
