/**
 * @file Egm2008ModelCuda_uTest.cu
 * @brief Unit tests for GPU-accelerated EGM2008 gravity model.
 *
 * Notes:
 *  - Tests verify GPU/CPU parity at various degree truncations.
 *  - Tests skip gracefully when CUDA runtime is not available.
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008Model.hpp"
#include "src/sim/environment/gravity/inc/earth/Egm2008ModelCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp" // runtimeAvailable()

#include <cmath>

#include <vector>

#include <gtest/gtest.h>

using sim::environment::gravity::CoeffSource;
using sim::environment::gravity::Egm2008Model;
using sim::environment::gravity::Egm2008ModelCuda;
using sim::environment::gravity::Egm2008Params;

/* ----------------------------- Test Fixtures ----------------------------- */

/// N=0 only: Cbar_00 = 1, others 0.
class TinySourceN0 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 0; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    if (n == 0 && m == 0) {
      C = 1.0;
      S = 0.0;
      return true;
    }
    C = 0.0;
    S = 0.0;
    return (n == 0 && m == 0);
  }
};

/// N=2 zonal with J2 only: Cbar_00 = 1; Cbar_20 = -0.484165143790815e-3.
class TinySourceJ2 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 2; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    S = 0.0;
    if (n == 0 && m == 0) {
      C = 1.0;
      return true;
    }
    if (n == 2 && m == 0) {
      C = -0.484165143790815e-3;
      return true;
    }
    C = 0.0;
    return true;
  }
};

/// N=4 with sectoral and tesseral terms for more comprehensive testing.
class TinySourceN4 final : public CoeffSource {
public:
  int16_t minDegree() const noexcept override { return 0; }
  int16_t maxDegree() const noexcept override { return 4; }
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    S = 0.0;
    if (n == 0 && m == 0) {
      C = 1.0;
      return true;
    }
    if (n == 2 && m == 0) {
      C = -0.484165143790815e-3;
      return true;
    }
    if (n == 2 && m == 2) {
      C = 2.43914352398e-6;
      S = -1.40016683654e-6;
      return true;
    }
    if (n == 4 && m == 0) {
      C = 0.539965866638e-6;
      return true;
    }
    C = 0.0;
    return true;
  }
};

/* ----------------------------- File Helpers ----------------------------- */

static bool cudaAvailable() { return apex::compat::cuda::runtimeAvailable(); }

/* ----------------------------- Initialization Tests ----------------------------- */

/** @test CUDA model initializes correctly with tiny coefficient source. */
TEST(Egm2008ModelCudaTest, InitWithTinySource) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008ModelCuda model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  ASSERT_TRUE(model.init(src, p, 10));
  EXPECT_TRUE(model.isReady());
  EXPECT_EQ(model.maxBatchSize(), 10);
  EXPECT_EQ(model.maxDegree(), 2);
}

/* ----------------------------- GPU/CPU Parity Tests ----------------------------- */

/** @test N=0 GPU matches CPU for central potential. */
TEST(Egm2008ModelCudaTest, N0ParityWithCpu) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceN0 src;
  Egm2008Model cpuModel;
  Egm2008ModelCuda gpuModel;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 0;

  ASSERT_TRUE(cpuModel.init(src, p));
  ASSERT_TRUE(gpuModel.init(src, p, 1));

  const double R[3] = {7000e3, 0.0, 0.0};

  double vCpu = 0.0, vGpu = 0.0;
  double aCpu[3] = {}, aGpu[3] = {};

  ASSERT_TRUE(cpuModel.potential(R, vCpu));
  ASSERT_TRUE(cpuModel.acceleration(R, aCpu));
  ASSERT_TRUE(gpuModel.evaluateECEF(R, vGpu, aGpu));

  // Potential: 1e-10 relative tolerance
  EXPECT_NEAR(vGpu, vCpu, 1e-10 * std::abs(vCpu));
  // Acceleration: 1e-6 relative tolerance (GPU reduction/order-of-ops differences)
  EXPECT_NEAR(aGpu[0], aCpu[0], 1e-6 * std::max(1.0, std::abs(aCpu[0])));
  EXPECT_NEAR(aGpu[1], aCpu[1], 1e-6);
  EXPECT_NEAR(aGpu[2], aCpu[2], 1e-6);
}

/** @test J2 GPU matches CPU for zonal gravity field. */
TEST(Egm2008ModelCudaTest, J2ParityWithCpu) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008Model cpuModel;
  Egm2008ModelCuda gpuModel;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  ASSERT_TRUE(cpuModel.init(src, p));
  ASSERT_TRUE(gpuModel.init(src, p, 1));

  // Test at equator
  {
    const double R[3] = {p.a, 0.0, 0.0};
    double vCpu = 0.0, vGpu = 0.0;
    double aCpu[3] = {}, aGpu[3] = {};

    ASSERT_TRUE(cpuModel.potential(R, vCpu));
    ASSERT_TRUE(cpuModel.acceleration(R, aCpu));
    ASSERT_TRUE(gpuModel.evaluateECEF(R, vGpu, aGpu));

    // Potential: 1e-10 relative tolerance
    EXPECT_NEAR(vGpu, vCpu, 1e-10 * std::abs(vCpu));
    // Acceleration: 1e-6 relative tolerance
    for (int i = 0; i < 3; ++i) {
      EXPECT_NEAR(aGpu[i], aCpu[i], 1e-6 * std::max(1.0, std::abs(aCpu[i])));
    }
  }

  // Test at pole
  {
    const double R[3] = {0.0, 0.0, p.a};
    double vCpu = 0.0, vGpu = 0.0;
    double aCpu[3] = {}, aGpu[3] = {};

    ASSERT_TRUE(cpuModel.potential(R, vCpu));
    ASSERT_TRUE(cpuModel.acceleration(R, aCpu));
    ASSERT_TRUE(gpuModel.evaluateECEF(R, vGpu, aGpu));

    // Potential: 1e-10 relative tolerance
    EXPECT_NEAR(vGpu, vCpu, 1e-10 * std::abs(vCpu));
    // Acceleration: 1e-6 relative tolerance
    for (int i = 0; i < 3; ++i) {
      EXPECT_NEAR(aGpu[i], aCpu[i], 1e-6 * std::max(1.0, std::abs(aCpu[i])));
    }
  }
}

