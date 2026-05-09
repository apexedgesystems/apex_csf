/**
 * @file NonlinearDeviceCuda_uTest.cu
 * @brief Unit tests for CUDA-accelerated nonlinear device evaluation.
 *
 * Tests parallel device evaluation kernels and compares results with CPU
 * implementation for correctness.
 */

#include "src/sim/electronics/algorithms/nonlinear/inc/NonlinearDeviceCuda.cuh"

#include <gtest/gtest.h>

#include <cuda_runtime.h>

#include <cmath>
#include <vector>

using sim::electronics::algorithms::nonlinear::cuda::DeviceParams;
using sim::electronics::algorithms::nonlinear::cuda::DeviceType;
using sim::electronics::algorithms::nonlinear::cuda::evaluateDevicesCuda;

/* ----------------------------- Helper Functions ----------------------------- */

/**
 * @brief Check CUDA error and fail test if error occurred.
 */
#define CUDA_CHECK(call)                                                                           \
  do {                                                                                             \
    cudaError_t err = call;                                                                        \
    if (err != cudaSuccess) {                                                                      \
      FAIL() << "CUDA error: " << cudaGetErrorString(err) << " at " << __FILE__ << ":"             \
             << __LINE__;                                                                          \
    }                                                                                              \
  } while (0)

/* ----------------------------- Diode Tests ----------------------------- */

/** @test Evaluate single diode on GPU. */
TEST(NonlinearDeviceCudaTest, SingleDiodeEvaluation) {
  // Device: Diode with Is=1e-12, Vt=0.026
  DeviceParams dev;
  dev.type = DeviceType::DIODE;
  dev.posNet = 1;
  dev.negNet = 0;
  dev.params[0] = 1e-12; // Is
  dev.params[1] = 0.026; // Vt

  // Node voltages: V0=0, V1=0.7
  std::vector<double> nodeVoltages = {0.0, 0.7};

  // Allocate GPU memory
  DeviceParams* d_dev;
  double* d_voltages;
  double* d_currents;
  double* d_conductances;

  CUDA_CHECK(cudaMalloc(&d_dev, sizeof(DeviceParams)));
  CUDA_CHECK(cudaMalloc(&d_voltages, 2 * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_currents, sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_conductances, sizeof(double)));

  // Copy to GPU
  CUDA_CHECK(cudaMemcpy(d_dev, &dev, sizeof(DeviceParams), cudaMemcpyHostToDevice));
  CUDA_CHECK(
      cudaMemcpy(d_voltages, nodeVoltages.data(), 2 * sizeof(double), cudaMemcpyHostToDevice));

  // Evaluate on GPU
  evaluateDevicesCuda(d_dev, d_voltages, d_currents, d_conductances, 1);
  CUDA_CHECK(cudaDeviceSynchronize());

  // Copy results back
  double current, conductance;
  CUDA_CHECK(cudaMemcpy(&current, d_currents, sizeof(double), cudaMemcpyDeviceToHost));
  CUDA_CHECK(cudaMemcpy(&conductance, d_conductances, sizeof(double), cudaMemcpyDeviceToHost));

  // Verify results (CPU reference)
  double V = 0.7;
  double Is = 1e-12;
  double Vt = 0.026;
  double expArg = std::min(V / Vt, 40.0);
  double expVal = std::exp(expArg);
  double expectedCurrent = Is * (expVal - 1.0);
  double expectedConductance = (Is / Vt) * expVal;

  EXPECT_NEAR(current, expectedCurrent, 1e-15);
  EXPECT_NEAR(conductance, expectedConductance, 1e-15);

  // Cleanup
  CUDA_CHECK(cudaFree(d_dev));
  CUDA_CHECK(cudaFree(d_voltages));
  CUDA_CHECK(cudaFree(d_currents));
  CUDA_CHECK(cudaFree(d_conductances));
}

