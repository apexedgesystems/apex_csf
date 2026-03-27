/**
 * @file ITickSource_uTest.cpp
 * @brief Unit tests for ITickSource interface.
 */

#include "src/system/core/executive/lite/inc/ITickSource.hpp"

#include <gtest/gtest.h>

using executive::lite::ITickSource;

/* ----------------------------- Test Implementation ----------------------------- */

/**
 * @brief Concrete test tick source for interface testing.
 */
class TestTickSource : public ITickSource {
public:
  explicit TestTickSource(std::uint32_t freqHz = 100) noexcept : freq_(freqHz) {}

  void waitForNextTick() noexcept override {
    if (running_) {
      ++tick_;
      ++waitCount_;
    }
  }

  [[nodiscard]] std::uint32_t currentTick() const noexcept override { return tick_; }
  [[nodiscard]] std::uint32_t tickFrequency() const noexcept override { return freq_; }

  void start() noexcept override {
    tick_ = 0;
    running_ = true;
  }

  void stop() noexcept override { running_ = false; }
  [[nodiscard]] bool isRunning() const noexcept override { return running_; }

  void ackTick() noexcept override { ++ackCount_; }

  int waitCount() const noexcept { return waitCount_; }
  int ackCount() const noexcept { return ackCount_; }

private:
  std::uint32_t freq_;
  std::uint32_t tick_{0};
  int waitCount_{0};
  int ackCount_{0};
  bool running_{false};
};

/* ----------------------------- Default Behavior Tests ----------------------------- */

/** @test tickPeriodUs() computes period correctly for 100 Hz. */
TEST(ITickSource_DefaultBehavior, TickPeriodUs100Hz) {
  TestTickSource source(100);

  const std::uint32_t PERIOD = source.tickPeriodUs();

  // 1,000,000 / 100 = 10,000 us
  EXPECT_EQ(PERIOD, 10000);
}

/** @test tickPeriodUs() computes period correctly for 1000 Hz. */
TEST(ITickSource_DefaultBehavior, TickPeriodUs1000Hz) {
  TestTickSource source(1000);

  const std::uint32_t PERIOD = source.tickPeriodUs();

  // 1,000,000 / 1000 = 1,000 us
  EXPECT_EQ(PERIOD, 1000);
}

/** @test tickPeriodUs() handles edge case of 1 Hz. */
TEST(ITickSource_DefaultBehavior, TickPeriodUs1Hz) {
  TestTickSource source(1);

  const std::uint32_t PERIOD = source.tickPeriodUs();

  // 1,000,000 / 1 = 1,000,000 us
  EXPECT_EQ(PERIOD, 1000000);
}

/** @test tickPeriodUs() returns 0 for 0 Hz (degenerate case). */
TEST(ITickSource_DefaultBehavior, TickPeriodUs0Hz) {
  TestTickSource source(0);

  const std::uint32_t PERIOD = source.tickPeriodUs();

  // Division by zero protection: returns 0
  EXPECT_EQ(PERIOD, 0);
}

/** @test Default ackTick() is a no-op. */
TEST(ITickSource_DefaultBehavior, AckTickCallable) {
  TestTickSource source;

  // Should not throw or crash
  source.ackTick();

  EXPECT_EQ(source.ackCount(), 1);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test start() enables tick generation. */
TEST(ITickSource_Lifecycle, StartEnablesTicks) {
  TestTickSource source;

  source.start();

  EXPECT_TRUE(source.isRunning());
  EXPECT_EQ(source.currentTick(), 0);
}

/** @test stop() disables tick generation. */
TEST(ITickSource_Lifecycle, StopDisablesTicks) {
  TestTickSource source;
  source.start();

  source.stop();

  EXPECT_FALSE(source.isRunning());
}

/** @test waitForNextTick() increments tick when running. */
TEST(ITickSource_Lifecycle, WaitIncrementsTickWhenRunning) {
  TestTickSource source;
  source.start();

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 2);
  EXPECT_EQ(source.waitCount(), 2);
}

/** @test waitForNextTick() does not increment tick when stopped. */
TEST(ITickSource_Lifecycle, WaitDoesNotIncrementWhenStopped) {
  TestTickSource source;

  source.waitForNextTick();
  source.waitForNextTick();

  EXPECT_EQ(source.currentTick(), 0);
}

/* ----------------------------- Interface Polymorphism Tests ----------------------------- */

/** @test Interface methods work through base pointer. */
TEST(ITickSource_Interface, PolymorphicAccess) {
  TestTickSource source(50);
  ITickSource* iface = &source;

  EXPECT_EQ(iface->tickFrequency(), 50);
  EXPECT_EQ(iface->tickPeriodUs(), 20000);
  EXPECT_FALSE(iface->isRunning());

  iface->start();
  EXPECT_TRUE(iface->isRunning());

  iface->waitForNextTick();
  EXPECT_EQ(iface->currentTick(), 1);

  iface->ackTick();
  iface->stop();
  EXPECT_FALSE(iface->isRunning());
}