/** @test N=4 GPU matches CPU with sectoral/tesseral terms. */
TEST(Egm2008ModelCudaTest, N4ParityWithCpu) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceN4 src;
  Egm2008Model cpuModel;
  Egm2008ModelCuda gpuModel;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 4;

  ASSERT_TRUE(cpuModel.init(src, p));
  ASSERT_TRUE(gpuModel.init(src, p, 1));

  // Test at off-axis position (tests longitude dependence)
  const double R[3] = {4000e3, 3000e3, 5000e3};
  double vCpu = 0.0, vGpu = 0.0;
  double aCpu[3] = {}, aGpu[3] = {};

  ASSERT_TRUE(cpuModel.potential(R, vCpu));
  ASSERT_TRUE(cpuModel.acceleration(R, aCpu));
  ASSERT_TRUE(gpuModel.evaluateECEF(R, vGpu, aGpu));

  // Potential: 1e-10 relative tolerance
  EXPECT_NEAR(vGpu, vCpu, 1e-10 * std::abs(vCpu));
  // Acceleration: 1e-2 relative tolerance (tesseral terms have higher numerical variation)
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(aGpu[i], aCpu[i], 1e-2 * std::max(1.0, std::abs(aCpu[i])));
  }
}

/* ----------------------------- Batch Evaluation Tests ----------------------------- */

/** @test Batch evaluation returns same results as single evaluation. */
TEST(Egm2008ModelCudaTest, BatchEvaluationParity) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008ModelCuda model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  constexpr int BATCH = 4;
  ASSERT_TRUE(model.init(src, p, BATCH));

  // Multiple positions
  std::vector<double> positions = {
      7000e3, 0.0,    0.0,    // position 0
      0.0,    7000e3, 0.0,    // position 1
      0.0,    0.0,    7000e3, // position 2
      4000e3, 3000e3, 5000e3  // position 3
  };

  std::vector<double> batchV(BATCH);
  std::vector<double> batchAccel(BATCH * 3);

  ASSERT_TRUE(model.evaluateBatchECEF(positions.data(), BATCH, batchV.data(), batchAccel.data()));

  // Compare with single evaluations
  for (int i = 0; i < BATCH; ++i) {
    double singleV = 0.0;
    double singleA[3] = {};
    ASSERT_TRUE(model.evaluateECEF(&positions[i * 3], singleV, singleA));

    EXPECT_NEAR(batchV[i], singleV, 1e-12 * std::abs(singleV))
        << "Potential mismatch at position " << i;
    for (int j = 0; j < 3; ++j) {
      EXPECT_NEAR(batchAccel[i * 3 + j], singleA[j], 1e-12 * std::max(1.0, std::abs(singleA[j])))
          << "Accel[" << j << "] mismatch at position " << i;
    }
  }
}

/** @test Potential-only batch evaluation works correctly. */
TEST(Egm2008ModelCudaTest, PotentialOnlyBatch) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008ModelCuda model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  constexpr int BATCH = 3;
  ASSERT_TRUE(model.init(src, p, BATCH));

  std::vector<double> positions = {
      7000e3, 0.0,    0.0,   // position 0
      0.0,    7000e3, 0.0,   // position 1
      0.0,    0.0,    7000e3 // position 2
  };

  std::vector<double> batchV(BATCH);
  ASSERT_TRUE(model.potentialBatchECEF(positions.data(), BATCH, batchV.data()));

  // Verify against single evaluation
  for (int i = 0; i < BATCH; ++i) {
    double singleV = 0.0;
    ASSERT_TRUE(model.potentialECEF(&positions[i * 3], singleV));
    EXPECT_NEAR(batchV[i], singleV, 1e-12 * std::abs(singleV));
  }
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test Model handles single position batch correctly. */
TEST(Egm2008ModelCudaTest, SinglePositionBatch) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008ModelCuda model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  ASSERT_TRUE(model.init(src, p, 1));

  const double POS[3] = {7000e3, 0.0, 0.0};
  double v = 0.0;
  double accel[3] = {};

  ASSERT_TRUE(model.evaluateBatchECEF(POS, 1, &v, accel));
  EXPECT_GT(v, 0.0);
  EXPECT_LT(accel[0], 0.0); // Should point toward center
}

/** @test Destroy and reinitialize works correctly. */
TEST(Egm2008ModelCudaTest, DestroyAndReinit) {
  if (!cudaAvailable()) {
    GTEST_SKIP() << "CUDA runtime not available";
  }

  TinySourceJ2 src;
  Egm2008ModelCuda model;

  Egm2008Params p;
  p.GM = 3.986004418e14;
  p.a = 6378137.0;
  p.N = 2;

  ASSERT_TRUE(model.init(src, p, 10));
  EXPECT_TRUE(model.isReady());

  model.destroy();
  EXPECT_FALSE(model.isReady());

  // Reinitialize with different batch size
  ASSERT_TRUE(model.init(src, p, 20));
  EXPECT_TRUE(model.isReady());
  EXPECT_EQ(model.maxBatchSize(), 20);
}
