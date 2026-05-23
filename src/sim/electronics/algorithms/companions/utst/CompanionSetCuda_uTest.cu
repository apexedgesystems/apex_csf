/**
 * @file CompanionSetCuda_uTest.cu
 * @brief Unit tests for the CUDA companion-evaluation kernels.
 *
 * Verifies that GPU evaluation of CapacitorCompanion / InductorCompanion
 * produces the same Geq + Ieq as the per-device CPU formulas across
 * Backward Euler, Trapezoidal, and GEAR2 integration methods.
 *
 * Notes:
 *  - Each test gracefully skips if no CUDA runtime is available.
 *  - Tolerances are tight (1e-12) since the GPU and CPU paths share the
 *    same closed-form math; only host-device transfer is in question.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/companions/inc/CompanionSetCuda.hpp"
#include "src/utilities/compatibility/inc/compat_cuda_blas.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

using sim::electronics::algorithms::companions::CapacitorCompanion;
using sim::electronics::algorithms::companions::InductorCompanion;
using sim::electronics::algorithms::companions::cuda::evaluateCapacitorsCuda;
using sim::electronics::algorithms::companions::cuda::evaluateInductorsCuda;
using sim::electronics::algorithms::transient::IntegrationMethod;

namespace {

constexpr double TOL = 1e-12;

class CompanionSetCudaTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (!apex::compat::cuda::runtimeAvailable()) {
      GTEST_SKIP() << "CUDA runtime not available.";
    }
  }
};

std::vector<CapacitorCompanion> buildCaps(std::size_t n) {
  std::vector<CapacitorCompanion> caps(n);
  for (std::size_t i = 0; i < n; ++i) {
    caps[i].posNet = static_cast<unsigned>(i + 1);
    caps[i].negNet = 0;
    caps[i].capacitance = 1e-9 * (1.0 + 0.01 * static_cast<double>(i));
    caps[i].prevVoltage = 0.1 * static_cast<double>(i % 10);
    caps[i].prev2Voltage = 0.05 * static_cast<double>(i % 10);
    caps[i].current = 0.0;
  }
  return caps;
}

std::vector<InductorCompanion> buildInductors(std::size_t n) {
  std::vector<InductorCompanion> inds(n);
  for (std::size_t i = 0; i < n; ++i) {
    inds[i].posNet = static_cast<unsigned>(i + 1);
    inds[i].negNet = 0;
    inds[i].inductance = 1e-3 * (1.0 + 0.01 * static_cast<double>(i));
    inds[i].prevCurrent = 1e-3 * static_cast<double>(i % 5);
    inds[i].prev2Current = 0.5e-3 * static_cast<double>(i % 5);
    inds[i].voltage = 0.0;
  }
  return inds;
}

} // namespace

/* ----------------------------- Capacitor Geq/Ieq parity ----------------------------- */

/** @test GPU capacitor evaluation matches CPU under Backward Euler */
TEST_F(CompanionSetCudaTest, CapacitorBackwardEulerMatchesCpu) {
  const auto CAPS = buildCaps(/*n=*/256);
  const double DT = 1e-6;
  std::vector<double> geq(CAPS.size()), ieq(CAPS.size());
  evaluateCapacitorsCuda(CAPS.data(), static_cast<int>(CAPS.size()), DT,
                         IntegrationMethod::BACKWARD_EULER, geq.data(), ieq.data());

  for (std::size_t i = 0; i < CAPS.size(); ++i) {
    EXPECT_NEAR(geq[i], CAPS[i].geq(DT, IntegrationMethod::BACKWARD_EULER), TOL);
    EXPECT_NEAR(ieq[i], CAPS[i].ieq(DT, IntegrationMethod::BACKWARD_EULER), TOL);
  }
}

/** @test GPU capacitor evaluation matches CPU under Trapezoidal */
TEST_F(CompanionSetCudaTest, CapacitorTrapezoidalMatchesCpu) {
  auto caps = buildCaps(/*n=*/512);
  for (auto& c : caps)
    c.current = 1e-4 * c.capacitance; // non-zero current term
  const double DT = 1e-6;
  std::vector<double> geq(caps.size()), ieq(caps.size());
  evaluateCapacitorsCuda(caps.data(), static_cast<int>(caps.size()), DT,
                         IntegrationMethod::TRAPEZOIDAL, geq.data(), ieq.data());

  for (std::size_t i = 0; i < caps.size(); ++i) {
    EXPECT_NEAR(geq[i], caps[i].geq(DT, IntegrationMethod::TRAPEZOIDAL), TOL);
    EXPECT_NEAR(ieq[i], caps[i].ieq(DT, IntegrationMethod::TRAPEZOIDAL), TOL);
  }
}

