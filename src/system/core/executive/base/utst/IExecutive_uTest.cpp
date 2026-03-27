/**
 * @file IExecutive_uTest.cpp
 * @brief Unit tests for IExecutive interface.
 */

#include "src/system/core/executive/base/inc/IExecutive.hpp"

#include <gtest/gtest.h>

using executive::IExecutive;
using executive::RunResult;

/* ----------------------------- Test Implementation ----------------------------- */

/**
 * @brief Minimal concrete implementation for testing IExecutive.
 *
 * Demonstrates that IExecutive can be implemented with minimal overhead.
 */
class TestExecutive final : public IExecutive {
public:
  /* ----------------------------- IExecutive ----------------------------- */

  [[nodiscard]] RunResult run() noexcept override {
    if (!initialized_) {
      return RunResult::ERROR_INIT;
    }
    while (!shutdownRequested_) {
      ++cycleCount_;
      if (cycleCount_ >= maxCycles_) {
        break;
      }
    }
    return RunResult::SUCCESS;
  }

  void shutdown() noexcept override { shutdownRequested_ = true; }

  [[nodiscard]] bool isShutdownRequested() const noexcept override { return shutdownRequested_; }

  [[nodiscard]] std::uint64_t cycleCount() const noexcept override { return cycleCount_; }

  /* ----------------------------- Test Helpers ----------------------------- */

  void setMaxCycles(std::uint64_t max) noexcept { maxCycles_ = max; }

  void init() noexcept { initialized_ = true; }

  void reset() noexcept {
    initialized_ = false;
    cycleCount_ = 0;
    shutdownRequested_ = false;
  }

private:
  std::uint64_t cycleCount_{0};
  std::uint64_t maxCycles_{10};
  bool initialized_{false};
  bool shutdownRequested_{false};
};

/* ----------------------------- IExecutive Tests ----------------------------- */

/** @test Verify IExecutive can be instantiated via concrete implementation. */
TEST(IExecutive, CanInstantiateConcrete) {
  TestExecutive exec;
  EXPECT_EQ(exec.cycleCount(), 0);
  EXPECT_FALSE(exec.isShutdownRequested());
}

/** @test Verify run fails without init. */
TEST(IExecutive, RunFailsWithoutInit) {
  TestExecutive exec;
  const auto RESULT = exec.run();
  EXPECT_EQ(RESULT, RunResult::ERROR_INIT);
}

/** @test Verify run succeeds after init. */
TEST(IExecutive, RunSucceedsAfterInit) {
  TestExecutive exec;
  exec.setMaxCycles(5);
  exec.init();

  const auto RESULT = exec.run();
  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 5);
}

/** @test Verify shutdown stops execution. */
TEST(IExecutive, ShutdownStopsExecution) {
  TestExecutive exec;
  exec.setMaxCycles(1000000);
  exec.init();
  EXPECT_FALSE(exec.isShutdownRequested());

  exec.shutdown();
  EXPECT_TRUE(exec.isShutdownRequested());

  const auto RESULT = exec.run();
  EXPECT_EQ(RESULT, RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 0);
}

/** @test Verify cycleCount increments during run. */
TEST(IExecutive, CycleCountIncrements) {
  TestExecutive exec;
  exec.setMaxCycles(3);
  exec.init();
  EXPECT_EQ(exec.cycleCount(), 0);

  EXPECT_EQ(exec.run(), RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 3);
}

/** @test Verify reset clears cycleCount and shutdownRequested. */
TEST(IExecutive, ResetClearsState) {
  TestExecutive exec;
  exec.setMaxCycles(5);
  exec.init();
  EXPECT_EQ(exec.run(), RunResult::SUCCESS);
  EXPECT_EQ(exec.cycleCount(), 5);

  exec.shutdown();
  EXPECT_TRUE(exec.isShutdownRequested());

  exec.reset();
  EXPECT_EQ(exec.cycleCount(), 0);
  EXPECT_FALSE(exec.isShutdownRequested());
}

/** @test Verify IExecutive pointer can hold concrete implementation. */
TEST(IExecutive, PolymorphicAccess) {
  TestExecutive concrete;
  concrete.setMaxCycles(3);
  concrete.init();
  IExecutive* iface = &concrete;

  EXPECT_EQ(iface->cycleCount(), 0);
  EXPECT_EQ(iface->run(), RunResult::SUCCESS);
  EXPECT_EQ(iface->cycleCount(), 3);
}

/* ----------------------------- RunResult Tests ----------------------------- */

/** @test Verify RunResult SUCCESS is zero. */
TEST(RunResult, SuccessIsZero) { EXPECT_EQ(static_cast<std::uint8_t>(RunResult::SUCCESS), 0); }

/** @test Verify RunResult error codes are distinct. */
TEST(RunResult, ErrorCodesDistinct) {
  EXPECT_NE(RunResult::SUCCESS, RunResult::ERROR_INIT);
  EXPECT_NE(RunResult::SUCCESS, RunResult::ERROR_RUNTIME);
  EXPECT_NE(RunResult::SUCCESS, RunResult::ERROR_SHUTDOWN);
  EXPECT_NE(RunResult::ERROR_INIT, RunResult::ERROR_RUNTIME);
  EXPECT_NE(RunResult::ERROR_INIT, RunResult::ERROR_SHUTDOWN);
  EXPECT_NE(RunResult::ERROR_RUNTIME, RunResult::ERROR_SHUTDOWN);
}
