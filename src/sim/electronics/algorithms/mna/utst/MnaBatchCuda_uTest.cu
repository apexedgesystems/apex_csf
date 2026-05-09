/**
 * @file MnaBatchCuda_uTest.cu
 * @brief Unit tests for custom CUDA batch MNA solver.
 *
 * Notes:
 *  - Tests verify GPU/CPU parity and correctness.
 *  - Tests skip gracefully when CUDA runtime is not available.
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"

#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#if defined(__has_include)
#if __has_include(<cuda_runtime_api.h>)
#define LOCAL_HAS_CUDA_RUNTIME 1
#include <cuda_runtime_api.h>
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif
#else
#define LOCAL_HAS_CUDA_RUNTIME 0
#endif

namespace cuda = sim::electronics::algorithms::mna::cuda;

/* ----------------------------- Test Fixture ----------------------------- */

class MnaBatchCudaTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (!::apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime or device not available.";
    }
  }
};

/* ----------------------------- Helpers ----------------------------- */

/**
 * @brief Build batch of systems with known solutions.
 */
static void buildBatchKnownSystems(std::size_t dim, std::size_t batch, std::vector<double>& As,
                                   std::vector<double>& bs, std::vector<double>& expected) {
  As.resize(batch * dim * dim);
  bs.resize(batch * dim);
  expected.resize(dim);

  for (std::size_t i = 0; i < dim; ++i) {
    expected[i] = static_cast<double>(i + 1);
  }

  for (std::size_t k = 0; k < batch; ++k) {
    double scale = 1.0 + static_cast<double>(k) * 0.01;

    for (std::size_t i = 0; i < dim; ++i) {
      double rowSum = 0.0;
      for (std::size_t j = 0; j < dim; ++j) {
        double val;
        if (i == j) {
          val = static_cast<double>(dim + 1) * scale;
        } else {
          val = scale;
        }
        As[k * dim * dim + i * dim + j] = val;
        rowSum += val * expected[j];
      }
      bs[k * dim + i] = rowSum;
    }
  }
}

/* ----------------------------- Availability Tests ----------------------------- */

/** @test batchAvailable returns consistent value */
TEST(MnaBatchCudaSupportTest, Availability) {
  bool avail = cuda::batchAvailable();
  EXPECT_EQ(avail, cuda::batchAvailable());
}

/** @test isSupportedDim returns true for 8, 16, 32, 64 */
TEST(MnaBatchCudaSupportTest, SupportedDimensions) {
  EXPECT_FALSE(cuda::isSupportedDim(4));
  EXPECT_TRUE(cuda::isSupportedDim(8));
  EXPECT_FALSE(cuda::isSupportedDim(12));
  EXPECT_TRUE(cuda::isSupportedDim(16));
  EXPECT_FALSE(cuda::isSupportedDim(24));
  EXPECT_TRUE(cuda::isSupportedDim(32));
  EXPECT_FALSE(cuda::isSupportedDim(48));
  EXPECT_TRUE(cuda::isSupportedDim(64));
  EXPECT_FALSE(cuda::isSupportedDim(128));
}

/* ----------------------------- Workspace Tests ----------------------------- */

/** @test Workspace prepare and release */
TEST_F(MnaBatchCudaTest, WorkspacePrepareRelease) {
  cuda::MnaBatchWorkspace ws;
  EXPECT_FALSE(ws.initialized);

  EXPECT_TRUE(ws.prepare(16, 100));
  EXPECT_TRUE(ws.initialized);
  EXPECT_TRUE(ws.canHandle(16, 100));
  EXPECT_TRUE(ws.canHandle(8, 50));
  EXPECT_FALSE(ws.canHandle(32, 100));

  ws.release();
  EXPECT_FALSE(ws.initialized);
}

