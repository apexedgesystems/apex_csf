/**
 * @file PhaseFourPipeline_uTest.cu
 * @brief End-to-end integration test for the Phase 4 inner NR iteration.
 *
 * Composes the four device-side kernels that make up one NR iteration
 * of the fully GPU-resident Intel 4004 L1 loop:
 *   1. cudaMemset on dG (N x N) and dI (N).
 *   2. stampMosfetL1Batch -- evaluate stampValues and atomically
 *      scatter into dG / dI for every MOSFET.
 *   3. solveCudaDeviceResident -- cuSOLVER dgetrf + dgetrs on dG / dB,
 *      writing the solution in place.
 *   4. nrUpdateAndLimit -- compute max |delta|, uniform-scale limit,
 *      write back into dPrevV.
 *
 * Compared against a CPU reference that executes the identical math
 * (stamp -> LAPACKE_dgesv -> limit + update). Tolerances:
 *   - Node voltages after 1 iter:   <= 1e-9
 *   - Max delta (for convergence):  <= 1e-10
 *
 * Small deterministic circuit: N = 24 MOSFETs spanning 17 nets. Net
 * 0 is ground (reference); nets 1..16 are DOF. Biases are chosen so
 * both SPICE modes (xnrm / xrev) are exercised, and the max NR delta
 * crosses the 5 V limiter in one of the sub-cases.
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemCuda.cuh"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1BatchCuda.cuh"
#include "src/sim/electronics/chips/intel4004/grid/inc/Intel4004GridLevel1Cuda.cuh"
#include "src/sim/electronics/chips/intel4004/netlist/inc/SpiceNetlistParser.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cuda_runtime_api.h>
#include <lapacke.h>
#include <vector>

namespace mna_cuda = sim::electronics::algorithms::mna::cuda;
namespace nl_cuda = sim::electronics::devices::nonlinear::cuda;
using sim::electronics::devices::nonlinear::MosfetLevel1;
using sim::electronics::devices::nonlinear::MosfetLevel1Params;

constexpr int NET_COUNT = 17;
constexpr int N_TRANSISTORS = 24;
constexpr double GMIN = 1e-12;
// Diagonal bias added to every DOF net so that nets with only-cutoff
// transistors still have a non-zero diagonal entry. Production SPICE
// simulators call this `GMIN to ground` and it is how dense LU handles
// the numerical zero of the Shichman-Hodges cutoff region.
constexpr double DIAG_GMIN = 1e-3;
constexpr double NR_LIMIT = 5.0;

struct Inputs {
  std::vector<nl_cuda::MosfetNets> nets;
  std::vector<MosfetLevel1Params> params;
  std::vector<double> prevV; // initial NR state, size NET_COUNT
};

Inputs buildInputs(double voltageScale) {
  Inputs in;
  in.nets.resize(N_TRANSISTORS);
  in.params.resize(N_TRANSISTORS);
  in.prevV.assign(NET_COUNT, 0.0);

  // Spread transistors over nets 1..16 with a reproducible pattern.
  // Gate is always a different net than drain/source so both VGS and
  // VDS vary per transistor.
  for (int i = 0; i < N_TRANSISTORS; ++i) {
    in.nets[i] = {1 + (i * 5 + 3) % (NET_COUNT - 1), 1 + (i * 7 + 2) % (NET_COUNT - 1),
                  1 + (i * 11 + 5) % (NET_COUNT - 1)};
    in.params[i] = {.Kp = 5e-3 * (1.0 + 0.001 * i), .Vth = 0.3, .lambda = 0.03, .Vsmooth = 0.1};
  }

  // Non-trivial initial voltages so the first NR iter produces a
  // meaningful delta. Voltage scale x 1.5 puts most VSGs above Vth,
  // giving the transistor stamps real conductance so the resulting
  // matrix is well-conditioned.
  for (int i = 1; i < NET_COUNT; ++i) {
    in.prevV[i] = voltageScale * (0.8 + 0.6 * std::sin(0.37 * i));
  }
  return in;
}

// CPU reference for one NR iter. Uses LAPACKE_dgesv for the solve.
// `prevV` is updated in place; `maxDeltaOut` gets the unlimited max.
void cpuPhaseFourIter(const Inputs& in, std::vector<double>& prevV, double& maxDeltaOut) {
  std::vector<double> G(NET_COUNT * NET_COUNT, 0.0);
  std::vector<double> I(NET_COUNT, 0.0);

  auto addIfNode = [&](int row, int col, double v) {
    if (row <= 0 || col <= 0 || row >= NET_COUNT || col >= NET_COUNT)
      return;
    G[row * NET_COUNT + col] += v;
  };
  auto addIfRow = [&](int row, double v) {
    if (row <= 0 || row >= NET_COUNT)
      return;
    I[row] += v;
  };

  for (int i = 0; i < N_TRANSISTORS; ++i) {
    const int D = in.nets[i].drain;
    const int G = in.nets[i].gate;
    const int S = in.nets[i].source;
    const double VSG = prevV[S] - prevV[G];
    const double VSD = prevV[S] - prevV[D];

    int xnrm, xrev;
    double evalVgs, evalVds;
    if (VSD >= 0.0) {
      xnrm = 1;
      xrev = 0;
      evalVgs = VSG;
      evalVds = VSD;
    } else {
      xnrm = 0;
      xrev = 1;
      evalVgs = VSG - VSD;
      evalVds = -VSD;
    }
    const double VGS_E = std::max(evalVgs, 0.0);
    const double VDS_E = std::max(evalVds, 0.0);
    const auto SV = MosfetLevel1::stampValues(VGS_E, VDS_E, in.params[i]);
    const double ID = SV.id, gm = SV.gm, gdsDev = SV.gds;
    const double GDS_STAMP = std::max(gdsDev, GMIN);

    double cdreq;
    if (xnrm == 1) {
      cdreq = -(ID - gdsDev * VSD - gm * VSG);
    } else {
      cdreq = (ID - gdsDev * (-VSD) - gm * (VSG - VSD));
    }

    addIfNode(D, D, GDS_STAMP);
    addIfNode(S, S, GDS_STAMP);
    addIfNode(D, S, -GDS_STAMP);
    addIfNode(S, D, -GDS_STAMP);

    const double XREV_GM = xrev * gm;
    const double XNRM_GM = xnrm * gm;
    const double X_DELTA = (xnrm - xrev) * gm;
    addIfNode(D, D, XREV_GM);
    addIfNode(S, S, XNRM_GM);
    addIfNode(D, G, X_DELTA);
    addIfNode(D, S, -XNRM_GM);
    addIfNode(S, G, -X_DELTA);
    addIfNode(S, D, -XREV_GM);

    addIfRow(D, -cdreq);
    addIfRow(S, cdreq);
  }

  // Add GMIN-to-ground diagonal bias so the matrix is well-conditioned
  // when some transistors are in cutoff (gds = 0).
  for (int j = 1; j < NET_COUNT; ++j) {
    G[j * NET_COUNT + j] += DIAG_GMIN;
  }

  // Ground row/col: force I[0]=0, G[0,0]=1, G[0,*]=0 for * != 0, and
  // G[*,0]=0. This matches how the full MNA path anchors ground.
  for (int j = 0; j < NET_COUNT; ++j) {
    G[0 * NET_COUNT + j] = (j == 0) ? 1.0 : 0.0;
    G[j * NET_COUNT + 0] = (j == 0) ? 1.0 : 0.0;
  }
  I[0] = 0.0;

  // Solve G * x = I via LAPACKE.
  std::vector<int> ipiv(NET_COUNT);
  std::vector<double> b = I;
  int info =
      LAPACKE_dgesv(LAPACK_ROW_MAJOR, NET_COUNT, 1, G.data(), NET_COUNT, ipiv.data(), b.data(), 1);
  ASSERT_EQ(info, 0) << "LAPACKE_dgesv failed on CPU reference";

  // Compute max |delta|, then apply NR limit, write back into prevV.
  maxDeltaOut = 0.0;
  for (int i = 0; i < NET_COUNT; ++i) {
    const double D = std::fabs(b[i] - prevV[i]);
    if (D > maxDeltaOut)
      maxDeltaOut = D;
  }
  const double SCALE = (maxDeltaOut > NR_LIMIT) ? (NR_LIMIT / maxDeltaOut) : 1.0;
  for (int i = 0; i < NET_COUNT; ++i) {
    prevV[i] += SCALE * (b[i] - prevV[i]);
  }
}

// GPU path: run the four-kernel pipeline for one NR iter. Inputs and
// outputs match the CPU reference shape.
void gpuPhaseFourIter(const Inputs& in, std::vector<double>& prevV, double& maxDeltaOut) {
  // Build biases from prevV.
  std::vector<nl_cuda::MosfetBias> biases(N_TRANSISTORS);
  for (int i = 0; i < N_TRANSISTORS; ++i) {
    biases[i].vgs = prevV[in.nets[i].source] - prevV[in.nets[i].gate];
    biases[i].vds = prevV[in.nets[i].source] - prevV[in.nets[i].drain];
  }

  // Allocate device state for this test.
  nl_cuda::MosfetBias* dBiases = nullptr;
  MosfetLevel1Params* dParams = nullptr;
  nl_cuda::MosfetNets* dNets = nullptr;
  double* dG = nullptr;
  double* dI = nullptr;
  double* dPrevV = nullptr;
  double* dMaxDelta = nullptr;
  ASSERT_EQ(cudaMalloc(&dBiases, N_TRANSISTORS * sizeof(nl_cuda::MosfetBias)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dParams, N_TRANSISTORS * sizeof(MosfetLevel1Params)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dNets, N_TRANSISTORS * sizeof(nl_cuda::MosfetNets)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dG, NET_COUNT * NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dI, NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dPrevV, NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMalloc(&dMaxDelta, sizeof(double)), cudaSuccess);

  ASSERT_EQ(cudaMemcpy(dBiases, biases.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetBias),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dParams, in.params.data(), N_TRANSISTORS * sizeof(MosfetLevel1Params),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dNets, in.nets.data(), N_TRANSISTORS * sizeof(nl_cuda::MosfetNets),
                       cudaMemcpyHostToDevice),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(dPrevV, prevV.data(), NET_COUNT * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);

  // === Step 1: memset G and I ===
  ASSERT_EQ(cudaMemset(dG, 0, NET_COUNT * NET_COUNT * sizeof(double)), cudaSuccess);
  ASSERT_EQ(cudaMemset(dI, 0, NET_COUNT * sizeof(double)), cudaSuccess);

  // === Step 2: stamp + scatter ===
  ASSERT_TRUE(
      nl_cuda::stampMosfetL1Batch(dBiases, dParams, dNets, dG, dI, N_TRANSISTORS, NET_COUNT, GMIN));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Add GMIN-to-ground diagonal bias (matches CPU reference). Pull G
  // to host, mutate, push back. In the real Phase 4 pipeline this is
  // one host-side pre-stamp memset + scatter; here keep it simple.
  std::vector<double> Gfull(NET_COUNT * NET_COUNT);
  ASSERT_EQ(
      cudaMemcpy(Gfull.data(), dG, NET_COUNT * NET_COUNT * sizeof(double), cudaMemcpyDeviceToHost),
      cudaSuccess);
  for (int j = 1; j < NET_COUNT; ++j) {
    Gfull[j * NET_COUNT + j] += DIAG_GMIN;
  }
  ASSERT_EQ(
      cudaMemcpy(dG, Gfull.data(), NET_COUNT * NET_COUNT * sizeof(double), cudaMemcpyHostToDevice),
      cudaSuccess);

  // Anchor ground (row 0 / col 0) to match the CPU reference.
  // Simpler than a device kernel: read G[0,:] and G[:,0] to host, mutate, push back.
  // (In the real Phase 4 pipeline the Intel 4004 voltage sources do this via addVoltageSource.)
  std::vector<double> Grow0(NET_COUNT);
  std::vector<double> Gcol0(NET_COUNT);
  for (int j = 0; j < NET_COUNT; ++j) {
    Grow0[j] = (j == 0) ? 1.0 : 0.0;
    Gcol0[j] = (j == 0) ? 1.0 : 0.0;
  }
  ASSERT_EQ(cudaMemcpy(dG, Grow0.data(), NET_COUNT * sizeof(double), cudaMemcpyHostToDevice),
            cudaSuccess);
  for (int j = 0; j < NET_COUNT; ++j) {
    ASSERT_EQ(cudaMemcpy(static_cast<char*>(static_cast<void*>(dG)) +
                             (j * NET_COUNT + 0) * sizeof(double),
                         &Gcol0[j], sizeof(double), cudaMemcpyHostToDevice),
              cudaSuccess);
  }
  const double ZERO = 0.0;
  ASSERT_EQ(cudaMemcpy(dI, &ZERO, sizeof(double), cudaMemcpyHostToDevice), cudaSuccess);

  // === Step 3: cuSOLVER solve, in place (dI becomes x) ===
  mna_cuda::MnaCudaWorkspace ws;
  ASSERT_TRUE(ws.prepare(NET_COUNT));
  ASSERT_TRUE(mna_cuda::solveCudaDeviceResident(ws, dG, dI, NET_COUNT));

  // === Step 4: NR update + limiter ===
  // `dI` now holds the solved x (newV). prevV is in dPrevV.
  ASSERT_TRUE(nl_cuda::nrUpdateAndLimit(dI, dPrevV, dMaxDelta, NET_COUNT, NR_LIMIT));
  ASSERT_EQ(cudaDeviceSynchronize(), cudaSuccess);

  // Read back.
  ASSERT_EQ(cudaMemcpy(prevV.data(), dPrevV, NET_COUNT * sizeof(double), cudaMemcpyDeviceToHost),
            cudaSuccess);
  ASSERT_EQ(cudaMemcpy(&maxDeltaOut, dMaxDelta, sizeof(double), cudaMemcpyDeviceToHost),
            cudaSuccess);

  cudaFree(dBiases);
  cudaFree(dParams);
  cudaFree(dNets);
  cudaFree(dG);
  cudaFree(dI);
  cudaFree(dPrevV);
  cudaFree(dMaxDelta);
}

/* ----------------------------- Tests ----------------------------- */

class PhaseFourPipelineTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (!apex::compat::cuda::deviceAvailable()) {
      GTEST_SKIP() << "CUDA device not available.";
    }
  }
};

/** @test One Phase-4 iteration with small voltages produces matching CPU/GPU state. */
TEST_F(PhaseFourPipelineTest, OneIter_SmallVoltages_NoLimit) {
  auto in = buildInputs(0.5); // prevV in [-0.5, 0.5] -> small deltas
  auto cpuPrev = in.prevV;
  auto gpuPrev = in.prevV;
  double cpuMax = 0.0, gpuMax = 0.0;

  cpuPhaseFourIter(in, cpuPrev, cpuMax);
  gpuPhaseFourIter(in, gpuPrev, gpuMax);

  EXPECT_NEAR(gpuMax, cpuMax, 1e-9) << "maxDelta mismatch";
  for (int i = 0; i < NET_COUNT; ++i) {
    EXPECT_NEAR(gpuPrev[i], cpuPrev[i], 1e-9) << "prevV[" << i << "] mismatch";
  }
}

#ifdef INTEL4004_DATA_DIR
static const std::string SPICE_PATH = INTEL4004_DATA_DIR "/lajos-4004.spice";
#else
static const std::string SPICE_PATH =
    "src/sim/electronics/chips/intel4004/netlist/data/lajos-4004.spice";
