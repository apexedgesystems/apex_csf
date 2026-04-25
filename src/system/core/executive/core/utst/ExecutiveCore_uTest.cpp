/**
 * @file ExecutiveCore_uTest.cpp
 * @brief Unit tests for ExecutiveCore (shared concrete IExecutive base).
 *
 * Covers:
 *   - Identity constants (id, name, type, label)
 *   - IExecutive contract enforcement (run, shutdown, isShutdownRequested,
 *     cycleCount remain pure virtual until derived)
 *   - Concrete derivation produces a usable IExecutive instance
 */

#include "src/system/core/executive/core/inc/ExecutiveCore.hpp"

#include <gtest/gtest.h>

/* ----------------------------- Test Fixture ----------------------------- */

/**
 * @brief Minimal concrete ExecutiveCore subclass.
 *
 * Implements IExecutive with trivial in-memory state so the contract
 * surface can be exercised without pulling in a component base or
 * platform-specific machinery.
 */
class TestExecutive : public executive::ExecutiveCore {
public:
  TestExecutive() noexcept = default;

  [[nodiscard]] executive::RunResult run() noexcept override {
    runCalls_++;
    return runResult_;
  }

  void shutdown() noexcept override { shutdownRequested_ = true; }

  [[nodiscard]] bool isShutdownRequested() const noexcept override {
    return shutdownRequested_;
  }

  [[nodiscard]] uint64_t cycleCount() const noexcept override { return cycles_; }

  // Test controls.
  void setRunResult(executive::RunResult r) noexcept { runResult_ = r; }
  void advanceCycles(uint64_t n) noexcept { cycles_ += n; }
  int runCalls() const noexcept { return runCalls_; }

private:
  executive::RunResult runResult_{executive::RunResult::SUCCESS};
  bool shutdownRequested_{false};
  uint64_t cycles_{0};
  int runCalls_{0};
};

/* ----------------------------- Identity Constants ----------------------------- */

/** @test ExecutiveCore::COMPONENT_ID is 0 (root component). */
TEST(ExecutiveCore_Identity, ComponentIdIsZero) {
  EXPECT_EQ(executive::ExecutiveCore::COMPONENT_ID, 0u);
}

/** @test ExecutiveCore::COMPONENT_NAME is "Executive". */
TEST(ExecutiveCore_Identity, ComponentNameIsExecutive) {
  EXPECT_STREQ(executive::ExecutiveCore::COMPONENT_NAME, "Executive");
}

/** @test ExecutiveCore::COMPONENT_LABEL is "EXECUTIVE". */
TEST(ExecutiveCore_Identity, ComponentLabelIsExecutive) {
  EXPECT_STREQ(executive::ExecutiveCore::COMPONENT_LABEL, "EXECUTIVE");
}

/** @test ExecutiveCore::COMPONENT_TYPE is ComponentType::EXECUTIVE. */
TEST(ExecutiveCore_Identity, ComponentTypeIsExecutive) {
  EXPECT_EQ(executive::ExecutiveCore::COMPONENT_TYPE,
            system_core::system_component::ComponentType::EXECUTIVE);
}

/* ----------------------------- IExecutive Contract ----------------------------- */

/** @test Default-constructed executive reports no shutdown and zero cycles. */
TEST(ExecutiveCore_Contract, DefaultStateIsClean) {
  TestExecutive exec;

  EXPECT_FALSE(exec.isShutdownRequested());
  EXPECT_EQ(exec.cycleCount(), 0u);
}

/** @test run() returns SUCCESS and tallies invocation. */
TEST(ExecutiveCore_Contract, RunReturnsSuccess) {
  TestExecutive exec;

  const executive::RunResult RESULT = exec.run();

  EXPECT_EQ(RESULT, executive::RunResult::SUCCESS);
  EXPECT_EQ(exec.runCalls(), 1);
}

/** @test run() can return error codes from the IExecutive contract. */
TEST(ExecutiveCore_Contract, RunPropagatesErrorResults) {
  TestExecutive exec;
  exec.setRunResult(executive::RunResult::ERROR_INIT);

  EXPECT_EQ(exec.run(), executive::RunResult::ERROR_INIT);

  exec.setRunResult(executive::RunResult::ERROR_RUNTIME);
  EXPECT_EQ(exec.run(), executive::RunResult::ERROR_RUNTIME);
}

/** @test shutdown() flips the shutdown-requested flag. */
TEST(ExecutiveCore_Contract, ShutdownSetsFlag) {
  TestExecutive exec;
  ASSERT_FALSE(exec.isShutdownRequested());

  exec.shutdown();

  EXPECT_TRUE(exec.isShutdownRequested());
}

/** @test cycleCount() reflects derived storage. */
TEST(ExecutiveCore_Contract, CycleCountReflectsDerivedState) {
  TestExecutive exec;

  exec.advanceCycles(7);
  EXPECT_EQ(exec.cycleCount(), 7u);

  exec.advanceCycles(3);
  EXPECT_EQ(exec.cycleCount(), 10u);
}

/* ----------------------------- Polymorphic Use ----------------------------- */

/** @test ExecutiveCore satisfies IExecutive via polymorphic pointer. */
TEST(ExecutiveCore_API, ImplementsIExecutive) {
  TestExecutive exec;
  executive::IExecutive* iface = &exec;

  EXPECT_FALSE(iface->isShutdownRequested());
  EXPECT_EQ(iface->cycleCount(), 0u);
  EXPECT_EQ(iface->run(), executive::RunResult::SUCCESS);

  iface->shutdown();
  EXPECT_TRUE(iface->isShutdownRequested());
}

/** @test ExecutiveCore can be referred to as the canonical executive type. */
TEST(ExecutiveCore_API, UsableAsCanonicalExecutiveBase) {
  TestExecutive exec;
  executive::ExecutiveCore* core = &exec;

  EXPECT_FALSE(core->isShutdownRequested());
  core->shutdown();
  EXPECT_TRUE(core->isShutdownRequested());
}
