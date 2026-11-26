/**
 * @file CompanionSetCuda_uTest.cu
 * @brief Unit tests for the CUDA companion-evaluation kernels.
 *
 * The kernels call the companions' own geq() and ieq(), so the GPU values
 * are held to the host's to the last bit, for every method and with the
 * histories each method reads. The host values are held to hand numbers in
 * CompanionModels_uTest.cpp.
 *
 * The tests of evaluation need a CUDA device and are skipped without one;
 * the test of the status without a device runs only then.
 */

#include "src/sim/electronics/algorithms/companions/inc/CompanionModels.hpp"
#include "src/sim/electronics/algorithms/companions/inc/CompanionSetCuda.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include <cuda_runtime_api.h>

using sim::electronics::algorithms::companions::CapacitorCompanion;
using sim::electronics::algorithms::companions::InductorCompanion;
using sim::electronics::algorithms::companions::cuda::evaluateCapacitorsCuda;
using sim::electronics::algorithms::companions::cuda::evaluateInductorsCuda;
using sim::electronics::algorithms::transient::IntegrationMethod;

namespace {

constexpr IntegrationMethod METHODS[] = {IntegrationMethod::BACKWARD_EULER,
                                         IntegrationMethod::TRAPEZOIDAL, IntegrationMethod::GEAR2};

bool deviceAvailable() {
  int devices = 0;
  return cudaGetDeviceCount(&devices) == cudaSuccess && devices > 0;
}

class CompanionSetCudaTest : public ::testing::Test {
protected:
  void SetUp() override {
    if (!deviceAvailable()) {
      GTEST_SKIP() << "No CUDA device.";
    }
  }
};

/** @brief Capacitors that differ in value, in both voltages, in current and in the step before. */
std::vector<CapacitorCompanion> buildCaps(std::size_t n) {
  std::vector<CapacitorCompanion> caps(n);
  for (std::size_t i = 0; i < n; ++i) {
    caps[i].posNet = static_cast<unsigned>(i + 1);
    caps[i].negNet = 0;
    caps[i].capacitance = 1e-9 * (1.0 + 0.01 * static_cast<double>(i));
    caps[i].prevVoltage = 0.1 * static_cast<double>(i % 10);
    caps[i].prev2Voltage = 0.05 * static_cast<double>(i % 7);
    caps[i].current = 1e-6 * static_cast<double>(i % 5);
    caps[i].prevStep = 1e-6 * static_cast<double>(i % 4); // 0: none recorded
  }
  return caps;
}

/** @brief Inductors that differ in value, in both currents, in voltage and in the step before. */
std::vector<InductorCompanion> buildInductors(std::size_t n) {
  std::vector<InductorCompanion> inds(n);
  for (std::size_t i = 0; i < n; ++i) {
    inds[i].posNet = static_cast<unsigned>(i + 1);
    inds[i].negNet = 0;
    inds[i].inductance = 1e-3 * (1.0 + 0.01 * static_cast<double>(i));
    inds[i].prevCurrent = 1e-3 * static_cast<double>(i % 5);
    inds[i].prev2Current = 0.5e-3 * static_cast<double>(i % 3);
    inds[i].voltage = 1e-3 * static_cast<double>(i % 6);
    inds[i].prevStep = 1e-6 * static_cast<double>(i % 4);
  }
  return inds;
}

} // namespace

/* ----------------------------- Parity With The Host ----------------------------- */

/** @test The GPU gives each capacitor the host's Geq and Ieq under every method */
TEST_F(CompanionSetCudaTest, CapacitorsMatchHostUnderEveryMethod) {
  // More than one block of 256.
  const auto CAPS = buildCaps(/*n=*/600);
  const double DT = 1e-6;
  for (const IntegrationMethod METHOD : METHODS) {
    std::vector<double> geq(CAPS.size(), -1.0);
    std::vector<double> ieq(CAPS.size(), -1.0);
    ASSERT_TRUE(evaluateCapacitorsCuda(CAPS.data(), static_cast<int>(CAPS.size()), DT, METHOD,
                                       geq.data(), ieq.data()));
    for (std::size_t i = 0; i < CAPS.size(); ++i) {
      ASSERT_EQ(geq[i], CAPS[i].geq(DT, METHOD))
          << "method " << static_cast<int>(METHOD) << ", capacitor " << i;
      ASSERT_EQ(ieq[i], CAPS[i].ieq(DT, METHOD))
          << "method " << static_cast<int>(METHOD) << ", capacitor " << i;
    }
  }
}

