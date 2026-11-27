/**
 * @file MnaBatchCuda_uTest.cu
 * @brief Unit tests for custom CUDA batch MNA solver.
 *
 * Every solve is held to a solution known beforehand or to the dense CPU
 * solver, at each supported dimension and in both precisions. The systems
 * include ones that need a row exchange at every step, ones that cannot be
 * solved, and ones whose entries are far from 1.
 *
 * The tests need a CUDA device and are skipped where there is none.
 *
 * @note NOT RT-SAFE: GPU operations involve kernel launches.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaBatchCuda.cuh"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <cuda_runtime_api.h>

namespace cuda = sim::electronics::algorithms::mna::cuda;
using sim::electronics::algorithms::mna::MnaSystem;

/* ----------------------------- Test Fixture ----------------------------- */

class MnaBatchCudaTest : public ::testing::Test {
protected:
  void SetUp() override {
    int devices = 0;
    if (!cuda::batchAvailable() || cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
      GTEST_SKIP() << "No CUDA device.";
    }
  }
};

/* ----------------------------- Helpers ----------------------------- */

namespace {

constexpr std::array<std::size_t, 4> DIMS = {8, 16, 32, 64};

/** @brief The solver's interface in one precision. */
template <typename T> struct Solver;

template <> struct Solver<double> {
  using Workspace = cuda::MnaBatchWorkspace;
  static bool solve(Workspace& ws, const double* matrices, double* vectors, std::size_t dim,
                    std::size_t batch, bool* solved = nullptr) {
    return cuda::solveBatchCustom(ws, matrices, vectors, dim, batch, nullptr, solved);
  }
  // Agreement asked of a solution whose entries are 1 to dim.
  static constexpr double TOLERANCE = 1e-9;
};

template <> struct Solver<float> {
  using Workspace = cuda::MnaBatchWorkspaceF32;
  static bool solve(Workspace& ws, const float* matrices, float* vectors, std::size_t dim,
                    std::size_t batch, bool* solved = nullptr) {
    return cuda::solveBatchCustomF32(ws, matrices, vectors, dim, batch, nullptr, solved);
  }
  static constexpr float TOLERANCE = 1e-3F;
};

/**
 * @brief Systems whose solution is 1, 2, ..., dim.
 *
 * System k has (dim + 1) * s on the diagonal and s elsewhere, with
 * s = scale * (1 + k / 100): diagonally dominant, so elimination never
 * exchanges rows. `reversed` writes the rows in reverse order, which leaves
 * the solution alone and makes every step but the middle ones exchange.
 */
template <typename T>
void buildKnownSystems(std::size_t dim, std::size_t batch, T scale, bool reversed,
                       std::vector<T>& matrices, std::vector<T>& vectors) {
  matrices.assign(batch * dim * dim, T(0));
  vectors.assign(batch * dim, T(0));
  for (std::size_t k = 0; k < batch; ++k) {
    const T S = scale * (T(1) + static_cast<T>(k) * T(0.01));
    for (std::size_t i = 0; i < dim; ++i) {
      const std::size_t ROW = reversed ? dim - 1 - i : i;
      T rowSum = T(0);
      for (std::size_t j = 0; j < dim; ++j) {
        const T VALUE = (i == j) ? static_cast<T>(dim + 1) * S : S;
        matrices[k * dim * dim + ROW * dim + j] = VALUE;
        rowSum += VALUE * static_cast<T>(j + 1);
      }
      vectors[k * dim + ROW] = rowSum;
    }
  }
}

/** @brief Expects system `k` of `vectors` to hold 1, 2, ..., dim. */
template <typename T>
void expectKnownSolution(const std::vector<T>& vectors, std::size_t dim, std::size_t k) {
  for (std::size_t i = 0; i < dim; ++i) {
    EXPECT_NEAR(vectors[k * dim + i], static_cast<T>(i + 1), Solver<T>::TOLERANCE)
        << "dim " << dim << ", system " << k << ", element " << i;
  }
}

/** @brief Known systems at every dimension, solved and compared. */
template <typename T> void expectKnownSystemsSolved(T scale, bool reversed) {
  for (const std::size_t DIM : DIMS) {
    constexpr std::size_t BATCH = 70; // More than one block of the 8 and 16 kernels
    std::vector<T> matrices;
    std::vector<T> vectors;
    buildKnownSystems<T>(DIM, BATCH, scale, reversed, matrices, vectors);

    typename Solver<T>::Workspace ws;
    ASSERT_TRUE(ws.prepare(DIM, BATCH));
    std::array<bool, BATCH> solved{};
    ASSERT_TRUE(Solver<T>::solve(ws, matrices.data(), vectors.data(), DIM, BATCH, solved.data()))
        << "dim " << DIM;
    EXPECT_EQ(std::count(solved.begin(), solved.end(), true), static_cast<std::ptrdiff_t>(BATCH));
    for (std::size_t k = 0; k < BATCH; ++k) {
      // The scale divides out of the solution.
      for (std::size_t i = 0; i < DIM; ++i) {
        EXPECT_NEAR(vectors[k * DIM + i], static_cast<T>(i + 1), Solver<T>::TOLERANCE)
            << "dim " << DIM << ", system " << k << ", element " << i;
      }
    }
  }
}

/**
 * @brief A batch of three with the middle system replaced by `spoil`: the
 *        call reports failure, the flags name the middle system, every
 *        entry of it comes back as not-a-number, and its neighbours are
 *        solved.
 */
template <typename T, typename Spoil> void expectMiddleSystemReported(Spoil spoil) {
  for (const std::size_t DIM : DIMS) {
    constexpr std::size_t BATCH = 3;
    std::vector<T> matrices;
    std::vector<T> vectors;
    buildKnownSystems<T>(DIM, BATCH, T(1), false, matrices, vectors);
    spoil(matrices.data() + DIM * DIM, DIM);
    for (std::size_t i = 0; i < DIM; ++i) {
      vectors[DIM + i] = T(100) + static_cast<T>(i);
    }

    typename Solver<T>::Workspace ws;
    ASSERT_TRUE(ws.prepare(DIM, BATCH));
    std::array<bool, BATCH> solved = {false, true, false};
    EXPECT_FALSE(Solver<T>::solve(ws, matrices.data(), vectors.data(), DIM, BATCH, solved.data()))
        << "dim " << DIM;
    EXPECT_TRUE(solved[0]) << "dim " << DIM;
    EXPECT_FALSE(solved[1]) << "dim " << DIM;
    EXPECT_TRUE(solved[2]) << "dim " << DIM;

    expectKnownSolution(vectors, DIM, 0);
    expectKnownSolution(vectors, DIM, 2);
    for (std::size_t i = 0; i < DIM; ++i) {
      EXPECT_TRUE(std::isnan(vectors[DIM + i])) << "dim " << DIM << ", element " << i;
    }
  }
}

/** @brief Every entry zero. */
template <typename T> void allZeros(T* matrix, std::size_t dim) {
  std::fill_n(matrix, dim * dim, T(0));
}

/**
 * @brief The last column a copy of the one before it: singular, with every
 *        entry of the size of its neighbours'. What elimination leaves in
 *        the last column is rounding, not zero. The two columns are zero in
 *        the first row, so their size cannot be read off that row.
 */
template <typename T> void lastTwoColumnsEqual(T* matrix, std::size_t dim) {
  for (std::size_t i = 0; i < dim; ++i) {
    // Entries that do not divide evenly, so the cancellation is inexact.
    matrix[i * dim + (dim - 2)] = (i == 0) ? T(0) : T(1) / static_cast<T>(3 + i);
    matrix[i * dim + (dim - 1)] = matrix[i * dim + (dim - 2)];
  }
}

/**
 * @brief The first and the last column equal: singular, and the rounding the
 *        last column is left with has passed through every step.
 */
template <typename T> void firstAndLastColumnsEqual(T* matrix, std::size_t dim) {
  for (std::size_t i = 0; i < dim; ++i) {
    matrix[i * dim] = T(1) / static_cast<T>(3 + i);
    matrix[i * dim + (dim - 1)] = matrix[i * dim];
  }
}

/**
 * @brief The identity, with the last two rows and columns holding
 *        [1 1; 1 1 + excess]. Elimination leaves exactly `excess` as the
 *        last candidate, in a column whose largest entry is 1 and whose
 *        first-row entry is 0.
 */
template <typename T> void identityWithLastPivot(T* matrix, std::size_t dim, T excess) {
  std::fill_n(matrix, dim * dim, T(0));
  for (std::size_t i = 0; i < dim; ++i) {
    matrix[i * dim + i] = T(1);
  }
  matrix[(dim - 2) * dim + (dim - 1)] = T(1);
  matrix[(dim - 1) * dim + (dim - 2)] = T(1);
  matrix[(dim - 1) * dim + (dim - 1)] = T(1) + excess;
}

/** @brief A last candidate of four epsilons: below dim epsilons at every dimension. */
template <typename T> void lastPivotBelowFloor(T* matrix, std::size_t dim) {
  identityWithLastPivot(matrix, dim, T(4) * std::numeric_limits<T>::epsilon());
}

/**
 * @brief Known systems with every other column scaled up and the rest scaled
 *        down by `spread`: the solution's entries scale the other way. A
 *        pivot is small or large with its own column, not with the matrix.
 */
template <typename T> void expectColumnScaledSystemsSolved(T spread) {
  for (const std::size_t DIM : DIMS) {
    constexpr std::size_t BATCH = 3;
    std::vector<T> matrices;
    std::vector<T> vectors;
    buildKnownSystems<T>(DIM, BATCH, T(1), true, matrices, vectors);
    for (std::size_t k = 0; k < BATCH; ++k) {
      for (std::size_t i = 0; i < DIM; ++i) {
        for (std::size_t j = 0; j < DIM; ++j) {
          matrices[k * DIM * DIM + i * DIM + j] *= (j % 2 == 0) ? spread : T(1) / spread;
        }
      }
    }

    typename Solver<T>::Workspace ws;
    ASSERT_TRUE(ws.prepare(DIM, BATCH));
    ASSERT_TRUE(Solver<T>::solve(ws, matrices.data(), vectors.data(), DIM, BATCH)) << "dim " << DIM;
    for (std::size_t k = 0; k < BATCH; ++k) {
      for (std::size_t j = 0; j < DIM; ++j) {
        const T UNSCALED = vectors[k * DIM + j] * ((j % 2 == 0) ? spread : T(1) / spread);
        EXPECT_NEAR(UNSCALED, static_cast<T>(j + 1), Solver<T>::TOLERANCE)
            << "dim " << DIM << ", system " << k << ", element " << j;
      }
    }
  }
}

} // namespace