/** @test GPU capacitor evaluation matches CPU under GEAR2 */
TEST_F(CompanionSetCudaTest, CapacitorGear2MatchesCpu) {
  const auto CAPS = buildCaps(/*n=*/100);
  const double DT = 5e-7;
  std::vector<double> geq(CAPS.size()), ieq(CAPS.size());
  evaluateCapacitorsCuda(CAPS.data(), static_cast<int>(CAPS.size()), DT, IntegrationMethod::GEAR2,
                         geq.data(), ieq.data());

  for (std::size_t i = 0; i < CAPS.size(); ++i) {
    EXPECT_NEAR(geq[i], CAPS[i].geq(DT, IntegrationMethod::GEAR2), TOL);
    EXPECT_NEAR(ieq[i], CAPS[i].ieq(DT, IntegrationMethod::GEAR2), TOL);
  }
}

/* ----------------------------- Inductor Geq/Ieq parity ----------------------------- */

/** @test GPU inductor evaluation matches CPU under Backward Euler */
TEST_F(CompanionSetCudaTest, InductorBackwardEulerMatchesCpu) {
  const auto INDS = buildInductors(/*n=*/200);
  const double DT = 1e-7;
  std::vector<double> geq(INDS.size()), ieq(INDS.size());
  evaluateInductorsCuda(INDS.data(), static_cast<int>(INDS.size()), DT,
                        IntegrationMethod::BACKWARD_EULER, geq.data(), ieq.data());

  for (std::size_t i = 0; i < INDS.size(); ++i) {
    EXPECT_NEAR(geq[i], INDS[i].geq(DT, IntegrationMethod::BACKWARD_EULER), TOL);
    EXPECT_NEAR(ieq[i], INDS[i].ieq(DT, IntegrationMethod::BACKWARD_EULER), TOL);
  }
}

/** @test GPU inductor evaluation matches CPU under Trapezoidal */
TEST_F(CompanionSetCudaTest, InductorTrapezoidalMatchesCpu) {
  auto inds = buildInductors(/*n=*/300);
  for (auto& i : inds)
    i.voltage = 1e-3; // non-zero voltage history
  const double DT = 1e-7;
  std::vector<double> geq(inds.size()), ieq(inds.size());
  evaluateInductorsCuda(inds.data(), static_cast<int>(inds.size()), DT,
                        IntegrationMethod::TRAPEZOIDAL, geq.data(), ieq.data());

  for (std::size_t i = 0; i < inds.size(); ++i) {
    EXPECT_NEAR(geq[i], inds[i].geq(DT, IntegrationMethod::TRAPEZOIDAL), TOL);
    EXPECT_NEAR(ieq[i], inds[i].ieq(DT, IntegrationMethod::TRAPEZOIDAL), TOL);
  }
}

/** @test GPU inductor evaluation matches CPU under GEAR2 */
TEST_F(CompanionSetCudaTest, InductorGear2MatchesCpu) {
  const auto INDS = buildInductors(/*n=*/64);
  const double DT = 1e-7;
  std::vector<double> geq(INDS.size()), ieq(INDS.size());
  evaluateInductorsCuda(INDS.data(), static_cast<int>(INDS.size()), DT, IntegrationMethod::GEAR2,
                        geq.data(), ieq.data());

  for (std::size_t i = 0; i < INDS.size(); ++i) {
    EXPECT_NEAR(geq[i], INDS[i].geq(DT, IntegrationMethod::GEAR2), TOL);
    EXPECT_NEAR(ieq[i], INDS[i].ieq(DT, IntegrationMethod::GEAR2), TOL);
  }
}

/* ----------------------------- Edge cases ----------------------------- */

/** @test n=0 invocation is a safe no-op */
TEST_F(CompanionSetCudaTest, ZeroCountIsNoOp) {
  std::vector<double> geq, ieq;
  evaluateCapacitorsCuda(nullptr, 0, 1e-6, IntegrationMethod::BACKWARD_EULER, geq.data(),
                         ieq.data());
  evaluateInductorsCuda(nullptr, 0, 1e-6, IntegrationMethod::BACKWARD_EULER, geq.data(),
                        ieq.data());
  SUCCEED();
}

/** @test Determinism: same input produces same output across runs */
TEST_F(CompanionSetCudaTest, RepeatedEvaluationIsDeterministic) {
  const auto CAPS = buildCaps(/*n=*/128);
  const double DT = 1e-6;
  std::vector<double> g1(CAPS.size()), i1(CAPS.size());
  std::vector<double> g2(CAPS.size()), i2(CAPS.size());

  evaluateCapacitorsCuda(CAPS.data(), static_cast<int>(CAPS.size()), DT,
                         IntegrationMethod::TRAPEZOIDAL, g1.data(), i1.data());
  for (int run = 0; run < 5; ++run) {
    evaluateCapacitorsCuda(CAPS.data(), static_cast<int>(CAPS.size()), DT,
                           IntegrationMethod::TRAPEZOIDAL, g2.data(), i2.data());
    for (std::size_t i = 0; i < CAPS.size(); ++i) {
      EXPECT_DOUBLE_EQ(g2[i], g1[i]);
      EXPECT_DOUBLE_EQ(i2[i], i1[i]);
    }
  }
}