/** @test Evaluate multiple diodes in parallel. */
TEST(NonlinearDeviceCudaTest, MultipleDiodesParallel) {
  constexpr int N = 1000; // 1000 diodes

  // Create device array
  std::vector<DeviceParams> devices(N);
  for (int i = 0; i < N; ++i) {
    devices[i].type = DeviceType::DIODE;
    devices[i].posNet = i + 1;
    devices[i].negNet = 0;
    devices[i].params[0] = 1e-12; // Is
    devices[i].params[1] = 0.026; // Vt
  }

  // Node voltages: V0=0, V1..VN = 0.6..0.8 (varying)
  std::vector<double> nodeVoltages(N + 1);
  nodeVoltages[0] = 0.0;
  for (int i = 1; i <= N; ++i) {
    nodeVoltages[i] = 0.6 + 0.2 * (i - 1) / (N - 1);
  }

  // Allocate GPU memory
  DeviceParams* d_devices;
  double* d_voltages;
  double* d_currents;
  double* d_conductances;

  CUDA_CHECK(cudaMalloc(&d_devices, N * sizeof(DeviceParams)));
  CUDA_CHECK(cudaMalloc(&d_voltages, (N + 1) * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_currents, N * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_conductances, N * sizeof(double)));

  // Copy to GPU
  CUDA_CHECK(
      cudaMemcpy(d_devices, devices.data(), N * sizeof(DeviceParams), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_voltages, nodeVoltages.data(), (N + 1) * sizeof(double),
                        cudaMemcpyHostToDevice));

  // Evaluate on GPU
  evaluateDevicesCuda(d_devices, d_voltages, d_currents, d_conductances, N);
  CUDA_CHECK(cudaDeviceSynchronize());

  // Copy results back
  std::vector<double> currents(N);
  std::vector<double> conductances(N);
  CUDA_CHECK(cudaMemcpy(currents.data(), d_currents, N * sizeof(double), cudaMemcpyDeviceToHost));
  CUDA_CHECK(
      cudaMemcpy(conductances.data(), d_conductances, N * sizeof(double), cudaMemcpyDeviceToHost));

  // Verify results against CPU reference
  for (int i = 0; i < N; ++i) {
    double V = nodeVoltages[i + 1];
    double Is = 1e-12;
    double Vt = 0.026;
    double expArg = std::min(V / Vt, 40.0);
    double expVal = std::exp(expArg);
    double expectedCurrent = Is * (expVal - 1.0);
    double expectedConductance = (Is / Vt) * expVal;

    EXPECT_NEAR(currents[i], expectedCurrent, 1e-6 * std::abs(expectedCurrent) + 1e-15);
    EXPECT_NEAR(conductances[i], expectedConductance, 1e-6 * std::abs(expectedConductance) + 1e-15);
  }

  // Cleanup
  CUDA_CHECK(cudaFree(d_devices));
  CUDA_CHECK(cudaFree(d_voltages));
  CUDA_CHECK(cudaFree(d_currents));
  CUDA_CHECK(cudaFree(d_conductances));
}

/* ----------------------------- Nonlinear Resistor Tests ----------------------------- */

/** @test Evaluate nonlinear resistor on GPU. */
TEST(NonlinearDeviceCudaTest, NonlinearResistorEvaluation) {
  // Device: Nonlinear resistor with G0=0.01, alpha=1e-4
  DeviceParams dev;
  dev.type = DeviceType::NONLINEAR_RESISTOR;
  dev.posNet = 1;
  dev.negNet = 0;
  dev.params[0] = 0.01; // G0
  dev.params[1] = 1e-4; // alpha

  // Node voltages: V0=0, V1=5.0
  std::vector<double> nodeVoltages = {0.0, 5.0};

  // Allocate GPU memory
  DeviceParams* d_dev;
  double* d_voltages;
  double* d_currents;
  double* d_conductances;

  CUDA_CHECK(cudaMalloc(&d_dev, sizeof(DeviceParams)));
  CUDA_CHECK(cudaMalloc(&d_voltages, 2 * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_currents, sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_conductances, sizeof(double)));

  // Copy to GPU
  CUDA_CHECK(cudaMemcpy(d_dev, &dev, sizeof(DeviceParams), cudaMemcpyHostToDevice));
  CUDA_CHECK(
      cudaMemcpy(d_voltages, nodeVoltages.data(), 2 * sizeof(double), cudaMemcpyHostToDevice));

  // Evaluate on GPU
  evaluateDevicesCuda(d_dev, d_voltages, d_currents, d_conductances, 1);
  CUDA_CHECK(cudaDeviceSynchronize());

  // Copy results back
  double current, conductance;
  CUDA_CHECK(cudaMemcpy(&current, d_currents, sizeof(double), cudaMemcpyDeviceToHost));
  CUDA_CHECK(cudaMemcpy(&conductance, d_conductances, sizeof(double), cudaMemcpyDeviceToHost));

  // Verify results (CPU reference)
  double V = 5.0;
  double G0 = 0.01;
  double alpha = 1e-4;
  double expectedCurrent = G0 * V + alpha * V * V * V; // 0.01*5 + 1e-4*125 = 0.05 + 0.0125 = 0.0625
  double expectedConductance =
      G0 + 3.0 * alpha * V * V; // 0.01 + 3*1e-4*25 = 0.01 + 0.0075 = 0.0175

  EXPECT_NEAR(current, expectedCurrent, 1e-10);
  EXPECT_NEAR(conductance, expectedConductance, 1e-10);

  // Cleanup
  CUDA_CHECK(cudaFree(d_dev));
  CUDA_CHECK(cudaFree(d_voltages));
  CUDA_CHECK(cudaFree(d_currents));
  CUDA_CHECK(cudaFree(d_conductances));
}