/* ----------------------------- Support ----------------------------- */

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

/* ----------------------------- Workspace ----------------------------- */

/** @test Workspace prepare and release */
TEST_F(MnaBatchCudaTest, WorkspacePrepareRelease) {
  cuda::MnaBatchWorkspace ws;
  EXPECT_FALSE(ws.initialized);

  EXPECT_TRUE(ws.prepare(16, 100));
  EXPECT_TRUE(ws.initialized);
  EXPECT_TRUE(ws.canHandle(16, 100));
  EXPECT_TRUE(ws.canHandle(8, 50));
  EXPECT_FALSE(ws.canHandle(32, 100));
  EXPECT_FALSE(ws.canHandle(16, 101));

  ws.release();
  EXPECT_FALSE(ws.initialized);
  EXPECT_FALSE(ws.canHandle(16, 100));
}

/** @test Preparing for more than the workspace holds reallocates it */
TEST_F(MnaBatchCudaTest, WorkspaceReallocation) {
  cuda::MnaBatchWorkspaceF32 ws;

  EXPECT_TRUE(ws.prepare(16, 100));
  EXPECT_TRUE(ws.prepare(32, 200));
  EXPECT_TRUE(ws.canHandle(32, 200));
  EXPECT_TRUE(ws.canHandle(16, 100));
}

