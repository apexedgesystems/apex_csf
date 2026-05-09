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
 * CompanionSet companions;
 * // ... add capacitors and inductors ...
 *
 * std::vector<double> capGeq(companions.capacitorCount());
 * std::vector<double> capIeq(companions.capacitorCount());
 *
 * cuda::evaluateCapacitorsCuda(companions.capacitors().data(),
 *                              companions.capacitorCount(),
 *                              dt, IntegrationMethod::TRAPEZOIDAL,
 *                              capGeq.data(), capIeq.data());
 * ```
 *
 * Performance: For circuits with 10,000+ capacitors/inductors,
 * GPU evaluation provides ~100x speedup over CPU sequential evaluation.
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
 *
 * @note NOT RT-safe: performs device memory allocation and data transfers.
 * @note Requires CUDA-capable GPU and CUDA toolkit.
 */
void evaluateCapacitorsCuda(const CapacitorCompanion* capacitors, int n, double dt,
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
 *
 * @note NOT RT-safe: performs device memory allocation and data transfers.
 * @note Requires CUDA-capable GPU and CUDA toolkit.
 */
void evaluateInductorsCuda(const InductorCompanion* inductors, int n, double dt,
                           IntegrationMethod method, double* geqOut, double* ieqOut);

} // namespace sim::electronics::algorithms::companions::cuda

#endif // APEX_COMPANIONSETCUDA_HPP
