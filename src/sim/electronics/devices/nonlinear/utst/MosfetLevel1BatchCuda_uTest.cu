/**
 * @file MosfetLevel1BatchCuda_uTest.cu
 * @brief GPU vs CPU parity tests for the batch MOSFET stamp kernel.
 *
 * Verifies that MosfetLevel1BatchCuda::evalStampBatch{Uniform,} produces
 * the same {id, gm, gds} as MosfetLevel1::stampValues on the CPU for a
 * deterministic spread of operating points. Tolerance is tight (1e-12)
 * since both paths reuse the same SIM_HD stampValues function.
 */

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"

#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cuda_runtime_api.h>
#include <vector>

namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

/* ----------------------------- Fixture ----------------------------- */

class MosfetBatchCudaFixture : public ::testing::Test {
protected:
  void SetUp() override {
    if (!apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime not available.";
    }
  }
};

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Build a deterministic spread of MOSFET biases across all regions.
 *
 * Yields cutoff, subthreshold, linear, and saturation samples to exercise
 * every branch of stampValues.
 */
static std::vector<nl_cuda::MosfetBias> buildBiases(std::size_t count) {
  std::vector<nl_cuda::MosfetBias> biases;
  biases.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    double t = static_cast<double>(i) / static_cast<double>(count);
    biases.push_back({.vgs = 0.2 + 4.0 * t, .vds = 0.05 + 5.0 * t});
  }
  return biases;
}

/* ----------------------------- Uniform-Params Parity ----------------------------- */

/** @test Uniform-params GPU batch matches CPU stampValues element-wise. */
TEST_F(MosfetBatchCudaFixture, UniformMatchesCpu) {
  const std::size_t N = 4096;
  const MosfetLevel1Params PARAMS{.Kp = 100e-6, .Vth = 0.7, .lambda = 0.02, .Vsmooth = 0.1};

  const auto biases = buildBiases(N);

  nl_cuda::MosfetBias* dBiases = nullptr;
  nl_cuda::MosfetStamp* dStamps = nullptr;
  ASSERT_EQ(cudaMalloc(&dBiases, N * sizeof(nl_cuda::MosfetBias)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dStamps, N * sizeof(nl_cuda::MosfetStamp)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dBiases, biases.data(), N * sizeof(nl_cuda::MosfetBias),
                       cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(nl_cuda::evalStampBatchUniform(dBiases, PARAMS, dStamps, N));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<nl_cuda::MosfetStamp> gpuStamps(N);
  ASSERT_EQ(cudaMemcpy(gpuStamps.data(), dStamps, N * sizeof(nl_cuda::MosfetStamp),
                       cudaMemcpyDeviceToHost),
            cudaSuccess);

  for (std::size_t i = 0; i < N; ++i) {
    const auto cpuSv = MosfetLevel1::stampValues(biases[i].vgs, biases[i].vds, PARAMS);
    EXPECT_NEAR(gpuStamps[i].id, cpuSv.id, 1e-12) << "id mismatch at i=" << i;
    EXPECT_NEAR(gpuStamps[i].gm, cpuSv.gm, 1e-12) << "gm mismatch at i=" << i;
    EXPECT_NEAR(gpuStamps[i].gds, cpuSv.gds, 1e-12) << "gds mismatch at i=" << i;
  }

  cudaFree(dBiases);
  cudaFree(dStamps);
}

/* ----------------------------- Per-Device-Params Parity ----------------------------- */

/** @test Per-device-params GPU batch matches CPU stampValues element-wise. */
TEST_F(MosfetBatchCudaFixture, PerDeviceMatchesCpu) {
  const std::size_t N = 2242; // size of the Intel 4004 transistor set
  const auto biases = buildBiases(N);

  // Emulate W/L binning: Kp varies per device; Vth alternates enhancement / depletion.
  std::vector<MosfetLevel1Params> params(N);
  for (std::size_t i = 0; i < N; ++i) {
    params[i].Kp = 5e-3 * (0.5 + 0.001 * static_cast<double>(i));
    params[i].Vth = (i % 7 == 0) ? -0.17 : 1.17;
    params[i].lambda = 0.03;
    params[i].Vsmooth = 0.1;
  }

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetStamp* dStamps = nullptr;
  ASSERT_EQ(cudaMalloc(&dBiases, N * sizeof(nl_cuda::MosfetBias)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dParams, N * sizeof(MosfetLevel1Params)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dStamps, N * sizeof(nl_cuda::MosfetStamp)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dBiases, biases.data(), N * sizeof(nl_cuda::MosfetBias),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(
      cudaMemcpy(dParams, params.data(), N * sizeof(MosfetLevel1Params), cudaMemcpyHostToDevice),
      cudaSuccess);

  ASSERT_TRUE(nl_cuda::evalStampBatch(dBiases, dParams, dStamps, N));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<nl_cuda::MosfetStamp> gpuStamps(N);
  ASSERT_EQ(cudaMemcpy(gpuStamps.data(), dStamps, N * sizeof(nl_cuda::MosfetStamp),
                       cudaMemcpyDeviceToHost),
            cudaSuccess);

  for (std::size_t i = 0; i < N; ++i) {
    const auto cpuSv = MosfetLevel1::stampValues(biases[i].vgs, biases[i].vds, params[i]);
    EXPECT_NEAR(gpuStamps[i].id, cpuSv.id, 1e-12) << "id mismatch at i=" << i;
    EXPECT_NEAR(gpuStamps[i].gm, cpuSv.gm, 1e-12) << "gm mismatch at i=" << i;
    EXPECT_NEAR(gpuStamps[i].gds, cpuSv.gds, 1e-12) << "gds mismatch at i=" << i;
  }

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dStamps);
}