/** @test prepare() refuses a dimension with no kernel, and an empty batch */
TEST_F(MnaBatchCudaTest, WorkspaceRefusesWhatCannotRun) {
  cuda::MnaBatchWorkspace ws;
  EXPECT_FALSE(ws.prepare(12, 4));
  EXPECT_FALSE(ws.prepare(8, 0));
  EXPECT_FALSE(ws.initialized);

  // A refusal leaves a prepared workspace as it was.
  ASSERT_TRUE(ws.prepare(8, 4));
  EXPECT_FALSE(ws.prepare(12, 4));
  EXPECT_TRUE(ws.canHandle(8, 4));
}

/* ----------------------------- Solutions ----------------------------- */

/** @test Diagonally dominant systems, double precision, every dimension */
TEST_F(MnaBatchCudaTest, KnownSystemsSolve) { expectKnownSystemsSolved<double>(1.0, false); }

/** @test Diagonally dominant systems, single precision, every dimension */
TEST_F(MnaBatchCudaTest, KnownSystemsSolveF32) { expectKnownSystemsSolved<float>(1.0F, false); }

/** @test Rows in reverse order: a row exchange at nearly every step */
TEST_F(MnaBatchCudaTest, ReversedRowsSolveByPivoting) {
  expectKnownSystemsSolved<double>(1.0, true);
}

