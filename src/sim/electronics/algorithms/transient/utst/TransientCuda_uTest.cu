/**
 * @file TransientCuda_uTest.cu
 * @brief Unit tests for CUDA-accelerated transient simulation.
 *
 * Tests GPU transient stepping and verifies CPU fallback behavior
 * for small circuits below the 100-net GPU threshold.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientCuda.cuh"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystem.hpp"
#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"

#include <gtest/gtest.h>

#include <cuda_runtime.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using sim::electronics::algorithms::companions::CompanionSet;
using sim::electronics::algorithms::mna::MnaSolveWorkspace;
using sim::electronics::algorithms::mna::MnaSystem;
using sim::electronics::algorithms::mna::cuda::MnaCudaWorkspace;
using sim::electronics::algorithms::transient::IntegrationMethod;
using sim::electronics::algorithms::transient::TransientSolver;
using sim::electronics::algorithms::transient::TransientState;
using sim::electronics::algorithms::transient::TransientStatus;
using sim::electronics::algorithms::transient::cuda::available;
using sim::electronics::algorithms::transient::cuda::stepCuda;

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

/* ----------------------------- Constants ----------------------------- */

static constexpr double VDD = 5.0;
static constexpr IntegrationMethod METHOD = IntegrationMethod::BACKWARD_EULER;

/* ----------------------------- CPU Fallback Tests ----------------------------- */

/** @test Small RC circuit falls back to CPU (below 100-net threshold). */
TEST(TransientCudaTest, SmallCircuitFallsBackToCPU) {
  // 3-net RC circuit: Gnd(0), Vdd(1), Vout(2)
  // Forces CPU fallback path
  MnaSystem mna(3);
  CompanionSet companions;
  companions.addCapacitor(2, 0, 1e-6);

  double R = 1000.0;
  double G = 1.0 / R;

  sim::electronics::algorithms::transient::StampCallback stampCb = [G](MnaSystem& m,
                                                                       double /*time*/) {
    m.addVoltageSource(1, 0, VDD);
    m.addConductance(1, 2, G);
  };

  MnaCudaWorkspace cudaWs;
  MnaSolveWorkspace workspace;
  workspace.prepare(32);

  double time = 0.0;
  std::vector<double> prevVoltages(3, 0.0);
  TransientState state;
  state.resize(3, 0);

  double dt = 1e-7;

  // stepCuda should return ERROR_STEP_FAILED for small circuits,
  // signalling the caller to use CPU solve instead.
  TransientStatus status =
      stepCuda(cudaWs, mna, companions, stampCb, dt, METHOD, time, prevVoltages, workspace, state);

  EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED)
      << "Small circuit should fall back (return error) for caller to use CPU";

  // Time should not have advanced
  EXPECT_DOUBLE_EQ(time, 0.0);
}

/** @test Stateful stamp callback also falls back for small circuits. */
TEST(TransientCudaTest, StatefulCallbackSmallCircuitFallback) {
  MnaSystem mna(3);
  CompanionSet companions;
  companions.addCapacitor(2, 0, 1e-6);

  double R = 1000.0;
  double G = 1.0 / R;

  sim::electronics::algorithms::transient::StatefulStampCallback stampCb =
      [G](MnaSystem& m, double /*time*/, const std::vector<double>& /*prevV*/) {
        m.addVoltageSource(1, 0, VDD);
        m.addConductance(1, 2, G);
      };

  MnaCudaWorkspace cudaWs;
  MnaSolveWorkspace workspace;
  workspace.prepare(32);

  double time = 0.0;
  std::vector<double> prevVoltages(3, 0.0);
  TransientState state;
  state.resize(3, 0);

  double dt = 1e-7;

  TransientStatus status =
      stepCuda(cudaWs, mna, companions, stampCb, dt, METHOD, time, prevVoltages, workspace, state);

  EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED)
      << "Stateful callback variant should also fall back for small circuits";
}

/* ----------------------------- Availability Tests ----------------------------- */

/** @test available() returns consistent value. */
TEST(TransientCudaTest, AvailabilityConsistent) {
  // available() should match MNA CUDA availability
  bool transientAvail = available();
  bool mnaAvail = sim::electronics::algorithms::mna::cuda::available();
  EXPECT_EQ(transientAvail, mnaAvail)
      << "Transient CUDA availability should match MNA CUDA availability";
}

/** @test Uninitialized workspace rejects any dimension. */
TEST(TransientCudaTest, UninitializedWorkspaceCannotHandle) {
  MnaCudaWorkspace cudaWs;
  EXPECT_FALSE(cudaWs.canHandle(1));
  EXPECT_FALSE(cudaWs.canHandle(100));
}

/* ----------------------------- GPU Path Tests ----------------------------- */

/** @test stepCuda with uninitialized workspace returns error. */
TEST(TransientCudaTest, UninitializedWorkspaceReturnsError) {
  // Create a circuit large enough to pass the 100-net threshold check
  // but with uninitialized workspace, so GPU path cannot proceed.
  constexpr std::size_t NET_COUNT = 110;
  MnaSystem mna(NET_COUNT);
  CompanionSet companions;

  sim::electronics::algorithms::transient::StampCallback stampCb = [](MnaSystem& /*m*/,
                                                                      double /*time*/) {};

  // Workspace not prepared (initialized = false)
  MnaCudaWorkspace cudaWs;
  MnaSolveWorkspace workspace;
  workspace.prepare(NET_COUNT + 16);

  double time = 0.0;
  std::vector<double> prevVoltages(NET_COUNT, 0.0);
  TransientState state;
  state.resize(NET_COUNT, 0);

  double dt = 1e-7;

  if (!available()) {
    // CUDA not compiled in: should fail at availability check
    TransientStatus status = stepCuda(cudaWs, mna, companions, stampCb, dt, METHOD, time,
                                      prevVoltages, workspace, state);
    EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED);
  } else {
    // CUDA compiled in but workspace not prepared: should fail at canHandle
    TransientStatus status = stepCuda(cudaWs, mna, companions, stampCb, dt, METHOD, time,
                                      prevVoltages, workspace, state);
    EXPECT_EQ(status, TransientStatus::ERROR_STEP_FAILED);
  }
}

