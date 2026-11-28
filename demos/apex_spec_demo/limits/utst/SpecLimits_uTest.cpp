/**
 * @file SpecLimits_uTest.cpp
 * @brief Unit tests for the parameter load of a spec-generated component base.
 *
 * The component under test is built on the base generated for SpecLimits and
 * adds a policy hook that refuses a parameter set. Covers:
 *   - No payload file at the first load: the designed defaults run.
 *   - A payload the hook accepts replaces the running set.
 *   - A payload the hook refuses on a live component leaves the running
 *     set, the state its publication seeded and the generation as they were.
 */

#include "SpecLimitsSpecBase_auto.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using appsim::spec::NudgeRequest;
using appsim::spec::SpecLimitsSpecBase;
using appsim::spec::SpecLimitsTunableParams;
using system_core::system_component::SwModelBase;
using system_core::system_component::TprmIngest;
using system_core::system_component::TprmPayloadHeader;

namespace {

/// The generated base with a policy that refuses a band above GUARD.
class GuardedLimits final : public SpecLimitsSpecBase<GuardedLimits, SwModelBase> {
public:
  static constexpr float GUARD = 8.0F;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 216; }
  [[nodiscard]] const char* componentName() const noexcept override { return "GuardedLimits"; }
  [[nodiscard]] const char* label() const noexcept override { return "GUARDED_LIM"; }

  std::uint8_t step() noexcept { return 0; }

  [[nodiscard]] float banded() const noexcept { return paramBank_.active().banded; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return paramBank_.activeGeneration(); }
  [[nodiscard]] std::uint32_t publications() const noexcept { return publications_; }

protected:
  [[nodiscard]] bool validateParams(const SpecLimitsTunableParams& p) noexcept override {
    return p.banded <= GUARD;
  }
  void onParamsLoaded() noexcept override { ++publications_; }
  [[nodiscard]] std::uint8_t onNudge(const NudgeRequest& /*request*/) noexcept override {
    return 0;
  }

private:
  std::uint32_t publications_{0};
};

/// A fresh directory for one test's payload files.
std::filesystem::path scratchDir(const char* name) {
  const auto DIR = std::filesystem::temp_directory_path() /
                   (std::string("spec_limits_utest_") + name + "_" + std::to_string(::getpid()));
  std::filesystem::remove_all(DIR);
  std::filesystem::create_directories(DIR);
  return DIR;
}

/// Write COMPONENT's payload file into DIR with the band set to BANDED.
void stagePayload(const std::filesystem::path& dir, const GuardedLimits& component, float banded) {
  SpecLimitsTunableParams params{};
  params.banded = banded;

  TprmPayloadHeader header{};
  header.magic = system_core::system_component::TPRM_PAYLOAD_MAGIC;
  header.version = system_core::system_component::TPRM_PAYLOAD_VERSION;
  header.payloadSize = sizeof(params);
  header.fullUid = component.fullUid();
  header.layoutHash = appsim::spec::SPEC_LIMITS_TUNABLE_PARAMS_LAYOUT_HASH;
  header.payloadCrc = system_core::system_component::tprmCrc32(
      reinterpret_cast<const std::uint8_t*>(&params), sizeof(params));

  std::ofstream out(dir / GuardedLimits::tprmFilename(component.fullUid()),
                    std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&header), sizeof(header));
  out.write(reinterpret_cast<const char*>(&params), sizeof(params));
  ASSERT_TRUE(out.good());
}

} // namespace

/** @test With no payload file the first load publishes the designed defaults. */
TEST(SpecLimitsLoadTest, FirstLoadWithoutAFileRunsTheDefaults) {
  const auto DIR = scratchDir("defaults");
  GuardedLimits limits;
  limits.setInstanceIndex(0);

  EXPECT_EQ(limits.loadTprm(DIR), TprmIngest::DEFAULTS);
  EXPECT_FLOAT_EQ(limits.banded(), SpecLimitsTunableParams{}.banded);
  EXPECT_EQ(limits.publications(), 1U);
  std::filesystem::remove_all(DIR);
}

/** @test A payload the policy accepts replaces the running set, at boot and on a reload. */
TEST(SpecLimitsLoadTest, AcceptedPayloadReplacesTheRunningSet) {
  const auto DIR = scratchDir("accepted");
  GuardedLimits limits;
  limits.setInstanceIndex(0);

  ASSERT_NO_FATAL_FAILURE(stagePayload(DIR, limits, 7.0F));
  EXPECT_EQ(limits.loadTprm(DIR), TprmIngest::LOADED);
  EXPECT_FLOAT_EQ(limits.banded(), 7.0F);

  ASSERT_NO_FATAL_FAILURE(stagePayload(DIR, limits, 6.0F));
  EXPECT_EQ(limits.loadTprm(DIR), TprmIngest::LOADED);
  EXPECT_FLOAT_EQ(limits.banded(), 6.0F);
  EXPECT_EQ(limits.publications(), 2U);
  std::filesystem::remove_all(DIR);
}

/**
 * @test A payload the policy refuses on a live component is reported as
 *       rejected and changes nothing: the running set stays, the designed
 *       defaults are not applied in its place, and nothing is published.
 */
TEST(SpecLimitsLoadTest, RefusedReloadKeepsTheRunningSet) {
  const auto DIR = scratchDir("refused");
  GuardedLimits limits;
  limits.setInstanceIndex(0);

  ASSERT_NO_FATAL_FAILURE(stagePayload(DIR, limits, 7.0F));
  ASSERT_EQ(limits.loadTprm(DIR), TprmIngest::LOADED);
  const std::uint64_t GENERATION = limits.generation();

  ASSERT_NO_FATAL_FAILURE(stagePayload(DIR, limits, GuardedLimits::GUARD + 1.0F));
  EXPECT_EQ(limits.loadTprm(DIR), TprmIngest::REJECTED);
  EXPECT_FLOAT_EQ(limits.banded(), 7.0F);
  EXPECT_EQ(limits.generation(), GENERATION);
  EXPECT_EQ(limits.publications(), 1U);
  std::filesystem::remove_all(DIR);
}