/** @test Rows in reverse order, single precision */
TEST_F(MnaBatchCudaTest, ReversedRowsSolveByPivotingF32) {
  expectKnownSystemsSolved<float>(1.0F, true);
}

/** @test Well-conditioned systems solve whatever the size of their entries */
TEST_F(MnaBatchCudaTest, SystemsOfAnyScaleSolve) {
  // Picosiemens squared and teraohm-scale reciprocals: the solution, 1 to
  // dim, does not depend on the scale.
  expectKnownSystemsSolved<double>(1e-20, true);
  expectKnownSystemsSolved<double>(1e+20, true);
}

/** @test Single-precision systems of megohm conductances solve */
TEST_F(MnaBatchCudaTest, F32SmallConductanceSystemSolves) {
  // 0.1 uS off the diagonal: every entry is below a millionth.
  expectKnownSystemsSolved<float>(1e-7F, false);
  expectKnownSystemsSolved<float>(1e-7F, true);
  expectKnownSystemsSolved<float>(1e+12F, true);
}

/** @test Columns sixteen and eight decades apart in size solve: each pivot is judged by its column
 */
TEST_F(MnaBatchCudaTest, ColumnScaledSystemsSolve) {
  expectColumnScaledSystemsSolved<double>(1e8);
  expectColumnScaledSystemsSolved<float>(1e4F);
}

/** @test A circuit's matrix that needs two row exchanges agrees with the dense solver */
TEST_F(MnaBatchCudaTest, MnaSystemWithVoltageSourcesMatchesDense) {
  // Six nets and two voltage sources: an 8x8 system whose source rows have
  // zeros on the diagonal.
  MnaSystem mna(6);
  mna.addConductance(1, 2, 1e-3);
  mna.addConductance(2, 0, 2e-3);
  mna.addConductance(2, 3, 1e-3);
  mna.addConductance(3, 4, 5e-4);
  mna.addConductance(4, 0, 1e-3);
  mna.addConductance(4, 5, 1e-3);
  mna.addConductance(5, 0, 1e-3);
  mna.addCurrent(0, 3, 1e-3);
  mna.addVoltageSource(1, 0, 5.0);
  mna.addVoltageSource(5, 3, 1.5);
  ASSERT_EQ(mna.augmentedDim(), 8U);

  std::vector<double> matrix(64, 0.0);
  std::vector<double> rhs(8, 0.0);
  mna.buildAugmentedMatrix(matrix.data(), rhs.data());
  const auto REFERENCE = mna.solve();
  ASSERT_TRUE(REFERENCE.success) << REFERENCE.errorMessage;

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(8, 1));
  ASSERT_TRUE(cuda::solveBatchCustom(ws, matrix.data(), rhs.data(), 8, 1));
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_NEAR(rhs[i], REFERENCE.nodeVoltages[i], 1e-9) << "net " << i;
  }
  for (std::size_t i = 0; i < 2; ++i) {
    EXPECT_NEAR(rhs[6 + i], REFERENCE.branchCurrents[i], 1e-12) << "source " << i;
  }
}

