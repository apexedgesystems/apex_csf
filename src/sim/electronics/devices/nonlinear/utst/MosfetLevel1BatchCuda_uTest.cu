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

TEST_F(MosfetBatchCudaFixture, SoAMatchesCpu) {
  const std::size_t N = 2242;
  const auto biases = buildBiases(N);

  std::vector<MosfetLevel1Params> params(N);
  for (std::size_t i = 0; i < N; ++i) {
    params[i].Kp = 5e-3 * (0.5 + 0.001 * static_cast<double>(i));
    params[i].Vth = (i % 7 == 0) ? -0.17 : 1.17;
    params[i].lambda = 0.03;
    params[i].Vsmooth = 0.1;
  }

  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  double* dId = nullptr;
  double* dGm = nullptr;
  double* dGds = nullptr;
  ASSERT_EQ(cudaMalloc(&dBiases, N * sizeof(nl_cuda::MosfetBias)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dParams, N * sizeof(MosfetLevel1Params)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dId, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dGm, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dGds, N * sizeof(double)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dBiases, biases.data(), N * sizeof(nl_cuda::MosfetBias),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(
      cudaMemcpy(dParams, params.data(), N * sizeof(MosfetLevel1Params), cudaMemcpyHostToDevice),
      cudaSuccess);

  ASSERT_TRUE(nl_cuda::evalStampBatchSoA(dBiases, dParams, dId, dGm, dGds, N));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<double> id(N), gm(N), gds(N);
  ASSERT_EQ(cudaMemcpy(id.data(), dId, N * sizeof(double), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(gm.data(), dGm, N * sizeof(double), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(gds.data(), dGds, N * sizeof(double), cudaMemcpyDeviceToHost), cudaSuccess);

  for (std::size_t i = 0; i < N; ++i) {
    const auto cpuSv = MosfetLevel1::stampValues(biases[i].vgs, biases[i].vds, params[i]);
    EXPECT_NEAR(id[i], cpuSv.id, 1e-12) << "id mismatch at i=" << i;
    EXPECT_NEAR(gm[i], cpuSv.gm, 1e-12) << "gm mismatch at i=" << i;
    EXPECT_NEAR(gds[i], cpuSv.gds, 1e-12) << "gds mismatch at i=" << i;
  }

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dId);
  cudaFree(dGm);
  cudaFree(dGds);
}

namespace {

// CPU reference for the fused stamp. Matches the kernel exactly so
// parity is enforced at 1e-12 tolerance against the GPU path.
void cpuStampMosfetL1(const std::vector<nl_cuda::MosfetBias>& biases,
                      const std::vector<MosfetLevel1Params>& params,
                      const std::vector<nl_cuda::MosfetNets>& nets, std::vector<double>& G,
                      std::vector<double>& I, int netCount, double gmin) {
  auto addIfNode = [&](int row, int col, double v) {
    if (row <= 0 || col <= 0 || row >= netCount || col >= netCount) return;
    G[row * netCount + col] += v;
  };
  auto addIfRow = [&](int row, double v) {
    if (row <= 0 || row >= netCount) return;
    I[row] += v;
  };

  const std::size_t N = biases.size();
  for (std::size_t i = 0; i < N; ++i) {
    const double vsg = biases[i].vgs;
    const double vsd = biases[i].vds;
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
      evalVgs = vsg - vsd;
      evalVds = -vsd;
    }
    const double vgsE = std::max(evalVgs, 0.0);
    const double vdsE = std::max(evalVds, 0.0);
    const auto sv = MosfetLevel1::stampValues(vgsE, vdsE, params[i]);
    const double id = sv.id, gm = sv.gm, gdsDev = sv.gds;
    const double gdsStamp = std::max(gdsDev, gmin);

    double cdreq;
    if (xnrm == 1) {
      cdreq = -(id - gdsDev * vsd - gm * vsg);
    } else {
      cdreq = (id - gdsDev * (-vsd) - gm * (vsg - vsd));
    }

    const int d = nets[i].drain;
    const int g = nets[i].gate;
    const int s = nets[i].source;

    addIfNode(d, d, gdsStamp);
    addIfNode(s, s, gdsStamp);
    addIfNode(d, s, -gdsStamp);
    addIfNode(s, d, -gdsStamp);

    const double xrevGm = xrev * gm;
    const double xnrmGm = xnrm * gm;
    const double xDelta = (xnrm - xrev) * gm;
    addIfNode(d, d, xrevGm);
    addIfNode(s, s, xnrmGm);
    addIfNode(d, g, xDelta);
    addIfNode(d, s, -xnrmGm);
    addIfNode(s, g, -xDelta);
    addIfNode(s, d, -xrevGm);

    addIfRow(d, -cdreq);
    addIfRow(s, cdreq);
  }
}

} // namespace

TEST_F(MosfetBatchCudaFixture, StampMosfetL1BatchMatchesCpu) {
  // Small deterministic circuit: 17 nets (net 0 = ground, 1..16 DOF),
  // 64 MOSFETs scattered over nets 1..16 with reproducible params.
  constexpr int NET_COUNT = 17;
  constexpr int N_TRANSISTORS = 64;
  constexpr double GMIN = 1e-12;

  std::vector<nl_cuda::MosfetBias> biases(N_TRANSISTORS);
  std::vector<MosfetLevel1Params> params(N_TRANSISTORS);
  std::vector<nl_cuda::MosfetNets> nets(N_TRANSISTORS);

  // Spread VSG/VSD around threshold with both signs of VSD so both
  // SPICE modes are exercised.
  for (int i = 0; i < N_TRANSISTORS; ++i) {
    const double vsg = 0.5 + 0.05 * static_cast<double>(i % 31);
    const double vsd = (i % 3 == 0) ? -0.4 + 0.02 * i : 0.2 + 0.03 * i;
    biases[i] = {vsg, vsd};
    params[i] = {.Kp = 5e-3 * (1.0 + 0.001 * i), .Vth = 1.17, .lambda = 0.03, .Vsmooth = 0.1};
    nets[i] = {1 + (i * 5 + 3) % (NET_COUNT - 1), 1 + (i * 7 + 2) % (NET_COUNT - 1),
               1 + (i * 11 + 5) % (NET_COUNT - 1)};
  }

  // CPU reference.
  std::vector<double> cpuG(NET_COUNT * NET_COUNT, 0.0);
  std::vector<double> cpuI(NET_COUNT, 0.0);
  cpuStampMosfetL1(biases, params, nets, cpuG, cpuI, NET_COUNT, GMIN);

  // GPU path.
  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetNets* dNets = nullptr;
  double* dG = nullptr;
  double* dI = nullptr;
  ASSERT_EQ(cudaMalloc(&dBiases, N_TRANSISTORS * sizeof(nl_cuda::MosfetBias)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dParams, N_TRANSISTORS * sizeof(MosfetLevel1Params)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dNets, N_TRANSISTORS * sizeof(nl_cuda::MosfetNets)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dG, NET_COUNT * NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dI, NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMemset(dG, 0, NET_COUNT * NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMemset(dI, 0, NET_COUNT * sizeof(double)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dBiases, biases.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetBias),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dParams, params.data(), N_TRANSISTORS * sizeof(MosfetLevel1Params),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dNets, nets.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetNets),
                       cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(nl_cuda::stampMosfetL1Batch(dBiases, dParams, dNets, dG, dI, N_TRANSISTORS,
                                          NET_COUNT, GMIN));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  std::vector<double> gpuG(NET_COUNT * NET_COUNT);
  std::vector<double> gpuI(NET_COUNT);
  ASSERT_EQ(
      cudaMemcpy(gpuG.data(), dG, NET_COUNT * NET_COUNT * sizeof(double), cudaMemcpyDeviceToHost),
      cudaSuccess);
  ASSERT_EQ(cudaMemcpy(gpuI.data(), dI, NET_COUNT * sizeof(double), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // G matrix: atomic-add order-independent; use a slightly looser
  // tolerance than the deterministic SoA stamp (1e-10 vs 1e-12) to
  // absorb float non-associativity across 64 concurrent adds.
  for (int r = 0; r < NET_COUNT; ++r) {
    for (int c = 0; c < NET_COUNT; ++c) {
      EXPECT_NEAR(gpuG[r * NET_COUNT + c], cpuG[r * NET_COUNT + c], 1e-10)
          << "G(" << r << "," << c << ") mismatch";
    }
    EXPECT_NEAR(gpuI[r], cpuI[r], 1e-10) << "I(" << r << ") mismatch";
  }

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dNets);
  cudaFree(dG);
  cudaFree(dI);
}

TEST_F(MosfetBatchCudaFixture, NrUpdateAndLimit_NoLimit) {
  // Case: max delta below the limit -> should apply 1x scale.
  constexpr int N = 1121;
  constexpr double LIMIT = 5.0;

  std::vector<double> prev(N), next(N);
  for (int i = 0; i < N; ++i) {
    prev[i] = 0.01 * static_cast<double>(i);
    next[i] = prev[i] + 0.1 * std::sin(0.01 * i); // max |delta| = 0.1
  }

  double expectedMax = 0.0;
  for (int i = 0; i < N; ++i) {
    expectedMax = std::max(expectedMax, std::fabs(next[i] - prev[i]));
  }
  ASSERT_LT(expectedMax, LIMIT);

  double *dNewV = nullptr, *dPrevV = nullptr, *dMaxDelta = nullptr;
  ASSERT_EQ(cudaMalloc(&dNewV, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPrevV, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dMaxDelta, sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dNewV, next.data(), N * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dPrevV, prev.data(), N * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(nl_cuda::nrUpdateAndLimit(dNewV, dPrevV, dMaxDelta, N, LIMIT));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  double gpuMax = 0.0;
  std::vector<double> gpuOut(N);
  ASSERT_EQ(cudaMemcpy(&gpuMax, dMaxDelta, sizeof(double), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(gpuOut.data(), dPrevV, N * sizeof(double), cudaMemcpyDeviceToHost),
            cudaSuccess);

  EXPECT_NEAR(gpuMax, expectedMax, 1e-12);
  for (int i = 0; i < N; ++i) {
    // No limiting: prev should have been updated to exactly next[i].
    EXPECT_NEAR(gpuOut[i], next[i], 1e-12) << "i=" << i;
  }

  cudaFree(dNewV);
  cudaFree(dPrevV);
  cudaFree(dMaxDelta);
}

TEST_F(MosfetBatchCudaFixture, NrUpdateAndLimit_Limited) {
  // Case: max delta exceeds the limit -> should scale all deltas.
  constexpr int N = 1121;
  constexpr double LIMIT = 5.0;

  std::vector<double> prev(N), next(N);
  for (int i = 0; i < N; ++i) {
    prev[i] = 0.0;
    next[i] = 10.0 * std::sin(0.01 * i); // max |delta| ~ 9.95 > 5
  }

  double expectedMax = 0.0;
  for (int i = 0; i < N; ++i) {
    expectedMax = std::max(expectedMax, std::fabs(next[i] - prev[i]));
  }
  ASSERT_GT(expectedMax, LIMIT);
  const double scale = LIMIT / expectedMax;

  double *dNewV = nullptr, *dPrevV = nullptr, *dMaxDelta = nullptr;
  ASSERT_EQ(cudaMalloc(&dNewV, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPrevV, N * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dMaxDelta, sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dNewV, next.data(), N * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dPrevV, prev.data(), N * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);

  ASSERT_TRUE(nl_cuda::nrUpdateAndLimit(dNewV, dPrevV, dMaxDelta, N, LIMIT));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  double gpuMax = 0.0;
  std::vector<double> gpuOut(N);
  ASSERT_EQ(cudaMemcpy(&gpuMax, dMaxDelta, sizeof(double), cudaMemcpyDeviceToHost), cudaSuccess);
  ASSERT_EQ(cudaMemcpy(gpuOut.data(), dPrevV, N * sizeof(double), cudaMemcpyDeviceToHost),
            cudaSuccess);

  // Max-delta returned is the UNLIMITED max (caller uses it for
  // convergence testing against NR threshold, not the limited one).
  EXPECT_NEAR(gpuMax, expectedMax, 1e-12);

  // Each prev[i] should have moved by scale * (next[i] - prev[i]).
  // Since prev was 0.0, gpuOut[i] == scale * next[i].
  for (int i = 0; i < N; ++i) {
    EXPECT_NEAR(gpuOut[i], scale * next[i], 1e-12) << "i=" << i;
  }

  cudaFree(dNewV);
  cudaFree(dPrevV);
  cudaFree(dMaxDelta);
}