/* ----------------------------- Performance Tests ----------------------------- */

/** @test Benchmark large-scale parallel evaluation. */
TEST(NonlinearDeviceCudaTest, LargeScalePerformance) {
  constexpr int N = 10000; // 10,000 devices

  // Create device array (mix of diodes and nonlinear resistors)
  std::vector<DeviceParams> devices(N);
  for (int i = 0; i < N; ++i) {
    if (i % 2 == 0) {
      devices[i].type = DeviceType::DIODE;
      devices[i].params[0] = 1e-12;
      devices[i].params[1] = 0.026;
    } else {
      devices[i].type = DeviceType::NONLINEAR_RESISTOR;
      devices[i].params[0] = 0.01;
      devices[i].params[1] = 1e-4;
    }
    devices[i].posNet = i + 1;
    devices[i].negNet = 0;
  }

  // Node voltages
  std::vector<double> nodeVoltages(N + 1, 0.0);
  for (int i = 1; i <= N; ++i) {
    nodeVoltages[i] = 0.5 + 0.5 * (i - 1) / (N - 1);
  }

  // Allocate GPU memory
  DeviceParams* d_devices;
  double* d_voltages;
  double* d_currents;
  double* d_conductances;

  CUDA_CHECK(cudaMalloc(&d_devices, N * sizeof(DeviceParams)));
  CUDA_CHECK(cudaMalloc(&d_voltages, (N + 1) * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_currents, N * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_conductances, N * sizeof(double)));

  // Copy to GPU
  CUDA_CHECK(
      cudaMemcpy(d_devices, devices.data(), N * sizeof(DeviceParams), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_voltages, nodeVoltages.data(), (N + 1) * sizeof(double),
                        cudaMemcpyHostToDevice));

  // Evaluate on GPU
  evaluateDevicesCuda(d_devices, d_voltages, d_currents, d_conductances, N);
  CUDA_CHECK(cudaDeviceSynchronize());

  // Copy results back
  std::vector<double> currents(N);
  std::vector<double> conductances(N);
  CUDA_CHECK(cudaMemcpy(currents.data(), d_currents, N * sizeof(double), cudaMemcpyDeviceToHost));
  CUDA_CHECK(
      cudaMemcpy(conductances.data(), d_conductances, N * sizeof(double), cudaMemcpyDeviceToHost));

  // Spot-check a few devices
  for (int i = 0; i < 10; ++i) {
    EXPECT_GT(currents[i], 0.0);     // Should have positive current
    EXPECT_GT(conductances[i], 0.0); // Should have positive conductance
  }

  // Cleanup
  CUDA_CHECK(cudaFree(d_devices));
  CUDA_CHECK(cudaFree(d_voltages));
  CUDA_CHECK(cudaFree(d_currents));
  CUDA_CHECK(cudaFree(d_conductances));
}