namespace {

/**
 * @brief A resistor ladder with two voltage sources, as a system of `dim`
 *        unknowns: dim - 2 nets and the two source currents.
 *
 * Each net is tied to the next by 1 kohm and to ground by 2 kohm. One
 * source holds net 1 at 5 V; the other holds the last net 1.5 V above the
 * middle one. The source rows have zeros on the diagonal, so elimination
 * cannot do without row exchanges.
 */
template <typename T> void expectLadderMatchesDense(std::size_t dim, T tolerance) {
  const std::size_t NETS = dim - 2;
  MnaSystem mna(NETS);
  for (std::size_t net = 1; net < NETS; ++net) {
    if (net + 1 < NETS) {
      mna.addConductance(net, net + 1, 1e-3);
    }
    mna.addConductance(net, 0, 0.5e-3);
  }
  mna.addVoltageSource(1, 0, 5.0);
  mna.addVoltageSource(NETS - 1, NETS / 2, 1.5);
  ASSERT_EQ(mna.augmentedDim(), dim);

  std::vector<double> matrix(dim * dim, 0.0);
  std::vector<double> rhs(dim, 0.0);
  mna.buildAugmentedMatrix(matrix.data(), rhs.data());
  const auto REFERENCE = mna.solve();
  ASSERT_TRUE(REFERENCE.success) << REFERENCE.errorMessage;

  const std::vector<T> MATRIX(matrix.begin(), matrix.end());
  std::vector<T> solution(rhs.begin(), rhs.end());
  typename Solver<T>::Workspace ws;
  ASSERT_TRUE(ws.prepare(dim, 1));
  ASSERT_TRUE(Solver<T>::solve(ws, MATRIX.data(), solution.data(), dim, 1)) << "dim " << dim;

  for (std::size_t net = 0; net < NETS; ++net) {
    EXPECT_NEAR(solution[net], static_cast<T>(REFERENCE.nodeVoltages[net]), tolerance)
        << "dim " << dim << ", net " << net;
  }
  // Currents are milliamperes where the voltages are volts.
  for (std::size_t source = 0; source < 2; ++source) {
    EXPECT_NEAR(solution[NETS + source], static_cast<T>(REFERENCE.branchCurrents[source]),
                tolerance * T(1e-3))
        << "dim " << dim << ", source " << source;
  }
}

} // namespace

/** @test A ladder with two voltage sources agrees with the dense solver at every dimension */
TEST_F(MnaBatchCudaTest, LadderWithVoltageSourcesMatchesDense) {
  for (const std::size_t DIM : DIMS) {
    expectLadderMatchesDense<double>(DIM, 1e-9);
    expectLadderMatchesDense<float>(DIM, 2e-4F);
  }
}

/* ----------------------------- Failure Reporting ----------------------------- */

/** @test A batch with an all-zero system reports it and solves the others */
TEST_F(MnaBatchCudaTest, SingularMemberReportsFailure) {
  expectMiddleSystemReported<double>(allZeros<double>);
  expectMiddleSystemReported<float>(allZeros<float>);
}

/** @test A system with two equal columns is reported, though no pivot is exactly zero */
TEST_F(MnaBatchCudaTest, RankDeficientMemberReportsFailure) {
  expectMiddleSystemReported<double>(lastTwoColumnsEqual<double>);
  expectMiddleSystemReported<float>(lastTwoColumnsEqual<float>);
  expectMiddleSystemReported<double>(firstAndLastColumnsEqual<double>);
  expectMiddleSystemReported<float>(firstAndLastColumnsEqual<float>);
}

/** @test The floor under a pivot is dim epsilons of its column: just below is no pivot, just above
 * is */
