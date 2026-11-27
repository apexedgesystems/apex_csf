/**
 * @file CompanionSetCuda.cu
 * @brief CUDA kernels for parallel companion model evaluation.
 *
 * Provides GPU-accelerated batch evaluation of companion model parameters
 * (geq, ieq) for large circuits with thousands of reactive elements.
 *
 * The kernels call the companions' own geq() and ieq(), which are host and
 * device functions: the formulas exist once.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionSetCuda.hpp"

#include <cuda_runtime.h>

#include <cstddef>

namespace sim::electronics::algorithms::companions::cuda {

using algorithms::companions::CapacitorCompanion;
using algorithms::companions::InductorCompanion;
using algorithms::transient::IntegrationMethod;

namespace {

/* ----------------------------- Kernel ----------------------------- */

/**
 * @brief Kernel to evaluate companion parameters in parallel.
 *
 * Each thread processes one element, computing its geq and ieq.
 *
 * @tparam Element CapacitorCompanion or InductorCompanion.
 * @param elements Array of companions (device memory).
 * @param geqOut Output array for geq values (device memory).
 * @param ieqOut Output array for ieq values (device memory).
 * @param n Number of elements.
 * @param dt Time step.
 * @param method Integration method.
 */
template <typename Element>
__global__ void evaluateKernel(const Element* elements, double* geqOut, double* ieqOut, int n,
                               double dt, IntegrationMethod method) {
  const int IDX = blockIdx.x * blockDim.x + threadIdx.x;

  if (IDX < n) {
    geqOut[IDX] = elements[IDX].geq(dt, method);
    ieqOut[IDX] = elements[IDX].ieq(dt, method);
  }
}

/* ----------------------------- Device Memory ----------------------------- */

/** @brief An array on the device, freed on every way out of its scope. */
template <typename T> struct DeviceArray {
  T* data = nullptr;

  DeviceArray() = default;
  ~DeviceArray() {
    if (data != nullptr) {
      cudaFree(data);
    }
  }
  DeviceArray(const DeviceArray&) = delete;
  DeviceArray& operator=(const DeviceArray&) = delete;

  [[nodiscard]] bool allocate(std::size_t count) {
    void* memory = nullptr;
    if (cudaMalloc(&memory, count * sizeof(T)) != cudaSuccess) {
      return false;
    }
    data = static_cast<T*>(memory);
    return true;
  }
};

/* ----------------------------- Evaluation ----------------------------- */

template <typename Element>
bool evaluate(const Element* elements, int n, double dt, IntegrationMethod method, double* geqOut,
              double* ieqOut) {
  if (n == 0) {
    return true;
  }
  if (n < 0 || elements == nullptr || geqOut == nullptr || ieqOut == nullptr) {
    return false;
  }
  const auto COUNT = static_cast<std::size_t>(n);

  DeviceArray<Element> dElements;
  DeviceArray<double> dGeq;
  DeviceArray<double> dIeq;
  if (!dElements.allocate(COUNT) || !dGeq.allocate(COUNT) || !dIeq.allocate(COUNT)) {
    return false;
  }
  if (cudaMemcpy(dElements.data, elements, COUNT * sizeof(Element), cudaMemcpyHostToDevice) !=
      cudaSuccess) {
    return false;
  }

  constexpr int THREADS_PER_BLOCK = 256;
  const int BLOCKS = (n + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;
  evaluateKernel<Element>
      <<<BLOCKS, THREADS_PER_BLOCK>>>(dElements.data, dGeq.data, dIeq.data, n, dt, method);
  if (cudaGetLastError() != cudaSuccess) {
    return false;
  }

  return cudaMemcpy(geqOut, dGeq.data, COUNT * sizeof(double), cudaMemcpyDeviceToHost) ==
             cudaSuccess &&
         cudaMemcpy(ieqOut, dIeq.data, COUNT * sizeof(double), cudaMemcpyDeviceToHost) ==
             cudaSuccess;
}

} // namespace

/* ----------------------------- Host Functions ----------------------------- */

bool evaluateCapacitorsCuda(const CapacitorCompanion* capacitors, int n, double dt,
                            IntegrationMethod method, double* geqOut, double* ieqOut) {
  return evaluate(capacitors, n, dt, method, geqOut, ieqOut);
}

bool evaluateInductorsCuda(const InductorCompanion* inductors, int n, double dt,
                           IntegrationMethod method, double* geqOut, double* ieqOut) {
  return evaluate(inductors, n, dt, method, geqOut, ieqOut);
}

} // namespace sim::electronics::algorithms::companions::cuda