/** @test The GPU gives each inductor the host's Geq and Ieq under every method */
TEST_F(CompanionSetCudaTest, InductorsMatchHostUnderEveryMethod) {
  const auto INDS = buildInductors(/*n=*/600);
  const double DT = 1e-7;
  for (const IntegrationMethod METHOD : METHODS) {
    std::vector<double> geq(INDS.size(), -1.0);
    std::vector<double> ieq(INDS.size(), -1.0);
    ASSERT_TRUE(evaluateInductorsCuda(INDS.data(), static_cast<int>(INDS.size()), DT, METHOD,
                                      geq.data(), ieq.data()));
    for (std::size_t i = 0; i < INDS.size(); ++i) {
      ASSERT_EQ(geq[i], INDS[i].geq(DT, METHOD))
          << "method " << static_cast<int>(METHOD) << ", inductor " << i;
      ASSERT_EQ(ieq[i], INDS[i].ieq(DT, METHOD))
          << "method " << static_cast<int>(METHOD) << ", inductor " << i;
    }
  }
}

/* ----------------------------- Status ----------------------------- */

/** @test Nothing to evaluate is a success that touches nothing */
TEST_F(CompanionSetCudaTest, ZeroCountSucceeds) {
  EXPECT_TRUE(evaluateCapacitorsCuda(nullptr, 0, 1e-6, IntegrationMethod::BACKWARD_EULER, nullptr,
                                     nullptr));
  EXPECT_TRUE(
      evaluateInductorsCuda(nullptr, 0, 1e-6, IntegrationMethod::BACKWARD_EULER, nullptr, nullptr));
}

/** @test A missing array or a negative count is refused and nothing is written */
TEST_F(CompanionSetCudaTest, BadArgumentsAreRefused) {
  const auto CAPS = buildCaps(/*n=*/4);
  const auto INDS = buildInductors(/*n=*/4);
  std::vector<double> geq(4, -1.0);
  std::vector<double> ieq(4, -1.0);
  const auto BE = IntegrationMethod::BACKWARD_EULER;

  EXPECT_FALSE(evaluateCapacitorsCuda(nullptr, 4, 1e-6, BE, geq.data(), ieq.data()));
  EXPECT_FALSE(evaluateCapacitorsCuda(CAPS.data(), 4, 1e-6, BE, nullptr, ieq.data()));
  EXPECT_FALSE(evaluateCapacitorsCuda(CAPS.data(), 4, 1e-6, BE, geq.data(), nullptr));
  EXPECT_FALSE(evaluateCapacitorsCuda(CAPS.data(), -1, 1e-6, BE, geq.data(), ieq.data()));
  EXPECT_FALSE(evaluateInductorsCuda(nullptr, 4, 1e-6, BE, geq.data(), ieq.data()));
  EXPECT_FALSE(evaluateInductorsCuda(INDS.data(), -1, 1e-6, BE, geq.data(), ieq.data()));

  EXPECT_EQ(std::count(geq.begin(), geq.end(), -1.0), 4);
  EXPECT_EQ(std::count(ieq.begin(), ieq.end(), -1.0), 4);
}

/**
 * @test Without a device the evaluators say so and leave the outputs alone.
 *
 * Runs only where no device is visible (`CUDA_VISIBLE_DEVICES=`).
 */
TEST(CompanionSetCudaStatusTest, NoDeviceIsReported) {
  if (deviceAvailable()) {
    GTEST_SKIP() << "A CUDA device is present.";
  }
  const auto CAPS = buildCaps(/*n=*/8);
  const auto INDS = buildInductors(/*n=*/8);
  std::vector<double> geq(8, -1.0);
  std::vector<double> ieq(8, -1.0);

  EXPECT_FALSE(evaluateCapacitorsCuda(CAPS.data(), 8, 1e-6, IntegrationMethod::BACKWARD_EULER,
                                      geq.data(), ieq.data()));
  EXPECT_FALSE(evaluateInductorsCuda(INDS.data(), 8, 1e-6, IntegrationMethod::BACKWARD_EULER,
                                     geq.data(), ieq.data()));
  EXPECT_EQ(std::count(geq.begin(), geq.end(), -1.0), 8);
  EXPECT_EQ(std::count(ieq.begin(), ieq.end(), -1.0), 8);
}