TEST_F(MnaBatchCudaTest, PivotFloorIsDimEpsilonsOfTheColumn) {
  expectMiddleSystemReported<double>(lastPivotBelowFloor<double>);
  expectMiddleSystemReported<float>(lastPivotBelowFloor<float>);

  // Four times the floor: solved. The last two unknowns are 2 and 1.
  const auto SOLVES_ABOVE = [](auto epsilon) {
    using T = decltype(epsilon);
    for (const std::size_t DIM : DIMS) {
      const T EXCESS = T(4) * static_cast<T>(DIM) * epsilon;
      std::vector<T> matrix(DIM * DIM);
      identityWithLastPivot(matrix.data(), DIM, EXCESS);
      std::vector<T> solution(DIM, T(0));
      solution[DIM - 2] = T(3);
      solution[DIM - 1] = T(3) + EXCESS;

      typename Solver<T>::Workspace ws;
      ASSERT_TRUE(ws.prepare(DIM, 1));
      ASSERT_TRUE(Solver<T>::solve(ws, matrix.data(), solution.data(), DIM, 1)) << "dim " << DIM;
      EXPECT_EQ(solution[DIM - 2], T(2)) << "dim " << DIM;
      EXPECT_EQ(solution[DIM - 1], T(1)) << "dim " << DIM;
    }
  };
  SOLVES_ABOVE(std::numeric_limits<double>::epsilon());
  SOLVES_ABOVE(std::numeric_limits<float>::epsilon());
}

/** @test Without the flags the return value alone says a system was not solved */
TEST_F(MnaBatchCudaTest, FailureIsReportedWithoutFlags) {
  constexpr std::size_t DIM = 8;
  std::vector<double> matrices;
  std::vector<double> vectors;
  buildKnownSystems<double>(DIM, 2, 1.0, false, matrices, vectors);
  allZeros(matrices.data() + DIM * DIM, DIM);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(DIM, 2));
  EXPECT_FALSE(cuda::solveBatchCustom(ws, matrices.data(), vectors.data(), DIM, 2));

  // The same workspace, a batch that solves: nothing of the failure is left.
  buildKnownSystems<double>(DIM, 2, 1.0, false, matrices, vectors);
  EXPECT_TRUE(cuda::solveBatchCustom(ws, matrices.data(), vectors.data(), DIM, 2));
  expectKnownSolution(vectors, DIM, 1);
}

/** @test A batch that does not run is refused: no solution written, no system flagged solved */
TEST_F(MnaBatchCudaTest, BatchThatCannotRunIsRefused) {
  constexpr std::size_t DIM = 8;
  std::vector<double> matrices;
  std::vector<double> vectors;
  buildKnownSystems<double>(DIM, 4, 1.0, false, matrices, vectors);
  const std::vector<double> GIVEN = vectors;
  // Each refusal starts from flags all true, so that one refusal's clearing
  // does not stand in for the next one's.
  std::array<bool, 4> solved{};
  const auto FLAGGED_AFTER = [&](auto& workspace, std::size_t dim, std::size_t batch) {
    solved.fill(true);
    EXPECT_FALSE(cuda::solveBatchCustom(workspace, matrices.data(), vectors.data(), dim, batch,
                                        nullptr, solved.data()));
    return std::count(solved.begin(), solved.begin() + static_cast<std::ptrdiff_t>(batch), true);
  };

  cuda::MnaBatchWorkspace unprepared;
  EXPECT_EQ(FLAGGED_AFTER(unprepared, DIM, 4), 0);

  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(16, 2));
  // More systems than the workspace holds, and a dimension with no kernel.
  EXPECT_EQ(FLAGGED_AFTER(ws, DIM, 4), 0);
  EXPECT_EQ(FLAGGED_AFTER(ws, 12, 1), 0);

  EXPECT_EQ(vectors, GIVEN);
}

