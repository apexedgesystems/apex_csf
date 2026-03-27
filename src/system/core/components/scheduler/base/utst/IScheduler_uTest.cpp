/**
 * @file IScheduler_uTest.cpp
 * @brief Unit tests for IScheduler interface.
 */

#include "src/system/core/components/scheduler/base/inc/IScheduler.hpp"

#include <gtest/gtest.h>

using system_core::scheduler::IScheduler;

/* ----------------------------- Test Implementation ----------------------------- */

/**
 * @brief Minimal concrete implementation for testing IScheduler.
 *
 * Demonstrates that IScheduler can be implemented with minimal overhead.
 */
class TestScheduler final : public IScheduler {
public:
  static constexpr std::uint16_t FUNDAMENTAL_FREQ = 100;

  /* ----------------------------- IScheduler ----------------------------- */

  [[nodiscard]] std::uint16_t fundamentalFreq() const noexcept override { return FUNDAMENTAL_FREQ; }

  void tick() noexcept override { ++tickCount_; }

  [[nodiscard]] std::uint64_t tickCount() const noexcept override { return tickCount_; }

  [[nodiscard]] std::size_t taskCount() const noexcept override { return taskCount_; }

  void setTaskCount(std::size_t count) noexcept { taskCount_ = count; }

  void reset() noexcept { tickCount_ = 0; }

private:
  std::uint64_t tickCount_{0};
  std::size_t taskCount_{0};
};

/* ----------------------------- IScheduler Tests ----------------------------- */

/** @test Verify IScheduler can be instantiated via concrete implementation. */
TEST(IScheduler, CanInstantiateConcrete) {
  TestScheduler sched;
  EXPECT_EQ(sched.fundamentalFreq(), 100);
}

/** @test Verify fundamentalFreq returns configured value. */
TEST(IScheduler, FundamentalFreqReturnsConfigured) {
  TestScheduler sched;
  EXPECT_EQ(sched.fundamentalFreq(), 100);
}

/** @test Verify tick increments tickCount. */
TEST(IScheduler, TickIncrementsCount) {
  TestScheduler sched;
  EXPECT_EQ(sched.tickCount(), 0);

  sched.tick();
  EXPECT_EQ(sched.tickCount(), 1);

  sched.tick();
  sched.tick();
  EXPECT_EQ(sched.tickCount(), 3);
}

/** @test Verify reset clears tickCount. */
TEST(IScheduler, ResetClearsTickCount) {
  TestScheduler sched;
  sched.tick();
  sched.tick();
  EXPECT_EQ(sched.tickCount(), 2);

  sched.reset();
  EXPECT_EQ(sched.tickCount(), 0);
}

/** @test Verify taskCount returns configured value. */
TEST(IScheduler, TaskCountReturnsValue) {
  TestScheduler sched;
  EXPECT_EQ(sched.taskCount(), 0);

  sched.setTaskCount(5);
  EXPECT_EQ(sched.taskCount(), 5);
}

/** @test Verify IScheduler pointer can hold concrete implementation. */
TEST(IScheduler, PolymorphicAccess) {
  TestScheduler concrete;
  IScheduler* iface = &concrete;

  EXPECT_EQ(iface->fundamentalFreq(), 100);
  EXPECT_EQ(iface->tickCount(), 0);

  iface->tick();
  EXPECT_EQ(iface->tickCount(), 1);
}