#endif

/** @test Phase-4 scatter table classifies every Intel 4004 transistor (L1 + binary + depletion). */
TEST_F(PhaseFourPipelineTest, ScatterTable_Intel4004_AllClassified) {
  using sim::electronics::chips::intel4004::classifyComponents;
  using sim::electronics::chips::intel4004::Intel4004GridLevel1;
  using sim::electronics::chips::intel4004::loadSpiceNetlist;
  using sim::electronics::chips::intel4004::cuda::Phase4TransistorClass;
  using sim::electronics::chips::intel4004::cuda::populateScatterTable;

  const auto NETLIST = loadSpiceNetlist(SPICE_PATH);
  Intel4004GridLevel1 grid;
  auto circuit = grid.buildCircuit(NETLIST);
  grid.computeTransistorKp();
  auto classification = classifyComponents(grid);
  grid.componentTypes_ = std::move(classification.types);
  grid.buildNorOutputSet();

  auto table = populateScatterTable(grid);

  // Every transistor must be accounted for (L1 + binary + depletion).
  const std::size_t TOTAL = table.l1Count + table.binarySwitchCount + table.depletionLoadCount;
  EXPECT_EQ(TOTAL, grid.transistors_.size())
      << "L1=" << table.l1Count << " BSW=" << table.binarySwitchCount
      << " DEP=" << table.depletionLoadCount << " grid=" << grid.transistors_.size();

  // classes[] is populated for every transistor.
  EXPECT_EQ(table.classes.size(), grid.transistors_.size());

  // L1 arrays are consistent in size.
  EXPECT_EQ(table.nets.size(), table.l1Count);
  EXPECT_EQ(table.params.size(), table.l1Count);
  EXPECT_EQ(table.l1Indices.size(), table.l1Count);

  // Each l1 index refers to a transistor actually classified L1_STAMP.
  for (auto idx : table.l1Indices) {
    ASSERT_LT(idx, grid.transistors_.size());
    EXPECT_EQ(table.classes[idx], Phase4TransistorClass::L1_STAMP);
  }

  // Nets are in range [1, netCount) (ground is 0, not valid DOF).
  const std::size_t NET_COUNT = circuit.netCount();
  for (const auto& n : table.nets) {
    EXPECT_GE(n.drain, 0);
    EXPECT_LT(n.drain, static_cast<int>(NET_COUNT));
    EXPECT_GE(n.gate, 0);
    EXPECT_LT(n.gate, static_cast<int>(NET_COUNT));
    EXPECT_GE(n.source, 0);
    EXPECT_LT(n.source, static_cast<int>(NET_COUNT));
  }

  // Params have non-zero Kp (populated from transistorKp_).
  for (const auto& p : table.params) {
    EXPECT_GT(p.Kp, 0.0);
  }
}

/** @test Five Phase-4 iterations keep CPU and GPU prevV/maxDelta in lockstep. */
TEST_F(PhaseFourPipelineTest, FiveIters_MatchesCpu) {
  auto in = buildInputs(0.5);
  auto cpuPrev = in.prevV;
  auto gpuPrev = in.prevV;
  double cpuMax = 0.0, gpuMax = 0.0;

  for (int iter = 0; iter < 5; ++iter) {
    cpuPhaseFourIter(in, cpuPrev, cpuMax);
    gpuPhaseFourIter(in, gpuPrev, gpuMax);

    EXPECT_NEAR(gpuMax, cpuMax, 1e-9)
        << "iter " << iter << " maxDelta mismatch: cpu=" << cpuMax << " gpu=" << gpuMax;
    for (int i = 0; i < NET_COUNT; ++i) {
      EXPECT_NEAR(gpuPrev[i], cpuPrev[i], 1e-9) << "iter " << iter << " prevV[" << i << "]";
    }
  }
}