/** @test An empty batch is solved: there is nothing in it that was not */
TEST_F(MnaBatchCudaTest, EmptyBatchIsSolved) {
  cuda::MnaBatchWorkspace ws;
  ASSERT_TRUE(ws.prepare(8, 4));
  std::vector<double> matrices(64, 1.0);
  std::vector<double> vectors(8, 1.0);
  EXPECT_TRUE(cuda::solveBatchCustom(ws, matrices.data(), vectors.data(), 8, 0));
  EXPECT_EQ(std::count(vectors.begin(), vectors.end(), 1.0), 8);

  // And leaves no CUDA error behind for the next call to trip on.
  buildKnownSystems<double>(8, 1, 1.0, false, matrices, vectors);
  EXPECT_TRUE(cuda::solveBatchCustom(ws, matrices.data(), vectors.data(), 8, 1));
  expectKnownSolution(vectors, 8, 0);
}

/* ----------------------------- Launch Configuration ----------------------------- */

/** @test The reported launch is the launch performed */
TEST_F(MnaBatchCudaTest, LaunchConfigMatchesLaunch) {
  // 8 and 16: one thread per system, 64 systems to a block, nothing shared.
  for (const std::size_t DIM : {std::size_t{8}, std::size_t{16}}) {
    for (const auto& CFG :
         {cuda::getLaunchConfig(DIM, 1000), cuda::getLaunchConfigF32(DIM, 1000)}) {
      EXPECT_EQ(CFG.blockX, 64U) << "dim " << DIM;
      EXPECT_EQ(CFG.gridX, 16U) << "dim " << DIM;
      EXPECT_EQ(CFG.threadsPerSystem, 1U) << "dim " << DIM;
      EXPECT_EQ(CFG.sharedMemBytes, 0U) << "dim " << DIM;
    }
  }

  // 32: one block of 128 threads per system; the matrix, the right-hand
  // side and the partial sums are in shared memory.
  const auto CFG_32 = cuda::getLaunchConfig(32, 1000);
  EXPECT_EQ(CFG_32.blockX, 128U);
  EXPECT_EQ(CFG_32.gridX, 1000U);
  EXPECT_EQ(CFG_32.threadsPerSystem, 128U);
  EXPECT_GE(CFG_32.sharedMemBytes, (32 * 33 + 32 + 128) * sizeof(double));
  EXPECT_LT(CFG_32.sharedMemBytes, (32 * 33 + 32 + 128 + 8) * sizeof(double));

  const auto CFG_32_F32 = cuda::getLaunchConfigF32(32, 1000);
  EXPECT_EQ(CFG_32_F32.blockX, 128U);
  EXPECT_EQ(CFG_32_F32.gridX, 1000U);
  EXPECT_GE(CFG_32_F32.sharedMemBytes, (32 * 33 + 32 + 128) * sizeof(float));
  EXPECT_LT(CFG_32_F32.sharedMemBytes, (32 * 33 + 32 + 128 + 8) * sizeof(float));

  // 64 in double precision: one block of 256 threads; the matrix stays in
  // global memory, so shared memory holds a pivot row, the right-hand side
  // and eight partial sums.
  const auto CFG_64 = cuda::getLaunchConfig(64, 1000);
  EXPECT_EQ(CFG_64.blockX, 256U);
  EXPECT_EQ(CFG_64.gridX, 1000U);
  EXPECT_EQ(CFG_64.threadsPerSystem, 256U);
  EXPECT_GE(CFG_64.sharedMemBytes, (65 + 64 + 8) * sizeof(double));
  EXPECT_LT(CFG_64.sharedMemBytes, (65 + 64 + 8 + 8) * sizeof(double));

  // 64 in single precision: the matrix is in shared memory.
  const auto CFG_64_F32 = cuda::getLaunchConfigF32(64, 1000);
  EXPECT_EQ(CFG_64_F32.blockX, 256U);
  EXPECT_EQ(CFG_64_F32.gridX, 1000U);
  EXPECT_GE(CFG_64_F32.sharedMemBytes, (64 * 65 + 64 + 256) * sizeof(float));
  EXPECT_LT(CFG_64_F32.sharedMemBytes, (64 * 65 + 64 + 256 + 8) * sizeof(float));

  // A dimension with no kernel has no launch.
  const auto NONE = cuda::getLaunchConfig(12, 1000);
  EXPECT_EQ(NONE.gridX, 0U);
  EXPECT_EQ(NONE.blockX, 0U);
}