/* ----------------------------- Device Step ----------------------------- */

namespace {

constexpr std::size_t LADDER_NETS = 102; // Ground, the source and a hundred rungs.

/** @brief 5 V on net 1 and 1 kohm from each net to the next, along a ladder. */
void stampLadder(MnaSystem& mna) {
  mna.addVoltageSource(1, 0, VDD);
  for (std::uint32_t k = 1; k + 1 < LADDER_NETS; ++k) {
    mna.addConductance(k, k + 1, 1e-3);
  }
}

/** @brief 1 nF from every rung to ground. */
void addLadderCapacitors(CompanionSet& companions) {
  for (std::uint32_t k = 2; k < LADDER_NETS; ++k) {
    companions.addCapacitor(k, 0, 1e-9);
  }
}

/** @brief Whether a CUDA device is there to run on. */
bool deviceThere() {
  int devices = 0;
  return available() && cudaGetDeviceCount(&devices) == cudaSuccess && devices > 0;
}

} // namespace

/**
 * @test A GPU step is the step TransientSolver takes, by each method, with
 *       one MnaSystem used for every step.
 *
 * The ladder charges over twenty steps of 1 us. The GPU steps stamp into a
 * system that is not cleared by the test; the CPU solver steps the same
 * circuit by the same method.
 */
TEST(TransientCudaTest, DeviceStepMatchesTheSolverByEveryMethod) {
  if (!deviceThere()) {
    GTEST_SKIP() << "No CUDA device.";
  }
  for (const IntegrationMethod CASE : {IntegrationMethod::BACKWARD_EULER,
                                       IntegrationMethod::TRAPEZOIDAL, IntegrationMethod::GEAR2}) {
    SCOPED_TRACE(static_cast<int>(CASE));
    TransientSolver solver(LADDER_NETS);
    addLadderCapacitors(solver.companions());
    solver.setStampCallback([](MnaSystem& mna, double /*seconds*/) { stampLadder(mna); });
    solver.setIntegrationMethod(CASE);
    TransientState cpu;
    cpu.resize(LADDER_NETS, 1);

    MnaSystem mna(LADDER_NETS);
    CompanionSet companions;
    addLadderCapacitors(companions);
    const sim::electronics::algorithms::transient::StampCallback STAMP =
        [](MnaSystem& system, double /*seconds*/) { stampLadder(system); };
    MnaCudaWorkspace cudaWs;
    ASSERT_TRUE(cudaWs.prepare(LADDER_NETS + 1));
    MnaSolveWorkspace workspace;
    double time = 0.0;
    std::vector<double> prevVoltages(LADDER_NETS, 0.0);
    TransientState gpu;
    gpu.resize(LADDER_NETS, 1);

    for (int step = 1; step <= 20; ++step) {
      ASSERT_EQ(solver.step(1e-6, cpu), TransientStatus::SUCCESS);
      ASSERT_EQ(
          stepCuda(cudaWs, mna, companions, STAMP, 1e-6, CASE, time, prevVoltages, workspace, gpu),
          TransientStatus::SUCCESS);
      EXPECT_EQ(mna.voltageSourceCount(), 1U) << "step " << step;
      for (std::size_t net = 1; net < LADDER_NETS; ++net) {
        ASSERT_NEAR(gpu.voltage(net), cpu.voltage(net), 1e-9) << "step " << step << ", net " << net;
      }
    }
    EXPECT_DOUBLE_EQ(time, solver.time());
    EXPECT_GT(cpu.voltage(2), 1.0)
        << "the ladder has charged: the comparison is of a moving circuit";
    cudaWs.release();
  }
}

/** @test The stateful form hands the callback the voltages of the step before. */
TEST(TransientCudaTest, StatefulDeviceStepPassesThePreviousVoltages) {
  if (!deviceThere()) {
    GTEST_SKIP() << "No CUDA device.";
  }
  MnaSystem mna(LADDER_NETS);
  CompanionSet companions;
  addLadderCapacitors(companions);
  std::vector<double> seen;
  const sim::electronics::algorithms::transient::StatefulStampCallback STAMP =
      [&seen](MnaSystem& system, double /*seconds*/, const std::vector<double>& previous) {
        seen = previous;
        stampLadder(system);
      };
  MnaCudaWorkspace cudaWs;
  ASSERT_TRUE(cudaWs.prepare(LADDER_NETS + 1));
  MnaSolveWorkspace workspace;
  double time = 0.0;
  std::vector<double> prevVoltages(LADDER_NETS, 0.0);
  TransientState state;
  state.resize(LADDER_NETS, 1);

  ASSERT_EQ(
      stepCuda(cudaWs, mna, companions, STAMP, 1e-6, METHOD, time, prevVoltages, workspace, state),
      TransientStatus::SUCCESS);
  const std::vector<double> FIRST = state.nodeVoltages;
  ASSERT_EQ(
      stepCuda(cudaWs, mna, companions, STAMP, 1e-6, METHOD, time, prevVoltages, workspace, state),
      TransientStatus::SUCCESS);
  EXPECT_EQ(seen, FIRST);
  EXPECT_DOUBLE_EQ(time, 2e-6);
  cudaWs.release();
}