/** @test Workspace reallocation */
TEST_F(MnaBatchCudaTest, WorkspaceReallocation) {
  cuda::MnaBatchWorkspace ws;

  EXPECT_TRUE(ws.prepare(16, 100));
  EXPECT_TRUE(ws.canHandle(16, 100));

  EXPECT_TRUE(ws.prepare(32, 200));
  EXPECT_TRUE(ws.canHandle(32, 200));
  EXPECT_TRUE(ws.canHandle(16, 100));

  ws.release();
}

/* ----------------------------- Correctness Tests ----------------------------- */

/** @test 8x8 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve8x8_Correctness) {
  constexpr std::size_t DIM = 8;
  constexpr std::size_t BATCH = 200;

  std::vector<double> As, bs, expected;
  buildBatchKnownSystems(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustom(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr double TOL = 1e-10;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 16x16 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve16x16_Correctness) {
  constexpr std::size_t DIM = 16;
  constexpr std::size_t BATCH = 100;

  std::vector<double> As, bs, expected;
  buildBatchKnownSystems(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustom(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr double TOL = 1e-10;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 32x32 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve32x32_Correctness) {
  constexpr std::size_t DIM = 32;
  constexpr std::size_t BATCH = 50;

  std::vector<double> As, bs, expected;
  buildBatchKnownSystems(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustom(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr double TOL = 1e-10;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 64x64 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve64x64_Correctness) {
  constexpr std::size_t DIM = 64;
  constexpr std::size_t BATCH = 20;

  std::vector<double> As, bs, expected;
  buildBatchKnownSystems(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustom(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr double TOL = 1e-9;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/* ----------------------------- Launch Config Tests ----------------------------- */

/** @test getLaunchConfig returns valid config */
TEST_F(MnaBatchCudaTest, LaunchConfig) {
  auto cfg8 = cuda::getLaunchConfig(8, 1000);
  EXPECT_GT(cfg8.gridX, 0u);
  EXPECT_GT(cfg8.blockX, 0u);
  EXPECT_GE(cfg8.threadsPerSystem, 1u);

  auto cfg16 = cuda::getLaunchConfig(16, 1000);
  EXPECT_GT(cfg16.gridX, 0u);
  EXPECT_GT(cfg16.blockX, 0u);
  EXPECT_GE(cfg16.threadsPerSystem, 1u);

  auto cfg32 = cuda::getLaunchConfig(32, 1000);
  EXPECT_GT(cfg32.gridX, 0u);
  EXPECT_GT(cfg32.blockX, 0u);

  auto cfg64 = cuda::getLaunchConfig(64, 1000);
  EXPECT_GT(cfg64.gridX, 0u);
  EXPECT_GT(cfg64.blockX, 0u);
  EXPECT_GT(cfg64.sharedMemBytes, 0u);
}

/* ----------------------------- FP32 Tests ----------------------------- */

/**
 * @brief Build batch of systems with known solutions (FP32).
 */
static void buildBatchKnownSystemsF32(std::size_t dim, std::size_t batch, std::vector<float>& As,
                                      std::vector<float>& bs, std::vector<float>& expected) {
  As.resize(batch * dim * dim);
  bs.resize(batch * dim);
  expected.resize(dim);

  for (std::size_t i = 0; i < dim; ++i) {
    expected[i] = static_cast<float>(i + 1);
  }

  for (std::size_t k = 0; k < batch; ++k) {
    float scale = 1.0f + static_cast<float>(k) * 0.01f;

    for (std::size_t i = 0; i < dim; ++i) {
      float rowSum = 0.0f;
      for (std::size_t j = 0; j < dim; ++j) {
        float val;
        if (i == j) {
          val = static_cast<float>(dim + 1) * scale;
        } else {
          val = scale;
        }
        As[k * dim * dim + i * dim + j] = val;
        rowSum += val * expected[j];
      }
      bs[k * dim + i] = rowSum;
    }
  }
}

/** @test FP32 workspace prepare and release */
TEST_F(MnaBatchCudaTest, WorkspaceF32_PrepareRelease) {
  cuda::MnaBatchWorkspaceF32 ws;
  EXPECT_FALSE(ws.initialized);

  EXPECT_TRUE(ws.prepare(16, 100));
  EXPECT_TRUE(ws.initialized);
  EXPECT_TRUE(ws.canHandle(16, 100));
  EXPECT_TRUE(ws.canHandle(8, 50));
  EXPECT_FALSE(ws.canHandle(32, 100));

  ws.release();
  EXPECT_FALSE(ws.initialized);
}

/** @test 8x8 FP32 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve8x8F32_Correctness) {
  constexpr std::size_t DIM = 8;
  constexpr std::size_t BATCH = 200;

  std::vector<float> As, bs, expected;
  buildBatchKnownSystemsF32(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspaceF32 ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustomF32(ws, As.data(), bs.data(), DIM, BATCH));

  // FP32 has ~7 digits precision, use looser tolerance
  constexpr float TOL = 1e-4f;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 16x16 FP32 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve16x16F32_Correctness) {
  constexpr std::size_t DIM = 16;
  constexpr std::size_t BATCH = 100;

  std::vector<float> As, bs, expected;
  buildBatchKnownSystemsF32(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspaceF32 ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustomF32(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr float TOL = 1e-4f;
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 32x32 FP32 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve32x32F32_Correctness) {
  constexpr std::size_t DIM = 32;
  constexpr std::size_t BATCH = 50;

  std::vector<float> As, bs, expected;
  buildBatchKnownSystemsF32(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspaceF32 ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustomF32(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr float TOL = 1e-3f; // Looser for larger matrices
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test 64x64 FP32 batch solve correctness */
TEST_F(MnaBatchCudaTest, Solve64x64F32_Correctness) {
  constexpr std::size_t DIM = 64;
  constexpr std::size_t BATCH = 20;

  std::vector<float> As, bs, expected;
  buildBatchKnownSystemsF32(DIM, BATCH, As, bs, expected);

  cuda::MnaBatchWorkspaceF32 ws;
  ASSERT_TRUE(ws.prepare(DIM, BATCH));

  ASSERT_TRUE(cuda::solveBatchCustomF32(ws, As.data(), bs.data(), DIM, BATCH));

  constexpr float TOL = 1e-2f; // Even looser for 64x64
  for (std::size_t k = 0; k < BATCH; ++k) {
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_NEAR(bs[k * DIM + i], expected[i], TOL) << "Batch " << k << ", element " << i;
    }
  }

  ws.release();
}

/** @test getLaunchConfigF32 returns valid config */
TEST_F(MnaBatchCudaTest, LaunchConfigF32) {
  auto cfg8 = cuda::getLaunchConfigF32(8, 1000);
  EXPECT_GT(cfg8.gridX, 0u);
  EXPECT_GT(cfg8.blockX, 0u);

  auto cfg16 = cuda::getLaunchConfigF32(16, 1000);
  EXPECT_GT(cfg16.gridX, 0u);
  EXPECT_GT(cfg16.blockX, 0u);

  auto cfg32 = cuda::getLaunchConfigF32(32, 1000);
  EXPECT_GT(cfg32.gridX, 0u);
  EXPECT_GT(cfg32.blockX, 0u);
  EXPECT_GT(cfg32.sharedMemBytes, 0u);
  // FP32 shared mem should be half of FP64
  auto cfg32_f64 = cuda::getLaunchConfig(32, 1000);
  EXPECT_LT(cfg32.sharedMemBytes, cfg32_f64.sharedMemBytes);

  auto cfg64 = cuda::getLaunchConfigF32(64, 1000);
  EXPECT_GT(cfg64.gridX, 0u);
  EXPECT_GT(cfg64.blockX, 0u);
  EXPECT_GT(cfg64.sharedMemBytes, 0u);
}
